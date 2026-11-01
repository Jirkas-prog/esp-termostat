#pragma once
#include "core.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_timer.h"
using namespace thermo;
inline uint32_t now_ms() { return uint32_t(esp_timer_get_time()/1000); }
struct Snapshot { Frame self, peer; bool peer_fresh=false; uint32_t wire_errors=0; };
struct Measurement { int16_t raw; bool valid; uint32_t at; };
enum class CommandType { Settings, Calibration, PeerCalibration, Manual, Commission };
struct Command { CommandType type; Settings settings; int16_t calibration=0; };
struct SaveResult { uint32_t revision, calibration_revision; bool success; };
extern QueueHandle_t measurementQueue, commandQueue, saveQueue, savedQueue;
Snapshot snapshot();
bool command(const Command &c);
void route_link(bool connected);
void route_sent(uint32_t sequence);
void route_ack(uint32_t sequence);
void sensor_start();
void ui_start();
void transport_start();
void relay_early_init();
void relay_start(uint8_t mode);
void relay_tick(bool on, uint16_t pulse_ms, uint32_t now);
bool relay_fault();
bool relay_command_sent();
bool storage_init(Stored &settings);
void storage_start();
bool network_save(const char *json);
void console_start();
char *snapshot_json(const Snapshot &s); // caller frees with cJSON_free
