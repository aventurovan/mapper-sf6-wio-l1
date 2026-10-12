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
#include <bluefruit.h>

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
#ifndef MOVE_SPREAD_M
  #define MOVE_SPREAD_M       20.0      // ruch = przesuniecie o tyle metrow w oknie ostatnich ~20 s
#endif
#define MOVE_SAMPLE_MS        2000UL    // co ile probka pozycji do wykrywania ruchu
#define MOVE_WINDOW           10        // liczba probek w oknie (10 x 2 s = 20 s)
#define MOVE_MIN_SAMPLES      6         // minimum probek do decyzji (~10 s)
#define FIX_WARMUP_MS         15000UL   // pierwsze pozycje po (ponownym) fixie sa niepewne - pomijamy

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

static char tracker_name[MAP_NAME_MAX + 1];          // nazwa trackera = nick konta w Mapperze
static char ble_full_name[sizeof(BLE_NAME_PREFIX) + MAP_NAME_MAX + 1];

// podpis (logowanie do Mappera)
static uint8_t  sign_buf[MAP_SIGN_MAX];
static uint16_t sign_len = 0;
static bool     sign_active = false;

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

// ustawienia z menu urzadzenia
static bool     buzzer_on = true;
static bool     gps_on = true;
static bool     ble_on = true;
static bool     lock_on = true;

// wykrywanie ruchu: probki pozycji z ostatnich ~20 s
static int32_t  mv_lat[MOVE_WINDOW], mv_lon[MOVE_WINDOW];
static int      mv_count = 0, mv_head = 0;
static unsigned long mv_next = 0;
static unsigned long fix_since = 0;   // od kiedy jest fix (0 = brak)

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
  if (!gps_on) f |= MAP_STATE_GPS_OFF;
  return f;
}

// ---------------- ustawienia ----------------
#define SETTINGS_FILE   "/sf6cfg"

struct __attribute__((packed)) MapperSettings {
  uint8_t magic;
  uint8_t interval_s;
  char    name[MAP_NAME_MAX + 1];   // od v1.0.2
  uint8_t buzzer_on;                // od v1.0.3
  uint8_t gps_on;
  uint8_t ble_on;
  uint8_t lock_on;
};
#define SETTINGS_V102_SIZE  (2 + MAP_NAME_MAX + 1)
#define SETTINGS_MAGIC  0x5A

static bool nameValid(const char* n, int len) {
  if (len < MAP_NAME_MIN || len > MAP_NAME_MAX) return false;
  for (int i = 0; i < len; i++) {
    if (n[i] < 0x21 || n[i] > 0x7E) return false;   // ASCII bez spacji i znakow sterujacych
  }
  return true;
}

static void loadSettings() {
  File f = InternalFS.open(SETTINGS_FILE, FILE_O_READ);
  if (!f) return;
  MapperSettings st;
  memset(&st, 0, sizeof(st));
  int n = f.read((uint8_t*)&st, sizeof(st));
  f.close();
  if (n < 2 || st.magic != SETTINGS_MAGIC) return;
  if (mapIntervalValid(st.interval_s)) interval_s = st.interval_s;
  if (n >= SETTINGS_V102_SIZE) {                    // plik z v1.0.2+ zawiera nazwe
    st.name[MAP_NAME_MAX] = 0;
    if (nameValid(st.name, strlen(st.name))) strcpy(tracker_name, st.name);
  }
  if (n == sizeof(st)) {                            // v1.0.3+: ustawienia z menu
    buzzer_on = st.buzzer_on != 0;
    gps_on    = st.gps_on != 0;
    ble_on    = st.ble_on != 0;
    lock_on   = st.lock_on != 0;
  }
}

static void saveSettings() {
  MapperSettings st;
  memset(&st, 0, sizeof(st));
  st.magic = SETTINGS_MAGIC;
  st.interval_s = interval_s;
  strncpy(st.name, tracker_name, MAP_NAME_MAX);
  st.buzzer_on = buzzer_on; st.gps_on = gps_on; st.ble_on = ble_on; st.lock_on = lock_on;
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

// ---------------- wykrywanie ruchu ----------------
// Probka pozycji co 2 s. Po utracie fixu okno jest czyszczone, a pierwsze 15 s po nowym fixie pomijane
// (pozycja "skacze"). Ruch to rzeczywiste przemieszczanie w oknie ~20 s, a nie pojedynczy skok pozycji.
static void movementLoop() {
  if (!gps_on || !gpsFix()) { fix_since = 0; mv_count = 0; return; }
  unsigned long now = millis();
  if (fix_since == 0) { fix_since = now; mv_count = 0; }
  if (now - fix_since < FIX_WARMUP_MS) return;
  if ((long)(now - mv_next) < 0) return;
  mv_next = now + MOVE_SAMPLE_MS;
  LocationProvider* l = gps();
  mv_lat[mv_head] = l->getLatitude();
  mv_lon[mv_head] = l->getLongitude();
  mv_head = (mv_head + 1) % MOVE_WINDOW;
  if (mv_count < MOVE_WINDOW) mv_count++;
}

// czy w ostatnich ~20 s tracker faktycznie sie przemieszcza. Pojedynczy skok pozycji GPS daje
// przesuniecie tylko w jednej polowie okna - prawdziwy ruch przesuwa pozycje w obu polowach.
static bool isMoving() {
  if (mv_count < MOVE_MIN_SAMPLES) return false;
  int newest = (mv_head + MOVE_WINDOW - 1) % MOVE_WINDOW;
  int oldest = (mv_head + MOVE_WINDOW - mv_count) % MOVE_WINDOW;
  int mid    = (oldest + mv_count / 2) % MOVE_WINDOW;
  double total = distMeters(mv_lat[oldest], mv_lon[oldest], mv_lat[newest], mv_lon[newest]);
  double first = distMeters(mv_lat[oldest], mv_lon[oldest], mv_lat[mid], mv_lon[mid]);
  double second = distMeters(mv_lat[mid], mv_lon[mid], mv_lat[newest], mv_lon[newest]);
  return total >= MOVE_SPREAD_M && first >= MOVE_SPREAD_M * 0.3 && second >= MOVE_SPREAD_M * 0.3;
}

// ---------------- mapowanie ----------------
static void startPing() {
  LocationProvider* l = gps();
  int32_t lat = l->getLatitude();
  int32_t lon = l->getLongitude();

  // wykrywanie postoju
  bool far = !have_last_pos || distMeters(last_lat, last_lon, lat, lon) >= MOVE_THRESHOLD_M;
  if (still_count >= STATIONARY_PINGS) {            // pauza: wznawiamy tylko przy prawdziwym ruchu
    if (!far) { next_ping_at = millis() + 2000; return; }
    if (!isMoving()) {
      // pozycja przeskoczyla (nowy fix, odbicia), ale tracker stoi - przesun punkt postoju, bez pingu
      last_lat = lat; last_lon = lon;
      next_ping_at = millis() + 2000;
      return;
    }
    still_count = 1;                                // ruszylismy
    last_lat = lat; last_lon = lon; have_last_pos = true;
  } else if (!far) {
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
  if (!run_mapping || !gps_on || maplog.isFull() || !gpsFix()) return;
  if ((long)(millis() - next_ping_at) >= 0) startPing();
}

// ---------------- nazwa BLE ----------------
// pelna nazwa rozgloszeniowa: "MeshCore-" + nazwa trackera (filtr aplikacji szuka "MeshCore")
static void buildBleName() {
  snprintf(ble_full_name, sizeof(ble_full_name), "%s%s", BLE_NAME_PREFIX, tracker_name);
}

// zmiana nazwy w trakcie pracy: nowa nazwa w GAP i w odpowiedzi na skanowanie;
// zadziala od nastepnego rozgloszenia (po rozlaczeniu telefonu)
static void applyBleName() {
  buildBleName();
  Bluefruit.setName(ble_full_name);
  Bluefruit.ScanResponse.clearData();
  Bluefruit.ScanResponse.addName();
  strncpy(status.name, tracker_name, sizeof(status.name) - 1);
  status.name[sizeof(status.name) - 1] = 0;
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
  frame[i++] = (uint8_t) fl;
  memcpy(&frame[i], fw, fl); i += fl;
  int nl = strlen(tracker_name);
  frame[i++] = (uint8_t) nl;
  memcpy(&frame[i], tracker_name, nl); i += nl;
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

    case CMD_MAP_SET_NAME: {
      int nl = (int)len - 1;
      if (!nameValid((const char*)&cmd[1], nl)) { sendErr(MAP_ERR_BAD_ARGS); break; }
      memcpy(tracker_name, &cmd[1], nl);
      tracker_name[nl] = 0;
      saveSettings();
      applyBleName();
      frame[0] = RESP_MAP_NAME;
      memcpy(&frame[1], tracker_name, nl);
      ble.writeFrame(frame, 1 + nl);
      break;
    }

    case CMD_SIGN_START: {
      sign_active = true;
      sign_len = 0;
      uint8_t f[6] = { RESP_SIGN_START, 0, 0, 0, 0, 0 };
      uint32_t mx = MAP_SIGN_MAX;
      memcpy(&f[2], &mx, 4);
      ble.writeFrame(f, 6);
      break;
    }

    case CMD_SIGN_DATA: {
      if (!sign_active) { sendErr(MAP_ERR_BAD_STATE); break; }
      if (len < 2) { sendErr(MAP_ERR_BAD_ARGS); break; }
      if (sign_len + (len - 1) > MAP_SIGN_MAX) { sign_active = false; sendErr(MAP_ERR_TOO_LONG); break; }
      memcpy(&sign_buf[sign_len], &cmd[1], len - 1);
      sign_len += len - 1;
      uint8_t f[1] = { RESP_OK };
      ble.writeFrame(f, 1);
      break;
    }

    case CMD_SIGN_FINISH: {
      if (!sign_active) { sendErr(MAP_ERR_BAD_STATE); break; }
      frame[0] = RESP_SIGNATURE;
      the_mesh.self_id.sign(&frame[1], sign_buf, sign_len);
      sign_active = false;
      sign_len = 0;
      ble.writeFrame(frame, 1 + SIGNATURE_SIZE);
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

// ---------------- sterowanie z menu urzadzenia ----------------
static void applyGps() { sensors.setSettingValue("gps", gps_on ? "1" : "0"); if (!gps_on) { fix_since = 0; mv_count = 0; } }
static void applyBle() { if (ble_on) ble.enable(); else { sync_active = false; ble.disable(); } }

class TrackerControl : public MapperControl {
public:
  bool isRunning() override { return run_mapping; }
  void setRunning(bool on) override {
    run_mapping = on;
    if (on) { next_ping_at = millis(); still_count = 0; }
  }
  int  getInterval() override { return interval_s; }
  void setIntervalSecs(int s) override { setInterval(s); }
  bool isGpsOn() override { return gps_on; }
  void setGpsOn(bool on) override { gps_on = on; saveSettings(); applyGps(); }
  bool isBleOn() override { return ble_on; }
  void setBleOn(bool on) override { ble_on = on; saveSettings(); applyBle(); }
  bool isBuzzerOn() override { return buzzer_on; }
  void setBuzzerOn(bool on) override { buzzer_on = on; saveSettings(); }
  bool isLockOn() override { return lock_on; }
  void setLockOn(bool on) override { lock_on = on; saveSettings(); }
  int  getInfoLines(char lines[][22], int max) override {
    int n = 0;
    if (n < max) snprintf(lines[n++], 22, "%s", tracker_name);
    if (n < max) snprintf(lines[n++], 22, "BT PIN: %lu", (unsigned long) BLE_PIN_CODE);
    if (n < max) snprintf(lines[n++], 22, "Klucz: %02X%02X%02X%02X", the_mesh.self_id.pub_key[0], the_mesh.self_id.pub_key[1],
                          the_mesh.self_id.pub_key[2], the_mesh.self_id.pub_key[3]);
    if (n < max) snprintf(lines[n++], 22, "Firmware: %s", MAPPER_FW_VERSION);
    uint32_t pct = maplog.maxBytes() ? (uint32_t)((uint64_t) maplog.fileBytes() * 100 / maplog.maxBytes()) : 0;
    if (n < max) snprintf(lines[n++], 22, "Pamiec: %lu%% %lu pkt", (unsigned long) pct, (unsigned long) maplog.pending());
    if (n < max) snprintf(lines[n++], 22, "Wszystkie: %lu pkt", (unsigned long) maplog.total());
    return n;
  }
};
static TrackerControl control;

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
  applyGps();

  // BLE: nazwa MeshCore-SF6Map-XXXX (XXXX = poczatek klucza publicznego)
  if (tracker_name[0] == 0) {   // domyslna nazwa: SF6Map-XXXX (poczatek klucza)
    snprintf(tracker_name, sizeof(tracker_name), "SF6Map-%02X%02X", the_mesh.self_id.pub_key[0], the_mesh.self_id.pub_key[1]);
  }
  ble.begin(BLE_NAME_PREFIX, tracker_name, BLE_PIN_CODE);
  if (ble_on) ble.enable();

  memset(&status, 0, sizeof(status));
  status.last_n = -1;
  strncpy(status.name, tracker_name, sizeof(status.name) - 1);

  ui.begin(has_display, &control);
  next_ping_at = millis() + 3000;

  board.onBootComplete();
}

void loop() {
  the_mesh.loop();
  sensors.loop();
  bleLoop();

  movementLoop();
  mappingLoop();

  // stan dla ekranu
  LocationProvider* l = gps();
  status.run = run_mapping;
  status.gps_on = gps_on;
  status.ble_on = ble_on;
  status.gps_fix = gps_on && gpsFix();
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
