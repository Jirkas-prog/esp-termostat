#include "app.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "mqtt_client.h"
#include "esp_log.h"
#include "cJSON.h"
#include "nvs.h"
#include <atomic>
#include <cstring>
#include <cstdlib>
#include <cstdio>

struct NetworkConfig { char ssid[33]{}, password[65]{}, uri[129]{}, user[65]{}, mqttPassword[97]{}; };
static NetworkConfig config;
static esp_mqtt_client_handle_t mqtt;
static std::atomic<bool> wifiUp{false}, mqttUp{false};
static std::atomic<uint32_t> disconnectedAt{0};
static bool copy(const cJSON *value,char *dest,size_t capacity) {
    if(!cJSON_IsString(value) || !value->valuestring || strlen(value->valuestring)>=capacity) return false;
    for(const unsigned char *p=(const unsigned char *)value->valuestring;*p;++p) if(*p<32 || *p==127) return false;
    strcpy(dest,value->valuestring); return true;
}
static bool parse_config(const char *json,NetworkConfig &out) {
    cJSON *doc=cJSON_Parse(json); NetworkConfig next; bool ok=cJSON_IsObject(doc) && cJSON_GetArraySize(doc)==5;
    if(ok) ok=copy(cJSON_GetObjectItemCaseSensitive(doc,"ssid"),next.ssid,sizeof(next.ssid)) &&
        copy(cJSON_GetObjectItemCaseSensitive(doc,"password"),next.password,sizeof(next.password)) &&
        copy(cJSON_GetObjectItemCaseSensitive(doc,"mqtt_uri"),next.uri,sizeof(next.uri)) &&
        copy(cJSON_GetObjectItemCaseSensitive(doc,"mqtt_user"),next.user,sizeof(next.user)) &&
        copy(cJSON_GetObjectItemCaseSensitive(doc,"mqtt_password"),next.mqttPassword,sizeof(next.mqttPassword));
    // This installation uses a trusted local MQTT network. No unauthenticated AP.
    if(ok) ok=*next.ssid && strlen(next.password)>=8 && strlen(next.password)<=63 &&
        !strncmp(next.uri,"mqtt://",7) && strlen(next.uri)>7 && !strchr(next.uri,'@') && !strchr(next.uri,' ');
    cJSON_Delete(doc); if(ok) out=next; return ok;
}
bool network_save(const char *json) {
    NetworkConfig next; if(!parse_config(json,next)) return false;
    nvs_handle_t h; if(nvs_open("network",NVS_READWRITE,&h)!=ESP_OK) return false;
    bool ok=nvs_set_str(h,"json",json)==ESP_OK && nvs_commit(h)==ESP_OK; nvs_close(h); return ok;
}
static void wifi_event(void *,esp_event_base_t base,int32_t id,void *) {
    if(base==WIFI_EVENT && id==WIFI_EVENT_STA_START) esp_wifi_connect();
    if(base==WIFI_EVENT && id==WIFI_EVENT_STA_DISCONNECTED) {
        wifiUp=false; route_link(false); disconnectedAt=now_ms();
    }
    if(base==IP_EVENT && id==IP_EVENT_STA_GOT_IP) { wifiUp=true; route_link(true); }
}
static void mqtt_event(void *,esp_event_base_t,int32_t id,void *data) {
    auto e=static_cast<esp_mqtt_event_handle_t>(data);
    if(id==MQTT_EVENT_CONNECTED) {
        mqttUp=true;
        esp_mqtt_client_subscribe(mqtt,"kamna/termostat/A/ack",1);
        esp_mqtt_client_enqueue(mqtt,"kamna/termostat/A/availability","online",0,1,1,true);
    } else if(id==MQTT_EVENT_DISCONNECTED) mqttUp=false;
    else if(id==MQTT_EVENT_DATA && !e->retain && e->current_data_offset==0 && e->data_len==e->total_data_len && e->data_len>0 && e->data_len<=10 &&
        e->topic_len==int(strlen("kamna/termostat/A/ack")) && !memcmp(e->topic,"kamna/termostat/A/ack",e->topic_len)) {
        uint64_t number=0; bool ok=true;
        for(int i=0;i<e->data_len;++i) { if(e->data[i]<'0' || e->data[i]>'9') { ok=false; break; } number=number*10+e->data[i]-'0'; }
        if(ok && number<=UINT32_MAX) route_ack(uint32_t(number));
    }
}
static void network_task(void *) {
    nvs_handle_t h; char json[600]{}; size_t size=sizeof(json); bool have=false;
    if(nvs_open("network",NVS_READONLY,&h)==ESP_OK) { have=nvs_get_str(h,"json",json,&size)==ESP_OK; nvs_close(h); }
    if(!have || !parse_config(json,config)) {
        ESP_LOGW("network","Configure Wi-Fi/MQTT through USB JSON; local control is running"); vTaskDelete(nullptr); return;
    }
    ESP_ERROR_CHECK(esp_netif_init()); ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t *netif=esp_netif_create_default_wifi_sta();
    esp_netif_set_hostname(netif,"kamna-termostat-a");
    wifi_init_config_t init=WIFI_INIT_CONFIG_DEFAULT(); ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT,ESP_EVENT_ANY_ID,wifi_event,nullptr));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT,IP_EVENT_STA_GOT_IP,wifi_event,nullptr));
    wifi_config_t wifi{}; memcpy(wifi.sta.ssid,config.ssid,strlen(config.ssid)); memcpy(wifi.sta.password,config.password,strlen(config.password));
    wifi.sta.threshold.authmode=WIFI_AUTH_WPA2_PSK; wifi.sta.pmf_cfg.capable=true;
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM)); ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA,&wifi)); ESP_ERROR_CHECK(esp_wifi_start()); esp_wifi_set_ps(WIFI_PS_NONE);
    esp_mqtt_client_config_t mc{};
    mc.broker.address.uri=config.uri; mc.credentials.username=config.user; mc.credentials.authentication.password=config.mqttPassword;
    mc.credentials.client_id="kamna-termostat-A"; mc.session.keepalive=15;
    mc.session.last_will.topic="kamna/termostat/A/availability"; mc.session.last_will.msg="offline";
    mc.session.last_will.qos=1; mc.session.last_will.retain=1;
    mc.network.reconnect_timeout_ms=5000; mc.network.timeout_ms=4000;
    mc.buffer.size=2048; mc.task.priority=4; mc.task.stack_size=6144;
    mc.outbox.limit=8192;
    mqtt=esp_mqtt_client_init(&mc); configASSERT(mqtt);
    ESP_ERROR_CHECK(esp_mqtt_client_register_event(mqtt,MQTT_EVENT_ANY,mqtt_event,nullptr)); ESP_ERROR_CHECK(esp_mqtt_client_start(mqtt));
    uint32_t published=now_ms()-5000, retried=0, wifiReset=now_ms();
    uint32_t faults=UINT32_MAX; bool peerAlarm=true;
    for (;;) {
        uint32_t now=now_ms();
        if(!wifiUp && age(now,retried)>=5000 && age(now,disconnectedAt)>=1000) { esp_wifi_connect(); retried=now; }
        if(!wifiUp && age(now,wifiReset)>=120000) { esp_wifi_stop(); esp_wifi_start(); wifiReset=now; }
        if(wifiUp) wifiReset=now;
        auto s=snapshot();
        if(mqttUp && (age(now,published)>=5000 || s.self.faults!=faults || peerAlarm!=!s.peer_fresh)) {
            // Bound the outbox during a half-open TCP connection; retry latest state later.
            if(esp_mqtt_client_get_outbox_size(mqtt)<4096) {
                char *text=snapshot_json(s);
                if(text) {
                    route_sent(s.self.sequence);
                    int sent=esp_mqtt_client_enqueue(mqtt,"kamna/termostat/A/state",text,0,1,0,false);
                    if(sent>=0) {
                        esp_mqtt_client_enqueue(mqtt,"kamna/termostat/A/peer_alarm",s.peer_fresh?"OFF":"ON",0,1,0,false);
                        if(s.self.faults!=faults) esp_mqtt_client_enqueue(mqtt,"kamna/termostat/A/event",text,0,1,0,false);
                        published=now; faults=s.self.faults; peerAlarm=!s.peer_fresh;
                    }
                    cJSON_free(text);
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(250));
    }
}
void transport_start() { configASSERT(xTaskCreate(network_task,"network",8192,nullptr,4,nullptr)==pdPASS); }
