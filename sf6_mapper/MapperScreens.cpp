#include "MapperScreens.h"
#include <stdio.h>
#include <string.h>

#define MENU_ROWS 5
#define ROW_H     10

void mapperFmtSnr(char* dest, int sz, int8_t snr_x4) {
  int t = (int)snr_x4 * 10 / 4;
  bool neg = t < 0;
  if (neg) t = -t;
  snprintf(dest, sz, "%s%d,%d", neg ? "-" : "", t / 10, t % 10);
}

static void fmtAge(char* d, int sz, uint32_t s) {
  if (s < 60) snprintf(d, sz, "%lus", (unsigned long) s);
  else if (s < 3600) snprintf(d, sz, "%lum", (unsigned long)(s / 60));
  else snprintf(d, sz, "%luh", (unsigned long)(s / 3600));
}

// pasek stanu: czas · GPS + satelity · BT · dzwiek · bateria
static void topBar(MapperGfx& g, const MapperStatus& st, bool blink) {
  char b[16];
  if (st.time_valid) snprintf(b, sizeof(b), "%02u:%02u", st.hh, st.mm);
  else strcpy(b, "--:--");
  g.text(0, 0, b);

  int x = 36;
  if (st.gps_on) {
    if (st.gps_fix || blink) g.iconGps(x, 0, st.gps_fix);
    snprintf(b, sizeof(b), "%d", st.sats);
    g.text(x + 9, 0, b);
  } else {
    g.text(x, 0, "GPS-");
  }
  x = 64;
  if (st.ble_on) g.iconBt(x, 0, st.ble_connected);
  if (!st.buzzer_on) g.iconBell(x + 10, 0, false);

  if (st.usb) g.textRight(127, 0, "USB");
  else { snprintf(b, sizeof(b), "%d%%", st.batt_pct); g.textRight(127, 0, b); }
  g.iconBattery(st.usb ? 92 : 128 - g.textWidth(b) - 18, 0, st.batt_pct, st.usb);
  g.hline(0, 9, 128);
}

static const char* stateLine(const MapperStatus& st, bool blink, bool* warn) {
  *warn = false;
  if (st.syncing) return "ZRZUT DANYCH...";
  if (st.low_batt) { *warn = true; return "SŁABA BATERIA"; }
  if (st.log_full) { *warn = true; return "PAMIĘĆ PEŁNA"; }
  if (st.mem_pct >= 80 && blink) { *warn = true; return "PAMIĘĆ 80% - ZRZUĆ"; }
  if (!st.run) return "STOP";
  if (!st.gps_on) return "GPS WYŁĄCZONY";
  if (!st.gps_fix) return "CZEKAM NA GPS";
  if (st.stationary) return "PAUZA · POSTÓJ";
  return "MAPUJE";
}

void drawMainScreen(MapperGfx& g, const MapperStatus& st, int lock_pct, bool blink) {
  char b[24], c[24];
  topBar(g, st, blink);

  // stan + odliczanie
  bool warn;
  const char* s = stateLine(st, blink, &warn);
  if (warn) { g.rect(0, 11, 128, 9); g.color(false); g.text(2, 12, s); g.color(true); }
  else {
    g.text(0, 12, s);
    if (st.run && st.gps_on && st.gps_fix && !st.stationary && !st.syncing && !st.low_batt && !st.log_full) {
      snprintf(b, sizeof(b), "co %ds · %ds", st.interval_s, st.next_ping_secs);
      g.textRight(127, 12, b);
    }
  }

  // duze liczby
  g.text(0, 23, "PINGI");
  g.textRight(127, 23, "DO WYSŁANIA");
  snprintf(b, sizeof(b), "%lu", (unsigned long) st.sess_pings);
  g.text(0, 33, b, 2);
  snprintf(b, sizeof(b), "%lu", (unsigned long) st.pending);
  g.textRight(127, 33, b, 2);
  g.hline(0, 51, 128);

  // ostatni repeater albo blokada
  if (lock_pct >= 0) {
    g.iconLock(0, 55);
    if (lock_pct > 0) g.progress(11, 55, 117, 8, lock_pct);
    else g.text(11, 55, "Trzymaj OK 3 s");
    return;
  }
  if (st.last_n < 0) { g.text(0, 55, "Ostatni: -"); return; }
  fmtAge(c, sizeof(c), st.last_age_s);
  if (st.last_n == 0) {
    g.text(0, 55, "Brak odpowiedzi");
    g.textRight(127, 55, c);
    return;
  }
  char snr[10];
  mapperFmtSnr(snr, sizeof(snr), st.last_snr_x4);
  snprintf(b, sizeof(b), "%s %s", snr, c);
  int rw = g.textRight(127, 55, b);
  char nm[20];
  const char* name = st.last_name;
  if (st.last_n > 1) { snprintf(c, sizeof(c), "+%d", st.last_n - 1); int w = g.textWidth(c); g.text(128 - rw - w - 4, 55, c); rw += w + 4; }
  MapperGfx::fit(nm, sizeof(nm), name, 128 - rw - 6);
  g.text(0, 55, nm);
}

void drawSessionScreen(MapperGfx& g, const MapperStatus& st) {
  char b[24], nm[20], snr[10];
  snprintf(b, sizeof(b), "SESJA %d", st.sess_no);
  g.text(0, 0, b);
  unsigned long km10 = (st.sess_dist_m + 50) / 100;
  snprintf(b, sizeof(b), "%lu,%lu km", km10 / 10, km10 % 10);
  g.textRight(127, 0, b);
  g.hline(0, 9, 128);

  int pct = st.sess_pings ? (int)(st.sess_hits * 100 / st.sess_pings) : 0;
  snprintf(b, sizeof(b), "%lu pingów  %d%%", (unsigned long) st.sess_pings, pct);
  g.text(0, 11, b);
  unsigned long m = st.sess_secs / 60;
  snprintf(b, sizeof(b), "%lu:%02luh", m / 60, m % 60);
  g.textRight(127, 11, b);
  g.hline(0, 20, 128);

  int y = 23;
  int shown = 0;
  for (int i = 0; i < MAP_RECENT; i++) {
    const MapperRecent& r = st.recent[i];
    if (!r.valid) continue;
    snprintf(b, sizeof(b), "%02u:%02u", r.hh, r.mm);
    g.text(0, y, b);
    mapperFmtSnr(snr, sizeof(snr), r.snr_x4);
    int w = g.textRight(127, y, snr);
    MapperGfx::fit(nm, sizeof(nm), r.name, 128 - 34 - w - 4);
    g.text(34, y, nm);
    y += 8;
    shown++;
  }
  if (!shown) g.textCenter(64, 38, "brak odpowiedzi RPT");
}

void drawGpsScreen(MapperGfx& g, const MapperStatus& st) {
  char b[28];
  g.text(0, 0, "GPS");
  g.textRight(127, 0, !st.gps_on ? "WYŁĄCZONY" : st.gps_fix ? "FIX" : "SZUKA...");
  g.hline(0, 9, 128);
  snprintf(b, sizeof(b), "Satelity: %d", st.sats);
  g.text(0, 12, b);
  g.textRight(127, 12, st.moving ? "w ruchu" : "postój");
  if (st.gps_fix) {
    long la = st.lat, lo = st.lon;
    snprintf(b, sizeof(b), "%ld,%06ld°%c", (la < 0 ? -la : la) / 1000000, (la < 0 ? -la : la) % 1000000, la < 0 ? 'S' : 'N');
    g.text(0, 22, b);
    snprintf(b, sizeof(b), "%ld,%06ld°%c", (lo < 0 ? -lo : lo) / 1000000, (lo < 0 ? -lo : lo) % 1000000, lo < 0 ? 'W' : 'E');
    g.text(0, 31, b);
    snprintf(b, sizeof(b), "Wysokość: %d m", st.alt);
    g.text(0, 40, b);
  } else {
    g.text(0, 26, "brak pozycji");
  }
  if (st.time_valid) {
    snprintf(b, sizeof(b), "%02u.%02u.%04u %02u:%02u:%02u", st.day, st.mon, st.year, st.hh, st.mm, st.ss);
    g.text(0, 55, b);
  } else g.text(0, 55, "czas: brak");
}

void drawMenuScreen(MapperGfx& g, const char* title, const char* const* labels, const char* const* values,
                    int count, int sel, int top) {
  char b[12];
  g.text(0, 0, title);
  snprintf(b, sizeof(b), "%d/%d", sel + 1, count);
  g.textRight(127, 0, b);
  g.hline(0, 9, 128);
  for (int r = 0; r < MENU_ROWS; r++) {
    int i = top + r;
    if (i >= count) break;
    int y = 12 + r * ROW_H;
    if (i == sel) { g.rect(0, y - 1, 128, ROW_H); g.color(false); }
    g.text(2, y, labels[i]);
    if (values[i] && values[i][0]) g.textRight(125, y, values[i]);
    if (i == sel) g.color(true);
  }
}

void drawConfirmScreen(MapperGfx& g, const char* title, const char* l1, const char* l2, const char* hint, int pct) {
  g.rect(0, 0, 128, 10);
  g.color(false); g.textCenter(64, 1, title); g.color(true);
  if (l1) g.textCenter(64, 15, l1);
  if (l2) g.textCenter(64, 25, l2);
  if (pct >= 0) g.progress(8, 37, 112, 8, pct);
  if (hint) g.textCenter(64, 55, hint);
}

void drawLinesScreen(MapperGfx& g, const char* title, const char lines[][MAP_LINE_SZ], int n) {
  g.text(0, 0, title);
  g.hline(0, 9, 128);
  for (int i = 0; i < n && i < 6; i++) g.text(0, 11 + i * 9, lines[i]);
}

void drawMessageScreen(MapperGfx& g, const char* l1, const char* l2) {
  g.textCenter(64, l2 ? 22 : 28, l1);
  if (l2) g.textCenter(64, 34, l2);
}

// Ekran startowy: "SF6 Mapper" wjezdza z prawej (0,5 s, z wyhamowaniem), pod nim fale radiowe
// rozchodza sie od punktu, potem nazwa trackera i wersja firmware.
void drawSplashScreen(MapperGfx& g, unsigned long t, const char* name, const char* version) {
  const int title_w = g.textWidth("SF6 Mapper", 2);
  const int x_end = (128 - title_w) / 2;
  int x = x_end;
  if (t < 500) {
    long p = (long)t * 1000 / 500;                  // 0..1000
    long inv = 1000 - p;
    long ease = 1000 - inv * inv * inv / 1000000;   // wyhamowanie (ease-out cubic)
    x = 128 - (int)((128 - x_end) * ease / 1000);
  }
  g.text(x, 4, "SF6 Mapper", 2);

  if (t >= 300) {                                   // fale: 3 luki co 6 px, rosna od punktu
    const int cx = 64, cy = 41;
    g.rect(cx - 1, cy - 1, 3, 3);
    unsigned long tw = t - 300;
    for (int k = 0; k < 3; k++) {
      int r = (int)((tw / 45 + k * 6) % 18) + 3;
      g.arcTop(cx, cy, r, 22);
    }
  }
  if (t >= 600 && name && name[0]) g.textCenter(64, 45, name);
  if (t >= 900 && version) {
    char b[16];
    snprintf(b, sizeof(b), "v%s", version);
    g.textRight(127, 56, b);
  }
}
