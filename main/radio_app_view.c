/**
    radio_app_view.c : RFScout radio application views
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/
#include "radio_app_view.h"
#include "display.h"
#include <stdio.h>
#define BG 0x0000U
#define SUBDUED 0x03F0U
#define TEXT 0xBDF7U
#define DIM 0x7BEFU
#define YELLOW 0xFFE0U
#define GRAPH 0x07E0U
static void mhz(char *b,size_t n,uint32_t k){snprintf(b,n,"%lu.%03lu",(unsigned long)(k/1000U),(unsigned long)(k%1000U));}
void radio_app_view_cc1101_spectrum(const int8_t *bins,size_t count,uint32_t low,uint32_t high,bool detected){display_clear(BG);display_draw_text(8,7,"CC1101",SUBDUED,2U);char a[24],b[24],line[64];mhz(a,sizeof(a),low);mhz(b,sizeof(b),high);snprintf(line,sizeof(line),"%s - %s MHz",a,b);display_draw_text(8,30,line,DIM,1U);if(!detected){display_draw_text(8,70,"CC1101 NOT DETECTED",YELLOW,1U);display_draw_text(8,126,"CENTER: SETTINGS",SUBDUED,1U);(void)display_present();return;}const int x0=8,y0=45,w=304,h=100;display_fill_rect(x0,y0,w,1,DIM);display_fill_rect(x0,y0+h,w,1,DIM);for(size_t i=0;i<count;i++){int r=bins[i];if(r<-120)r=-120;if(r>-20)r=-20;int bh=(r+120)*(h-4)/100;if(bh<1)bh=1;int x=x0+(int)((i*(size_t)(w-1))/(count>1?count-1:1));display_fill_rect(x,y0+h-bh,2,bh,GRAPH);}display_draw_text(250,151,"SETTINGS",SUBDUED,1U);(void)display_present();}
void radio_app_view_cc1101_menu(unsigned selected,bool editing,const char *profile,uint32_t low,uint32_t high,uint32_t step){display_clear(BG);display_draw_text(8,7,"CC1101 SETTINGS",SUBDUED,2U);const char *labels[]={"PROFILE","LOW","HIGH","STEP","BACK"};char val[32];for(unsigned i=0;i<5;i++){int y=38+(int)i*24;bool active=i==selected;display_draw_text(8,y,active?(editing?"*":">"):" ",active?YELLOW:DIM,1U);display_draw_text(22,y,labels[i],active?TEXT:DIM,1U);val[0]='\0';if(i==0)snprintf(val,sizeof(val),"%s",profile);else if(i==1)snprintf(val,sizeof(val),"%lu.%03lu MHz",(unsigned long)(low/1000),(unsigned long)(low%1000));else if(i==2)snprintf(val,sizeof(val),"%lu.%03lu MHz",(unsigned long)(high/1000),(unsigned long)(high%1000));else if(i==3)snprintf(val,sizeof(val),"%lu kHz",(unsigned long)step);if(val[0]){if(active&&editing){int width=174;display_fill_rect(126,y-4,width,18,YELLOW);display_draw_text(130,y,val,BG,1U);}else display_draw_text(130,y,val,active?YELLOW:DIM,1U);}}display_draw_text(8,158,editing?"TURN: CHANGE       CENTER: DONE":"TURN: SELECT      CENTER: ENTER",editing?YELLOW:SUBDUED,1U);(void)display_present();}
void radio_app_view_nrf24(void){display_clear(BG);display_draw_text(8,7,"nRF24",SUBDUED,2U);display_draw_text(8,42,"2.4 GHz RADIO AWARENESS",TEXT,1U);display_draw_text(8,70,"ONBOARD RADIO RESERVED",DIM,1U);display_draw_text(8,90,"DRIVER / VIEW COMING NEXT",DIM,1U);display_draw_text(8,126,"GPIO43 CE / GPIO44 CSN",SUBDUED,1U);(void)display_present();}
