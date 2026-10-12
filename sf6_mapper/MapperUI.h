#pragma once
#include <Arduino.h>
#include <target.h>

#ifdef PIN_BUZZER
  #include <helpers/ui/buzzer.h>
#endif

#ifndef UI_SCREEN_TIMEOUT_MS
  #define UI_SCREEN_TIMEOUT_MS  60000UL   // wygaszenie ekranu po 60 s bez przycisku
#endif

struct MapperStatus {
  bool     run;
  bool     gps_fix;
  bool     stationary;
  bool     log_full;
  bool     ble_connected;
  bool     syncing;
  int      sats;
  uint32_t pings;          // od wlaczenia
  uint32_t hits;           // pingi z co najmniej jedna odpowiedzia
  uint32_t pending;        // punkty czekajace na wyslanie
  uint16_t batt_mv;
  int      next_ping_secs; // -1 = nie dotyczy
  int      interval_s;     // interwal pingow (s)
  int      last_n;         // liczba odpowiedzi w ostatnim pingu (-1 = brak)
  uint8_t  last_prefix[2]; // najlepszy repeater ostatniego pingu
  int8_t   last_snr_rx;
  int8_t   last_snr_tx;
  char     name[24];
};

class MapperUI {
#ifdef DISPLAY_CLASS
  DisplayDriver* _disp;
#endif
  unsigned long _last_activity;
  unsigned long _next_refresh;
  bool _toggle_run_req;
  int  _interval_step;    // -1 / 0 / +1 - zadanie zmiany interwalu z joysticka
  bool _buzzer_on;
#ifdef PIN_BUZZER
  genericBuzzer _buzzer;
#endif
  void render(const MapperStatus& st);

public:
  MapperUI() : _last_activity(0), _next_refresh(0), _toggle_run_req(false), _interval_step(0), _buzzer_on(true) {
#ifdef DISPLAY_CLASS
    _disp = NULL;
#endif
  }
  void begin(bool has_display);
  void loop(const MapperStatus& st);
  void showMessage(const char* line1, const char* line2 = NULL);
  void notifyHit();      // krotki sygnal po trafieniu
  void wake() { _last_activity = millis(); _next_refresh = 0; }
  bool takeToggleRunRequest() { bool r = _toggle_run_req; _toggle_run_req = false; return r; }
  int  takeIntervalStep() { int r = _interval_step; _interval_step = 0; return r; }
};
