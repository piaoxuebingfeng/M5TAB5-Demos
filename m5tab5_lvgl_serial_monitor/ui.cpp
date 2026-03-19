#include "serial_monitor.h"

extern M5GFX display;

typedef struct _objects_t {
    lv_obj_t *main;
    lv_obj_t *mainscr;
    lv_obj_t *monitorpanel;
    lv_obj_t *leftpanel;
    lv_obj_t *startbtn;
    lv_obj_t *obj0;
    lv_obj_t *obj1;
    lv_obj_t *obj2;
    lv_obj_t *obj3;
    lv_obj_t *obj4;
    lv_obj_t *obj5;
    lv_obj_t *portselect;
    lv_obj_t *baudselect;
    lv_obj_t *databitselect;
    lv_obj_t *stopbitselect;
    lv_obj_t *parityselect;
    lv_obj_t *clearbtn;
    lv_obj_t *obj6;
    lv_obj_t *satatuspanel;
    lv_obj_t *txlabel;
    lv_obj_t *statusled;
    lv_obj_t *appname;
    lv_obj_t *rxlabel;
    lv_obj_t *errlabel;
    lv_obj_t *txcount;
    lv_obj_t *rxcount;
    lv_obj_t *errcount;
    lv_obj_t *helpbtn;
    lv_obj_t *helpbtnlabel;
} objects_t;

objects_t objects;



static lv_obj_t *ui_panInfo;
static lv_obj_t *ui_lblBandrate;
// static lv_obj_t *ui_lblStatus;
static lv_obj_t *ui_ddSerialSource;

static bool line_exceeded;
static bool is_monitoring = false;
static uint32_t tx_count = 0;
static uint32_t rx_count = 0;
static uint32_t err_count = 0;
static lv_obj_t *ui_panList;
static lv_obj_t *currentLine;

static const uint32_t baud_rates[] = {9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600, 1500000};
static const uint8_t data_bits[] = {8, 7};
static const uint8_t stop_bits[] = {1, 2};
static const char* parity_names[] = {"None", "Even", "Odd"};

static uint32_t current_baud = 115200;
static uint8_t current_databits = 8;
static uint8_t current_stopbits = 1;
static uint8_t current_parity = 0;

static void helpbtn_event_cb(lv_event_t * e);

static lv_obj_t * help_dialog = NULL;

static void help_dialog_close_event_cb(lv_event_t * e)
{
    (void)e;
    if (help_dialog != NULL) {
        lv_obj_add_flag(help_dialog, LV_OBJ_FLAG_HIDDEN);
    }
}

static void show_help_dialog(void)
{
    if (help_dialog != NULL) {
        lv_obj_clear_flag(help_dialog, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    lv_obj_t * parent = objects.main;

    lv_obj_t * dialog = lv_obj_create(parent);
    help_dialog = dialog;
    lv_obj_set_pos(dialog, 340, 160);
    lv_obj_set_size(dialog, 600, 360);
    lv_obj_set_style_bg_color(dialog, lv_color_hex(0xff666666), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(dialog, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(dialog, lv_color_hex(0xff888888), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(dialog, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(dialog, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    {
        lv_obj_t * title = lv_label_create(dialog);
        lv_label_set_text(title, "Serial Port Pin Info");
        lv_obj_set_style_text_font(title, &lv_font_montserrat_24, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(title, lv_color_hex(0xffffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 15);
    }

    {
        lv_obj_t * close_btn = lv_btn_create(dialog);
        lv_obj_set_size(close_btn, 40, 40);
        lv_obj_align(close_btn, LV_ALIGN_TOP_RIGHT, -5, 5);
        lv_obj_set_style_bg_color(close_btn, lv_color_hex(0xffaa4444), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_radius(close_btn, 5, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_add_event_cb(close_btn, help_dialog_close_event_cb, LV_EVENT_CLICKED, NULL);
        {
            lv_obj_t * lbl = lv_label_create(close_btn);
            lv_label_set_text(lbl, LV_SYMBOL_CLOSE);
            lv_obj_set_style_text_font(lbl, &lv_font_montserrat_24, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_color(lbl, lv_color_hex(0xffffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_center(lbl);
        }
    }

    const char * pin_names[] = {"GND", "+5V", "G53", "G54"};
    const char * pin_funcs[] = {"GND", "+5V", "RXD", "TXD"};
    lv_color_t pin_colors[] = {
        lv_color_hex(0xff000000),
        lv_color_hex(0xffcc0000),
        lv_color_hex(0xffcccc00),
        lv_color_hex(0xffffffff)
    };

    int32_t rect_w = 120;
    int32_t rect_h = 60;
    int32_t spacing = 10;

    for (int i = 0; i < 4; i++) {
        lv_obj_t * rect = lv_obj_create(dialog);
        lv_obj_set_size(rect, rect_w, rect_h);
        lv_obj_align(rect, LV_ALIGN_CENTER, -225 + i * (rect_w + spacing), -30);
        lv_obj_set_style_bg_color(rect, pin_colors[i], LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(rect, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_color(rect, lv_color_hex(0xff888888), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_radius(rect, 5, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_clear_flag(rect, LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t * label = lv_label_create(rect);
        lv_label_set_text(label, pin_names[i]);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_22, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_color_t txt_color = (i == 0) ? lv_color_hex(0xffffffff) : lv_color_hex(0xff000000);
        lv_obj_set_style_text_color(label, txt_color, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_center(label);
    }

    for (int i = 0; i < 4; i++) {
        lv_obj_t * rect = lv_obj_create(dialog);
        lv_obj_set_size(rect, rect_w, rect_h);
        lv_obj_align(rect, LV_ALIGN_CENTER, -225 + i * (rect_w + spacing), 50);
        lv_obj_set_style_bg_color(rect, lv_color_hex(0xff444444), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(rect, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_color(rect, lv_color_hex(0xff888888), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_radius(rect, 5, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_clear_flag(rect, LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t * label = lv_label_create(rect);
        lv_label_set_text(label, pin_funcs[i]);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_22, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(label, lv_color_hex(0xffffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_center(label);
    }
}

static void helpbtn_event_cb(lv_event_t * e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        show_help_dialog();
    }
}

static void update_status_led(bool running)
{
    if (running) {
        lv_led_set_color(objects.statusled, lv_color_hex(0xff00ff00));
        lv_led_set_brightness(objects.statusled, 255);
    } else {
        lv_led_set_color(objects.statusled, lv_color_hex(0xffff0000));
        lv_led_set_brightness(objects.statusled, 128);
    }
}

static void update_counts(void)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "%lu", tx_count);
    lv_label_set_text(objects.txcount, buf);
    snprintf(buf, sizeof(buf), "%lu", rx_count);
    lv_label_set_text(objects.rxcount, buf);
    snprintf(buf, sizeof(buf), "%lu", err_count);
    lv_label_set_text(objects.errcount, buf);
}


static void clear_terminal(void)
{
    tx_count = 0;
    rx_count = 0;
    err_count = 0;
    line_exceeded = false;
    
    while (lv_obj_get_child_cnt(ui_panList) > 0) {
        lv_obj_del(lv_obj_get_child(ui_panList, 0));
    }
    
    currentLine = lv_label_create(ui_panList);
    lv_label_set_text(currentLine, "Serial Monitor Ready");
    lv_obj_set_style_text_color(currentLine, lv_color_hex(COLOR_INFO_TEXT), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(currentLine, &lv_font_montserrat_22, LV_PART_MAIN | LV_STATE_DEFAULT);
    
    update_counts();
}

static void apply_uart_config(void)
{
    if (currentSerialSource == SERIAL_SOURCE_SERIAL) {
        // Serial is HWCDC (USB), baud rate is virtual
    } else {
        Serial2.updateBaudRate(current_baud);
    }
}

static void dd_serial_source_event_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_VALUE_CHANGED) {
        lv_obj_t *dropdown = lv_event_get_target(e);
        uint16_t selected = lv_dropdown_get_selected(dropdown);
        if (selected == 0) {
            uart_change_source(SERIAL_SOURCE_SERIAL);
        } else {
            uart_change_source(SERIAL_SOURCE_SERIAL2);
        }
        if (is_monitoring) {
            apply_uart_config();
        }
    }
}

static void dd_baudrate_event_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_VALUE_CHANGED) {
        lv_obj_t *dropdown = lv_event_get_target(e);
        uint16_t selected = lv_dropdown_get_selected(dropdown);
        if (selected < sizeof(baud_rates)/sizeof(baud_rates[0])) {
            current_baud = baud_rates[selected];
            if (is_monitoring) {
                apply_uart_config();
            }
        }
    }
}

static void dd_databits_event_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_VALUE_CHANGED) {
        lv_obj_t *dropdown = lv_event_get_target(e);
        uint16_t selected = lv_dropdown_get_selected(dropdown);
        if (selected < sizeof(data_bits)/sizeof(data_bits[0])) {
            current_databits = data_bits[selected];
        }
    }
}

static void dd_stopbits_event_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_VALUE_CHANGED) {
        lv_obj_t *dropdown = lv_event_get_target(e);
        uint16_t selected = lv_dropdown_get_selected(dropdown);
        if (selected < sizeof(stop_bits)/sizeof(stop_bits[0])) {
            current_stopbits = stop_bits[selected];
        }
    }
}

static void dd_parity_event_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_VALUE_CHANGED) {
        lv_obj_t *dropdown = lv_event_get_target(e);
        uint16_t selected = lv_dropdown_get_selected(dropdown);
        current_parity = selected;
    }
}

static void btn_start_event_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        is_monitoring = !is_monitoring;
        
        if (is_monitoring) {
            apply_uart_config();
            lv_label_set_text(objects.obj0, "Stop");
            lv_obj_set_style_bg_color(objects.startbtn, lv_color_hex(0xff4a4a4a), LV_PART_MAIN | LV_STATE_DEFAULT);
        } else {
            lv_label_set_text(objects.obj0, "Start");
            lv_obj_set_style_bg_color(objects.startbtn, lv_color_hex(0xff2a2a2a), LV_PART_MAIN | LV_STATE_DEFAULT);
        }
        
        update_status_led(is_monitoring);
    }
}

static void btn_clear_event_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        clear_terminal();
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
                if (is_monitoring) {
                    if (q.is_rx) {
                        rx_count += q.length;
                    } else {
                        tx_count += q.length;
                    }
                    update_counts();
                    
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
                }
                xSemaphoreGive(lvgl_mutex);
            }
        }
        vTaskDelay(20);
    }
    vTaskDelete(NULL);
}


void create_screen_main() {
    // lv_obj_t *obj = lv_obj_create(0);
    // lv_obj_t *obj = lv_obj_create(lv_scr_act());
    lv_obj_t *obj = lv_scr_act();
    objects.main = obj;
    lv_obj_set_pos(obj, 0, 0);
    lv_obj_set_size(obj, 1280, 720);
    {
        lv_obj_t *parent_obj = obj;
        {
            // mainscr
            lv_obj_t *obj = lv_obj_create(parent_obj);
            objects.mainscr = obj;
            lv_obj_set_pos(obj, 0, 0);
            lv_obj_set_size(obj, 1280, 720);
            lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_style_radius(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_color(obj, lv_color_hex(0xff1a1a1a), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            {
                lv_obj_t *parent_obj = obj;
                {
                    // monitorpanel
                    lv_obj_t *obj = lv_obj_create(parent_obj);
                    objects.monitorpanel = obj;
                    lv_obj_set_pos(obj, 200, 40);
                    lv_obj_set_size(obj, 1060, 660);
                    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
                    lv_obj_set_style_radius(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_bg_color(obj, lv_color_hex(0xff514b4b), LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_pad_left(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_pad_right(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_pad_top(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_pad_bottom(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                    {
                        lv_obj_t *parent_obj = obj;
                        {
                            // ui_panList - terminal list
                            lv_obj_t *obj = lv_obj_create(parent_obj);
                            ui_panList = obj;
                            lv_obj_set_pos(obj, 0, 0);
                            lv_obj_set_size(obj, 1060, 660);
                            lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_COLUMN);
                            lv_obj_set_flex_align(obj, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
                            lv_obj_set_scroll_dir(obj, LV_DIR_VER);
                            lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_ON);
                            lv_obj_set_style_radius(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_bg_color(obj, lv_color_hex(COLOR_SCREEN_BG), LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_bg_opa(obj, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_pad_left(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_pad_right(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_pad_top(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_pad_bottom(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                            
                            // initial line
                            currentLine = lv_label_create(obj);
                            lv_label_set_text(currentLine, "Serial Monitor Ready");
                            lv_obj_set_style_text_color(currentLine, lv_color_hex(COLOR_INFO_TEXT), LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_text_font(currentLine, &lv_font_montserrat_22, LV_PART_MAIN | LV_STATE_DEFAULT);
                        }
                    }
                }
                {
                    // leftpanel
                    lv_obj_t *obj = lv_obj_create(parent_obj);
                    objects.leftpanel = obj;
                    lv_obj_set_pos(obj, -20, 40);
                    lv_obj_set_size(obj, 220, 660);
                    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
                    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_radius(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_bg_color(obj, lv_color_hex(0xff1a1a3a), LV_PART_MAIN | LV_STATE_DEFAULT);
                    {
                        lv_obj_t *parent_obj = obj;
                        {
                            // startbtn
                            lv_obj_t *obj = lv_btn_create(parent_obj);
                            objects.startbtn = obj;
                            lv_obj_set_pos(obj, 0, 496);
                            lv_obj_set_size(obj, 180, 50);
                            lv_obj_set_style_bg_color(obj, lv_color_hex(0xff2a2a2a), LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_add_event_cb(obj, btn_start_event_handler, LV_EVENT_CLICKED, NULL);
                            {
                                lv_obj_t *parent_obj = obj;
                                {
                                    lv_obj_t *obj = lv_label_create(parent_obj);
                                    objects.obj0 = obj;
                                    lv_obj_set_pos(obj, 0, 0);
                                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                                    lv_obj_set_style_align(obj, LV_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_28, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_text_color(obj, lv_color_hex(0xffffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_label_set_text(obj, "Start");
                                }
                            }
                        }
                        {
                            lv_obj_t *obj = lv_label_create(parent_obj);
                            objects.obj1 = obj;
                            lv_obj_set_pos(obj, -9, 378);
                            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                            lv_obj_set_style_text_color(obj, lv_color_hex(0xffffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_text_font(obj, &lv_font_montserrat_22, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_label_set_text(obj, "Parity:");
                        }
                        {
                            lv_obj_t *obj = lv_label_create(parent_obj);
                            objects.obj2 = obj;
                            lv_obj_set_pos(obj, -9, 281);
                            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                            lv_obj_set_style_text_color(obj, lv_color_hex(0xffffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_text_font(obj, &lv_font_montserrat_22, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_label_set_text(obj, "Stop bits:");
                        }
                        {
                            lv_obj_t *obj = lv_label_create(parent_obj);
                            objects.obj3 = obj;
                            lv_obj_set_pos(obj, -9, 183);
                            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                            lv_obj_set_style_text_color(obj, lv_color_hex(0xffffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_text_font(obj, &lv_font_montserrat_22, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_label_set_text(obj, "Data bits:");
                        }
                        {
                            lv_obj_t *obj = lv_label_create(parent_obj);
                            objects.obj4 = obj;
                            lv_obj_set_pos(obj, -9, 86);
                            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                            lv_obj_set_style_text_color(obj, lv_color_hex(0xffffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_text_font(obj, &lv_font_montserrat_22, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_label_set_text(obj, "Baud Rate:");
                        }
                        {
                            lv_obj_t *obj = lv_label_create(parent_obj);
                            objects.obj5 = obj;
                            lv_obj_set_pos(obj, -9, -12);
                            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                            lv_obj_set_style_text_color(obj, lv_color_hex(0xffffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_text_font(obj, &lv_font_montserrat_22, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_label_set_text(obj, "Port:");
                        }
                        {
                            // portselect
                            lv_obj_t *obj = lv_dropdown_create(parent_obj);
                            objects.portselect = obj;
                            lv_obj_set_pos(obj, 15, 30);
                            lv_obj_set_size(obj, 150, LV_SIZE_CONTENT);
                            lv_dropdown_set_options(obj, "Serial\nSerial2");
                            lv_dropdown_set_selected(obj, 0);
                            lv_obj_add_event_cb(obj, dd_serial_source_event_handler, LV_EVENT_VALUE_CHANGED, NULL);
                        }
                        {
                            // baudselect
                            lv_obj_t *obj = lv_dropdown_create(parent_obj);
                            objects.baudselect = obj;
                            lv_obj_set_pos(obj, 15, 128);
                            lv_obj_set_size(obj, 150, LV_SIZE_CONTENT);
                            lv_dropdown_set_options(obj, "9600\n19200\n38400\n57600\n115200\n230400\n460800\n921600\n1500000");
                            lv_dropdown_set_selected(obj, 4);
                            lv_obj_add_event_cb(obj, dd_baudrate_event_handler, LV_EVENT_VALUE_CHANGED, NULL);
                        }
                        {
                            // databitselect
                            lv_obj_t *obj = lv_dropdown_create(parent_obj);
                            objects.databitselect = obj;
                            lv_obj_set_pos(obj, 15, 227);
                            lv_obj_set_size(obj, 150, LV_SIZE_CONTENT);
                            lv_dropdown_set_options(obj, "8\n7");
                            lv_dropdown_set_selected(obj, 0);
                            lv_obj_add_event_cb(obj, dd_databits_event_handler, LV_EVENT_VALUE_CHANGED, NULL);
                        }
                        {
                            // stopbitselect
                            lv_obj_t *obj = lv_dropdown_create(parent_obj);
                            objects.stopbitselect = obj;
                            lv_obj_set_pos(obj, 15, 325);
                            lv_obj_set_size(obj, 150, LV_SIZE_CONTENT);
                            lv_dropdown_set_options(obj, "1\n2");
                            lv_dropdown_set_selected(obj, 0);
                            lv_obj_add_event_cb(obj, dd_stopbits_event_handler, LV_EVENT_VALUE_CHANGED, NULL);
                        }
                        {
                            // parityselect
                            lv_obj_t *obj = lv_dropdown_create(parent_obj);
                            objects.parityselect = obj;
                            lv_obj_set_pos(obj, 15, 423);
                            lv_obj_set_size(obj, 150, LV_SIZE_CONTENT);
                            lv_dropdown_set_options(obj, "None\nEven\nOdd");
                            lv_dropdown_set_selected(obj, 0);
                            lv_obj_add_event_cb(obj, dd_parity_event_handler, LV_EVENT_VALUE_CHANGED, NULL);
                        }
                        {
                            // clearbtn
                            lv_obj_t *obj = lv_btn_create(parent_obj);
                            objects.clearbtn = obj;
                            lv_obj_set_pos(obj, 0, 564);
                            lv_obj_set_size(obj, 180, 50);
                            lv_obj_set_style_bg_color(obj, lv_color_hex(0xff3a3a3a), LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_add_event_cb(obj, btn_clear_event_handler, LV_EVENT_CLICKED, NULL);
                            {
                                lv_obj_t *parent_obj = obj;
                                {
                                    lv_obj_t *obj = lv_label_create(parent_obj);
                                    objects.obj6 = obj;
                                    lv_obj_set_pos(obj, 0, 0);
                                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                                    lv_obj_set_style_align(obj, LV_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_28, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_text_color(obj, lv_color_hex(0xffffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_label_set_text(obj, "ClearData");
                                }
                            }
                        }
                    }
                }
                {
                    // satatuspanel
                    lv_obj_t *obj = lv_obj_create(parent_obj);
                    objects.satatuspanel = obj;
                    lv_obj_set_pos(obj, -20, -20);
                    lv_obj_set_size(obj, 1280, 60);
                    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
                    lv_obj_set_style_bg_color(obj, lv_color_hex(0xff1a1a2a), LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_radius(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                    {
                        lv_obj_t *parent_obj = obj;
                        {
                            // txlabel
                            lv_obj_t *obj = lv_label_create(parent_obj);
                            objects.txlabel = obj;
                            lv_obj_set_pos(obj, 816, -3);
                            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                            lv_obj_set_style_text_color(obj, lv_color_hex(0xff0000ff), LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_text_font(obj, &lv_font_montserrat_24, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_label_set_text(obj, "TX:");
                        }
                        {
                            // statusled
                            lv_obj_t *obj = lv_led_create(parent_obj);
                            objects.statusled = obj;
                            lv_obj_set_pos(obj, 243, 0);
                            lv_obj_set_size(obj, 20, 20);
                            lv_led_set_color(obj, lv_color_hex(0xffff0000));
                            lv_led_set_brightness(obj, 255);
                            lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
                        }
                        {
                            // helpbtn
                            lv_obj_t *obj = lv_btn_create(parent_obj);
                            objects.helpbtn = obj;
                            lv_obj_set_size(obj, 180, 50);
                            lv_obj_set_style_bg_color(obj, lv_color_hex(0xff2a2a2a), LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_pos(obj, 300, -15);
                            lv_obj_add_event_cb(obj, helpbtn_event_cb, LV_EVENT_CLICKED, NULL);
                            {
                                // helpbtnlabel
                                lv_obj_t *parent_obj = obj;
                                {
                                    lv_obj_t *obj = lv_label_create(parent_obj);
                                    objects.helpbtnlabel = obj;
                                    lv_obj_set_pos(obj, 0, 0);
                                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                                    lv_obj_set_style_align(obj, LV_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_28, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_text_color(obj, lv_color_hex(0xffffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_label_set_text(obj, "Help");
                                }
                            }
                        }
                        {
                            // appname
                            lv_obj_t *obj = lv_label_create(parent_obj);
                            objects.appname = obj;
                            lv_obj_set_pos(obj, 14, -5);
                            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                            lv_obj_set_style_text_color(obj, lv_color_hex(0xffffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_text_font(obj, &lv_font_montserrat_28, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_label_set_text(obj, "Serial Monitor");
                        }
                        {
                            // rxlabel
                            lv_obj_t *obj = lv_label_create(parent_obj);
                            objects.rxlabel = obj;
                            lv_obj_set_pos(obj, 962, -3);
                            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                            lv_obj_set_style_text_color(obj, lv_color_hex(0xff00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_text_font(obj, &lv_font_montserrat_24, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_label_set_text(obj, "RX:");
                        }
                        {
                            // errlabel
                            lv_obj_t *obj = lv_label_create(parent_obj);
                            objects.errlabel = obj;
                            lv_obj_set_pos(obj, 1109, -3);
                            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                            lv_obj_set_style_text_color(obj, lv_color_hex(0xffff0000), LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_text_font(obj, &lv_font_montserrat_24, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_label_set_text(obj, "ERR:");
                        }
                        {
                            // txcount
                            lv_obj_t *obj = lv_label_create(parent_obj);
                            objects.txcount = obj;
                            lv_obj_set_pos(obj, 889, -3);
                            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                            lv_obj_set_style_text_color(obj, lv_color_hex(0xff0000ff), LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_text_font(obj, &lv_font_montserrat_24, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_label_set_text(obj, "0");
                        }
                        {
                            // rxcount
                            lv_obj_t *obj = lv_label_create(parent_obj);
                            objects.rxcount = obj;
                            lv_obj_set_pos(obj, 1036, -3);
                            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                            lv_obj_set_style_text_color(obj, lv_color_hex(0xff00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_text_font(obj, &lv_font_montserrat_24, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_label_set_text(obj, "0");
                        }
                        {
                            // errcount
                            lv_obj_t *obj = lv_label_create(parent_obj);
                            objects.errcount = obj;
                            lv_obj_set_pos(obj, 1182, -3);
                            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                            lv_obj_set_style_text_color(obj, lv_color_hex(0xffffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_text_font(obj, &lv_font_montserrat_24, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_label_set_text(obj, "0");
                        }
                    }
                }
            }
        }
    }
}


static void ui_draw_main_screen(void)
{
    lv_disp_t *dispp = lv_disp_get_default();
    lv_theme_t *theme = lv_theme_default_init(dispp, lv_palette_main(LV_PALETTE_BLUE), lv_palette_main(LV_PALETTE_RED), false, LV_FONT_DEFAULT);
    lv_disp_set_theme(dispp, theme);

    create_screen_main();
    
    xTaskCreatePinnedToCore(ui_task_update_main_screen, "ui_update", 8192, NULL, 3, NULL, 1);
}

void ui_init(void)
{
    lvgl_mutex = xSemaphoreCreateMutex();
    ui_draw_main_screen();
    update_status_led(false);
}
