/** radio_app_view.c : RFScout radio application views */
#include "radio_app_view.h"
#include "battery_indicator.h"
#include "cc1101.h"
#include "display.h"
#include "nrf24_activity.h"
#include "status_bar.h"
#include <stdio.h>

#define BG 0x0000U
#define SUBDUED 0x03F0U
#define TEXT 0xBDF7U
#define DIM 0x7BEFU
#define YELLOW 0xFFE0U
#define RED 0xF800U
#define LIMIT 0xFD20U
#define GRAPH 0x03E0U
#define PEAK 0x07FFU
#define APP_COLUMN_DIVIDER_X 198
#define APP_RIGHT_COLUMN_X 205

static void draw_status_chrome( void ) { status_bar_draw(); battery_indicator_draw(); }
static void mhz( char *b, size_t n, uint32_t khz ) { snprintf( b, n, "%lu.%03lu", (unsigned long)( khz / 1000U ), (unsigned long)( khz % 1000U ) ); }

void radio_app_view_cc1101_spectrum( const uint8_t *cur, const uint8_t *peak, size_t count,
                                     uint32_t low_khz, uint32_t high_khz, bool detected,
                                     const cc1101_fingerprint_t *devices, size_t device_count )
{
    display_clear( BG ); display_draw_text( 8, 7, "CC1101", SUBDUED, 2U ); draw_status_chrome();
    char low[24], high[24], line[64]; mhz( low, sizeof low, low_khz ); mhz( high, sizeof high, high_khz );
    snprintf( line, sizeof line, "%s - %s MHz", low, high ); display_draw_text( 8, 30, line, DIM, 1U );
    if( !detected ) { display_draw_text( 8,70,"CC1101 NOT DETECTED",YELLOW,1U ); display_draw_text( 8,126,"CENTER: SETTINGS",SUBDUED,1U ); (void)display_present(); return; }
    const int x0=8,y0=48,width=APP_COLUMN_DIVIDER_X-14,height=92; display_fill_rect( x0,y0+height,width,1,DIM );
    for( size_t i=0U;i<count;++i ) { const int x=x0+(int)(i*(size_t)(width-1)/(count>1U?count-1U:1U)); const int h=(int)cur[i]*(height-3)/255; if(h>0)display_fill_rect(x,y0+height-h,2,h,GRAPH); if(peak[i]>0U){int py=y0+height-(int)peak[i]*(height-3)/255;if(py<y0)py=y0;display_fill_rect(x,py,2,2,PEAK);} }
    display_fill_rect(APP_COLUMN_DIVIDER_X,36,1,RFSCOUT_DISPLAY_HEIGHT-36,DIM); display_draw_text(APP_RIGHT_COLUMN_X,43,"LIKELY DEVICES",SUBDUED,1U);
    for(size_t i=0U;(i<device_count)&&(i<CC1101_FINGERPRINT_MAX);++i){const int y=64+(int)i*18;const char *label=devices[i].label[0]?devices[i].label:cc1101_activity_fingerprint_name(devices[i].type);snprintf(line,sizeof line,"%-12.12s %3u",label,(unsigned)devices[i].count);display_draw_text(APP_RIGHT_COLUMN_X,y,line,(0U==devices[i].count)?DIM:RED,1U);}
    display_draw_text(8,148,"RSSI / PEAK HOLD",DIM,1U); display_draw_text(250,148,"SETTINGS",SUBDUED,1U); (void)display_present();
}

void radio_app_view_cc1101_menu( unsigned selected, bool editing, const char *profile,
                                 uint32_t low, uint32_t high, uint32_t step, size_t samples,
                                 bool limit_reached, bool peak_hold )
{
    display_clear(BG); display_draw_text(8,7,"CC1101 SETTINGS",SUBDUED,2U); draw_status_chrome();
    const char *labels[]={"PROFILE","LOW","HIGH","STEP","PEAK HOLD","RESET COUNTS","BACK"}; char value[32];
    for(unsigned i=0U;i<7U;++i){const int y=32+(int)i*18;const bool active=i==selected;const bool action=i>=5U;const uint16_t accent=(active&&editing&&limit_reached)?LIMIT:YELLOW;display_draw_text(8,y,active?(action?">":(editing?"*":">")):" ",active?accent:DIM,1U);display_draw_text(22,y,labels[i],active?TEXT:DIM,1U);value[0]='\0';if(i==0U)snprintf(value,sizeof value,"%s",profile);else if(i==1U)snprintf(value,sizeof value,"%lu.%03lu MHz",(unsigned long)(low/1000U),(unsigned long)(low%1000U));else if(i==2U)snprintf(value,sizeof value,"%lu.%03lu MHz",(unsigned long)(high/1000U),(unsigned long)(high%1000U));else if(i==3U)snprintf(value,sizeof value,"%lu kHz",(unsigned long)step);else if(i==4U)snprintf(value,sizeof value,"%s",peak_hold?"ON":"OFF");if(value[0]){if(active&&editing){display_fill_rect(126,y-4,174,18,accent);display_draw_text(130,y,value,BG,1U);}else display_draw_text(130,y,value,active?YELLOW:DIM,1U);}}
    snprintf(value,sizeof value,"%u/%u SAMPLES",(unsigned)samples,(unsigned)CC1101_MAX_SWEEP_SAMPLES);display_draw_text(8,160,value,limit_reached?LIMIT:DIM,1U);(void)display_present();
}

void radio_app_view_nrf24_spectrum( const uint8_t *fast,const uint8_t *slow,size_t count,uint8_t low,uint8_t high,bool detected,const nrf24_fingerprint_t *devices,size_t device_count )
{
    display_clear(BG);display_draw_text(8,7,"nRF24",SUBDUED,2U);draw_status_chrome();char line[64];snprintf(line,sizeof line,"CH %u-%u   %u-%u MHz",(unsigned)low,(unsigned)high,(unsigned)(2400U+low),(unsigned)(2400U+high));display_draw_text(8,30,line,DIM,1U);if(!detected){display_draw_text(8,70,"nRF24 NOT DETECTED",YELLOW,1U);display_draw_text(8,126,"CENTER: SETTINGS",SUBDUED,1U);(void)display_present();return;}
    const int x0=8,y0=48,width=APP_COLUMN_DIVIDER_X-14,height=92;display_fill_rect(x0,y0+height,width,1,DIM);const size_t span=(size_t)(high-low)+1U;for(size_t i=0U;i<span;++i){const size_t ch=(size_t)low+i;if(ch>=count)break;const int x=x0+(int)(i*(size_t)(width-1)/(span>1U?span-1U:1U));const int h=(int)fast[ch]*(height-3)/255;if(h>0)display_fill_rect(x,y0+height-h,2,h,GRAPH);if(slow[ch]>0U){int py=y0+height-(int)slow[ch]*(height-3)/255;if(py<y0)py=y0;display_fill_rect(x,py,2,2,PEAK);}}
    display_fill_rect(APP_COLUMN_DIVIDER_X,36,1,RFSCOUT_DISPLAY_HEIGHT-36,DIM);display_draw_text(APP_RIGHT_COLUMN_X,43,"LIKELY DEVICES",SUBDUED,1U);for(size_t i=0U;(i<device_count)&&(i<NRF24_FINGERPRINT_MAX);++i){const int y=64+(int)i*18;const char *label=devices[i].label[0]?devices[i].label:nrf24_activity_fingerprint_name(devices[i].type);snprintf(line,sizeof line,"%-12.12s %3lu",label,(unsigned long)devices[i].packets);display_draw_text(APP_RIGHT_COLUMN_X,y,line,(0U==devices[i].packets)?DIM:RED,1U);}display_draw_text(8,148,"FAST ACTIVITY / PEAK HOLD",DIM,1U);display_draw_text(250,148,"SETTINGS",SUBDUED,1U);(void)display_present();
}

void radio_app_view_nrf24_menu( unsigned selected,bool editing,uint8_t low,uint8_t high,uint16_t dwell,bool peak_hold )
{
    display_clear(BG);display_draw_text(8,7,"nRF24 SETTINGS",SUBDUED,2U);draw_status_chrome();const char *labels[]={"LOW CH","HIGH CH","DWELL","PEAK HOLD","RESET COUNTS","BACK"};char value[32];for(unsigned i=0U;i<6U;++i){const int y=34+(int)i*20;const bool active=i==selected;const bool action=i>=4U;display_draw_text(8,y,active?(action?">":(editing?"*":">")):" ",active?YELLOW:DIM,1U);display_draw_text(22,y,labels[i],active?TEXT:DIM,1U);value[0]='\0';if(i==0U)snprintf(value,sizeof value,"%u / %u MHz",(unsigned)low,(unsigned)(2400U+low));else if(i==1U)snprintf(value,sizeof value,"%u / %u MHz",(unsigned)high,(unsigned)(2400U+high));else if(i==2U)snprintf(value,sizeof value,"%u us",(unsigned)dwell);else if(i==3U)snprintf(value,sizeof value,"%s",peak_hold?"ON":"OFF");if(value[0]){if(active&&editing){display_fill_rect(126,y-4,174,18,YELLOW);display_draw_text(130,y,value,BG,1U);}else display_draw_text(130,y,value,active?YELLOW:DIM,1U);}}display_draw_text(8,158,editing?"TURN: CHANGE  CENTER: DONE":"TURN: SELECT CENTER: ENTER",editing?YELLOW:SUBDUED,1U);(void)display_present();
}
