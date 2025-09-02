#ifndef ESP32_NTP_SYNC_TIME_H_
#define ESP32_NTP_SYNC_TIME_H_

#include <Arduino.h>
#include <WiFi.h>
#include <TimeLib.h>
#include "rx8130.h"

void createWiFiSyncTimeTask();


#endif