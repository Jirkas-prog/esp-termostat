#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace thermo {
constexpr uint16_t Magic = 0x5448;
constexpr uint32_t SensorTimeout = 3000, PeerTimeout = 5000;
constexpr int16_t InvalidTemp = INT16_MIN;
enum RelayMode : uint8_t { NC = 0, NO = 1, Latching = 2 };
enum Fault : uint32_t {
    NoFault = 0, Sensor = 1, PeerLost = 2, SensorDifference = 4, NotCommissioned = 8,
    Storage = 16, ConfigConflict = 32, HardwareChange = 64, Relay = 128,
    ConfigPending = 256, OwnRoute = 512, PeerRoute = 1024, CalibrationPending = 2048
};
#pragma pack(push, 1)
struct Settings {
    uint32_t revision = 1;
    int16_t on = 6000;
    uint16_t hysteresis = 500;
    int16_t force = 8500;
    uint16_t min_on_s = 60, manual_s = 600, difference = 500;
    uint8_t relay = NC;
    uint16_t pulse_ms = 50;
    uint8_t commissioned = 0;
};
// Protocol 1, little endian. CRC covers all preceding bytes; no native padding.
struct Frame {
    uint16_t magic = Magic;
    uint8_t version = 1, node = 0;
    uint32_t boot = 0, sequence = 0;
    int16_t raw = InvalidTemp, temperature = InvalidTemp, calibration = 0;
    uint32_t faults = 0;
    uint16_t sample_age_ms = UINT16_MAX;
    uint8_t local_vote = 1, output = 1, command_sent = 0, link = 0, delivered = 0, saved = 0;
    Settings settings;
    uint32_t calibration_revision = 1, manual_sequence = 0;
    uint16_t manual_left_s = 0;
    uint32_t echo_boot = 0, echo_sequence = 0;
    uint32_t calibration_request = 0;
    int16_t requested_calibration = 0;
    uint32_t calibration_ack = 0, calibration_ack_boot = 0;
    uint32_t fault_events = 0, last_fault = 0;
    uint32_t crc = 0;
};
struct Stored {
    uint32_t format = 1;
    Settings settings;
    int16_t calibration = 0;
    uint32_t calibration_revision = 1;
    uint32_t crc = 0;
};
#pragma pack(pop)
static_assert(sizeof(Settings) == 20, "Settings wire layout");
static_assert(sizeof(Frame) == 94, "Frame wire layout");
uint32_t crc32(const void *data, size_t length);
bool valid(const Settings &s);
bool same(const Settings &a, const Settings &b);
bool valid(const Frame &f);
bool valid(const Stored &s);
inline uint32_t age(uint32_t now, uint32_t then) { return now - then; }
inline bool newer(uint32_t a, uint32_t b) { return int32_t(a - b) > 0; }
template<class T> void seal(T &v) { v.crc = crc32(&v, sizeof(v) - 4); }
class Decoder {
    uint8_t bytes[sizeof(Frame)]{};
    size_t size = 0;
public:
    uint32_t errors = 0;
    bool push(uint8_t byte, Frame &out);
};

class Controller {
    uint8_t node;
    uint32_t boot, sequence = 0, sampleAt = 0, peerAt = 0, echoAt = 0, lastEcho = 0;
    uint32_t onAt = 0, manualAt = 0, manualDuration = 0, manualSequence = 0;
    uint32_t peerManualBoot = 0, peerManualSequence = 0, calSeen = 0, calSeenBoot = 0, calAckBoot = 0;
    uint32_t faultEvents = 0, lastFault = 0, previousFaults = UINT32_MAX;
    bool sampleValid = false, seenPeer = false, seenEcho = false, thermal = true, output = true;
    bool saved = false, storageError = false, conflict = false;
    int16_t raw = InvalidTemp;
    uint8_t installedRelay;
public:
    Stored store;
    Frame peer;
    uint32_t faults = 0, calRequest = 0, calAck = 0;
    int16_t calWanted = 0;
    bool dirty = true;
    Controller(uint8_t node, uint32_t boot, const Stored &initial, bool persisted);
    void sample(int16_t rawTemperature, bool ok, uint32_t now);
    bool receive(const Frame &frame, uint32_t now);
    bool change(Settings next); // Single writer: A (node 1).
    bool calibrate(int16_t offset);
    bool calibratePeer(int16_t offset);
    void persisted(uint32_t revision, uint32_t calibrationRevision, bool ok);
    void manual(uint32_t now);
    bool peerFresh(uint32_t now) const;
    bool synchronized(uint32_t now) const;
    Frame tick(uint32_t now, bool link, bool delivered, bool relayFault, bool commandSent);
};
}
