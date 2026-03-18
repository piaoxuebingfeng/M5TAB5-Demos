#include "serial_monitor.h"

extern M5GFX display;

static lv_obj_t *ui_scrMain;
static lv_obj_t *ui_panTerminal;
static lv_obj_t *ui_panList;
static lv_obj_t *currentLine;
static lv_obj_t *ui_panInfo;
static lv_obj_t *ui_lblBandrate;
static lv_obj_t *ui_lblLineCounter;
static lv_obj_t *ui_lblStatus;
static lv_obj_t *ui_ddSerialSource;

static bool line_exceeded;

static void dd_serial_source_event_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_VALUE_CHANGED) {
        lv_obj_t *dropdown = lv_event_get_target(e);
        uint16_t selected = lv_dropdown_get_selected(dropdown);
        if (selected == 0) {
            uart_change_source(SERIAL_SOURCE_SERIAL);
            lv_label_set_text(ui_lblStatus, "Serial");
        } else {
            uart_change_source(SERIAL_SOURCE_SERIAL2);
            lv_label_set_text(ui_lblStatus, "Serial2");
        }
    }
}

static void ui_task_update_main_screen(void *pvParameters)
{
    uart_data_t q;
    
    for (;;)
    {
        if (xQueueReceive(uart_queue, &q, 0) == pdTRUE)
        {
            if (pdTRUE == xSemaphoreTake(lvgl_mutex, portMAX_DELAY))
            {
                lineCount++;
                lv_label_set_text_fmt(ui_lblLineCounter, "%u", lineCount);
                
                if (line_exceeded)
                {
                    lv_obj_del(lv_obj_get_child(ui_panList, 0));
                }
                else
                {
                    if (lv_obj_get_child_cnt(ui_panList) >= TERM_LINE_MAX)
                    {
                        line_exceeded = true;
                    }
                }
                
                currentLine = lv_label_create(ui_panList);
                lv_obj_set_width(currentLine, lv_pct(100));
                lv_obj_set_height(currentLine, LV_SIZE_CONTENT);
                lv_obj_set_style_text_font(currentLine, &lv_font_montserrat_22, LV_PART_MAIN | LV_STATE_DEFAULT);
                lv_obj_set_style_pad_top(currentLine, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
                lv_obj_set_style_pad_bottom(currentLine, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
                
                if (q.is_rx)
                {
                    lv_obj_set_style_text_color(currentLine, lv_color_hex(COLOR_TERM_RX_TEXT), LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_text_align(currentLine, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
                }
                else
                {
                    lv_obj_set_style_text_color(currentLine, lv_color_hex(COLOR_TERM_TX_TEXT), LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_text_align(currentLine, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN | LV_STATE_DEFAULT);
                }
                
                lv_label_set_text(currentLine, q.data_string.c_str());
                lv_obj_scroll_to_view(currentLine, LV_ANIM_OFF);
                
                xSemaphoreGive(lvgl_mutex);
            }
        }
        vTaskDelay(20);
    }
    vTaskDelete(NULL);
}

static void ui_draw_main_screen(void)
{
    ui_scrMain = lv_obj_create(lv_scr_act());
    lv_obj_set_size(ui_scrMain, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_set_pos(ui_scrMain, 0, 0);
    lv_obj_set_style_radius(ui_scrMain, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_scrMain, lv_color_hex(COLOR_SCREEN_BG), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_scrMain, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui_scrMain, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(ui_scrMain, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_outline_width(ui_scrMain, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_scrollbar_mode(ui_scrMain, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(ui_scrMain, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_MOMENTUM | LV_OBJ_FLAG_SCROLL_CHAIN);
    
    ui_panInfo = lv_obj_create(ui_scrMain);
    lv_obj_set_pos(ui_panInfo, 0, 0);
    lv_obj_set_size(ui_panInfo, SCREEN_WIDTH, 60);
    lv_obj_clear_flag(ui_panInfo, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_MOMENTUM | LV_OBJ_FLAG_SCROLL_CHAIN);
    lv_obj_set_style_radius(ui_panInfo, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_panInfo, lv_color_hex(0x202020), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_panInfo, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui_panInfo, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    
    ui_lblBandrate = lv_label_create(ui_panInfo);
    lv_obj_set_align(ui_lblBandrate, LV_ALIGN_LEFT_MID);
    lv_obj_set_x(ui_lblBandrate, 20);
    lv_label_set_text(ui_lblBandrate, "115200");
    lv_obj_set_style_text_color(ui_lblBandrate, lv_color_hex(COLOR_INFO_TEXT), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_lblBandrate, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
    
    ui_ddSerialSource = lv_dropdown_create(ui_panInfo);
    lv_obj_set_pos(ui_ddSerialSource, 200, 5);
    lv_obj_set_size(ui_ddSerialSource, 160, 40);
    lv_dropdown_set_options(ui_ddSerialSource, "Serial\nSerial2");
    lv_dropdown_set_selected(ui_ddSerialSource, 1);
    lv_obj_set_style_text_font(ui_ddSerialSource, &lv_font_montserrat_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(ui_ddSerialSource, dd_serial_source_event_handler, LV_EVENT_VALUE_CHANGED, NULL);
    
    ui_lblLineCounter = lv_label_create(ui_panInfo);
    lv_obj_set_align(ui_lblLineCounter, LV_ALIGN_CENTER);
    lv_label_set_text(ui_lblLineCounter, "0");
    lv_obj_set_style_text_color(ui_lblLineCounter, lv_color_hex(COLOR_INFO_TEXT), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_lblLineCounter, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
    
    ui_lblStatus = lv_label_create(ui_panInfo);
    lv_obj_set_align(ui_lblStatus, LV_ALIGN_RIGHT_MID);
    lv_obj_set_x(ui_lblStatus, -20);
    lv_label_set_text(ui_lblStatus, "Serial2");
    lv_obj_set_style_text_color(ui_lblStatus, lv_color_hex(COLOR_TERM_RX_TEXT), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_lblStatus, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
    
    ui_panTerminal = lv_obj_create(ui_scrMain);
    lv_obj_set_pos(ui_panTerminal, 0, 60);
    lv_obj_set_size(ui_panTerminal, SCREEN_WIDTH, SCREEN_HEIGHT - 60);
    lv_obj_set_style_radius(ui_panTerminal, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_panTerminal, lv_color_hex(COLOR_SCREEN_BG), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_panTerminal, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui_panTerminal, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(ui_panTerminal, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(ui_panTerminal, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(ui_panTerminal, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(ui_panTerminal, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_MOMENTUM | LV_OBJ_FLAG_SCROLL_CHAIN);
    
    ui_panList = lv_obj_create(ui_panTerminal);
    lv_obj_set_pos(ui_panList, 0, 0);
    lv_obj_set_size(ui_panList, SCREEN_WIDTH, SCREEN_HEIGHT - 60);
    lv_obj_set_flex_flow(ui_panList, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(ui_panList, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(ui_panList, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(ui_panList, LV_SCROLLBAR_MODE_ON);
    lv_obj_set_style_radius(ui_panList, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_panList, lv_color_hex(COLOR_SCREEN_BG), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_panList, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui_panList, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(ui_panList, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(ui_panList, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(ui_panList, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(ui_panList, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    
    currentLine = lv_label_create(ui_panList);
    lv_label_set_text(currentLine, "Serial Monitor Ready");
    lv_obj_set_style_text_color(currentLine, lv_color_hex(COLOR_INFO_TEXT), LV_PART_MAIN | LV_STATE_DEFAULT);
    
    xTaskCreatePinnedToCore(ui_task_update_main_screen, "ui_update", 8192, NULL, 3, NULL, 1);
}

void ui_init(void)
{
    lvgl_mutex = xSemaphoreCreateMutex();
    ui_draw_main_screen();
}
