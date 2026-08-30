/**
    cc1101.c : RFScout CC1101 receive-only spectrum scanner
    Description: Sweeps configured sub-GHz frequencies and publishes complete RSSI frames.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "cc1101.h"
#include "board.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
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
#define CC1101_SETTLE_US 180U
#define CC1101_TASK_STACK_SIZE 3072U
#define CC1101_TASK_PRIORITY 4U
#define CC1101_SAMPLES_PER_SCHEDULER_BREAK 16U
#define CC1101_STOP_TIMEOUT_MS 250U

static const char *TAG = "cc1101";
static spi_device_handle_t device;
static TaskHandle_t scan_task;
static portMUX_TYPE state_lock = portMUX_INITIALIZER_UNLOCKED;
static bool initialized;
static bool detected;
static volatile bool running;
static volatile bool worker_active;
static bool frame_ready;
static uint32_t config_generation;
static int8_t display_spectrum[ CC1101_SPECTRUM_BINS ];
static cc1101_scan_config_t config = { CC1101_PROFILE_433, 432800U, 433200U, 5U };

static esp_err_t transfer( uint8_t a_header, uint8_t a_data, uint8_t *a_result )
{
    spi_transaction_t transaction = { .length = 16, .rxlength = 16 };
    uint8_t tx[ 2 ] = { a_header, a_data }, rx[ 2 ] = { 0U, 0U };
    transaction.tx_buffer = tx; transaction.rx_buffer = rx;
    const esp_err_t result = spi_device_transmit( device, &transaction );
    if( ESP_OK == result && NULL != a_result ) *a_result = rx[ 1 ];
    return result;
}
static esp_err_t strobe( uint8_t a_command ) { return transfer( a_command, 0U, NULL ); }
static esp_err_t write_register( uint8_t a_address, uint8_t a_value ) { return transfer( a_address, a_value, NULL ); }
static esp_err_t read_status( uint8_t a_address, uint8_t *a_value ) { return transfer( a_address | CC1101_READ_BURST, 0U, a_value ); }

static void select_rf_path( uint32_t f )
{
    int sw1 = 0, sw0 = 0;
    if( f <= 348000U ) sw1 = 1;
    else if( f <= 464000U ) { sw1 = 1; sw0 = 1; }
    else if( f >= 779000U ) sw0 = 1;
    gpio_set_level( RFSCOUT_BOARD_CC1101_SWITCH_1_PIN, sw1 );
    gpio_set_level( RFSCOUT_BOARD_CC1101_SWITCH_0_PIN, sw0 );
}
static void disable_rf_path( void ) { gpio_set_level( RFSCOUT_BOARD_CC1101_SWITCH_1_PIN, 0 ); gpio_set_level( RFSCOUT_BOARD_CC1101_SWITCH_0_PIN, 0 ); }
static int8_t decode_rssi( uint8_t raw ) { const int16_t s = raw >= 128U ? (int16_t)raw - 256 : (int16_t)raw; return (int8_t)( s / 2 - 74 ); }
static size_t sweep_count_for( uint32_t low, uint32_t high, uint32_t step ) { return ( 0U == step || high <= low ) ? 1U : (size_t)( ( high - low ) / step ) + 1U; }
static bool valid_sweep( uint32_t low, uint32_t high, uint32_t step ) { return high > low && sweep_count_for( low, high, step ) <= CC1101_MAX_SWEEP_SAMPLES; }
static cc1101_scan_config_t config_snapshot( uint32_t *generation ) { portENTER_CRITICAL( &state_lock ); const cc1101_scan_config_t s = config; if( generation ) *generation = config_generation; portEXIT_CRITICAL( &state_lock ); return s; }
static uint32_t frequency_for_sample( const cc1101_scan_config_t *c, size_t i ) { uint64_t f=(uint64_t)c->low_khz+(uint64_t)i*c->step_khz; return (uint32_t)( f > c->high_khz ? c->high_khz : f ); }
static size_t display_bin_for_sample( size_t i, size_t count ) { return count <= 1U ? 0U : ( i * ( CC1101_SPECTRUM_BINS - 1U ) ) / ( count - 1U ); }
static esp_err_t tune( uint32_t f )
{
    select_rf_path( f ); const uint64_t word=( (uint64_t)f * 1000ULL << 16 ) / CC1101_XOSC_HZ;
    esp_err_t r=strobe(CC1101_SIDLE); if(r==ESP_OK)r=write_register(CC1101_FREQ2,(uint8_t)(word>>16)); if(r==ESP_OK)r=write_register(CC1101_FREQ1,(uint8_t)(word>>8)); if(r==ESP_OK)r=write_register(CC1101_FREQ0,(uint8_t)word); if(r==ESP_OK)r=strobe(CC1101_SRX); return r;
}

static void scan_worker( void *arg )
{
    (void)arg; int8_t acquisition[ CC1101_SPECTRUM_BINS ];
    while( true )
    {
        ulTaskNotifyTake( pdTRUE, portMAX_DELAY );
        worker_active = true;
        while( running )
        {
            uint32_t generation=0U; const cc1101_scan_config_t snapshot=config_snapshot(&generation); const size_t count=sweep_count_for(snapshot.low_khz,snapshot.high_khz,snapshot.step_khz);
            memset(acquisition,-120,sizeof(acquisition)); bool scan_ok=true; unsigned samples_since_break=0U;
            for(size_t index=0U; running && index<count; ++index)
            {
                const uint32_t frequency=frequency_for_sample(&snapshot,index); esp_err_t result=tune(frequency);
                if(result!=ESP_OK){ ESP_LOGW(TAG,"tune failed at %lu kHz: %s",(unsigned long)frequency,esp_err_to_name(result)); scan_ok=false; break; }
                esp_rom_delay_us(CC1101_SETTLE_US); if(!running) break;
                uint8_t raw=0U; result=read_status(CC1101_RSSI,&raw);
                if(result!=ESP_OK){ ESP_LOGW(TAG,"RSSI read failed at %lu kHz: %s",(unsigned long)frequency,esp_err_to_name(result)); scan_ok=false; break; }
                const size_t bin=display_bin_for_sample(index,count); const int8_t rssi=decode_rssi(raw); if(rssi>acquisition[bin]) acquisition[bin]=rssi;
                if(++samples_since_break>=CC1101_SAMPLES_PER_SCHEDULER_BREAK){ samples_since_break=0U; vTaskDelay(1U); }
            }
            if(!running) break;
            if(!scan_ok){ running=false; disable_rf_path(); break; }
            portENTER_CRITICAL(&state_lock); if(generation==config_generation){ memcpy(display_spectrum,acquisition,sizeof(display_spectrum)); frame_ready=true; } portEXIT_CRITICAL(&state_lock); vTaskDelay(1U);
        }
        worker_active = false;
    }
}

esp_err_t cc1101_init( void )
{
    if(initialized) return detected?ESP_OK:ESP_ERR_NOT_FOUND;
    gpio_reset_pin(RFSCOUT_BOARD_CC1101_CS_PIN);
    spi_device_interface_config_t dc={.clock_speed_hz=4000000,.mode=0,.spics_io_num=RFSCOUT_BOARD_CC1101_CS_PIN,.queue_size=1};
    esp_err_t result=spi_bus_add_device(CC1101_SPI_HOST,&dc,&device); if(result!=ESP_OK)return result;
    initialized=true; disable_rf_path(); (void)strobe(CC1101_SRES); uint8_t part=0xFFU,version=0xFFU; (void)read_status(CC1101_PARTNUM,&part); (void)read_status(CC1101_VERSION,&version);
    detected=(0x00U==part)&&(0x00U!=version)&&(0xFFU!=version); ESP_LOGI(TAG,"probe part=0x%02x version=0x%02x detected=%d",part,version,detected); memset(display_spectrum,-120,sizeof(display_spectrum));
    if(detected && NULL==scan_task && pdPASS!=xTaskCreate(scan_worker,"cc1101_scan",CC1101_TASK_STACK_SIZE,NULL,CC1101_TASK_PRIORITY,&scan_task)){scan_task=NULL;return ESP_ERR_NO_MEM;} return detected?ESP_OK:ESP_ERR_NOT_FOUND;
}
esp_err_t cc1101_start( void ) { esp_err_t r=cc1101_init(); if(r!=ESP_OK)return r; r=write_register(CC1101_MCSM0,0x18U); if(r!=ESP_OK)return r; portENTER_CRITICAL(&state_lock);frame_ready=false;running=true;portEXIT_CRITICAL(&state_lock);xTaskNotifyGive(scan_task);return ESP_OK; }
void cc1101_stop( void )
{
    if(!initialized)return; running=false;
    const TickType_t start=xTaskGetTickCount(), timeout=pdMS_TO_TICKS(CC1101_STOP_TIMEOUT_MS);
    while(worker_active && (xTaskGetTickCount()-start)<timeout) vTaskDelay(pdMS_TO_TICKS(1U));
    if(worker_active){ ESP_LOGE(TAG,"scan worker did not quiesce within %u ms",(unsigned)CC1101_STOP_TIMEOUT_MS); return; }
    (void)strobe(CC1101_SIDLE); (void)strobe(CC1101_SPWD); disable_rf_path();
}
void cc1101_poll(void){}
bool cc1101_is_detected(void){return detected;}
const cc1101_scan_config_t *cc1101_config(void){return &config;}
size_t cc1101_sweep_count(void){const cc1101_scan_config_t s=config_snapshot(NULL);return sweep_count_for(s.low_khz,s.high_khz,s.step_khz);}
const char *cc1101_profile_name(cc1101_profile_t p){static const char *names[]={"433 ISM","315 MHz","868 MHz","915 MHz","MANUAL"};return p<CC1101_PROFILE_COUNT?names[p]:"?";}
void cc1101_set_profile(cc1101_profile_t p){if(p>=CC1101_PROFILE_COUNT)return;portENTER_CRITICAL(&state_lock);config.profile=p;switch(p){case CC1101_PROFILE_433:config.low_khz=432800U;config.high_khz=433200U;config.step_khz=5U;break;case CC1101_PROFILE_315:config.low_khz=300000U;config.high_khz=348000U;config.step_khz=500U;break;case CC1101_PROFILE_868:config.low_khz=863000U;config.high_khz=870000U;config.step_khz=100U;break;case CC1101_PROFILE_915:config.low_khz=902000U;config.high_khz=928000U;config.step_khz=250U;break;default:break;}++config_generation;frame_ready=false;portEXIT_CRITICAL(&state_lock);}
static uint32_t clamp_band(uint32_t f){if(f<300000U)return 300000U;if(f>928000U)return 928000U;if(f>348000U&&f<387000U)return f<367500U?348000U:387000U;if(f>464000U&&f<779000U)return f<621500U?464000U:779000U;return f;}
bool cc1101_adjust_low(int d){portENTER_CRITICAL(&state_lock);int64_t f=(int64_t)config.low_khz+(int64_t)d*config.step_khz;if(f<300000LL)f=300000LL;uint32_t c=clamp_band((uint32_t)f);if(c>=config.high_khz)c=config.high_khz-config.step_khz;bool v=valid_sweep(c,config.high_khz,config.step_khz);if(v){config.low_khz=c;config.profile=CC1101_PROFILE_MANUAL;++config_generation;frame_ready=false;}portEXIT_CRITICAL(&state_lock);return v;}
bool cc1101_adjust_high(int d){portENTER_CRITICAL(&state_lock);int64_t f=(int64_t)config.high_khz+(int64_t)d*config.step_khz;if(f>928000LL)f=928000LL;uint32_t c=clamp_band((uint32_t)f);if(c<=config.low_khz)c=config.low_khz+config.step_khz;bool v=valid_sweep(config.low_khz,c,config.step_khz);if(v){config.high_khz=c;config.profile=CC1101_PROFILE_MANUAL;++config_generation;frame_ready=false;}portEXIT_CRITICAL(&state_lock);return v;}
bool cc1101_adjust_step(int d){static const uint32_t steps[]={1U,5U,10U,25U,50U,100U,200U,250U,500U,1000U};portENTER_CRITICAL(&state_lock);size_t i=0U;while(i+1U<sizeof(steps)/sizeof(steps[0])&&steps[i]<config.step_khz)++i;size_t c=i;if(d>0&&c+1U<sizeof(steps)/sizeof(steps[0]))++c;else if(d<0&&c>0U)--c;else{portEXIT_CRITICAL(&state_lock);return false;}uint32_t s=steps[c];bool v=valid_sweep(config.low_khz,config.high_khz,s);if(v){config.step_khz=s;config.profile=CC1101_PROFILE_MANUAL;++config_generation;frame_ready=false;}portEXIT_CRITICAL(&state_lock);return v;}
size_t cc1101_spectrum(int8_t *out,size_t cap){if(!out||!cap)return 0U;size_t count=cap<CC1101_SPECTRUM_BINS?cap:CC1101_SPECTRUM_BINS;portENTER_CRITICAL(&state_lock);memcpy(out,display_spectrum,count);portEXIT_CRITICAL(&state_lock);return count;}
bool cc1101_take_frame_ready(void){portENTER_CRITICAL(&state_lock);bool ready=frame_ready;frame_ready=false;portEXIT_CRITICAL(&state_lock);return ready;}
