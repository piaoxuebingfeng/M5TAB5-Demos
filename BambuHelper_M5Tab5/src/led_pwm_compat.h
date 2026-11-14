#pragma once
#include <Arduino.h>
#include <esp_arduino_version.h>

inline void ledPwmWrite(uint8_t channel, uint32_t duty) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWriteChannel(channel, duty);
#else
  ledcWrite(channel, duty);
#endif
}

inline bool ledPwmAttach(uint8_t pin, uint8_t channel, uint32_t frequency, uint8_t resolution) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  return ledcAttachChannel(pin, frequency, resolution, channel);
#else
  if (!ledcSetup(channel, frequency, resolution)) return false;
  ledcAttachPin(pin, channel);
  return true;
#endif
}

inline void ledPwmDetach(uint8_t pin) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcDetach(pin);
#else
  ledcDetachPin(pin);
#endif
}
