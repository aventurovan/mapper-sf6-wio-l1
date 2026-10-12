#pragma once
#include <Arduino.h>
#include <target.h>

#ifdef PIN_BUZZER
  #include <helpers/ui/buzzer.h>
#endif

#ifndef UI_SCREEN_TIMEOUT_MS
  #define UI_SCREEN_TIMEOUT_MS  60000UL   // wygaszenie ekranu po 60 s bez przycisku
#endif
#ifndef UI_MENU_TIMEOUT_MS
  #define UI_MENU_TIMEOUT_MS    30000UL   // powrot z menu do ekranu stanu
#endif
#define UI_CONFIRM_MS           4000UL    // czas na potwierdzenie STOP w menu

struct MapperStatus {
  bool     run;
  bool     gps_on;
  bool     gps_fix;
  bool     stationary;
  bool     log_full;
  bool     ble_on;
  bool     ble_connected;
  bool     syncing;
  int      sats;
  uint32_t pings;          // od wlaczenia
  uint32_t hits;           // pingi z co najmniej jedna odpowiedzia
  uint32_t pending;        // punkty czekajace na wyslanie
  uint16_t batt_mv;
  int      next_ping_secs;
  int      interval_s;
  int      last_n;         // liczba odpowiedzi w ostatnim pingu (-1 = brak)
  uint8_t  last_prefix[2]; // najlepszy repeater ostatniego pingu
  int8_t   last_snr_rx;
  int8_t   last_snr_tx;
  char     name[24];
};

// sterowanie trackerem z menu (implementacja w main.cpp)
class MapperControl {
public:
  virtual bool isRunning() = 0;        virtual void setRunning(bool on) = 0;
  virtual int  getInterval() = 0;      virtual void setIntervalSecs(int s) = 0;
  virtual bool isGpsOn() = 0;          virtual void setGpsOn(bool on) = 0;
  virtual bool isBleOn() = 0;          virtual void setBleOn(bool on) = 0;
  virtual bool isBuzzerOn() = 0;       virtual void setBuzzerOn(bool on) = 0;
  virtual bool isLockOn() = 0;         virtual void setLockOn(bool on) = 0;
  virtual int  getInfoLines(char lines[][22], int max) = 0;   // strona "Informacje"
};

class MapperUI {
  enum Screen { SCR_STATUS, SCR_MENU, SCR_INFO };
#ifdef DISPLAY_CLASS
  DisplayDriver* _disp;
#endif
  MapperControl* _ctl;
  Screen   _screen;
  int      _sel;              // zaznaczona pozycja menu
  int      _top;              // pierwsza widoczna pozycja menu
  bool     _locked;
  unsigned long _last_activity;
  unsigned long _next_refresh;
  unsigned long _stop_armed;  // pierwsze nacisniecie "STOP" w menu
  unsigned long _flash_until; // krotki komunikat na ekranie
  char     _flash[22];
#ifdef PIN_BUZZER
  genericBuzzer _buzzer;
#endif

  void render(const MapperStatus& st);
  void renderStatus(const MapperStatus& st);
  void renderMenu(const MapperStatus& st);
  void renderInfo();
  void menuAction(int item, int dir);
  void flash(const char* msg);
  void screenOff();
  bool screenOn() const;

public:
  MapperUI() : _ctl(NULL), _screen(SCR_STATUS), _sel(0), _top(0), _locked(false), _last_activity(0),
               _next_refresh(0), _stop_armed(0), _flash_until(0) {
#ifdef DISPLAY_CLASS
    _disp = NULL;
#endif
    _flash[0] = 0;
  }
  void begin(bool has_display, MapperControl* ctl);
  void loop(const MapperStatus& st);
  void notifyHit();      // krotki sygnal po trafieniu
  void applyBuzzer();    // po zmianie ustawienia dzwieku
  void wake() { _last_activity = millis(); _next_refresh = 0; }
};
