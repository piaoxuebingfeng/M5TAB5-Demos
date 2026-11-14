#pragma once
#include <WiFiClientSecure.h>
#include <esp_arduino_version.h>

extern const uint8_t rootca_crt_bundle_start[] asm("_binary_x509_crt_bundle_start");

inline void configureCloudTrust(WiFiClientSecure& client) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  extern const uint8_t rootca_crt_bundle_end[] asm("_binary_x509_crt_bundle_end");
  client.setCACertBundle(rootca_crt_bundle_start,
                        rootca_crt_bundle_end - rootca_crt_bundle_start);
#else
  client.setCACertBundle(rootca_crt_bundle_start);
#endif
}
