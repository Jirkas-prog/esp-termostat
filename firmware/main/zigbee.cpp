#include "app.h"
#include "board.h"
#include "telemetry.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "ha/esp_zigbee_ha_standard.h"
#include <atomic>
#include <cstring>

static constexpr uint16_t Cluster=0xfc10;
static std::atomic<bool> started{false},joined{false};
static uint8_t report[1+sizeof(ZigbeePacket)]={sizeof(ZigbeePacket)};
static uint32_t ackValue=0;
static char manufacturer[]="\x08Jiri DIY";
static char model[]="\x0f" "ESP-TERMOSTAT-1";
bool network_save(const char *) { return false; }
static void commission(uint8_t mode) { esp_zb_bdb_start_top_level_commissioning(mode); }
extern "C" void esp_zb_app_signal_handler(esp_zb_app_signal_t *signal) {
    auto type=esp_zb_app_signal_type_t(*signal->p_app_signal);
    esp_err_t status=signal->esp_err_status;
    switch(type) {
    case ESP_ZB_ZDO_SIGNAL_SKIP_STARTUP: commission(ESP_ZB_BDB_MODE_INITIALIZATION); break;
    case ESP_ZB_BDB_SIGNAL_DEVICE_FIRST_START:
    case ESP_ZB_BDB_SIGNAL_DEVICE_REBOOT:
        if(status!=ESP_OK) esp_zb_scheduler_alarm(commission,ESP_ZB_BDB_MODE_INITIALIZATION,5000);
        else if(esp_zb_bdb_is_factory_new()) commission(ESP_ZB_BDB_MODE_NETWORK_STEERING);
        else { joined=true; route_link(true); }
        break;
    case ESP_ZB_BDB_SIGNAL_STEERING:
        joined=status==ESP_OK; route_link(joined);
        if(!joined) esp_zb_scheduler_alarm(commission,ESP_ZB_BDB_MODE_NETWORK_STEERING,10000);
        ESP_LOGI("zigbee","Join %s",joined?"OK":"retry"); break;
    case ESP_ZB_ZDO_SIGNAL_LEAVE:
        joined=false; route_link(false);
        esp_zb_scheduler_alarm(commission,ESP_ZB_BDB_MODE_NETWORK_STEERING,5000); break;
    default: break;
    }
}
static esp_err_t action(esp_zb_core_action_callback_id_t id,const void *message) {
    if(id==ESP_ZB_CORE_SET_ATTR_VALUE_CB_ID && message) {
        auto m=static_cast<const esp_zb_zcl_set_attr_value_message_t *>(message);
        if(m->info.status==ESP_ZB_ZCL_STATUS_SUCCESS && m->info.cluster==Cluster && m->attribute.id==1 &&
            m->attribute.data.type==ESP_ZB_ZCL_ATTR_TYPE_U32 && m->attribute.data.size==4 && m->attribute.data.value) {
            uint32_t nonce; memcpy(&nonce,m->attribute.data.value,4); route_ack(nonce);
        }
    }
    return ESP_OK;
}
static void reporting_task(void *) {
    for (;;) {
        if(started && joined && esp_zb_lock_acquire(pdMS_TO_TICKS(100))) {
            auto s=snapshot(); auto packet=telemetry(s.self,s.peer,s.peer_fresh);
            memcpy(report+1,&packet,sizeof(packet));
            esp_zb_zcl_set_attribute_val(1,Cluster,ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,0,report,false);
            int16_t temperature=s.self.temperature;
            esp_zb_zcl_set_attribute_val(1,ESP_ZB_ZCL_CLUSTER_ID_TEMP_MEASUREMENT,ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                ESP_ZB_ZCL_ATTR_TEMP_MEASUREMENT_VALUE_ID,&temperature,false);
            esp_zb_zcl_report_attr_cmd_t cmd{};
            cmd.zcl_basic_cmd.src_endpoint=1; cmd.zcl_basic_cmd.dst_endpoint=1;
            cmd.zcl_basic_cmd.dst_addr_u.addr_short=0; // Existing Z2M coordinator, never form a new network.
            cmd.address_mode=ESP_ZB_APS_ADDR_MODE_16_ENDP_PRESENT;
            cmd.clusterID=Cluster; cmd.attributeID=0; cmd.direction=ESP_ZB_ZCL_CMD_DIRECTION_TO_CLI; cmd.dis_default_resp=1;
            route_sent(s.self.sequence);
            esp_err_t err=esp_zb_zcl_report_attr_cmd_req(&cmd);
            if(err!=ESP_OK) ESP_LOGW("zigbee","Report rejected: %s",esp_err_to_name(err));
            esp_zb_lock_release();
        }
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
static void zigbee_task(void *) {
    esp_zb_cfg_t cfg{}; cfg.esp_zb_role=ESP_ZB_DEVICE_TYPE_ROUTER;
    cfg.install_code_policy=false; cfg.nwk_cfg.zczr_cfg.max_children=4;
    esp_zb_init(&cfg);
    esp_zb_temperature_sensor_cfg_t sensor=ESP_ZB_DEFAULT_TEMPERATURE_SENSOR_CONFIG();
    sensor.basic_cfg.power_source=ESP_ZB_ZCL_BASIC_POWER_SOURCE_MAINS_SINGLE_PHASE;
    sensor.temp_meas_cfg.min_value=-5500; sensor.temp_meas_cfg.max_value=12500;
    esp_zb_cluster_list_t *clusters=esp_zb_temperature_sensor_clusters_create(&sensor);
    auto basic=esp_zb_cluster_list_get_cluster(clusters,ESP_ZB_ZCL_CLUSTER_ID_BASIC,ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
    ESP_ERROR_CHECK(esp_zb_basic_cluster_add_attr(basic,ESP_ZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID,manufacturer));
    ESP_ERROR_CHECK(esp_zb_basic_cluster_add_attr(basic,ESP_ZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID,model));
    auto custom=esp_zb_zcl_attr_list_create(Cluster);
    ESP_ERROR_CHECK(esp_zb_custom_cluster_add_custom_attr(custom,0,ESP_ZB_ZCL_ATTR_TYPE_OCTET_STRING,
        ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY | ESP_ZB_ZCL_ATTR_ACCESS_REPORTING,report));
    ESP_ERROR_CHECK(esp_zb_custom_cluster_add_custom_attr(custom,1,ESP_ZB_ZCL_ATTR_TYPE_U32,ESP_ZB_ZCL_ATTR_ACCESS_READ_WRITE,&ackValue));
    ESP_ERROR_CHECK(esp_zb_cluster_list_add_custom_cluster(clusters,custom,ESP_ZB_ZCL_CLUSTER_SERVER_ROLE));
    auto endpoints=esp_zb_ep_list_create();
    esp_zb_endpoint_config_t endpoint{}; endpoint.endpoint=1; endpoint.app_profile_id=ESP_ZB_AF_HA_PROFILE_ID;
    endpoint.app_device_id=ESP_ZB_HA_TEMPERATURE_SENSOR_DEVICE_ID;
    ESP_ERROR_CHECK(esp_zb_ep_list_add_ep(endpoints,clusters,endpoint));
    ESP_ERROR_CHECK(esp_zb_device_register(endpoints)); esp_zb_core_action_handler_register(action);
    ESP_ERROR_CHECK(esp_zb_set_primary_network_channel_set(ESP_ZB_TRANSCEIVER_ALL_CHANNELS_MASK));
    ESP_ERROR_CHECK(esp_zb_start(false)); started=true;
    configASSERT(xTaskCreate(reporting_task,"zb_report",6144,nullptr,4,nullptr)==pdPASS);
    esp_zb_stack_main_loop();
}
void transport_start() {
    gpio_config_t rf{}; rf.pin_bit_mask=(1ULL<<3)|(1ULL<<14); rf.mode=GPIO_MODE_OUTPUT;
    ESP_ERROR_CHECK(gpio_config(&rf)); gpio_set_level(GPIO_NUM_3,0); gpio_set_level(GPIO_NUM_14,0);
    esp_zb_platform_config_t platform{};
    platform.radio_config.radio_mode=ZB_RADIO_MODE_NATIVE; platform.host_config.host_connection_mode=ZB_HOST_CONNECTION_MODE_NONE;
    ESP_ERROR_CHECK(esp_zb_platform_config(&platform));
    configASSERT(xTaskCreate(zigbee_task,"zigbee",6144,nullptr,5,nullptr)==pdPASS);
}
