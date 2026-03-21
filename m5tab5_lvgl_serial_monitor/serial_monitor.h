#ifndef _SERIAL_MONITOR_H
#define _SERIAL_MONITOR_H

#include <Arduino.h>
#include <lvgl.h>
#include <M5GFX.h>

#define SCREEN_WIDTH 1280
#define SCREEN_HEIGHT 720

#define UART_QUEUE_WAIT_MS 100
#define UART_TASKDELAY 20
#define TERM_LINE_MAX 128

#define COLOR_SCREEN_BG 0x000000
#define COLOR_TERM_RX_TEXT 0x00FF00
#define COLOR_TERM_TX_TEXT 0xFFAA00
#define COLOR_INFO_TEXT 0x808080

typedef struct {
    String data_string;
    bool is_rx;
    uint16_t length;
} uart_data_t;

enum SerialSource {
    SERIAL_SOURCE_SERIAL,
    SERIAL_SOURCE_SERIAL2
};

extern QueueHandle_t uart_queue;
extern bool is_queue_ok;
extern SemaphoreHandle_t lvgl_mutex;
extern uint32_t lineCount;
extern M5GFX display;
extern SerialSource currentSerialSource;

void uart_init(void);
void ui_init(void);
void uart_change_source(SerialSource source);
void uart_send_data(const uint8_t* data, size_t len);

#endif
