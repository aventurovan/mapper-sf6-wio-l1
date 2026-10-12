#pragma once
/*
 * Minimalny wezel MeshCore dla mappera:
 *  - nie przekazuje cudzych pakietow, nie wysyla adwertow,
 *  - wysyla zero-hop DISCOVER_REQ (tylko repeatery) i zbiera odpowiedzi.
 */
#include <Arduino.h>
#include <Mesh.h>
#include <helpers/ArduinoHelpers.h>
#include <helpers/StaticPoolPacketManager.h>
#include <helpers/SimpleMeshTables.h>
#include <helpers/AdvertDataHelpers.h>
#include "MapperProtocol.h"

#define CTL_TYPE_NODE_DISCOVER_REQ   0x80
#define CTL_TYPE_NODE_DISCOVER_RESP  0x90

class MapperMesh : public mesh::Mesh {
  uint32_t _pending_tag;
  bool     _collecting;
  int      _resp_count;
  MapResp  _resp[MAP_MAX_RESP];
  uint32_t _last_resp_millis;

protected:
  void onControlDataRecv(mesh::Packet* packet) override;
  bool allowPacketForward(const mesh::Packet* packet) override { return false; }

public:
  MapperMesh(mesh::Radio& radio, mesh::MillisecondClock& ms, mesh::RNG& rng, mesh::RTCClock& rtc, mesh::MeshTables& tables)
    : mesh::Mesh(radio, ms, rng, rtc, *new StaticPoolPacketManager(16), tables),
      _pending_tag(0), _collecting(false), _resp_count(0), _last_resp_millis(0) { }

  // wysyla ping discovery i zaczyna zbieranie odpowiedzi
  bool sendDiscover();
  // konczy zbieranie; zwraca liczbe odpowiedzi (skopiowanych do dest)
  int  finishCollect(MapResp* dest);

  bool isCollecting() const { return _collecting; }
  int  respCount() const { return _resp_count; }
  const MapResp* lastResp() const { return _resp_count > 0 ? &_resp[_resp_count - 1] : NULL; }
};
