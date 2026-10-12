#pragma once
/*
 * Nazwy repeaterow (prefiks klucza -> krotka nazwa UTF-8), wysylane z aplikacji Mapper przy zrzucie danych.
 * Trzymane w RAM i w pliku /sf6map/names.bin na flashu.
 */
#include <Arduino.h>
#include <helpers/IdentityStore.h>
#include "MapperProtocol.h"

#define RPT_NAME_LEN   16       // bajtow UTF-8 (z zerem na koncu)
#define RPT_NAMES_MAX  400

struct __attribute__((packed)) RptName {
  uint8_t prefix[MAP_PREFIX_LEN];
  char    name[RPT_NAME_LEN];
};

class RptNames {
  FILESYSTEM* _fs;
  RptName  _list[RPT_NAMES_MAX];
  uint16_t _count;
  uint32_t _crc;
public:
  RptNames() : _fs(NULL), _count(0), _crc(0) { }
  void begin(FILESYSTEM* fs);
  void clear() { _count = 0; }
  bool add(const uint8_t* prefix, const char* name, int len);
  bool save(uint32_t crc);
  void erase();
  const char* find(const uint8_t* prefix) const;
  // nazwa albo poczatek klucza (8 znakow hex)
  void label(const uint8_t* prefix, char* dest, size_t sz) const;
  uint16_t count() const { return _count; }
  uint32_t crc() const { return _crc; }
};
