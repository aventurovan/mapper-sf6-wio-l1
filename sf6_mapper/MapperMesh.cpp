#include "MapperMesh.h"
#include <target.h>

bool MapperMesh::sendDiscover() {
  uint8_t data[10];
  data[0] = CTL_TYPE_NODE_DISCOVER_REQ | 0x01;     // prefix_only=1: repeatery odsylaja 8 B klucza (krotszy pakiet)
  data[1] = (1 << ADV_TYPE_REPEATER);              // filtr: tylko repeatery
  getRNG()->random(&data[2], 4);                   // tag do dopasowania odpowiedzi
  memcpy(&_pending_tag, &data[2], 4);
  uint32_t since = 0;
  memcpy(&data[6], &since, 4);

  _resp_count = 0;
  _collecting = true;

  mesh::Packet* pkt = createControlData(data, sizeof(data));
  if (pkt == NULL) {
    _collecting = false;
    _pending_tag = 0;
    return false;
  }
  sendZeroHop(pkt);
  return true;
}

int MapperMesh::finishCollect(MapResp* dest) {
  _collecting = false;
  _pending_tag = 0;
  if (_resp_count > 0) memcpy(dest, _resp, _resp_count * sizeof(MapResp));
  return _resp_count;
}

void MapperMesh::onControlDataRecv(mesh::Packet* packet) {
  if (!_collecting || _pending_tag == 0) return;
  if (packet->payload_len < 6) return;

  uint8_t type = packet->payload[0] & 0xF0;
  if (type != CTL_TYPE_NODE_DISCOVER_RESP) return;
  if ((packet->payload[0] & 0x0F) != ADV_TYPE_REPEATER) return;

  uint32_t tag;
  memcpy(&tag, &packet->payload[2], 4);
  if (tag != _pending_tag) return;

  // odpowiedz zawiera 8 B prefiksu (prefix_only) albo pelny klucz (starsze/inne wersje)
  if (packet->payload_len < 6 + MAP_PREFIX_LEN) return;
  const uint8_t* key = &packet->payload[6];

  // nie dubluj tego samego repeatera
  for (int i = 0; i < _resp_count; i++) {
    if (memcmp(_resp[i].prefix, key, MAP_PREFIX_LEN) == 0) return;
  }
  if (_resp_count >= MAP_MAX_RESP) return;

  MapResp& r = _resp[_resp_count++];
  memcpy(r.prefix, key, MAP_PREFIX_LEN);
  r.snr_rx = packet->_snr;                      // jak my slyszymy repeater (x4)
  r.snr_tx = (int8_t) packet->payload[1];       // jak repeater uslyszal nasz ping (x4)
  float rssi = radio_driver.getLastRSSI();
  if (rssi < -127) rssi = -127;
  if (rssi > 0) rssi = 0;
  r.rssi = (int8_t) rssi;
  _last_resp_millis = millis();
}
