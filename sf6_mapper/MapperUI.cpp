#include "MapperUI.h"

/*
 * Obsluga:
 *  - ekran stanu; srodek joysticka = menu, przycisk wstecz = zgas ekran,
 *  - menu: gora/dol = wybor, srodek/prawo = zmiana, lewo = zmiana w dol, wstecz = wyjscie,
 *  - blokada ekranu (gdy wlaczona): ekran zgaszony = zablokowany; przycisk tylko budzi ekran,
 *    odblokowanie = przytrzymanie srodka joysticka przez 1 s.
 */

#ifdef DISPLAY_CLASS
static MomentaryButton joystick_up(JOYSTICK_UP, 1000, true, false, false);
static MomentaryButton joystick_down(JOYSTICK_DOWN, 1000, true, false, false);
#endif

enum MenuItem { MI_RUN, MI_IVL, MI_SOUND, MI_GPS, MI_BLE, MI_LOCK, MI_INFO, MI_EXIT, MI_COUNT };
#define MENU_ROWS   5     // widoczne wiersze menu
#define ROW_H       10

static void fmtSnr(char* dest, size_t sz, int8_t snr_x4) {
  int t = (int)snr_x4 * 10 / 4;   // dziesiate czesci dB
  char sign = t < 0 ? '-' : '+';
  if (t < 0) t = -t;
  snprintf(dest, sz, "%c%d.%d", sign, t / 10, t % 10);
}

void MapperUI::begin(bool has_display, MapperControl* ctl) {
  _ctl = ctl;
#ifdef DISPLAY_CLASS
  _disp = has_display ? &display : NULL;
  user_btn.begin();
  back_btn.begin();
  joystick_left.begin();
  joystick_right.begin();
  joystick_up.begin();
  joystick_down.begin();
#endif
#ifdef PIN_BUZZER
  _buzzer.begin();
#endif
  applyBuzzer();
  _last_activity = millis();
}

void MapperUI::applyBuzzer() {
#ifdef PIN_BUZZER
  _buzzer.quiet(_ctl ? !_ctl->isBuzzerOn() : false);
#endif
}

void MapperUI::notifyHit() {
#ifdef PIN_BUZZER
  if (_ctl && _ctl->isBuzzerOn() && !_buzzer.isPlaying()) _buzzer.play("hit:d=16,o=6,b=180:c,e");
#endif
}

void MapperUI::flash(const char* msg) {
  strncpy(_flash, msg, sizeof(_flash) - 1);
  _flash[sizeof(_flash) - 1] = 0;
  _flash_until = millis() + 1500;
  _next_refresh = 0;
}

bool MapperUI::screenOn() const {
#ifdef DISPLAY_CLASS
  return _disp && _disp->isOn() && _last_activity != 0;
#else
  return false;
#endif
}

void MapperUI::screenOff() {
#ifdef DISPLAY_CLASS
  if (_disp && _disp->isOn()) _disp->turnOff();
#endif
  _last_activity = 0;
  _screen = SCR_STATUS;
  _stop_armed = 0;
  if (_ctl && _ctl->isLockOn()) _locked = true;     // zgaszony ekran = zablokowany
}

void MapperUI::menuAction(int item, int dir) {
  if (!_ctl) return;
  switch (item) {
    case MI_RUN:
      if (!_ctl->isRunning()) { _ctl->setRunning(true); _stop_armed = 0; flash("Mapowanie WLACZONE"); }
      else if (_stop_armed && millis() - _stop_armed < UI_CONFIRM_MS) { _ctl->setRunning(false); _stop_armed = 0; flash("Mapowanie STOP"); }
      else _stop_armed = millis();                  // pierwsze nacisniecie - prosba o potwierdzenie
      break;
    case MI_IVL: {
      int s = _ctl->getInterval() + (dir < 0 ? -10 : 10);
      if (s > 60) s = 20;                           // w kolko: 20 -> 60 -> 20
      if (s < 20) s = 60;
      _ctl->setIntervalSecs(s);
      break;
    }
    case MI_SOUND: _ctl->setBuzzerOn(!_ctl->isBuzzerOn()); applyBuzzer(); break;
    case MI_GPS:   _ctl->setGpsOn(!_ctl->isGpsOn()); break;
    case MI_BLE:   _ctl->setBleOn(!_ctl->isBleOn()); break;
    case MI_LOCK:  _ctl->setLockOn(!_ctl->isLockOn()); break;
    case MI_INFO:  if (dir >= 0) _screen = SCR_INFO; break;
    case MI_EXIT:  if (dir >= 0) _screen = SCR_STATUS; break;
  }
}

void MapperUI::loop(const MapperStatus& st) {
#ifdef PIN_BUZZER
  _buzzer.loop();
#endif
#ifdef DISPLAY_CLASS
  int ev_ok    = user_btn.check();
  int ev_back  = back_btn.check();
  int ev_left  = joystick_left.check();
  int ev_right = joystick_right.check();
  int ev_up    = joystick_up.check();
  int ev_down  = joystick_down.check();
  bool any = ev_ok || ev_back || ev_left || ev_right || ev_up || ev_down;

  if (any) {
    if (!screenOn()) {                              // zgaszony ekran: przycisk tylko go budzi
      wake();
      if (_disp && !_disp->isOn()) _disp->turnOn();
    } else if (_locked) {
      wake();
      if (ev_ok == BUTTON_EVENT_LONG_PRESS) { _locked = false; flash("Odblokowano"); }
      else if (ev_back == BUTTON_EVENT_CLICK) screenOff();
    } else {
      wake();
      if (_screen == SCR_STATUS) {
        if (ev_ok == BUTTON_EVENT_CLICK) { _screen = SCR_MENU; _stop_armed = 0; }
        else if (ev_back == BUTTON_EVENT_CLICK) screenOff();
      } else if (_screen == SCR_MENU) {
        if (ev_up == BUTTON_EVENT_CLICK)   { _sel = (_sel + MI_COUNT - 1) % MI_COUNT; _stop_armed = 0; }
        if (ev_down == BUTTON_EVENT_CLICK) { _sel = (_sel + 1) % MI_COUNT; _stop_armed = 0; }
        if (ev_ok == BUTTON_EVENT_CLICK || ev_right == BUTTON_EVENT_CLICK) menuAction(_sel, +1);
        if (ev_left == BUTTON_EVENT_CLICK) {
          if (_sel == MI_IVL) menuAction(_sel, -1);
          else if (_sel == MI_INFO || _sel == MI_EXIT) _screen = SCR_STATUS;
          else menuAction(_sel, -1);
        }
        if (ev_back == BUTTON_EVENT_CLICK) _screen = SCR_STATUS;
        if (_sel < _top) _top = _sel;
        if (_sel >= _top + MENU_ROWS) _top = _sel - MENU_ROWS + 1;
      } else if (_screen == SCR_INFO) {
        if (ev_ok == BUTTON_EVENT_CLICK || ev_back == BUTTON_EVENT_CLICK || ev_left == BUTTON_EVENT_CLICK) _screen = SCR_MENU;
      }
    }
    _next_refresh = 0;
  }

  if (_disp == NULL) return;

  // menu wraca do ekranu stanu po chwili bezczynnosci
  if (_screen != SCR_STATUS && _last_activity != 0 && millis() - _last_activity > UI_MENU_TIMEOUT_MS) {
    _screen = SCR_STATUS;
    _next_refresh = 0;
  }
  // wygaszanie ekranu
  if (_last_activity != 0 && millis() - _last_activity > UI_SCREEN_TIMEOUT_MS) {
    screenOff();
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
  _disp->startFrame();
  _disp->setTextSize(1);
  _disp->setColor(UIColor::primary_txt);
  if (_flash_until && (long)(millis() - _flash_until) < 0) {
    _disp->drawTextCentered(_disp->width() / 2, 28, _flash);
  } else {
    _flash_until = 0;
    if (_screen == SCR_MENU) renderMenu(st);
    else if (_screen == SCR_INFO) renderInfo();
    else renderStatus(st);
  }
  _disp->endFrame();
#endif
}

void MapperUI::renderStatus(const MapperStatus& st) {
#ifdef DISPLAY_CLASS
  char buf[32];
  DisplayDriver* d = _disp;

  // wiersz 1: nazwa + BT + bateria
  int pct = ((int)st.batt_mv - 3300) * 100 / (4200 - 3300);
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  d->setCursor(0, 0);
  char nm[13];
  strncpy(nm, st.name[0] ? st.name : "SF6 Mapper", 12);
  nm[12] = 0;
  d->print(nm);
  snprintf(buf, sizeof(buf), "%s%3d%%", !st.ble_on ? "-- " : st.ble_connected ? "BT " : "", pct);
  d->drawTextRightAlign(d->width() - 1, 0, buf);

  // wiersz 2: stan
  const char* state;
  if (st.syncing) state = "SYNCHRONIZACJA";
  else if (st.log_full) state = "PAMIEC PELNA!";
  else if (!st.run) state = "STOP";
  else if (!st.gps_on) state = "GPS WYLACZONY";
  else if (!st.gps_fix) state = "CZEKAM NA GPS";
  else if (st.stationary) state = "PAUZA (postoj)";
  else state = "MAPUJE";
  d->setCursor(0, 11);
  d->print(state);
  if (st.run && st.gps_on && st.gps_fix && !st.stationary && st.next_ping_secs >= 0 && !st.syncing) {
    snprintf(buf, sizeof(buf), "%ds", st.next_ping_secs);
    d->drawTextRightAlign(d->width() - 1, 11, buf);
  }

  // wiersz 3: GPS + interwal
  d->setCursor(0, 22);
  if (!st.gps_on) snprintf(buf, sizeof(buf), "GPS: WYL");
  else snprintf(buf, sizeof(buf), "GPS: %s sat %d", st.gps_fix ? "FIX" : "---", st.sats);
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
    char a[8], b[8];
    fmtSnr(a, sizeof(a), st.last_snr_rx);
    fmtSnr(b, sizeof(b), st.last_snr_tx);
    snprintf(buf, sizeof(buf), "%02X%02X %s/%s x%d", st.last_prefix[0], st.last_prefix[1], a, b, st.last_n);
    d->print(buf);
  }

  // wiersz 6: do wyslania albo informacja o blokadzie
  d->setCursor(0, 55);
  if (_locked) d->print("BLOKADA: trzymaj OK");
  else {
    snprintf(buf, sizeof(buf), "Do wyslania: %lu", (unsigned long)st.pending);
    d->print(buf);
  }
#endif
}

void MapperUI::renderMenu(const MapperStatus& st) {
#ifdef DISPLAY_CLASS
  DisplayDriver* d = _disp;
  char val[12];
  d->setCursor(0, 0);
  d->print("MENU");
  snprintf(val, sizeof(val), "%d/%d", _sel + 1, MI_COUNT);
  d->drawTextRightAlign(d->width() - 1, 0, val);
  d->fillRect(0, 9, d->width(), 1);

  for (int r = 0; r < MENU_ROWS; r++) {
    int item = _top + r;
    if (item >= MI_COUNT) break;
    int y = 12 + r * ROW_H;
    const char* label = "";
    val[0] = 0;
    switch (item) {
      case MI_RUN:
        label = "Mapowanie";
        if (_stop_armed && millis() - _stop_armed < UI_CONFIRM_MS) strcpy(val, "STOP? OK");
        else strcpy(val, _ctl->isRunning() ? "WL" : "STOP");
        break;
      case MI_IVL:   label = "Ping co";    snprintf(val, sizeof(val), "%d s", _ctl->getInterval()); break;
      case MI_SOUND: label = "Dzwiek";     strcpy(val, _ctl->isBuzzerOn() ? "WL" : "WYL"); break;
      case MI_GPS:   label = "GPS";        strcpy(val, _ctl->isGpsOn() ? "WL" : "WYL"); break;
      case MI_BLE:   label = "Bluetooth";  strcpy(val, _ctl->isBleOn() ? "WL" : "WYL"); break;
      case MI_LOCK:  label = "Blokada";    strcpy(val, _ctl->isLockOn() ? "WL" : "WYL"); break;
      case MI_INFO:  label = "Informacje"; strcpy(val, ">"); break;
      case MI_EXIT:  label = "Wyjscie";    break;
    }
    if (item == _sel) {                             // zaznaczenie: odwrocone kolory
      d->fillRect(0, y - 1, d->width(), ROW_H);
      d->setColor(UIColor::window_bkg);
    }
    d->setCursor(2, y);
    d->print(label);
    if (val[0]) d->drawTextRightAlign(d->width() - 3, y, val);
    if (item == _sel) d->setColor(UIColor::primary_txt);
  }
#endif
}

void MapperUI::renderInfo() {
#ifdef DISPLAY_CLASS
  char lines[6][22];
  int n = _ctl ? _ctl->getInfoLines(lines, 6) : 0;
  _disp->setCursor(0, 0);
  _disp->print("INFORMACJE");
  _disp->fillRect(0, 9, _disp->width(), 1);
  for (int i = 0; i < n; i++) {
    _disp->setCursor(0, 12 + i * 9);
    _disp->print(lines[i]);
  }
#endif
}
