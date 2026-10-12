#pragma once
/*
 * SF6 Mapper - protokol BLE (NUS / BLEUart, ta sama usluga co companion MeshCore).
 * Kazda ramka = jeden zapis BLE, pierwszy bajt = kod komendy/odpowiedzi.
 * Liczby wielobajtowe: little-endian. Opis w PROTOCOL.md.
 */
#include <stdint.h>

#define MAPPER_PROTO_VER      1
#define MAPPER_FW_VERSION     "1.0.1"

// aplikacja -> tracker
#define CMD_MAP_HELLO         0xA0   // []                       -> RESP_MAP_HELLO
#define CMD_MAP_GET           0xA1   // [from_seq u32][max u8]   -> n x RESP_MAP_RECORD + RESP_MAP_END
#define CMD_MAP_ACK           0xA3   // [up_to_seq u32]          -> RESP_MAP_ACK
#define CMD_MAP_SET_RUN       0xA4   // [run u8]                 -> RESP_MAP_RUN
#define CMD_MAP_SET_INTERVAL  0xA5   // [secs u8] 20/30/40/50/60 -> RESP_MAP_INTERVAL

// tracker -> aplikacja
#define RESP_MAP_ERR          0x01   // [err u8]  (ten sam kod co RESP_CODE_ERR companiona)
#define RESP_MAP_HELLO        0xA0
#define RESP_MAP_RECORD       0xA1
#define RESP_MAP_END          0xA2
#define RESP_MAP_ACK          0xA3
#define RESP_MAP_RUN          0xA4
#define RESP_MAP_INTERVAL     0xA5

#define MAP_ERR_UNKNOWN_CMD   1
#define MAP_ERR_BAD_ARGS      2
#define MAP_ERR_BUSY          3

// flagi stanu w RESP_MAP_HELLO
#define MAP_STATE_RUN         0x01   // mapowanie wlaczone
#define MAP_STATE_GPS_FIX     0x02   // jest fix GPS
#define MAP_STATE_STATIONARY  0x04   // pauza - postoj (4 pingi bez ruchu)
#define MAP_STATE_LOG_FULL    0x08   // pamiec pelna, nowe punkty nie sa zapisywane

// interwal pingow (s)
#define MAP_INTERVAL_MIN      20
#define MAP_INTERVAL_MAX      60
#define MAP_INTERVAL_STEP     10
#define MAP_INTERVAL_DEFAULT  20
inline bool mapIntervalValid(int s) {
  return s >= MAP_INTERVAL_MIN && s <= MAP_INTERVAL_MAX && (s % MAP_INTERVAL_STEP) == 0;
}

// pole flags rekordu: bity 0-2 = interwal / 10 (2..6), bity 3-7 zarezerwowane
#define MAP_REC_INTERVAL_MASK 0x07

#define MAP_PREFIX_LEN        8      // bajty klucza publicznego repeatera
#define MAP_MAX_RESP          13     // max odpowiedzi w jednym punkcie (miesci sie w ramce 176 B)

struct __attribute__((packed)) MapResp {
  uint8_t prefix[MAP_PREFIX_LEN]; // poczatek klucza publicznego repeatera
  int8_t  snr_rx;                 // SNR odpowiedzi repeatera u nas  (dB x 4)
  int8_t  snr_tx;                 // SNR naszego pingu u repeatera   (dB x 4)
  int8_t  rssi;                   // RSSI odpowiedzi u nas (dBm)
};

struct __attribute__((packed)) MapRecordHdr {
  uint32_t seq;     // numer kolejny punktu (rosnacy, unikalny na urzadzeniu)
  uint32_t ts;      // czas UNIX (UTC) z GPS
  int32_t  lat;     // stopnie x 1e6
  int32_t  lon;     // stopnie x 1e6
  int16_t  alt;     // wysokosc npm (m)
  uint8_t  sats;    // liczba satelitow
  uint8_t  flags;   // bity 0-2: interwal pingow / 10 s; reszta zarezerwowana
  uint8_t  n;       // liczba odpowiedzi; 0 = pudlo (brak odpowiedzi)
};

#define MAP_RECORD_MAX_LEN  (sizeof(MapRecordHdr) + MAP_MAX_RESP * sizeof(MapResp))   // 164
