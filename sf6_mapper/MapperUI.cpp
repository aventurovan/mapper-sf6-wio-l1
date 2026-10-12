#include "MapperUI.h"

static void fmtSnr(char* dest, size_t sz, int8_t snr_x4) {
  int t = (int)snr_x4 * 10 / 4;   // dziesiate czesci dB
  char sign = t < 0 ? '-' : '+';
  if (t < 0) t = -t;
  snprintf(dest, sz, "%c%d.%d", sign, t / 10, t % 10);
}

void MapperUI::begin(bool has_display) {
#ifdef DISPLAY_CLASS
  _disp = has_display ? &display : NULL;
  user_btn.begin();
  back_btn.begin();
  joystick_left.begin();
  joystick_right.begin();
#endif
#ifdef PIN_BUZZER
  _buzzer.begin();
  _buzzer.quiet(!_buzzer_on);
#endif
  _last_activity = millis();
}

void MapperUI::showMessage(const char* line1, const char* line2) {
#ifdef DISPLAY_CLASS
  if (_disp == NULL) return;
  if (!_disp->isOn()) _disp->turnOn();
  _disp->startFrame();
  _disp->setTextSize(1);
  _disp->setColor(UIColor::primary_txt);
  _disp->drawTextCentered(_disp->width() / 2, 22, line1);
  if (line2) _disp->drawTextCentered(_disp->width() / 2, 36, line2);
  _disp->endFrame();
#endif
}

void MapperUI::notifyHit() {
#ifdef PIN_BUZZER
  if (_buzzer_on && !_buzzer.isPlaying()) _buzzer.play("hit:d=16,o=6,b=180:c,e");
#endif
}

void MapperUI::loop(const MapperStatus& st) {
#ifdef PIN_BUZZER
  _buzzer.loop();
#endif
#ifdef DISPLAY_CLASS
  // --- przyciski ---
  int ev = user_btn.check();               // srodek joysticka
  if (ev == BUTTON_EVENT_CLICK) {
    if (_disp && !_disp->isOn()) {
      wake();                              // pierwsze klikniecie tylko budzi ekran
    } else {
      _toggle_run_req = true;
      wake();
    }
  } else if (ev == BUTTON_EVENT_LONG_PRESS) {   // dlugie nacisniecie srodka: dzwiek wl./wyl.
    _buzzer_on = !_buzzer_on;
#ifdef PIN_BUZZER
    _buzzer.quiet(!_buzzer_on);
#endif
    wake();
  }
  if (back_btn.check() == BUTTON_EVENT_CLICK) {
    if (_disp && _disp->isOn()) {
      _disp->turnOff();
      _last_activity = 0;
    } else {
      wake();
    }
  }
  // joystick lewo/prawo: krotszy / dluzszy interwal pingow
  bool screen_on = _disp && _disp->isOn();
  if (joystick_left.check() == BUTTON_EVENT_CLICK) {
    if (screen_on) _interval_step = -1;
    wake();
  }
  if (joystick_right.check() == BUTTON_EVENT_CLICK) {
    if (screen_on) _interval_step = +1;
    wake();
  }

  if (_disp == NULL) return;

  // --- wygaszanie ekranu ---
  if (_last_activity != 0 && millis() - _last_activity > UI_SCREEN_TIMEOUT_MS) {
    if (_disp->isOn()) _disp->turnOff();
    _last_activity = 0;
    return;
  }
  if (_last_activity == 0) return;   // ekran wylaczony

  if ((long)(millis() - _next_refresh) < 0) return;
  _next_refresh = millis() + 500;
  if (!_disp->isOn()) _disp->turnOn();
  render(st);
#endif
}

void MapperUI::render(const MapperStatus& st) {
#ifdef DISPLAY_CLASS
  char buf[32];
  DisplayDriver* d = _disp;
  d->startFrame();
  d->setTextSize(1);
  d->setColor(UIColor::primary_txt);

  // wiersz 1: nazwa + BT + bateria
  int pct = ((int)st.batt_mv - 3300) * 100 / (4200 - 3300);
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  d->setCursor(0, 0);
  d->print("SF6 Mapper");
  snprintf(buf, sizeof(buf), "%s%3d%%", st.ble_connected ? "BT " : "", pct);
  d->drawTextRightAlign(d->width() - 1, 0, buf);

  // wiersz 2: stan
  const char* state;
  if (st.syncing) state = "SYNCHRONIZACJA";
  else if (st.log_full) state = "PAMIEC PELNA!";
  else if (!st.run) state = "STOP";
  else if (!st.gps_fix) state = "CZEKAM NA GPS";
  else if (st.stationary) state = "PAUZA (postoj)";
  else state = "MAPUJE";
  d->setCursor(0, 11);
  d->print(state);
  if (st.run && st.gps_fix && !st.stationary && st.next_ping_secs >= 0 && !st.syncing) {
    snprintf(buf, sizeof(buf), "%ds", st.next_ping_secs);
    d->drawTextRightAlign(d->width() - 1, 11, buf);
  }

  // wiersz 3: GPS
  d->setCursor(0, 22);
  snprintf(buf, sizeof(buf), "GPS: %s sat %d", st.gps_fix ? "FIX" : "---", st.sats);
  d->print(buf);
  snprintf(buf, sizeof(buf), "<%ds>", st.interval_s);
  d->drawTextRightAlign(d->width() - 1, 22, buf);

  // wiersz 4: pingi / trafienia
  d->setCursor(0, 33);
  snprintf(buf, sizeof(buf), "Ping %lu  traf %lu", (unsigned long)st.pings, (unsigned long)st.hits);
  d->print(buf);

  // wiersz 5: ostatni wynik
  d->setCursor(0, 44);
  if (st.last_n < 0) {
    d->print("Ostatni: -");
  } else if (st.last_n == 0) {
    d->print("Ostatni: brak odp.");
  } else {
    // SNR (x4) -> dB z jednym miejscem po przecinku, bez printf float (newlib-nano)
    char a[8], b[8];
    fmtSnr(a, sizeof(a), st.last_snr_rx);
    fmtSnr(b, sizeof(b), st.last_snr_tx);
    snprintf(buf, sizeof(buf), "%02X%02X %s/%s x%d", st.last_prefix[0], st.last_prefix[1], a, b, st.last_n);
    d->print(buf);
  }

  // wiersz 6: do wyslania
  d->setCursor(0, 55);
  snprintf(buf, sizeof(buf), "Do wyslania: %lu", (unsigned long)st.pending);
  d->print(buf);

  d->endFrame();
#endif
}
