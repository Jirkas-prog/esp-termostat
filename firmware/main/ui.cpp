#include "app.h"
#include "board.h"
#if CONFIG_IDF_TARGET_ESP32
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "font8x8_basic.h"
#include <cstdio>
#include <algorithm>

static esp_lcd_panel_handle_t panel;
static SemaphoreHandle_t drawn;
static uint16_t *pixels;
static bool screen=false, editing=false;
static int field=0;
static Settings draft;
static int16_t calA=0,calB=0;
static uint32_t lastTouch=0, errorUntil=0;
static constexpr const char *names[]={"Zapnout C","Hystereze C","Havarijni C","Min. beh s","Rucni beh s","Rozdil cidel C","Kalibrace A C","Kalibrace B C","Rele 0NC 1NO 2LAT","Impulz ms"};
static bool transfer(esp_lcd_panel_io_handle_t,esp_lcd_panel_io_event_data_t *,void *) {
    BaseType_t wake=pdFALSE; xSemaphoreGiveFromISR(drawn,&wake); return wake==pdTRUE;
}
static bool display_init() {
    gpio_set_direction(GPIO_NUM_4,GPIO_MODE_OUTPUT); gpio_set_level(GPIO_NUM_4,0);
    pixels=static_cast<uint16_t *>(heap_caps_malloc(240*135*2,MALLOC_CAP_DMA)); drawn=xSemaphoreCreateBinary();
    if (!pixels || !drawn) return false;
    spi_bus_config_t bus{}; bus.mosi_io_num=19; bus.sclk_io_num=18; bus.miso_io_num=-1;
    bus.quadwp_io_num=-1; bus.quadhd_io_num=-1; bus.max_transfer_sz=240*135*2;
    if (spi_bus_initialize(SPI2_HOST,&bus,SPI_DMA_CH_AUTO)!=ESP_OK) return false;
    esp_lcd_panel_io_spi_config_t ioCfg{};
    ioCfg.cs_gpio_num=5; ioCfg.dc_gpio_num=16; ioCfg.pclk_hz=20000000; ioCfg.trans_queue_depth=1;
    ioCfg.lcd_cmd_bits=8; ioCfg.lcd_param_bits=8; ioCfg.on_color_trans_done=transfer;
    esp_lcd_panel_io_handle_t io;
    if (esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST,&ioCfg,&io)!=ESP_OK) return false;
    esp_lcd_panel_dev_config_t dev{}; dev.reset_gpio_num=23; dev.rgb_ele_order=LCD_RGB_ELEMENT_ORDER_RGB; dev.bits_per_pixel=16;
    if (esp_lcd_new_panel_st7789(io,&dev,&panel)!=ESP_OK) return false;
    if (esp_lcd_panel_reset(panel)!=ESP_OK || esp_lcd_panel_init(panel)!=ESP_OK ||
        esp_lcd_panel_swap_xy(panel,true)!=ESP_OK || esp_lcd_panel_mirror(panel,true,false)!=ESP_OK ||
        esp_lcd_panel_set_gap(panel,40,53)!=ESP_OK || esp_lcd_panel_invert_color(panel,true)!=ESP_OK ||
        esp_lcd_panel_disp_on_off(panel,true)!=ESP_OK) return false;
    gpio_set_level(GPIO_NUM_4,1); return true;
}
static uint16_t color(uint16_t rgb) { return uint16_t(rgb<<8 | rgb>>8); }
static void line(int y,const char *text,uint16_t rgb=0xffff,int scale=1) {
    int x=4;
    while (*text && x+8*scale<=240) {
        uint8_t c=uint8_t(*text++); if(c>127) c='?';
        for (int row=0;row<8;++row) for(int col=0;col<8;++col) if(font8x8_basic[c][row] & (1u<<col))
            for(int a=0;a<scale;++a) for(int b=0;b<scale;++b)
                if(y+row*scale+a<135) pixels[(y+row*scale+a)*240+x+col*scale+b]=color(rgb);
        x+=8*scale;
    }
}
static int value() {
    switch(field) {
    case 0:return draft.on; case 1:return draft.hysteresis; case 2:return draft.force;
    case 3:return draft.min_on_s; case 4:return draft.manual_s; case 5:return draft.difference;
    case 6:return calA; case 7:return calB; case 8:return draft.relay; default:return draft.pulse_ms;
    }
}
static void adjust(int delta) {
    int n=value(); int step=(field<=2 || field==5)?50 : (field==6 || field==7)?10 : (field==3 || field==4)?10 : field==9?10:1;
    n+=delta*step;
    switch(field) {
    case 0:draft.on=std::clamp(n,3000,8000);break;
    case 1:draft.hysteresis=std::clamp(n,100,2000);break;
    case 2:draft.force=std::clamp(n,3500,9500);break;
    case 3:draft.min_on_s=std::clamp(n,10,600);break;
    case 4:draft.manual_s=std::clamp(n,30,3600);break;
    case 5:draft.difference=std::clamp(n,100,2000);break;
    case 6:calA=std::clamp(n,-500,500);break;
    case 7:calB=std::clamp(n,-500,500);break;
    case 8:draft.relay=std::clamp(n,0,2);break;
    case 9:draft.pulse_ms=std::clamp(n,20,200);break;
    }
}
static void unlock(uint32_t now) {
    auto s=snapshot(); draft=s.self.settings; calA=s.self.calibration; calB=s.peer.calibration;
    editing=true; field=0; lastTouch=now;
}
static void save(uint32_t now) {
    Command c{}; bool ok=true;
    if(field==6) { c.type=CommandType::Calibration; c.calibration=calA; }
    else if(field==7) {
        c.type=CommandType::PeerCalibration; c.calibration=calB;
        ok=snapshot().peer_fresh; // Don't edit an unknown/stale probe offset.
    } else { c.type=CommandType::Settings; c.settings=draft; ok=valid(draft); }
    if (ok && command(c)) editing=false; else errorUntil=now+3000;
}
static void render(uint32_t now) {
    auto s=snapshot(); memset(pixels,0,240*135*2);
    char text[40],a[12]="--.--",b[12]="--.--";
    if (!(s.self.faults & Sensor)) snprintf(a,sizeof(a),"%.2f",s.self.temperature/100.0);
    if (s.peer_fresh && !(s.peer.faults & Sensor)) snprintf(b,sizeof(b),"%.2f",s.peer.temperature/100.0);
    line(3,editing?"TERMOSTAT / NASTAVENI":"TERMOSTAT / ZAMCENO",0x07ff);
    snprintf(text,sizeof(text),"A %s C",a);line(18,text,0xffff,2);
    snprintf(text,sizeof(text),"B %s C",b);line(39,text,0xffff,2);
    snprintf(text,sizeof(text),"POZADAVEK A:%s B:%s",s.self.output?"ON":"OFF",s.peer_fresh?(s.peer.output?"ON":"OFF"):"?");line(60,text,0xffe0);
    if(editing) {
        snprintf(text,sizeof(text),"%d/10 %s",field+1,names[field]);line(76,text,0x07ff);
        if(field<=2 || (field>=5 && field<=7)) snprintf(text,sizeof(text),"%.2f",value()/100.0);
        else snprintf(text,sizeof(text),"%d",value());
        line(89,text,0xffff,2);
        line(113,"Zamek=dalsi  Ulozit=potvrdit");
    } else {
        snprintf(text,sizeof(text),"SYNC:%s HA:%s/%s",s.self.faults & ConfigPending?"CEKA":"OK",s.self.delivered?"OK":"--",s.peer_fresh && s.peer.delivered?"OK":"--");line(77,text);
        snprintf(text,sizeof(text),"ZAP %.1f VYP %.1f C",s.self.settings.on/100.0,(s.self.settings.on-s.self.settings.hysteresis)/100.0);line(93,text);
        if(s.self.faults) { snprintf(text,sizeof(text),"ZAVADY 0x%04lX",(unsigned long)s.self.faults);line(109,text,0xf800); }
        else line(109,"AUTO / dlouze ULOZIT = obeh");
    }
    if(int32_t(errorUntil-now)>0) line(125,"NEPLATNE / NEDOSTUPNE",0xf800);
    else if(s.self.faults & NotCommissioned) line(125,"NEUVEDENO DO PROVOZU",0xf800);
    else if(s.self.faults & HardwareChange) line(125,"RELE: NUTNY RESTART OBOU",0xf800);
    if(esp_lcd_panel_draw_bitmap(panel,0,0,240,135,pixels)!=ESP_OK || xSemaphoreTake(drawn,pdMS_TO_TICKS(500))!=pdTRUE) {
        screen=false; editing=false; ESP_LOGE("display","LCD failed; local control continues");
    }
}
struct Key { bool raw=false,stable=false,longDone=false; uint32_t changed=0,down=0; };
static void ui_task(void *) {
    gpio_config_t cfg{}; cfg.mode=GPIO_MODE_INPUT;
    for(int pin:KeyPins) cfg.pin_bit_mask |= 1ULL<<pin;
    if(gpio_config(&cfg)!=ESP_OK) { vTaskDelete(nullptr); return; }
    screen=display_init(); Key keys[4]; uint32_t drawnAt=now_ms()-500;
    for (;;) {
        uint32_t now=now_ms();
        for(int i=0;i<4;++i) {
            auto &k=keys[i]; bool pressed=gpio_get_level(gpio_num_t(KeyPins[i]));
            if(pressed!=k.raw) { k.raw=pressed; k.changed=now; }
            if(age(now,k.changed)>=60 && k.stable!=k.raw) {
                k.stable=k.raw;
                if(k.stable) { k.down=now; k.longDone=false; lastTouch=now; }
                else if(!k.longDone && screen && editing) {
                    if(i==0) field=(field+1)%10;
                    else if(i==1 || i==2) adjust(i==1?-1:1);
                    else if(i==3) save(now);
                }
            }
            if(k.stable && !k.longDone && age(now,k.down)>=(i==0?2000u:3000u)) {
                k.longDone=true;
                if(i==0 && screen) { if(editing) editing=false; else unlock(now); }
                if(i==3) { Command c{}; c.type=CommandType::Manual; command(c); editing=false; }
            }
        }
        if(editing && age(now,lastTouch)>30000) editing=false;
        if(screen && age(now,drawnAt)>=300) { render(now); drawnAt=now; }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
void ui_start() { configASSERT(xTaskCreate(ui_task,"display_keys",6144,nullptr,3,nullptr)==pdPASS); }
#else
void ui_start() {}
#endif
