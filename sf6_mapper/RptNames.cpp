#include "RptNames.h"

#define NAMES_FILE   "/sf6map/names.bin"
#define NAMES_MAGIC  0x534D4E31   // "1NMS"

struct __attribute__((packed)) NamesHdr { uint32_t magic; uint32_t crc; uint16_t count; };

void RptNames::begin(FILESYSTEM* fs) {
  _fs = fs;
  _count = 0; _crc = 0;
  File f = _fs->open(NAMES_FILE, FILE_O_READ);
  if (!f) return;
  NamesHdr h;
  if (f.read((uint8_t*)&h, sizeof(h)) == sizeof(h) && h.magic == NAMES_MAGIC && h.count <= RPT_NAMES_MAX) {
    int n = f.read((uint8_t*)_list, h.count * sizeof(RptName));
    if (n == (int)(h.count * sizeof(RptName))) { _count = h.count; _crc = h.crc; }
  }
  f.close();
  for (int i = 0; i < _count; i++) _list[i].name[RPT_NAME_LEN - 1] = 0;
}

bool RptNames::add(const uint8_t* prefix, const char* name, int len) {
  if (_count >= RPT_NAMES_MAX) return false;
  if (len > RPT_NAME_LEN - 1) len = RPT_NAME_LEN - 1;
  RptName& r = _list[_count++];
  memcpy(r.prefix, prefix, MAP_PREFIX_LEN);
  memcpy(r.name, name, len);
  r.name[len] = 0;
  return true;
}

bool RptNames::save(uint32_t crc) {
  _crc = crc;
  _fs->remove(NAMES_FILE);
  File f = _fs->open(NAMES_FILE, FILE_O_WRITE);
  if (!f) return false;
  NamesHdr h = { NAMES_MAGIC, _crc, _count };
  f.write((const uint8_t*)&h, sizeof(h));
  f.write((const uint8_t*)_list, _count * sizeof(RptName));
  f.close();
  return true;
}

void RptNames::erase() {
  _count = 0; _crc = 0;
  if (_fs) _fs->remove(NAMES_FILE);
}

const char* RptNames::find(const uint8_t* prefix) const {
  for (int i = 0; i < _count; i++) if (memcmp(_list[i].prefix, prefix, MAP_PREFIX_LEN) == 0) return _list[i].name;
  return NULL;
}

void RptNames::label(const uint8_t* prefix, char* dest, size_t sz) const {
  const char* n = find(prefix);
  if (n && n[0]) { strncpy(dest, n, sz - 1); dest[sz - 1] = 0; }
  else snprintf(dest, sz, "%02X%02X%02X%02X", prefix[0], prefix[1], prefix[2], prefix[3]);
}
