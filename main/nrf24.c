/**
    nrf24.c : RFScout nRF24 receive-only channel activity scanner
    Description: Sweeps nRF24 2.4 GHz channels using the RPD energy detector.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/
#include "nrf24.h"
#include "nrf24_activity.h"
#include "board.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#define NRF24_SPI_HOST SPI2_HOST
#define NRF24_CMD_R_REGISTER 0x00U
#define NRF24_CMD_W_REGISTER 0x20U
#define NRF24_CMD_NOP 0xFFU
#define NRF24_REG_CONFIG 0x00U
#define NRF24_REG_EN_AA 0x01U
#define NRF24_REG_EN_RXADDR 0x02U
#define NRF24_REG_SETUP_AW 0x03U
#define NRF24_REG_RF_CH 0x05U
#define NRF24_REG_RF_SETUP 0x06U
#define NRF24_REG_STATUS 0x07U
#define NRF24_REG_RPD 0x09U
#define NRF24_CONFIG_PRIM_RX 0x01U
#define NRF24_CONFIG_PWR_UP 0x02U
#define NRF24_MIN_RX_SETTLE_US 170U
#define NRF24_TASK_STACK_SIZE 3072U
#define NRF24_TASK_PRIORITY ( tskIDLE_PRIORITY + 1U )
#define NRF24_DELAY_SLICE_US 1000U
#define NRF24_CHANNELS_PER_SCHEDULER_BREAK 16U
#define NRF24_FRAME_NOTIFY_INTERVAL_MS 125U
#define NRF24_STOP_TIMEOUT_MS 250U
static const char *TAG="nrf24";
static spi_device_handle_t device; static TaskHandle_t scan_task; static portMUX_TYPE state_lock=portMUX_INITIALIZER_UNLOCKED;
static bool initialized,detected; static volatile bool running,worker_active,frame_ready; static TickType_t last_frame_take_tick;
static uint8_t acquisition[NRF24_CHANNEL_COUNT],completed[NRF24_CHANNEL_COUNT]; static nrf24_scan_config_t config={.low_channel=0U,.high_channel=125U,.dwell_us=250U};
static esp_err_t transfer(const uint8_t *tx,uint8_t *rx,size_t len){spi_transaction_t t={.length=len*8U,.rxlength=len*8U,.tx_buffer=tx,.rx_buffer=rx};return spi_device_transmit(device,&t);}
static esp_err_t read_register(uint8_t reg,uint8_t *value){uint8_t tx[2]={NRF24_CMD_R_REGISTER|(reg&0x1FU),NRF24_CMD_NOP},rx[2]={0};esp_err_t r=transfer(tx,rx,2);if(r==ESP_OK&&value)*value=rx[1];return r;}
static esp_err_t write_register(uint8_t reg,uint8_t value){uint8_t tx[2]={NRF24_CMD_W_REGISTER|(reg&0x1FU),value},rx[2]={0};return transfer(tx,rx,2);}
static void ce(bool on){gpio_set_level(RFSCOUT_BOARD_NRF24_CE_PIN,on?1:0);} static esp_err_t begin_channel(uint8_t ch){ce(false);esp_err_t r=write_register(NRF24_REG_RF_CH,ch);if(r!=ESP_OK)return r;ce(true);return ESP_OK;}
static nrf24_scan_config_t config_snapshot(void){portENTER_CRITICAL(&state_lock);nrf24_scan_config_t s=config;portEXIT_CRITICAL(&state_lock);return s;}
static void dwell_wait(uint16_t us){uint32_t rem=us;TickType_t tick=pdMS_TO_TICKS(1U);while(running&&rem>=NRF24_DELAY_SLICE_US){if(tick>0U)vTaskDelay(tick);else esp_rom_delay_us(NRF24_DELAY_SLICE_US);rem-=NRF24_DELAY_SLICE_US;}if(running&&rem>0U)esp_rom_delay_us(rem);}
static void scan_worker(void *arg){(void)arg;while(true){ulTaskNotifyTake(pdTRUE,portMAX_DELAY);worker_active=true;while(running){nrf24_scan_config_t s=config_snapshot();uint16_t dwell=s.dwell_us<NRF24_MIN_RX_SETTLE_US?NRF24_MIN_RX_SETTLE_US:s.dwell_us;memset(acquisition,0,sizeof(acquisition));bool ok=true;unsigned since=0U;for(uint16_t ch=s.low_channel;running&&ch<=s.high_channel;++ch){esp_err_t r=begin_channel((uint8_t)ch);if(r!=ESP_OK){ESP_LOGW(TAG,"RF_CH write failed for channel %u: %s",(unsigned)ch,esp_err_to_name(r));ce(false);ok=false;break;}dwell_wait(dwell);if(!running){ce(false);break;}uint8_t rpd=0U;r=read_register(NRF24_REG_RPD,&rpd);ce(false);if(r!=ESP_OK){ESP_LOGW(TAG,"RPD read failed for channel %u: %s",(unsigned)ch,esp_err_to_name(r));ok=false;break;}acquisition[ch]=(rpd&1U)?1U:0U;if(++since>=NRF24_CHANNELS_PER_SCHEDULER_BREAK){since=0U;vTaskDelay(1U);}}if(!running)break;if(!ok){running=false;break;}nrf24_activity_observe_sweep(acquisition,NRF24_CHANNEL_COUNT);portENTER_CRITICAL(&state_lock);memcpy(completed,acquisition,sizeof(completed));frame_ready=true;portEXIT_CRITICAL(&state_lock);vTaskDelay(1U);}ce(false);worker_active=false;}}
esp_err_t nrf24_init(void){if(initialized)return detected?ESP_OK:ESP_ERR_NOT_FOUND;gpio_reset_pin(RFSCOUT_BOARD_NRF24_CS_PIN);gpio_set_direction(RFSCOUT_BOARD_NRF24_CE_PIN,GPIO_MODE_OUTPUT);ce(false);spi_device_interface_config_t dc={.clock_speed_hz=4000000,.mode=0,.spics_io_num=RFSCOUT_BOARD_NRF24_CS_PIN,.queue_size=1};esp_err_t r=spi_bus_add_device(NRF24_SPI_HOST,&dc,&device);if(r!=ESP_OK)return r;initialized=true;uint8_t aw=0xFFU,ch=0xFFU,status=0xFFU;(void)read_register(NRF24_REG_SETUP_AW,&aw);(void)read_register(NRF24_REG_RF_CH,&ch);(void)read_register(NRF24_REG_STATUS,&status);detected=aw>=1U&&aw<=3U&&ch<=125U&&status!=0xFFU;ESP_LOGI(TAG,"probe setup_aw=0x%02x rf_ch=%u status=0x%02x detected=%d",aw,ch,status,detected);memset(acquisition,0,sizeof(acquisition));memset(completed,0,sizeof(completed));if(detected&&NULL==scan_task&&pdPASS!=xTaskCreate(scan_worker,"nrf24_scan",NRF24_TASK_STACK_SIZE,NULL,NRF24_TASK_PRIORITY,&scan_task)){scan_task=NULL;return ESP_ERR_NO_MEM;}return detected?ESP_OK:ESP_ERR_NOT_FOUND;}
esp_err_t nrf24_start(void){esp_err_t r=nrf24_init();if(r!=ESP_OK)return r;ce(false);r=write_register(NRF24_REG_EN_AA,0);if(r==ESP_OK)r=write_register(NRF24_REG_EN_RXADDR,0);if(r==ESP_OK)r=write_register(NRF24_REG_RF_SETUP,0x06U);if(r==ESP_OK)r=write_register(NRF24_REG_CONFIG,NRF24_CONFIG_PWR_UP|NRF24_CONFIG_PRIM_RX);if(r!=ESP_OK)return r;portENTER_CRITICAL(&state_lock);memset(acquisition,0,sizeof(acquisition));frame_ready=false;last_frame_take_tick=0U;running=true;portEXIT_CRITICAL(&state_lock);xTaskNotifyGive(scan_task);return ESP_OK;}
void nrf24_stop(void){if(!initialized)return;running=false;const TickType_t start=xTaskGetTickCount(),timeout=pdMS_TO_TICKS(NRF24_STOP_TIMEOUT_MS);while(worker_active&&(xTaskGetTickCount()-start)<timeout)vTaskDelay(pdMS_TO_TICKS(1U));if(worker_active){ESP_LOGE(TAG,"scan worker did not quiesce within %u ms",(unsigned)NRF24_STOP_TIMEOUT_MS);return;}ce(false);(void)write_register(NRF24_REG_CONFIG,0x00U);}
void nrf24_poll(void){} bool nrf24_is_detected(void){return detected;} const nrf24_scan_config_t *nrf24_config(void){return &config;}
static void restart_scan(void){portENTER_CRITICAL(&state_lock);frame_ready=false;portEXIT_CRITICAL(&state_lock);}
void nrf24_adjust_low(int d){portENTER_CRITICAL(&state_lock);int v=(int)config.low_channel+d;if(v<0)v=0;if(v>=config.high_channel)v=(int)config.high_channel-1;config.low_channel=(uint8_t)v;portEXIT_CRITICAL(&state_lock);restart_scan();}
void nrf24_adjust_high(int d){portENTER_CRITICAL(&state_lock);int v=(int)config.high_channel+d;if(v>125)v=125;if(v<=config.low_channel)v=(int)config.low_channel+1;config.high_channel=(uint8_t)v;portEXIT_CRITICAL(&state_lock);restart_scan();}
void nrf24_adjust_dwell(int d){static const uint16_t values[]={200U,250U,500U,1000U,2000U,5000U};portENTER_CRITICAL(&state_lock);size_t i=0U;while(i+1U<sizeof(values)/sizeof(values[0])&&values[i]<config.dwell_us)++i;if(d>0&&i+1U<sizeof(values)/sizeof(values[0]))++i;else if(d<0&&i>0U)--i;config.dwell_us=values[i];portEXIT_CRITICAL(&state_lock);restart_scan();}
bool nrf24_take_frame_ready(void){TickType_t now=xTaskGetTickCount(),min=pdMS_TO_TICKS(NRF24_FRAME_NOTIFY_INTERVAL_MS);portENTER_CRITICAL(&state_lock);bool elapsed=0U==last_frame_take_tick||0U==min||(now-last_frame_take_tick)>=min;bool ready=frame_ready&&elapsed;if(ready){frame_ready=false;last_frame_take_tick=now;}portEXIT_CRITICAL(&state_lock);return ready;}
size_t nrf24_spectrum(uint8_t *out,size_t cap){if(!out||!cap)return 0U;size_t count=cap<NRF24_CHANNEL_COUNT?cap:NRF24_CHANNEL_COUNT;portENTER_CRITICAL(&state_lock);memcpy(out,completed,count);portEXIT_CRITICAL(&state_lock);return count;}
