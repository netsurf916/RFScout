/**
    cc1101.c : RFScout CC1101 receive-only spectrum scanner
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
#define CC1101_READ_SINGLE 0x80U
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
static spi_device_handle_t dev;
static bool initialized, detected, running;
static int64_t tune_us;
static size_t bin;
static int8_t spectrum[CC1101_SPECTRUM_BINS];
static cc1101_scan_config_t cfg={CC1101_PROFILE_433,433050U,434790U,25U};
static esp_err_t xfer(uint8_t h,uint8_t d,uint8_t *r){ spi_transaction_t t={.length=16,.rxlength=16}; uint8_t tx[2]={h,d},rx[2]={0}; t.tx_buffer=tx;t.rx_buffer=rx;esp_err_t e=spi_device_transmit(dev,&t);if(e==ESP_OK&&r)*r=rx[1];return e; }
static esp_err_t strobe(uint8_t s){return xfer(s,0,NULL);}
static esp_err_t write_reg(uint8_t a,uint8_t v){return xfer(a,v,NULL);}
static esp_err_t read_status(uint8_t a,uint8_t *v){return xfer(a|CC1101_READ_BURST,0,v);}
static esp_err_t tune(uint32_t khz){uint64_t word=((uint64_t)khz*1000ULL<<16)/CC1101_XOSC_HZ;esp_err_t e=strobe(CC1101_SIDLE);if(e==ESP_OK)e=write_reg(CC1101_FREQ2,(word>>16)&0xff);if(e==ESP_OK)e=write_reg(CC1101_FREQ1,(word>>8)&0xff);if(e==ESP_OK)e=write_reg(CC1101_FREQ0,word&0xff);if(e==ESP_OK)e=strobe(CC1101_SRX);return e;}
static int8_t decode_rssi(uint8_t raw){int16_t s=(raw>=128U)?((int16_t)raw-256):(int16_t)raw;return (int8_t)(s/2-74);}
static uint32_t frequency_for_bin(size_t i){uint64_t span=cfg.high_khz-cfg.low_khz;return cfg.low_khz+(uint32_t)((span*i)/(CC1101_SPECTRUM_BINS-1U));}
esp_err_t cc1101_init(void){if(initialized)return detected?ESP_OK:ESP_ERR_NOT_FOUND;/* The LCD owns and initializes SPI2 first. CC1101 is another device on that shared bus, so only attach a device here. Reset CS from the temporary board GPIO ownership before handing it to the SPI driver. */gpio_reset_pin(RFSCOUT_BOARD_CC1101_CS_PIN);spi_device_interface_config_t dc={.clock_speed_hz=4000000,.mode=0,.spics_io_num=RFSCOUT_BOARD_CC1101_CS_PIN,.queue_size=1};esp_err_t e=spi_bus_add_device(CC1101_SPI_HOST,&dc,&dev);if(e!=ESP_OK)return e;initialized=true;(void)strobe(CC1101_SRES);uint8_t part=0xff,ver=0xff;(void)read_status(CC1101_PARTNUM,&part);(void)read_status(CC1101_VERSION,&ver);detected=(part==0x00U&&ver!=0x00U&&ver!=0xffU);ESP_LOGI(TAG,"probe part=0x%02x version=0x%02x detected=%d",part,ver,detected);memset(spectrum,-120,sizeof(spectrum));return detected?ESP_OK:ESP_ERR_NOT_FOUND;}
esp_err_t cc1101_start(void){esp_err_t e=cc1101_init();if(e!=ESP_OK)return e;running=true;bin=0;e=write_reg(CC1101_MCSM0,0x18U);if(e==ESP_OK)e=tune(frequency_for_bin(bin));tune_us=esp_timer_get_time();return e;}
void cc1101_stop(void){if(!initialized)return;running=false;(void)strobe(CC1101_SIDLE);(void)strobe(CC1101_SPWD);}
void cc1101_poll(void){if(!running||!detected)return;int64_t now=esp_timer_get_time();if(now-tune_us<CC1101_SETTLE_US)return;uint8_t raw;if(read_status(CC1101_RSSI,&raw)==ESP_OK)spectrum[bin]=decode_rssi(raw);bin=(bin+1U)%CC1101_SPECTRUM_BINS;if(tune(frequency_for_bin(bin))!=ESP_OK){running=false;return;}tune_us=now;}
bool cc1101_is_detected(void){return detected;}
const cc1101_scan_config_t *cc1101_config(void){return &cfg;}
const char *cc1101_profile_name(cc1101_profile_t p){static const char *n[]={"433 ISM","315 MHz","868 MHz","915 MHz","MANUAL"};return p<CC1101_PROFILE_COUNT?n[p]:"?";}
void cc1101_set_profile(cc1101_profile_t p){if(p>=CC1101_PROFILE_COUNT)return;cfg.profile=p;switch(p){case CC1101_PROFILE_433:cfg.low_khz=433050;cfg.high_khz=434790;cfg.step_khz=25;break;case CC1101_PROFILE_315:cfg.low_khz=300000;cfg.high_khz=348000;cfg.step_khz=250;break;case CC1101_PROFILE_868:cfg.low_khz=863000;cfg.high_khz=870000;cfg.step_khz=100;break;case CC1101_PROFILE_915:cfg.low_khz=902000;cfg.high_khz=928000;cfg.step_khz=200;break;case CC1101_PROFILE_MANUAL:break;default:break;}bin=0;}
static uint32_t clamp_band(uint32_t v){if(v<300000U)return 300000U;if(v>928000U)return 928000U;if(v>348000U&&v<387000U)return v<367500U?348000U:387000U;if(v>464000U&&v<779000U)return v<621500U?464000U:779000U;return v;}
void cc1101_adjust_low(int d){int64_t v=(int64_t)cfg.low_khz+(int64_t)d*(int64_t)cfg.step_khz;cfg.low_khz=clamp_band((uint32_t)(v<300000?300000:v));if(cfg.low_khz>=cfg.high_khz)cfg.low_khz=cfg.high_khz-cfg.step_khz;cfg.profile=CC1101_PROFILE_MANUAL;}
void cc1101_adjust_high(int d){int64_t v=(int64_t)cfg.high_khz+(int64_t)d*(int64_t)cfg.step_khz;cfg.high_khz=clamp_band((uint32_t)(v>928000?928000:v));if(cfg.high_khz<=cfg.low_khz)cfg.high_khz=cfg.low_khz+cfg.step_khz;cfg.profile=CC1101_PROFILE_MANUAL;}
void cc1101_adjust_step(int d){static const uint32_t s[]={10,25,50,100,200,250,500,1000};size_t i=0;while(i+1<sizeof(s)/sizeof(s[0])&&s[i]<cfg.step_khz)i++;if(d>0&&i+1<sizeof(s)/sizeof(s[0]))i++;else if(d<0&&i>0)i--;cfg.step_khz=s[i];cfg.profile=CC1101_PROFILE_MANUAL;}
size_t cc1101_spectrum(int8_t *o,size_t cap){if(!o||!cap)return 0;size_t n=cap<CC1101_SPECTRUM_BINS?cap:CC1101_SPECTRUM_BINS;memcpy(o,spectrum,n);return n;}
