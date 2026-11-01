#include "core.h"
#include <algorithm>
#include <cstdlib>

namespace thermo {
uint32_t crc32(const void *data, size_t length) {
    uint32_t crc = UINT32_MAX;
    auto p = static_cast<const uint8_t *>(data);
    while (length--) { crc ^= *p++; for (int i=0; i<8; ++i) crc = (crc >> 1) ^ (0xedb88320u & (0u-(crc & 1u))); }
    return ~crc;
}
bool valid(const Settings &s) {
    return s.revision && s.revision < UINT32_MAX && s.on >= 3000 && s.on <= 8000 &&
        s.hysteresis >= 100 && s.hysteresis <= 2000 && s.on - s.hysteresis >= 2000 &&
        s.force >= s.on + 500 && s.force <= 9500 && s.min_on_s >= 10 && s.min_on_s <= 600 &&
        s.manual_s >= 30 && s.manual_s <= 3600 && s.difference >= 100 && s.difference <= 2000 &&
        s.relay <= Latching && s.pulse_ms >= 20 && s.pulse_ms <= 200 && s.commissioned <= 1;
}
bool same(const Settings &a, const Settings &b) { return !memcmp(&a, &b, sizeof(a)); }
bool valid(const Frame &f) {
    return f.magic == Magic && f.version == 1 && (f.node == 1 || f.node == 2) && f.boot &&
        f.local_vote <= 1 && f.output <= 1 && f.command_sent <= 1 && f.link <= 1 && f.delivered <= 1 && f.saved <= 1 &&
        f.calibration >= -500 && f.calibration <= 500 &&
        (f.raw == InvalidTemp || (f.raw >= -5500 && f.raw <= 12500)) &&
        (f.temperature == InvalidTemp || (f.temperature >= -6000 && f.temperature <= 13000)) &&
        f.manual_left_s <= 3600 && valid(f.settings) && f.crc == crc32(&f, sizeof(f)-4);
}
bool valid(const Stored &s) {
    return s.format == 1 && valid(s.settings) && s.calibration >= -500 && s.calibration <= 500 &&
        s.calibration_revision && s.crc == crc32(&s, sizeof(s)-4);
}
bool Decoder::push(uint8_t byte, Frame &out) {
    bytes[size++] = byte;
    if (size < sizeof(Frame)) return false;
    memcpy(&out, bytes, sizeof(out));
    if (valid(out)) { size=0; return true; }
    ++errors; memmove(bytes, bytes+1, --size); return false;
}
Controller::Controller(uint8_t id, uint32_t session, const Stored &initial, bool persisted)
    : node(id), boot(session ? session : 1), installedRelay(initial.settings.relay), store(initial) {
    sequence=boot & 0x3fffffffu; // Unpredictable starting counter also avoids accepting an old HA ACK after reboot.
    saved=persisted; dirty=!persisted;
}
void Controller::sample(int16_t t, bool ok, uint32_t now) {
    raw=t; sampleAt=now; sampleValid=ok && t>=-5500 && t<=12500;
}
bool Controller::receive(const Frame &f, uint32_t now) {
    if (!valid(f) || f.node == node) return false;
    if (seenPeer && f.boot == peer.boot && !newer(f.sequence, peer.sequence)) return false;
    // A reverse-direction echo is necessary: one-way cable faults are visible at both ends.
    if (f.echo_boot == boot && f.echo_sequence && newer(f.echo_sequence, lastEcho) && !newer(f.echo_sequence, sequence)) {
        seenEcho=true; echoAt=now; lastEcho=f.echo_sequence;
    }
    peer=f; seenPeer=true; peerAt=now;
    if (f.saved && f.settings.revision > store.settings.revision) {
        store.settings=f.settings; saved=false; dirty=true; conflict=false;
    } else if (f.saved && f.settings.revision == store.settings.revision && !same(f.settings,store.settings)) conflict=true;
    if (node == 2 && (peerManualBoot != f.boot || peerManualSequence != f.manual_sequence)) {
        peerManualBoot=f.boot; peerManualSequence=f.manual_sequence;
        if (f.manual_left_s) { manualAt=now; manualDuration=std::min<uint32_t>(f.manual_left_s,store.settings.manual_s)*1000; }
    }
    if (node == 2 && f.calibration_request && (f.calibration_request != calSeen || f.boot != calSeenBoot) &&
        f.requested_calibration >= -500 && f.requested_calibration <= 500) {
        calSeen=f.calibration_request; calSeenBoot=f.boot;
        if (f.requested_calibration != store.calibration) calibrate(f.requested_calibration);
        if (saved) { calAck=calSeen; calAckBoot=calSeenBoot; }
    }
    return true;
}
bool Controller::change(Settings next) {
    if (node != 1 || store.settings.revision >= UINT32_MAX-1) return false;
    next.revision=store.settings.revision+1;
    if (!valid(next)) return false;
    store.settings=next; saved=false; dirty=true; conflict=false; return true;
}
bool Controller::calibrate(int16_t offset) {
    if (offset < -500 || offset > 500 || store.calibration_revision == UINT32_MAX) return false;
    store.calibration=offset; ++store.calibration_revision; saved=false; dirty=true; return true;
}
bool Controller::calibratePeer(int16_t offset) {
    if (node != 1 || offset < -500 || offset > 500) return false;
    calWanted=offset; if (!++calRequest) ++calRequest; return true;
}
void Controller::persisted(uint32_t rev, uint32_t calRev, bool ok) {
    if (rev != store.settings.revision || calRev != store.calibration_revision) return;
    saved=ok; storageError=!ok; dirty=false;
    if (ok && node == 2) { calAck=calSeen; calAckBoot=calSeenBoot; }
}
void Controller::manual(uint32_t now) {
    if (node != 1) return;
    manualAt=now; manualDuration=uint32_t(store.settings.manual_s)*1000; ++manualSequence;
}
bool Controller::peerFresh(uint32_t now) const {
    return seenPeer && seenEcho && age(now,peerAt)<PeerTimeout && age(now,echoAt)<PeerTimeout;
}
bool Controller::synchronized(uint32_t now) const {
    return peerFresh(now) && saved && peer.saved && same(store.settings,peer.settings);
}
Frame Controller::tick(uint32_t now, bool link, bool delivered, bool relayFault, bool commandSent) {
    const auto &s=store.settings;
    bool sensor=sampleValid && age(now,sampleAt)<SensorTimeout;
    int16_t temperature=sensor ? raw+store.calibration : InvalidTemp;
    bool fresh=peerFresh(now);
    bool different=fresh && sensor && !(peer.faults & Sensor) && peer.sample_age_ms<SensorTimeout &&
        peer.temperature!=InvalidTemp && std::abs(int(temperature)-int(peer.temperature))>s.difference;
    faults=(!sensor ? Sensor : NoFault) | (!fresh ? PeerLost : NoFault) | (different ? SensorDifference : NoFault) |
        (!s.commissioned ? NotCommissioned : NoFault) | (!saved || storageError ? Storage : NoFault) |
        (conflict ? ConfigConflict : NoFault) | (s.relay!=installedRelay ? HardwareChange : NoFault) |
        (relayFault ? Relay : NoFault) | (!synchronized(now) ? ConfigPending : NoFault) |
        (!delivered ? OwnRoute : NoFault) | (fresh && !peer.delivered ? PeerRoute : NoFault) |
        (node==1 && calRequest && (!fresh || peer.calibration_ack!=calRequest || peer.calibration_ack_boot!=boot) ? CalibrationPending : NoFault);
    if (!sensor || temperature>=s.on || temperature>=s.force) thermal=true;
    else if (temperature<=s.on-int(s.hysteresis)) thermal=false;
    bool manualActive=manualDuration && age(now,manualAt)<manualDuration;
    bool localVote=thermal || manualActive || (faults & (Sensor|NotCommissioned|Storage|ConfigConflict|HardwareChange|Relay));
    // Never echo the combined output as the local vote (that would latch both controllers ON forever).
    bool request=localVote || !fresh || different || (fresh && peer.local_vote);
    if (request && !output) onAt=now;
    if (request) output=true;
    else if (age(now,onAt)>=uint32_t(s.min_on_s)*1000) output=false;
    Frame f;
    f.node=node; f.boot=boot; f.sequence=++sequence;
    f.raw=sensor?raw:InvalidTemp; f.temperature=temperature; f.calibration=store.calibration;
    f.faults=faults; f.sample_age_ms=uint16_t(std::min<uint32_t>(age(now,sampleAt),UINT16_MAX));
    f.local_vote=localVote; f.output=output; f.command_sent=commandSent; f.link=link; f.delivered=delivered; f.saved=saved;
    f.settings=s; f.calibration_revision=store.calibration_revision;
    f.manual_sequence=manualSequence;
    f.manual_left_s=manualActive ? uint16_t((manualDuration-age(now,manualAt)+999)/1000) : 0;
    if (seenPeer) { f.echo_boot=peer.boot; f.echo_sequence=peer.sequence; }
    f.calibration_request=calRequest; f.requested_calibration=calWanted; f.calibration_ack=calAck; f.calibration_ack_boot=calAckBoot;
    if (faults!=previousFaults) { ++faultEvents; if(faults) lastFault=faults; previousFaults=faults; }
    f.fault_events=faultEvents; f.last_fault=lastFault;
    seal(f); return f;
}
}
