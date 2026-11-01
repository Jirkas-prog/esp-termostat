#include "app.h"
#include "board.h"
#include "driver/gpio.h"
#include "esp_check.h"

static uint8_t mode=NC;
static bool error=false, known=false, applied=true, target=true;
static uint32_t wakeAt=0, lastPulse=0;
static bool waking=false, everPulsed=false;
static esp_timer_handle_t cutoff;
static portMUX_TYPE relayMux=portMUX_INITIALIZER_UNLOCKED;
static volatile bool pulsing=false, completed=false;
static void outputs_off() {
    gpio_set_level(gpio_num_t(Relay1),0);
    gpio_set_level(gpio_num_t(Relay2),0);
    gpio_set_level(gpio_num_t(RelayEnable),0);
}
static void stop_pulse(void *) {
    portENTER_CRITICAL(&relayMux);
    outputs_off(); pulsing=false; completed=true;
    portEXIT_CRITICAL(&relayMux);
}
void relay_early_init() {
    outputs_off(); // Output latch LOW before switching pin direction.
    gpio_config_t cfg{};
    cfg.pin_bit_mask=(1ULL<<Relay1)|(1ULL<<Relay2)|(1ULL<<RelayEnable)|(1ULL<<WatchdogPin);
    cfg.mode=GPIO_MODE_OUTPUT;
    ESP_ERROR_CHECK(gpio_config(&cfg));
    outputs_off();
}
void relay_start(uint8_t selected) {
    mode=selected;
    esp_timer_create_args_t timer{};
    timer.callback=stop_pulse; timer.name="coil_cutoff";
    ESP_ERROR_CHECK(esp_timer_create(&timer,&cutoff));
}
bool relay_fault() { return error; }
bool relay_command_sent() { return known && !pulsing && !waking && !error; }
void relay_tick(bool on, uint16_t pulse_ms, uint32_t now) {
    if (mode != Latching) {
        // Bare coil via a 3.3 V-compatible low-side MOSFET, active HIGH.
        gpio_set_level(gpio_num_t(Relay1),mode==NC ? !on : on);
        applied=on; known=true; return;
    }
    portENTER_CRITICAL(&relayMux);
    bool done=completed, active=pulsing;
    completed=false;
    portEXIT_CRITICAL(&relayMux);
    if (done) { applied=target; known=true; }
    if (active || error) return;
    if (waking) {
        if (age(now,wakeAt)<3) return;
        target=on; waking=false;
        // The timer ends a pulse even if the control task stalls. An external
        // hardware one-shot is still required to cover a dead CPU/clock.
        portENTER_CRITICAL(&relayMux);
        pulsing=true;
        gpio_set_level(gpio_num_t(Relay1),target);
        gpio_set_level(gpio_num_t(Relay2),!target);
        portEXIT_CRITICAL(&relayMux);
        if (esp_timer_start_once(cutoff,uint64_t(pulse_ms)*1000)!=ESP_OK) {
            stop_pulse(nullptr); error=true; known=false;
        }
        lastPulse=now; everPulsed=true; return;
    }
    if (known && applied==on) return;
    // At most one 20..200 ms pulse per 2.1 s: <10% duty even at max pulse.
    if (everPulsed && age(now,lastPulse)<2100) return;
    outputs_off(); gpio_set_level(gpio_num_t(RelayEnable),1);
    waking=true; wakeAt=now;
}
