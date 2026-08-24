/**
    app_manager.c : RFScout application view manager implementation
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/
#include "app_manager.h"
#include "ble_monitor.h"
#include "cc1101.h"
#include "mlx90640.h"
#include "platform.h"
#include "radio_app_view.h"
#include "radio_monitor.h"
#include "thermal_view.h"
#include "ui.h"
#include "esp_log.h"
#include "esp_timer.h"
#define APP_REFRESH_INTERVAL_US 125000LL
#define APP_FEATURE_PROBE_INTERVAL_US 10000000LL
#define APP_RADIO_RETRY_INTERVAL_US 1000000LL
static const char *TAG="app_manager";
static rfscout_app_id_t active_app;
static int64_t last_app_refresh_us,last_thermal_probe_us,last_radio_retry_us;
static bool cc_menu,cc_edit,wifi_monitor_ready,ble_monitor_ready;
static unsigned cc_item;
static bool probe_thermal(void){bool was=mlx90640_is_detected();bool now=ESP_OK==mlx90640_probe();if(now&&!was)ESP_LOGI(TAG,"Thermal app available");else if(!now&&was)ESP_LOGW(TAG,"Thermal app unavailable");return now;}
static bool app_available(rfscout_app_id_t a){return a!=RFSCOUT_APP_THERMAL||mlx90640_is_detected();}
static void start_wifi_bt_collectors(void){esp_err_t r;if(!wifi_monitor_ready){r=radio_monitor_start();wifi_monitor_ready=r==ESP_OK;if(!wifi_monitor_ready)ESP_LOGW(TAG,"Wi-Fi monitor start failed: %s; retrying",esp_err_to_name(r));}if(!ble_monitor_ready){r=ble_monitor_start();ble_monitor_ready=r==ESP_OK;if(!ble_monitor_ready)ESP_LOGW(TAG,"BLE monitor start failed: %s; retrying",esp_err_to_name(r));}}
static void render_cc_menu(void){const cc1101_scan_config_t *c=cc1101_config();radio_app_view_cc1101_menu(cc_item,cc1101_profile_name(c->profile),c->low_khz,c->high_khz,c->step_khz);}
static void render_cc_spectrum(void){int8_t b[CC1101_SPECTRUM_BINS];size_t n=cc1101_spectrum(b,CC1101_SPECTRUM_BINS);const cc1101_scan_config_t *c=cc1101_config();radio_app_view_cc1101_spectrum(b,n,c->low_khz,c->high_khz,cc1101_is_detected());}
static void leave_app(rfscout_app_id_t a){if(a==RFSCOUT_APP_WIFI_BT){(void)ble_monitor_stop();(void)radio_monitor_stop();wifi_monitor_ready=false;ble_monitor_ready=false;}else if(a==RFSCOUT_APP_CC1101)cc1101_stop();cc_menu=false;cc_edit=false;}
static void enter_app(rfscout_app_id_t a){last_app_refresh_us=0;switch(a){case RFSCOUT_APP_WIFI_BT:wifi_monitor_ready=false;ble_monitor_ready=false;last_radio_retry_us=esp_timer_get_time();start_wifi_bt_collectors();break;case RFSCOUT_APP_CC1101:cc_item=0;cc_menu=false;cc_edit=false;if(cc1101_start()!=ESP_OK)ESP_LOGW(TAG,"CC1101 unavailable");render_cc_spectrum();break;case RFSCOUT_APP_NRF24:radio_app_view_nrf24();break;case RFSCOUT_APP_THERMAL:thermal_view_enter();break;default:break;}ESP_LOGI(TAG,"switched app=%d",(int)a);}
static rfscout_app_id_t next_available_app(int d,int64_t now){int n=(int)active_app;for(int tries=0;tries<RFSCOUT_APP_COUNT;tries++){n+=d;if(n<0)n=RFSCOUT_APP_COUNT-1;else if(n>=RFSCOUT_APP_COUNT)n=0;rfscout_app_id_t c=(rfscout_app_id_t)n;if(c==RFSCOUT_APP_THERMAL&&!app_available(c)){last_thermal_probe_us=now;if(!probe_thermal())continue;}return c;}return active_app;}
esp_err_t app_manager_init(void){active_app=RFSCOUT_APP_WIFI_BT;last_app_refresh_us=0;last_thermal_probe_us=esp_timer_get_time();last_radio_retry_us=0;cc_menu=false;cc_edit=false;cc_item=0;wifi_monitor_ready=true;ble_monitor_ready=true;esp_err_t r=ui_init();if(r!=ESP_OK)return r;(void)mlx90640_init();return ESP_OK;}
rfscout_app_id_t app_manager_current(void){return active_app;}
static void cc_input(bool pressed,int delta){if(!cc_menu){if(pressed){cc_menu=true;cc_edit=false;render_cc_menu();}return;}if(pressed){if(cc_item==4U){cc_menu=false;cc_edit=false;render_cc_spectrum();return;}cc_edit=!cc_edit;render_cc_menu();return;}if(!delta)return;if(!cc_edit){int i=(int)cc_item+(delta>0?1:-1);if(i<0)i=4;if(i>4)i=0;cc_item=(unsigned)i;}else{const cc1101_scan_config_t *c=cc1101_config();if(cc_item==0){int p=(int)c->profile+(delta>0?1:-1);if(p<0)p=CC1101_PROFILE_COUNT-1;if(p>=CC1101_PROFILE_COUNT)p=0;cc1101_set_profile((cc1101_profile_t)p);}else if(cc_item==1)cc1101_adjust_low(delta>0?1:-1);else if(cc_item==2)cc1101_adjust_high(delta>0?1:-1);else if(cc_item==3)cc1101_adjust_step(delta>0?1:-1);}render_cc_menu();}
void app_manager_poll(void){int64_t now=esp_timer_get_time();if(active_app!=RFSCOUT_APP_THERMAL&&now-last_thermal_probe_us>=APP_FEATURE_PROBE_INTERVAL_US){(void)probe_thermal();last_thermal_probe_us=now;}bool pressed=platform_encoder_button_pressed();int delta=platform_encoder_delta();if(active_app==RFSCOUT_APP_CC1101){cc_input(pressed,delta);if(!cc_menu){cc1101_poll();if(now-last_app_refresh_us>=APP_REFRESH_INTERVAL_US){last_app_refresh_us=now;render_cc_spectrum();}}return;}if(delta){rfscout_app_id_t c=next_available_app(delta>0?1:-1,now);if(c!=active_app){leave_app(active_app);active_app=c;enter_app(active_app);return;}}if(active_app==RFSCOUT_APP_WIFI_BT){if((!wifi_monitor_ready||!ble_monitor_ready)&&now-last_radio_retry_us>=APP_RADIO_RETRY_INTERVAL_US){last_radio_retry_us=now;start_wifi_bt_collectors();}radio_monitor_step();ui_poll();return;}if(active_app==RFSCOUT_APP_NRF24)return;if(active_app!=RFSCOUT_APP_THERMAL)return;if(!mlx90640_is_detected()){leave_app(active_app);active_app=RFSCOUT_APP_WIFI_BT;enter_app(active_app);return;}if(now-last_app_refresh_us<APP_REFRESH_INTERVAL_US)return;last_app_refresh_us=now;esp_err_t r=thermal_view_render();if(r!=ESP_OK&&r!=ESP_ERR_NOT_FINISHED&&!mlx90640_is_detected()){leave_app(active_app);active_app=RFSCOUT_APP_WIFI_BT;enter_app(active_app);}}
