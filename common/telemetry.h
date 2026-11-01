#pragma once
#include "core.h"
namespace thermo {
#pragma pack(push,1)
struct CompactNode { int16_t temperature, calibration; uint32_t faults; uint8_t flags; uint32_t boot; uint16_t fault_events, last_fault; };
struct ZigbeePacket {
    uint8_t version=1, node=2;
    uint32_t sequence;
    CompactNode local, peer;
    uint32_t peer_sequence;
    Settings settings;
    uint32_t peer_revision;
};
#pragma pack(pop)
static_assert(sizeof(ZigbeePacket)==68,"Zigbee telemetry payload");
inline CompactNode compact(const Frame &f,bool available) {
    return {available?f.temperature:InvalidTemp,f.calibration,available?f.faults:uint32_t(PeerLost),
        uint8_t((available?128:0) | f.output | (f.command_sent<<1) | (f.link<<2) | (f.delivered<<3) | (f.saved<<4)),f.boot,
        uint16_t(f.fault_events),uint16_t(f.last_fault)};
}
inline ZigbeePacket telemetry(const Frame &self,const Frame &peer,bool available) {
    return {1,self.node,self.sequence,compact(self,true),compact(peer,available),peer.sequence,self.settings,peer.settings.revision};
}
}
