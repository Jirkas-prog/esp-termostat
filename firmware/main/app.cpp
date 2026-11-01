#include "app.h"
#include "board.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_task_wdt.h"
#include "esp_random.h"
#include "esp_log.h"
#include "esp_system.h"
#if CONFIG_IDF_TARGET_ESP32C6
#include "hal/uart_ll.h"
#include "esp_private/uart_share_hw_ctrl.h"
#endif
#include <atomic>

QueueHandle_t measurementQueue, commandQueue, saveQueue, savedQueue;
static QueueHandle_t peerQueue;
static portMUX_TYPE stateMux=portMUX_INITIALIZER_UNLOCKED;
static Snapshot current;
static std::atomic<bool> connected{false}, acknowledged{false};
static std::atomic<uint32_t> lastAck{0}, pendingAck{0}, sentAt{0}, wireErrors{0};
static Stored initial;
static bool loaded=false;
Snapshot snapshot() {
    portENTER_CRITICAL(&stateMux); Snapshot s=current; portEXIT_CRITICAL(&stateMux); return s;
}
bool command(const Command &c) { return xQueueSend(commandQueue,&c,0)==pdTRUE; }
void route_link(bool value) { connected=value; }
void route_sent(uint32_t seq) { sentAt=now_ms(); pendingAck=seq; }
void route_ack(uint32_t seq) {
    if (seq && age(now_ms(),sentAt.load())<15000 && pendingAck.compare_exchange_strong(seq,0)) {
        lastAck=now_ms(); acknowledged=true;
    }
}
static void link_task(void *) {
    uart_config_t cfg{};
    cfg.baud_rate=115200; cfg.data_bits=UART_DATA_8_BITS; cfg.parity=UART_PARITY_DISABLE;
    cfg.stop_bits=UART_STOP_BITS_1; cfg.flow_ctrl=UART_HW_FLOWCTRL_DISABLE; cfg.source_clk=UART_SCLK_DEFAULT;
#if CONFIG_IDF_TARGET_ESP32C6
    // Same IDF 5.3.1 C6 clock-gate workaround proven in reference IRrem.
    HP_UART_SRC_CLK_ATOMIC() {
        uart_ll_set_sclk(UART_LL_GET_HW(UART_NUM_1),soc_module_clk_t(UART_SCLK_DEFAULT));
        uart_ll_sclk_enable(UART_LL_GET_HW(UART_NUM_1));
    }
#endif
    ESP_ERROR_CHECK(uart_param_config(UART_NUM_1,&cfg));
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_1,2048,1024,0,nullptr,0));
    ESP_ERROR_CHECK(uart_set_pin(UART_NUM_1,PeerTx,PeerRx,UART_PIN_NO_CHANGE,UART_PIN_NO_CHANGE));
    Decoder decoder; Frame f; uint32_t sent=now_ms()-1000;
    for (;;) {
        uint8_t data[128];
        int n=uart_read_bytes(UART_NUM_1,data,sizeof(data),pdMS_TO_TICKS(20));
        for (int i=0;i<n;++i) if (decoder.push(data[i],f)) xQueueOverwrite(peerQueue,&f);
        wireErrors=decoder.errors;
        if (age(now_ms(),sent)>=1000) {
            auto s=snapshot();
            if (s.self.node) uart_write_bytes(UART_NUM_1,&s.self,sizeof(s.self));
            sent=now_ms();
        }
    }
}
static void control_task(void *) {
    uint32_t boot=esp_random(); if (!boot) boot=1;
    Controller ctl(NodeId,boot,initial,loaded);
    ESP_ERROR_CHECK(esp_task_wdt_add(nullptr));
    TickType_t wake=xTaskGetTickCount();
    uint32_t previousFaults=UINT32_MAX, wdAt=0; bool wd=false;
    for (;;) {
        uint32_t now=now_ms(); Measurement measure; Frame peer; SaveResult saved;
        if (xQueueReceive(measurementQueue,&measure,0)==pdTRUE) ctl.sample(measure.raw,measure.valid,measure.at);
        if (xQueueReceive(peerQueue,&peer,0)==pdTRUE) ctl.receive(peer,now);
        if (xQueueReceive(savedQueue,&saved,0)==pdTRUE) ctl.persisted(saved.revision,saved.calibration_revision,saved.success);
        Command cmd;
        for (int i=0;i<4 && xQueueReceive(commandQueue,&cmd,0)==pdTRUE;++i) {
            switch (cmd.type) {
            case CommandType::Settings: ctl.change(cmd.settings); break;
            case CommandType::Calibration: ctl.calibrate(cmd.calibration); break;
            case CommandType::PeerCalibration: ctl.calibratePeer(cmd.calibration); break;
            case CommandType::Manual: ctl.manual(now); break;
            case CommandType::Commission: { auto cfg=ctl.store.settings; cfg.commissioned=1; ctl.change(cfg); break; }
            }
        }
        if (ctl.dirty) { xQueueOverwrite(saveQueue,&ctl.store); ctl.dirty=false; }
        bool delivered=connected && acknowledged && age(now,lastAck)<45000;
        Frame self=ctl.tick(now,connected,delivered,relay_fault(),relay_command_sent());
        relay_tick(self.output,self.settings.pulse_ms,now);
        Snapshot s{self,ctl.peer,ctl.peerFresh(now),wireErrors.load()};
        portENTER_CRITICAL(&stateMux); current=s; portEXIT_CRITICAL(&stateMux);
        // External watchdog is fed by this task only, AFTER the control step.
        if (age(now,wdAt)>=100) { wd=!wd; gpio_set_level(gpio_num_t(WatchdogPin),wd); wdAt=now; }
        ESP_ERROR_CHECK(esp_task_wdt_reset());
        if (ctl.faults != previousFaults) {
            ESP_LOGW("control","faults=0x%lx pump_request=%u",(unsigned long)ctl.faults,self.output);
            previousFaults=ctl.faults;
        }
        vTaskDelayUntil(&wake,pdMS_TO_TICKS(20));
    }
}
extern "C" void app_main() {
    relay_early_init();
    loaded=storage_init(initial);
    relay_start(initial.settings.relay);
    measurementQueue=xQueueCreate(1,sizeof(Measurement)); peerQueue=xQueueCreate(1,sizeof(Frame));
    commandQueue=xQueueCreate(8,sizeof(Command)); saveQueue=xQueueCreate(1,sizeof(Stored)); savedQueue=xQueueCreate(1,sizeof(SaveResult));
    configASSERT(measurementQueue && peerQueue && commandQueue && saveQueue && savedQueue);
    ESP_LOGI("boot","ESP TERMOSTAT 0.1.0 / %s / reset reason %d",BoardName,int(esp_reset_reason()));
    configASSERT(xTaskCreate(control_task,"control",6144,nullptr,12,nullptr)==pdPASS);
    storage_start();
    configASSERT(xTaskCreate(link_task,"peer_link",4096,nullptr,6,nullptr)==pdPASS);
    sensor_start(); ui_start(); console_start(); transport_start();
}
