/**
    cc1101.c : RFScout CC1101 receive-only spectrum scanner
    Description: Sweeps configured sub-GHz frequencies and reports RSSI bins.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/
#include "cc1101.h"
#include "board.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>
#define CC1101_SPI_HOST SPI2_HOST
#define CC1101_READ_BURST 0xC0U
#define CC1101_SRES 0x30U
#define CC1101_SIDLE 0x36U
#define CC1101_SRX 0x34U
#define CC1101_SPWD 0x39U
#define CC1101_FREQ2 0x0DU
#define CC1101_FREQ1 0x0EU
#define CC1101_FREQ0 0x0FU
#define CC1101_MCSM0 0x18U
#define CC1101_RSSI 0x34U
#define CC1101_PARTNUM 0x30U
#define CC1101_VERSION 0x31U
#define CC1101_XOSC_HZ 26000000ULL
#define CC1101_SETTLE_US 180LL
static const char *TAG="cc1101";
static spi_device_handle_t device;
static bool initialized,detected,running,frame_ready;
static int64_t tune_us;
static size_t sweep_index,sweep_count;
static int8_t scan_spectrum[CC1101_SPECTRUM_BINS];
static int8_t display_spectrum[CC1101_SPECTRUM_BINS];
static cc1101_scan_config_t config={CC1101_PROFILE_433,433050U,434790U,25U};
static esp_err_t transfer(uint8_t h,uint8_t d,uint8_t *r){spi_transaction_t t={.length=16,.rxlength=16};uint8_t tx[2]={h,d},rx[2]={0};t.tx_buffer=tx;t.rx_buffer=rx;esp_err_t e=spi_device_transmit(device,&t);if(e==ESP_OK&&r)*r=rx[1];return e;}
static esp_err_t strobe(uint8_t c){return transfer(c,0,NULL);}
static esp_err_t write_register(uint8_t a,uint8_t v){return transfer(a,v,NULL);}
static esp_err_t read_status(uint8_t a,uint8_t *v){return transfer(a|CC1101_READ_BURST,0,v);}
static void select_rf_path(uint32_t f){int sw1=0,sw0=0;if(f<=348000U){sw1=1;sw0=0;}else if(f<=464000U){sw1=1;sw0=1;}else if(f>=779000U){sw1=0;sw0=1;}gpio_set_level(RFSCOUT_BOARD_CC1101_SWITCH_1_PIN,sw1);gpio_set_level(RFSCOUT_BOARD_CC1101_SWITCH_0_PIN,sw0);}
static void disable_rf_path(void){gpio_set_level(RFSCOUT_BOARD_CC1101_SWITCH_1_PIN,0);gpio_set_level(RFSCOUT_BOARD_CC1101_SWITCH_0_PIN,0);}
static int8_t decode_rssi(uint8_t raw){int16_t s=(raw>=128U)?((int16_t)raw-256):(int16_t)raw;return(int8_t)(s/2-74);}
static size_t configured_sweep_count(void){if(!config.step_khz||config.high_khz<=config.low_khz)return 1U;return(size_t)((config.high_khz-config.low_khz)/config.step_khz)+1U;}
static uint32_t frequency_for_sample(size_t i){uint64_t f=(uint64_t)config.low_khz+(uint64_t)i*config.step_khz;if(f>config.high_khz)f=config.high_khz;return(uint32_t)f;}
static size_t display_bin_for_sample(size_t i){if(sweep_count<=1U)return 0U;return(i*(CC1101_SPECTRUM_BINS-1U))/(sweep_count-1U);}
static esp_err_t tune(uint32_t f){select_rf_path(f);uint64_t w=((uint64_t)f*1000ULL<<16)/CC1101_XOSC_HZ;esp_err_t e=strobe(CC1101_SIDLE);if(e==ESP_OK)e=write_register(CC1101_FREQ2,(w>>16)&0xffU);if(e==ESP_OK)e=write_register(CC1101_FREQ1,(w>>8)&0xffU);if(e==ESP_OK)e=write_register(CC1101_FREQ0,w&0xffU);if(e==ESP_OK)e=strobe(CC1101_SRX);return e;}
static void restart_sweep(void){sweep_index=0;sweep_count=configured_sweep_count();memset(scan_spectrum,-120,sizeof(scan_spectrum));/* Keep the last completed frame visible. A settings change invalidates its range, so suppress the ready event until the first new sweep completes. */frame_ready=false;if(running){esp_err_t e=tune(frequency_for_sample(0));if(e!=ESP_OK){ESP_LOGW(TAG,"unable to retune after configuration change: %s",esp_err_to_name(e));running=false;}tune_us=esp_timer_get_time();}}
esp_err_t cc1101_init(void){if(initialized)return detected?ESP_OK:ESP_ERR_NOT_FOUND;gpio_reset_pin(RFSCOUT_BOARD_CC1101_CS_PIN);spi_device_interface_config_t dc={.clock_speed_hz=4000000,.mode=0,.spics_io_num=RFSCOUT_BOARD_CC1101_CS_PIN,.queue_size=1};esp_err_t e=spi_bus_add_device(CC1101_SPI_HOST,&dc,&device);if(e!=ESP_OK)return e;initialized=true;disable_rf_path();(void)strobe(CC1101_SRES);uint8_t part=0xff,ver=0xff;(void)read_status(CC1101_PARTNUM,&part);(void)read_status(CC1101_VERSION,&ver);detected=(part==0x00U&&ver!=0x00U&&ver!=0xffU);ESP_LOGI(TAG,"probe part=0x%02x version=0x%02x detected=%d",part,ver,detected);memset(display_spectrum,-120,sizeof(display_spectrum));restart_sweep();return detected?ESP_OK:ESP_ERR_NOT_FOUND;}
esp_err_t cc1101_start(void){esp_err_t e=cc1101_init();if(e!=ESP_OK)return e;e=write_register(CC1101_MCSM0,0x18U);if(e!=ESP_OK)return e;running=true;restart_sweep();return running?ESP_OK:ESP_FAIL;}
void cc1101_stop(void){if(!initialized)return;running=false;(void)strobe(CC1101_SIDLE);(void)strobe(CC1101_SPWD);disable_rf_path();}
void cc1101_poll(void){if(!running||!detected)return;int64_t now=esp_timer_get_time();if(now-tune_us<CC1101_SETTLE_US)return;uint8_t raw;if(read_status(CC1101_RSSI,&raw)==ESP_OK){size_t b=display_bin_for_sample(sweep_index);int8_t r=decode_rssi(raw);if(r>scan_spectrum[b])scan_spectrum[b]=r;}++sweep_index;if(sweep_index>=sweep_count){/* Publish an atomic, complete frame. The LCD continues showing this frame while the next sweep is acquired. */memcpy(display_spectrum,scan_spectrum,sizeof(display_spectrum));memset(scan_spectrum,-120,sizeof(scan_spectrum));frame_ready=true;sweep_index=0;}esp_err_t e=tune(frequency_for_sample(sweep_index));if(e!=ESP_OK){ESP_LOGW(TAG,"CC1101 tune failed: %s",esp_err_to_name(e));running=false;disable_rf_path();return;}tune_us=now;}
bool cc1101_is_detected(void){return detected;}
const cc1101_scan_config_t *cc1101_config(void){return &config;}
const char *cc1101_profile_name(cc1101_profile_t p){static const char *n[]={"433 ISM","315 MHz","868 MHz","915 MHz","MANUAL"};return p<CC1101_PROFILE_COUNT?n[p]:"?";}
void cc1101_set_profile(cc1101_profile_t p){if(p>=CC1101_PROFILE_COUNT)return;config.profile=p;switch(p){case CC1101_PROFILE_433:config.low_khz=433050;config.high_khz=434790;config.step_khz=25;break;case CC1101_PROFILE_315:config.low_khz=300000;config.high_khz=348000;config.step_khz=250;break;case CC1101_PROFILE_868:config.low_khz=863000;config.high_khz=870000;config.step_khz=100;break;case CC1101_PROFILE_915:config.low_khz=902000;config.high_khz=928000;config.step_khz=200;break;case CC1101_PROFILE_MANUAL:default:break;}restart_sweep();}
static uint32_t clamp_band(uint32_t f){if(f<300000U)return 300000U;if(f>928000U)return 928000U;if(f>348000U&&f<387000U)return f<367500U?348000U:387000U;if(f>464000U&&f<779000U)return f<621500U?464000U:779000U;return f;}
void cc1101_adjust_low(int d){int64_t f=(int64_t)config.low_khz+(int64_t)d*config.step_khz;if(f<300000)f=300000;config.low_khz=clamp_band((uint32_t)f);if(config.low_khz>=config.high_khz)config.low_khz=config.high_khz-config.step_khz;config.profile=CC1101_PROFILE_MANUAL;restart_sweep();}
void cc1101_adjust_high(int d){int64_t f=(int64_t)config.high_khz+(int64_t)d*config.step_khz;if(f>928000)f=928000;config.high_khz=clamp_band((uint32_t)f);if(config.high_khz<=config.low_khz)config.high_khz=config.low_khz+config.step_khz;config.profile=CC1101_PROFILE_MANUAL;restart_sweep();}
void cc1101_adjust_step(int d){static const uint32_t s[]={10,25,50,100,200,250,500,1000};size_t i=0;while(i+1<sizeof(s)/sizeof(s[0])&&s[i]<config.step_khz)i++;if(d>0&&i+1<sizeof(s)/sizeof(s[0]))i++;else if(d<0&&i>0)i--;config.step_khz=s[i];config.profile=CC1101_PROFILE_MANUAL;restart_sweep();}
size_t cc1101_spectrum(int8_t *o,size_t cap){if(!o||!cap)return 0;size_t n=cap<CC1101_SPECTRUM_BINS?cap:CC1101_SPECTRUM_BINS;memcpy(o,display_spectrum,n);return n;}
bool cc1101_take_frame_ready(void){bool ready=frame_ready;frame_ready=false;return ready;}
