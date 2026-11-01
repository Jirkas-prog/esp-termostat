#include "app.h"
#include "board.h"
#include "onewire_bus.h"
#include "onewire_device.h"
#include "nvs.h"
#include "esp_log.h"
#include <cstring>

static uint8_t crc8(const uint8_t *p,size_t n) {
    uint8_t crc=0;
    while (n--) { uint8_t v=*p++; for (int i=0;i<8;++i) { bool bit=(crc^v)&1; crc>>=1; if (bit) crc^=0x8c; v>>=1; } }
    return crc;
}
static bool select(onewire_bus_handle_t bus,uint64_t address) {
    uint8_t cmd[9]={0x55}; memcpy(cmd+1,&address,8);
    return onewire_bus_reset(bus)==ESP_OK && onewire_bus_write_bytes(bus,cmd,sizeof(cmd))==ESP_OK;
}
static bool discover(onewire_bus_handle_t bus,uint64_t &address) {
    onewire_device_iter_handle_t it=nullptr;
    if (onewire_new_device_iter(bus,&it)!=ESP_OK) return false;
    onewire_device_t dev{}; unsigned count=0; esp_err_t result;
    while ((result=onewire_device_iter_get_next(it,&dev))==ESP_OK) { ++count; address=dev.address; if (count>1) break; }
    onewire_del_device_iter(it);
    if (count!=1 || result!=ESP_ERR_NOT_FOUND || uint8_t(address)!=0x28 || crc8(reinterpret_cast<uint8_t *>(&address),8)) return false;
    // A new probe must not silently inherit the previous probe's calibration.
    nvs_handle_t h; uint64_t bound=0; bool ok=false;
    if (nvs_open("probe",NVS_READWRITE,&h)==ESP_OK) {
        esp_err_t err=nvs_get_u64(h,"rom",&bound);
        if (err==ESP_ERR_NVS_NOT_FOUND) ok=nvs_set_u64(h,"rom",address)==ESP_OK && nvs_commit(h)==ESP_OK;
        else ok=err==ESP_OK && address==bound;
        nvs_close(h);
    }
    if (!ok) { ESP_LOGE("sensor","ROM mismatch or identity storage error"); return false; }
    // Volatile TH/TL marker disappears if the sensor loses power. No EEPROM writes.
    uint8_t cfg[]={0x4e,0x5a,0xa5,0x7f};
    return select(bus,address) && onewire_bus_write_bytes(bus,cfg,sizeof(cfg))==ESP_OK;
}
static void sensor_task(void *) {
    onewire_bus_handle_t bus=nullptr;
    onewire_bus_config_t cfg{}; cfg.bus_gpio_num=SensorPin;
    onewire_bus_rmt_config_t rmt{}; rmt.max_rx_bytes=10;
    if (onewire_new_bus_rmt(&cfg,&rmt,&bus)!=ESP_OK) { ESP_LOGE("sensor","Bus initialization failed"); vTaskDelete(nullptr); return; }
    uint64_t address=0; bool found=false; unsigned good=0;
    for (;;) {
        bool ok=true;
        if (!found) { found=discover(bus,address); good=0; }
        uint8_t convert=0x44;
        ok=found && select(bus,address) && onewire_bus_write_bytes(bus,&convert,1)==ESP_OK;
        // Blocking only this low-priority sensor task. Control/UART run separately.
        vTaskDelay(pdMS_TO_TICKS(800));
        uint8_t data[9]{}; uint8_t read=0xbe;
        ok=ok && select(bus,address) && onewire_bus_write_bytes(bus,&read,1)==ESP_OK &&
            onewire_bus_read_bytes(bus,data,sizeof(data))==ESP_OK && crc8(data,9)==0 &&
            data[2]==0x5a && data[3]==0xa5 && data[4]==0x7f;
        int16_t raw=InvalidTemp;
        if (ok) {
            int16_t sixteenths=int16_t(uint16_t(data[0]) | (uint16_t(data[1])<<8));
            int value=int(sixteenths)*100/16;
            ok=value>=-5500 && value<=12500; if (ok) raw=int16_t(value);
        }
        if (ok) { if (good<3) ++good; } else { good=0; found=false; }
        Measurement m{raw,ok && good>=3,now_ms()}; xQueueOverwrite(measurementQueue,&m);
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}
void sensor_start() { configASSERT(xTaskCreate(sensor_task,"ds18b20",4096,nullptr,5,nullptr)==pdPASS); }
