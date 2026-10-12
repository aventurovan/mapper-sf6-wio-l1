/*
 * SF6 Mapper - samodzielny tracker do mapowania zasiegu sieci MeshCore Poludnie (SF6)
 * Platforma: Seeed Wio Tracker L1 / L1 Pro (nRF52840 + SX1262 + GNSS L76K)
 *
 * Dzialanie:
 *  - po uzyskaniu fixu GPS co 20/30/40/50/60 s (ustawienie) wysyla zero-hop discovery (tylko repeatery),
 *  - przez 10 s zbiera odpowiedzi (SNR w obie strony, RSSI),
 *  - kazdy ping (trafienie albo pudlo) zapisuje z pozycja do flasha QSPI,
 *  - po 4 pingach bez ruchu robi pauze do czasu przemieszczenia,
 *  - punkty pobiera aplikacja Mapper SF6 przez BLE i kasuje je dopiero po ACK.
 */
#include <Arduino.h>
#include <Mesh.h>
#include <target.h>
#include <helpers/IdentityStore.h>
#include <helpers/nrf52/SerialBLEInterface.h>
#include <InternalFileSystem.h>
#include <CustomLFS_QSPIFlash.h>

#include "MapperProtocol.h"
#include "MapperMesh.h"
#include "MapLog.h"
#include "MapperUI.h"

// ---------------- konfiguracja ----------------
#define SF6_FREQ              869.618f
#define SF6_BW                62.5f
#define SF6_SF                6
#define SF6_CR                5

#ifndef COLLECT_MS
  #define COLLECT_MS          10000UL   // jak dlugo czekamy na odpowiedzi
#endif
#ifndef STATIONARY_PINGS
  #define STATIONARY_PINGS    4         // tyle pingow w miejscu -> pauza
#endif
#ifndef MOVE_THRESHOLD_M
  #define MOVE_THRESHOLD_M    30.0      // przesuniecie uznawane za ruch (m)
#endif
#ifndef MIN_SATS
  #define MIN_SATS            4
#endif
#ifndef BLE_PIN_CODE
  #define BLE_PIN_CODE        123456
#endif
#define BLE_NAME_PREFIX       "MeshCore-"   // ten sam prefiks co companion - aplikacja znajdzie urzadzenie tym samym filtrem
#define SYNC_MAX_BATCH        200

// ---------------- obiekty globalne ----------------
StdRNG fast_rng;
SimpleMeshTables tables;
MapperMesh the_mesh(radio_driver, *new ArduinoMillis(), fast_rng, rtc_clock, tables);
MapLog maplog;
SerialBLEInterface ble;
MapperUI ui;
MapperStatus status;

static char ble_name[24];

// stan mapowania
static bool     run_mapping = true;
static unsigned long next_ping_at = 0;
static unsigned long last_ping_at = 0;
static uint8_t  interval_s = MAP_INTERVAL_DEFAULT;   // interwal pingow (s), zapisywany w InternalFS
static unsigned long collect_until = 0;
static MapRecordHdr  ping_hdr;        // pozycja/czas zapamietane w chwili pingu
static bool     have_last_pos = false;
static int32_t  last_lat = 0, last_lon = 0;
static int      still_count = 0;      // ile kolejnych pingow w tym samym miejscu

// stan synchronizacji BLE
static bool     sync_active = false;
static uint32_t sync_next_seq = 0;
static uint16_t sync_left = 0;
static uint16_t sync_sent = 0;
static uint32_t sync_last_seq = 0;

static uint8_t  frame[MAX_FRAME_SIZE];

void halt() {
  while (1) ;
}

// ---------------- pomocnicze ----------------
static double distMeters(int32_t lat1, int32_t lon1, int32_t lat2, int32_t lon2) {
  const double R = 6371000.0;
  double la1 = lat1 / 1e6 * DEG_TO_RAD, la2 = lat2 / 1e6 * DEG_TO_RAD;
  double dla = la2 - la1;
  double dlo = (lon2 - lon1) / 1e6 * DEG_TO_RAD;
  double x = dlo * cos((la1 + la2) / 2);
  return sqrt(x * x + dla * dla) * R;
}

static LocationProvider* gps() { return sensors.getLocationProvider(); }

static bool gpsFix() {
  LocationProvider* l = gps();
  return l != NULL && l->isValid() && l->satellitesCount() >= MIN_SATS;
}

static uint8_t stateFlags() {
  uint8_t f = 0;
  if (run_mapping) f |= MAP_STATE_RUN;
  if (gpsFix()) f |= MAP_STATE_GPS_FIX;
  if (still_count >= STATIONARY_PINGS) f |= MAP_STATE_STATIONARY;
  if (maplog.isFull()) f |= MAP_STATE_LOG_FULL;
  return f;
}

// ---------------- ustawienia ----------------
#define SETTINGS_FILE   "/sf6cfg"

struct __attribute__((packed)) MapperSettings {
  uint8_t magic;
  uint8_t interval_s;
};
#define SETTINGS_MAGIC  0x5A

static void loadSettings() {
  File f = InternalFS.open(SETTINGS_FILE, FILE_O_READ);
  if (!f) return;
  MapperSettings st;
  if (f.read((uint8_t*)&st, sizeof(st)) == sizeof(st) && st.magic == SETTINGS_MAGIC && mapIntervalValid(st.interval_s)) {
    interval_s = st.interval_s;
  }
  f.close();
}

static void saveSettings() {
  MapperSettings st = { SETTINGS_MAGIC, interval_s };
  InternalFS.remove(SETTINGS_FILE);
  File f = InternalFS.open(SETTINGS_FILE, FILE_O_WRITE);
  if (!f) return;
  f.write((const uint8_t*)&st, sizeof(st));
  f.close();
}

static bool setInterval(int secs) {
  if (!mapIntervalValid(secs)) return false;
  if (secs != interval_s) {
    interval_s = (uint8_t) secs;
    saveSettings();
    // nowy interwal liczony od ostatniego pingu
    if (last_ping_at != 0 && still_count < STATIONARY_PINGS) {
      next_ping_at = last_ping_at + (unsigned long) interval_s * 1000UL;
    }
  }
  return true;
}

// ---------------- mapowanie ----------------
static void startPing() {
  LocationProvider* l = gps();
  int32_t lat = l->getLatitude();
  int32_t lon = l->getLongitude();

  // wykrywanie postoju
  if (have_last_pos && distMeters(last_lat, last_lon, lat, lon) < MOVE_THRESHOLD_M) {
    if (still_count >= STATIONARY_PINGS) {
      next_ping_at = millis() + 2000;   // stoimy - sprawdzaj ruch co 2 s, bez nadawania
      return;
    }
    still_count++;
  } else {
    still_count = 1;
    last_lat = lat;
    last_lon = lon;
    have_last_pos = true;
  }

  memset(&ping_hdr, 0, sizeof(ping_hdr));
  long ts = l->getTimestamp();
  ping_hdr.ts   = (ts > 1700000000L) ? (uint32_t) ts : rtc_clock.getCurrentTime();
  ping_hdr.lat  = lat;
  ping_hdr.lon  = lon;
  ping_hdr.alt  = (int16_t)(l->getAltitude() / 1000);
  ping_hdr.sats = (uint8_t) l->satellitesCount();
  ping_hdr.flags = (interval_s / 10) & MAP_REC_INTERVAL_MASK;

  if (the_mesh.sendDiscover()) {
    collect_until = millis() + COLLECT_MS;
    status.pings++;
  }
  last_ping_at = millis();
  next_ping_at = last_ping_at + (unsigned long) interval_s * 1000UL;
}

static void finishPing() {
  MapResp resp[MAP_MAX_RESP];
  int n = the_mesh.finishCollect(resp);
  ping_hdr.n = (uint8_t) n;

  if (!maplog.append(ping_hdr, resp)) {
    MESH_DEBUG_PRINTLN("MapLog: nie zapisano punktu (pamiec pelna?)");
  }

  status.last_n = n;
  if (n > 0) {
    status.hits++;
    int best = 0;   // najlepszy SNR odbioru
    for (int i = 1; i < n; i++) if (resp[i].snr_rx > resp[best].snr_rx) best = i;
    status.last_prefix[0] = resp[best].prefix[0];
    status.last_prefix[1] = resp[best].prefix[1];
    status.last_snr_rx = resp[best].snr_rx;
    status.last_snr_tx = resp[best].snr_tx;
    ui.notifyHit();
  }
}

static void mappingLoop() {
  if (the_mesh.isCollecting()) {
    if ((long)(millis() - collect_until) >= 0) finishPing();
    return;
  }
  if (!run_mapping || maplog.isFull() || !gpsFix()) return;
  if ((long)(millis() - next_ping_at) >= 0) startPing();
}

// ---------------- protokol BLE ----------------
static void sendErr(uint8_t err) {
  uint8_t f[2] = { RESP_MAP_ERR, err };
  ble.writeFrame(f, 2);
}

static void sendHello() {
  int i = 0;
  frame[i++] = RESP_MAP_HELLO;
  frame[i++] = MAPPER_PROTO_VER;
  frame[i++] = stateFlags();
  memcpy(&frame[i], the_mesh.self_id.pub_key, PUB_KEY_SIZE); i += PUB_KEY_SIZE;
  uint32_t v;
  v = maplog.pending();  memcpy(&frame[i], &v, 4); i += 4;
  v = maplog.ackedSeq(); memcpy(&frame[i], &v, 4); i += 4;
  v = maplog.lastSeq();  memcpy(&frame[i], &v, 4); i += 4;
  v = maplog.total();    memcpy(&frame[i], &v, 4); i += 4;
  uint16_t mv = board.getBattMilliVolts(); memcpy(&frame[i], &mv, 2); i += 2;
  LocationProvider* l = gps();
  frame[i++] = l ? (uint8_t) l->satellitesCount() : 0;
  int32_t lat = l && l->isValid() ? l->getLatitude() : 0;
  int32_t lon = l && l->isValid() ? l->getLongitude() : 0;
  memcpy(&frame[i], &lat, 4); i += 4;
  memcpy(&frame[i], &lon, 4); i += 4;
  uint32_t fill = maplog.fileBytes(), cap = maplog.maxBytes();
  memcpy(&frame[i], &fill, 4); i += 4;
  memcpy(&frame[i], &cap, 4); i += 4;
  frame[i++] = interval_s;
  const char* fw = MAPPER_FW_VERSION;
  int fl = strlen(fw);
  memcpy(&frame[i], fw, fl); i += fl;
  ble.writeFrame(frame, i);
}

static void handleBleFrame(const uint8_t* cmd, size_t len) {
  ui.wake();
  switch (cmd[0]) {
    case CMD_MAP_HELLO:
      sendHello();
      break;

    case CMD_MAP_GET: {
      if (len < 6) { sendErr(MAP_ERR_BAD_ARGS); break; }
      uint32_t from;
      memcpy(&from, &cmd[1], 4);
      uint8_t max = cmd[5];
      if (max == 0 || max > SYNC_MAX_BATCH) max = SYNC_MAX_BATCH;
      sync_active = true;
      sync_next_seq = from;
      sync_left = max;
      sync_sent = 0;
      sync_last_seq = 0;
      break;
    }

    case CMD_MAP_ACK: {
      if (len < 5) { sendErr(MAP_ERR_BAD_ARGS); break; }
      if (sync_active) { sendErr(MAP_ERR_BUSY); break; }
      uint32_t up_to;
      memcpy(&up_to, &cmd[1], 4);
      maplog.ack(up_to);
      int i = 0;
      frame[i++] = RESP_MAP_ACK;
      uint32_t v = maplog.ackedSeq(); memcpy(&frame[i], &v, 4); i += 4;
      v = maplog.pending();           memcpy(&frame[i], &v, 4); i += 4;
      ble.writeFrame(frame, i);
      break;
    }

    case CMD_MAP_SET_RUN: {
      if (len < 2) { sendErr(MAP_ERR_BAD_ARGS); break; }
      run_mapping = cmd[1] != 0;
      if (run_mapping) next_ping_at = millis();
      uint8_t f[3] = { RESP_MAP_RUN, (uint8_t)(run_mapping ? 1 : 0), stateFlags() };
      ble.writeFrame(f, 3);
      break;
    }

    case CMD_MAP_SET_INTERVAL: {
      if (len < 2 || !setInterval(cmd[1])) { sendErr(MAP_ERR_BAD_ARGS); break; }
      uint8_t f[2] = { RESP_MAP_INTERVAL, interval_s };
      ble.writeFrame(f, 2);
      break;
    }

    default:
      sendErr(MAP_ERR_UNKNOWN_CMD);   // np. komendy companiona - aplikacja wie, ze to tracker
      break;
  }
}

static void syncLoop() {
  if (!sync_active) return;
  if (!ble.isConnected()) { sync_active = false; return; }
  if (ble.isWriteBusy()) return;

  if (sync_left > 0) {
    uint32_t seq;
    int len = maplog.readFrom(sync_next_seq, &frame[1], &seq);
    if (len > 0) {
      frame[0] = RESP_MAP_RECORD;
      ble.writeFrame(frame, 1 + len);
      sync_next_seq = seq + 1;
      sync_last_seq = seq;
      sync_left--;
      sync_sent++;
      return;
    }
    sync_left = 0;   // nie ma wiecej punktow
  }

  // koniec paczki: [END][liczba u16][ostatni seq u32][czy sa dalsze u8]
  int i = 0;
  frame[i++] = RESP_MAP_END;
  memcpy(&frame[i], &sync_sent, 2); i += 2;
  memcpy(&frame[i], &sync_last_seq, 4); i += 4;
  frame[i++] = (sync_sent > 0 && sync_last_seq < maplog.lastSeq()) ? 1 : 0;
  ble.writeFrame(frame, i);
  sync_active = false;
}

static void bleLoop() {
  ble.loop();
  size_t len = ble.checkRecvFrame(frame);
  if (len > 0) {
    uint8_t cmd[MAX_FRAME_SIZE];
    memcpy(cmd, frame, len);
    handleBleFrame(cmd, len);
  }
  syncLoop();
}

// ---------------- setup / loop ----------------
void setup() {
  Serial.begin(115200);
  board.begin();

  bool has_display = false;
#ifdef DISPLAY_CLASS
  if (display.begin()) {
    has_display = true;
    display.startFrame();
    display.drawTextCentered(display.width() / 2, 28, "SF6 Mapper...");
    display.endFrame();
  }
#endif

  if (!radio_init()) { halt(); }
  fast_rng.begin(radio_driver.getRngSeed());
  radio_driver.setParams(SF6_FREQ, SF6_BW, SF6_SF, SF6_CR);   // preset SF6 na sztywno

  // tozsamosc (klucz) w pamieci wewnetrznej
  InternalFS.begin();
  loadSettings();
  IdentityStore store(InternalFS, "");
  if (!store.load("_main", the_mesh.self_id)) {
    the_mesh.self_id = radio_new_identity();
    int count = 0;
    while (count < 10 && (the_mesh.self_id.pub_key[0] == 0x00 || the_mesh.self_id.pub_key[0] == 0xFF)) {
      the_mesh.self_id = radio_new_identity(); count++;
    }
    store.save("_main", the_mesh.self_id);
  }

  // log punktow na flashu QSPI (2 MB); awaryjnie w pamieci wewnetrznej
  if (QSPIFlash.begin()) {
    maplog.begin(&QSPIFlash);
  } else {
    MESH_DEBUG_PRINTLN("QSPI flash niedostepny - log w InternalFS");
    maplog.begin(&InternalFS, 16 * 1024);
  }

  the_mesh.begin();

  sensors.begin();
  sensors.setSettingValue("gps", "1");

  // BLE: nazwa MeshCore-SF6Map-XXXX (XXXX = poczatek klucza publicznego)
  snprintf(ble_name, sizeof(ble_name), "SF6Map-%02X%02X", the_mesh.self_id.pub_key[0], the_mesh.self_id.pub_key[1]);
  ble.begin(BLE_NAME_PREFIX, ble_name, BLE_PIN_CODE);
  ble.enable();

  memset(&status, 0, sizeof(status));
  status.last_n = -1;
  strncpy(status.name, ble_name, sizeof(status.name) - 1);

  ui.begin(has_display);
  next_ping_at = millis() + 3000;

  board.onBootComplete();
}

void loop() {
  the_mesh.loop();
  sensors.loop();
  bleLoop();

  if (ui.takeToggleRunRequest()) {
    run_mapping = !run_mapping;
    if (run_mapping) { next_ping_at = millis(); still_count = 0; }
  }
  int step = ui.takeIntervalStep();
  if (step != 0) {
    int ns = interval_s + step * MAP_INTERVAL_STEP;
    if (ns < MAP_INTERVAL_MIN) ns = MAP_INTERVAL_MIN;
    if (ns > MAP_INTERVAL_MAX) ns = MAP_INTERVAL_MAX;
    setInterval(ns);
  }
  mappingLoop();

  // stan dla ekranu
  LocationProvider* l = gps();
  status.run = run_mapping;
  status.gps_fix = gpsFix();
  status.sats = l ? (int) l->satellitesCount() : 0;
  status.stationary = still_count >= STATIONARY_PINGS;
  status.log_full = maplog.isFull();
  status.ble_connected = ble.isConnected();
  status.syncing = sync_active;
  status.pending = maplog.pending();
  status.interval_s = interval_s;
  long secs = ((long)(next_ping_at - millis()) + 999) / 1000;
  status.next_ping_secs = secs < 0 ? 0 : (int) secs;
  static unsigned long next_batt = 0;
  if ((long)(millis() - next_batt) >= 0) {
    status.batt_mv = board.getBattMilliVolts();
    next_batt = millis() + 30000;
  }
  ui.loop(status);

  rtc_clock.tick();
}
