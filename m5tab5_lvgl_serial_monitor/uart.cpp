#include "serial_monitor.h"

QueueHandle_t uart_queue;
bool is_queue_ok;
SemaphoreHandle_t lvgl_mutex;
uint32_t lineCount = 0;
SerialSource currentSerialSource = SERIAL_SOURCE_SERIAL;

#define UART_RX_PIN 53
#define UART_TX_PIN 54
#define UART_BANDRATE 115200

static void task_uart(void *pvParameters)
{
    uart_data_t q;
    String input;
    
    while (1)
    {
        if (currentSerialSource == SERIAL_SOURCE_SERIAL)
        {
            if (Serial.available() > 0)
            {
                input = Serial.readStringUntil('\n');
                input.trim();
                if (input.length() > 0) {
                    q.data_string = input.substring(0, 256);
                    q.is_rx = true;
                    q.length = input.length();
                    if (is_queue_ok && uart_queue != NULL) {
                        xQueueSend(uart_queue, &q, 0);
                    }
                }
            }
        }
        else
        {
            if (Serial2.available() > 0)
            {
                input = Serial2.readStringUntil('\n');
                input.trim();
                if (input.length() > 0) {
                    q.data_string = input.substring(0, 256);
                    q.is_rx = true;
                    q.length = input.length();
                    if (is_queue_ok && uart_queue != NULL) {
                        xQueueSend(uart_queue, &q, 0);
                    }
                }
            }
        }
        vTaskDelay(UART_TASKDELAY);
    }
}

void uart_change_source(SerialSource source)
{
    currentSerialSource = source;
}

void uart_send_data(const uint8_t* data, size_t len)
{
    if (currentSerialSource == SERIAL_SOURCE_SERIAL)
    {
        Serial.write(data, len);
        Serial.flush();
    }
    else
    {
        Serial2.write(data, len);
        Serial2.flush();
    }
}

void uart_init(void)
{
    uart_queue = xQueueCreate(80, sizeof(uart_data_t));
    Serial2.setTimeout(200);
    Serial2.begin(UART_BANDRATE, SERIAL_8N1, UART_RX_PIN, UART_TX_PIN);
    xTaskCreatePinnedToCore(task_uart, "task_uart", 4096, NULL, 7, NULL, 0);
    is_queue_ok = true;
}
