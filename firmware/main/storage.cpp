#include "app.h"
#include "board.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "cJSON.h"
#include <cstring>
#include <cstdio>
#include <cmath>

static bool nvsReady=false;
bool storage_init(Stored &s) {
    // Never erase NVS automatically: it also contains sensor identity and Zigbee pairing.
    esp_err_t err=nvs_flash_init();
    nvsReady=err==ESP_OK;
    if (!nvsReady) { ESP_LOGE("storage","NVS unavailable: %s",esp_err_to_name(err)); return false; }
    nvs_handle_t h;
    if (nvs_open("thermo",NVS_READWRITE,&h)!=ESP_OK) return false;
    Stored loaded; size_t size=sizeof(loaded);
    err=nvs_get_blob(h,"settings",&loaded,&size); nvs_close(h);
    if (err==ESP_OK && size==sizeof(loaded) && valid(loaded)) { s=loaded; return true; }
    ESP_LOGW("storage","No valid commissioned settings; pump requested ON"); return false;
}
static void storage_task(void *) {
    Stored s;
    for (;;) if (xQueueReceive(saveQueue,&s,portMAX_DELAY)==pdTRUE) {
        seal(s); nvs_handle_t h; bool ok=false;
        if (nvsReady && nvs_open("thermo",NVS_READWRITE,&h)==ESP_OK) {
            ok=nvs_set_blob(h,"settings",&s,sizeof(s))==ESP_OK && nvs_commit(h)==ESP_OK;
            if (ok) { Stored read; size_t n=sizeof(read); ok=nvs_get_blob(h,"settings",&read,&n)==ESP_OK && n==sizeof(read) && !memcmp(&s,&read,n); }
            nvs_close(h);
        }
        SaveResult result{s.settings.revision,s.calibration_revision,ok};
        xQueueOverwrite(savedQueue,&result);
        ESP_LOGI("storage","Settings r%lu: %s",(unsigned long)s.settings.revision,ok?"saved":"FAILED");
    }
}
void storage_start() { configASSERT(xTaskCreate(storage_task,"settings",4096,nullptr,2,nullptr)==pdPASS); }

static bool integer(const cJSON *v,int low,int high,int &out) {
    if (!cJSON_IsNumber(v) || !std::isfinite(v->valuedouble) || v->valuedouble<low || v->valuedouble>high || std::floor(v->valuedouble)!=v->valuedouble) return false;
    out=int(v->valuedouble); return true;
}
static void console_line(const char *line) {
    cJSON *doc=cJSON_Parse(line);
    if (!cJSON_IsObject(doc) || cJSON_GetArraySize(doc)!=1) { puts("ERR: one JSON command required"); cJSON_Delete(doc); return; }
    const cJSON *v=doc->child; bool ok=false; Command c{};
    if (!strcmp(v->string,"status") && cJSON_IsTrue(v)) {
        char *json=snapshot_json(snapshot()); if (json) { puts(json); cJSON_free(json); } ok=true;
    } else if (!strcmp(v->string,"network") && cJSON_IsObject(v)) {
        char *json=cJSON_PrintUnformatted(v); if (json) { ok=network_save(json); cJSON_free(json); }
        if (ok) puts("Network stored; restart with pump protected to apply.");
    } else if (!strcmp(v->string,"manual") && cJSON_IsTrue(v)) {
        c.type=CommandType::Manual; ok=NodeId==1 && command(c);
    } else if (!strcmp(v->string,"commission") && cJSON_IsTrue(v)) {
        c.type=CommandType::Commission; ok=NodeId==1 && command(c);
    } else if (!strcmp(v->string,"calibration_centi") || !strcmp(v->string,"peer_calibration_centi")) {
        int offset;
        if (integer(v,-500,500,offset)) {
            c.type=!strcmp(v->string,"calibration_centi") ? CommandType::Calibration : CommandType::PeerCalibration;
            c.calibration=int16_t(offset); ok=command(c);
        }
    } else if (!strcmp(v->string,"settings") && cJSON_IsObject(v) && NodeId==1) {
        c.type=CommandType::Settings; c.settings=snapshot().self.settings; ok=cJSON_GetArraySize(v)>0;
        for (const cJSON *p=v->child; p && ok; p=p->next) {
            int n; if (!integer(p,0,65535,n)) { ok=false; break; }
            if (!strcmp(p->string,"on_centi")) c.settings.on=n;
            else if (!strcmp(p->string,"hysteresis_centi")) c.settings.hysteresis=n;
            else if (!strcmp(p->string,"force_centi")) c.settings.force=n;
            else if (!strcmp(p->string,"min_on_s")) c.settings.min_on_s=n;
            else if (!strcmp(p->string,"manual_s")) c.settings.manual_s=n;
            else if (!strcmp(p->string,"difference_centi")) c.settings.difference=n;
            else if (!strcmp(p->string,"relay_mode") && n<=2) c.settings.relay=n;
            else if (!strcmp(p->string,"pulse_ms")) c.settings.pulse_ms=n;
            else ok=false;
        }
        ok=ok && valid(c.settings) && command(c);
    }
    puts(ok?"OK: accepted (check saved/sync status)":"ERR: rejected"); cJSON_Delete(doc);
}
static void console_task(void *) {
    char line[600]; size_t used=0; bool overflow=false;
    for (;;) {
        int c=getchar();
        if (c==EOF) { clearerr(stdin); vTaskDelay(pdMS_TO_TICKS(50)); continue; }
        if (c=='\n' || c=='\r') {
            if (used && !overflow) { line[used]=0; console_line(line); }
            else if (overflow) puts("ERR: command too long");
            used=0; overflow=false;
        } else if (c>=32 && c<127) {
            if (used<sizeof(line)-1) line[used++]=char(c); else overflow=true;
        }
    }
}
void console_start() { configASSERT(xTaskCreate(console_task,"console",6144,nullptr,1,nullptr)==pdPASS); }

static void node_json(cJSON *obj,const Frame &f,bool available) {
    cJSON_AddBoolToObject(obj,"available",available);
    if (available && !(f.faults & Sensor) && f.temperature!=InvalidTemp) cJSON_AddNumberToObject(obj,"temperature",f.temperature/100.0);
    else cJSON_AddNullToObject(obj,"temperature");
    cJSON_AddNumberToObject(obj,"calibration",f.calibration/100.0);
    cJSON_AddNumberToObject(obj,"faults",available?f.faults:uint32_t(PeerLost));
    auto state=[&](const char *key,bool value) {
        if(available) cJSON_AddBoolToObject(obj,key,value); else cJSON_AddNullToObject(obj,key);
    };
    state("pump_request",f.output); state("command_sent",f.command_sent);
    state("link",f.link); state("ha_delivery",f.delivered);
    cJSON_AddNumberToObject(obj,"boot",f.boot);
    cJSON_AddNumberToObject(obj,"sequence",f.sequence);
    cJSON_AddNumberToObject(obj,"fault_events",f.fault_events);
    cJSON_AddNumberToObject(obj,"last_fault",f.last_fault);
}
char *snapshot_json(const Snapshot &s) {
    cJSON *j=cJSON_CreateObject(); if (!j) return nullptr;
    cJSON_AddStringToObject(j,"firmware","0.1.0");
    cJSON_AddStringToObject(j,"node",s.self.node==1?"A":"B");
    cJSON_AddNumberToObject(j,"sequence",s.self.sequence);
    node_json(cJSON_AddObjectToObject(j,"local"),s.self,true);
    node_json(cJSON_AddObjectToObject(j,"peer"),s.peer,s.peer_fresh);
    cJSON_AddBoolToObject(j,"peer_alarm",!s.peer_fresh);
    cJSON_AddBoolToObject(j,"sync_ok",!(s.self.faults & ConfigPending));
    cJSON_AddBoolToObject(j,"saved",s.self.saved);
    cJSON_AddNumberToObject(j,"wire_errors",s.wire_errors);
    cJSON *cfg=cJSON_AddObjectToObject(j,"settings");
    const auto &v=s.self.settings;
    cJSON_AddNumberToObject(cfg,"revision",v.revision);
    cJSON_AddNumberToObject(cfg,"on",v.on/100.0);
    cJSON_AddNumberToObject(cfg,"hysteresis",v.hysteresis/100.0);
    cJSON_AddNumberToObject(cfg,"force",v.force/100.0);
    cJSON_AddNumberToObject(cfg,"min_on_s",v.min_on_s);
    cJSON_AddNumberToObject(cfg,"manual_s",v.manual_s);
    cJSON_AddNumberToObject(cfg,"difference",v.difference/100.0);
    cJSON_AddNumberToObject(cfg,"relay_mode",v.relay);
    cJSON_AddNumberToObject(cfg,"pulse_ms",v.pulse_ms);
    cJSON_AddBoolToObject(cfg,"commissioned",v.commissioned);
    char *json=cJSON_PrintUnformatted(j); cJSON_Delete(j); return json;
}
