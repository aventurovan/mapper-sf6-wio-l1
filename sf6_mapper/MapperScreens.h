#pragma once
/*
 * Ekrany trackera (128x64). Same funkcje rysujace - bez obslugi przyciskow,
 * dzieki czemu da sie je testowac na komputerze.
 */
#include <stdint.h>
#include "MapperGfx.h"

#define MAP_RECENT  5   // ostatnie repeatery na ekranie sesji
#define MAP_LINE_SZ 32  // bajtow na wiersz tekstu (UTF-8)

struct MapperRecent {
  bool    valid;
  uint8_t hh, mm;       // czas lokalny odpowiedzi
  int8_t  snr_x4;       // SNR odbioru x4
  char    name[17];     // nazwa repeatera (UTF-8) albo poczatek klucza
};

struct MapperStatus {
  // czas lokalny (z GPS)
  bool     time_valid;
  uint8_t  hh, mm, ss, day, mon;
  uint16_t year;
  // zasilanie
  uint16_t batt_mv;
  int      batt_pct;
  bool     usb;
  bool     low_batt;
  // GPS
  bool     gps_on, gps_fix, moving;
  int      sats;
  int32_t  lat, lon;    // stopnie x 1e6
  int      alt;
  // BT
  bool     ble_on, ble_connected, syncing;
  // mapowanie
  bool     run, stationary, log_full;
  int      mem_pct;
  int      interval_s;
  int      next_ping_secs;
  uint32_t pending;
  bool     buzzer_on;
  // sesja
  int      sess_no;
  uint32_t sess_pings, sess_hits, sess_dist_m, sess_secs;
  // ostatni ping
  int      last_n;      // -1 brak pingu, 0 = brak odpowiedzi
  char     last_name[17];
  int8_t   last_snr_x4;
  uint32_t last_age_s;
  MapperRecent recent[MAP_RECENT];
  char     name[24];    // nazwa trackera
};

void mapperFmtSnr(char* dest, int sz, int8_t snr_x4);        // "-6,5"

void drawMainScreen(MapperGfx& g, const MapperStatus& st, bool locked, bool blink);
void drawSessionScreen(MapperGfx& g, const MapperStatus& st);
void drawGpsScreen(MapperGfx& g, const MapperStatus& st);
void drawMenuScreen(MapperGfx& g, const char* title, const char* const* labels, const char* const* values,
                    int count, int sel, int top);
void drawConfirmScreen(MapperGfx& g, const char* title, const char* l1, const char* l2, const char* hint, int pct);
void drawLinesScreen(MapperGfx& g, const char* title, const char lines[][MAP_LINE_SZ], int n);
void drawMessageScreen(MapperGfx& g, const char* l1, const char* l2);
