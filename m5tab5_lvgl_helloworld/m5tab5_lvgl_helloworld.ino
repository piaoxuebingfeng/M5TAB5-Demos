/*
M5TAB5 LVGL Demo
lvgl helloworld

Dependencies:

ESP-Arduino >= V3.2 (tested also working with 3.3.0-alpha1)
M5GFX >= V0.2.8
LVGL = V8.4.0

lv_conf.h:

#define LV_COLOR_DEPTH 16
#define LV_COLOR_16_SWAP 1
#define LV_MEM_CUSTOM 1
#define LV_TICK_CUSTOM 1

Build Options:

Board: "ESP32P4 Dev Module"
USB CDC on boot: "Enabled"
Flash Size: "16MB (128Mb)"
Partition Scheme: "Custom" (the supplied partitions.csv file allows almost full use of the flash for the main app)
PSRAM: "Enabled"
Upload Mode: "UART / Hardware CDC"
USB Mode: "Hardware CDC and JTAG"


Notes:

This demo uses a software rotate in order to give us landscape mode (disp_drv.rotated = LV_DISP_ROT_90;)
I've not been able to find the make and model of the display unit used in the Tab5 so I can't be sure if it supports a hardware accelerated rotation.
The display driver chip is ili9881c with native portrait orientation.

*/

#include <M5GFX.h>
#include "lvgl.h"

// Display
#define EXAMPLE_LCD_H_RES 720
#define EXAMPLE_LCD_V_RES 1280


#define LVGL_LCD_BUF_SIZE     (EXAMPLE_LCD_H_RES * EXAMPLE_LCD_V_RES)
#define SEND_BUF_SIZE         (EXAMPLE_LCD_H_RES * 10)


M5GFX display;


uint16_t count = 0;
bool automate = false;

static lv_disp_draw_buf_t draw_buf;
static lv_color_t *buf;




void lv_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p)
{
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);
    display.pushImageDMA(area->x1, area->y1, w, h, (uint16_t *)&color_p->full); 
    lv_disp_flush_ready(disp);
}



void my_rounder(lv_disp_drv_t *disp_drv, lv_area_t *area)
{
    if (area->x1 % 2 != 0)
        area->x1 += 1;
    if (area->y1 % 2 != 0)
        area->y1 += 1;

    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);
    if (w % 2 != 0)
        area->x2 -= 1;
    if (h % 2 != 0)
        area->y2 -= 1;
}



static void lv_indev_read(lv_indev_drv_t *indev_driver, lv_indev_data_t *data)
{  
     lgfx::touch_point_t tp[3];
     uint8_t touchpad = display.getTouchRaw(tp,3);
       if (touchpad > 0)
       {
          data->state = LV_INDEV_STATE_PR;
          data->point.x = tp[0].x;
          data->point.y = tp[0].y;
          //Serial.printf("X: %d   Y: %d\n", tp[0].x, tp[0].y); //for testing
       }
       else
       {
        data->state = LV_INDEV_STATE_REL;
       }
}


void lvgl_helloworld_show()
{
    lv_obj_t *scr = lv_scr_act();
    
    lv_obj_set_style_bg_color(scr, lv_color_make(70, 75, 85), LV_STATE_DEFAULT);

    lv_obj_t *label = lv_label_create(scr);
    lv_label_set_text(label, "helloworld");
    lv_obj_set_style_text_color(label, lv_color_hex(0xffffff), LV_STATE_DEFAULT);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
}




void setup() {
  // put your setup code here, to run once:

    display.init();
    Serial.begin(115200);//for debug


    /*Initialize LVGL*/
    lv_init();
    buf = (lv_color_t *)heap_caps_malloc(sizeof(lv_color_t) * LVGL_LCD_BUF_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    lv_disp_draw_buf_init(&draw_buf, buf, NULL, LVGL_LCD_BUF_SIZE);
    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = EXAMPLE_LCD_H_RES;
    disp_drv.ver_res = EXAMPLE_LCD_V_RES;
    //disp_drv.rounder_cb = my_rounder; 
    disp_drv.flush_cb = lv_disp_flush;
    disp_drv.draw_buf = &draw_buf;
    disp_drv.sw_rotate = 1;  
    disp_drv.rotated = LV_DISP_ROT_90; 
    lv_disp_drv_register(&disp_drv);



    /*Initialize touch*/
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = lv_indev_read;
    lv_indev_drv_register(&indev_drv); 

    lvgl_helloworld_show();
    display.setBrightness(255); 
}

void loop() {

    lv_timer_handler();
    delay(1);

}
