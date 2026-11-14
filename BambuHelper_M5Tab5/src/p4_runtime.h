#pragma once
#if defined(BOARD_IS_WS_P4_7B)
#include "bambu_state.h"
#include "bambu_mqtt.h"
#include "spsc_latest.h"
#include "p4_ui_state.h"
#include "display_ui.h"
#include "wifi_manager.h"
#include "p4_transport_gate.h"
#include <cstdint>

struct P4MqttCommand {
  enum Kind : uint8_t { Configure, Refresh, Reconnect, Stop, Resume, LightOn, LightOff };
  Kind kind = Configure;
  uint8_t slot = 0;
};

struct P4NetworkStatus {
  bool online = false, ap = false, ota = false, wifiOwnerReady = false;
  char ip[16]{};
  int8_t rssi = 0;
  uint8_t wifiState = 0;
};

void p4RuntimeStart(const P4UiSettings& initialSettings);
void p4RuntimeSync();
bool p4UiApplySettings(const P4UiSettings& settings);
void p4WifiWorkerStart();
bool p4WifiSettingsSnapshot(P4UiSettings& settings);
void p4WifiApplySettings(const P4UiSettings& settings);
bool p4WifiFailureSnapshot(WifiFailInfo& info);
bool p4UiWifiFailureSnapshot(WifiFailInfo& info);
void p4WifiReconnect();
void p4WifiRefreshTime();
void p4WifiGotIp();
void p4MqttWorkerStart();
bool p4MqttPost(const P4MqttCommand& command);
bool p4WorkerConfigSnapshot(uint8_t slot, PrinterConfig& config);
bool p4CameraConfigSnapshot(uint8_t slot, PrinterConfig& config);
bool p4PublishMqttSlot(uint8_t slot, const BambuState& state, const MqttDiag& diag);
bool p4MqttDiagSnapshot(uint8_t slot, MqttDiag& diag);
bool p4CameraSnapshot(uint8_t slot, BambuState& state);
bool p4MqttNetworkSnapshot(P4NetworkStatus& status);
bool p4CloudNetworkSnapshot(P4NetworkStatus& status);
bool p4CameraNetworkSnapshot(P4NetworkStatus& status);
bool p4HttpNetworkSnapshot(P4NetworkStatus& status);
bool p4HttpUiSnapshot(P4UiStatus& status);
bool p4CameraUiSnapshot(P4UiStatus& status);
void p4PublishUiStatus(const P4UiStatus& status);
uint32_t p4LoopStackMinWords();
uint32_t p4UiLoopStackMinWords();
void requestWiFiTransportRebuild();
void* p4WifiTaskHandle();
bool p4NetworkSnapshot(P4NetworkStatus& status);
bool p4WifiOnline();
bool p4WifiApMode();
void p4SetOtaActive(bool active);
void p4CameraVisibility(uint8_t slot, bool visible);
void p4UiServiceStart();
void p4UiPublishStatus();
void p4UiRepaint();
ScreenState p4WebScreenState();
bool p4WebNightBlackout();
bool p4UiToggleAms();
void p4UiSetScreen(uint8_t screen);
bool p4UiCopyFrame(const uint16_t*& pixels, int& width, int& height);
bool p4HttpTouchSnapshot(TouchDiagnostics& diagnostics, TouchContact& contact);
bool p4UiCameraImage(uint8_t slot);
bool p4UiPreviewBrightness(uint8_t value);
bool p4UiBuzzer(uint8_t event);
struct P4LedPreview {
  bool enabled = false, commonAnode = false;
  uint8_t driver = 0, red = 0, green = 0, blue = 0, brightness = 0;
  uint32_t color = 0;
};
struct P4LedEffect { uint8_t mode = 0; uint16_t seconds = 0; uint8_t brightness = 0; uint32_t color = 0; };
bool p4UiPreviewLed(const P4LedPreview& preview);
bool p4UiTestLed(const P4LedEffect& effect);
uint32_t p4UiRequestFrame();
bool p4UiFrameReady(uint32_t request);
bool p4HttpPrinterSnapshot(uint8_t slot, PrinterConfig& config, BambuState& state);
bool p4HttpPrinterConfigured(uint8_t slot);
bool p4HttpSettingsSnapshot(P4UiSettings& settings);
bool p4CloudCommitSettings(const P4UiSettings& settings);
void p4WebServiceStart();
bool p4WebRequestRestart(uint32_t delayMs);
uint32_t p4WebArmRestart();
void p4WebCancelRestart(uint32_t ticket);
bool p4WebFinishRestart(uint32_t ticket, bool stored);
bool p4WebAwaitStorage(uint32_t ticket, uint32_t restartDelayMs = 0);
bool p4CloudTokenSnapshot(char* token, size_t capacity);
bool p4CameraTokenSnapshot(char* token, size_t capacity);
void p4CloudPublishToken(const char* token);
// Non-blocking ownership for optional cloud/camera connections only. MQTT
// never participates in this gate and continues receiving independently.
bool p4AuxTransportTryAcquire(P4AuxTransportOwner owner);
void p4AuxTransportRelease(P4AuxTransportOwner owner);
bool p4AuxTransportBusy();
#else
inline void p4RuntimeStart() {}
inline void p4RuntimeSync() {}
inline void initP4CameraService() {}
inline void startP4CameraService() {}
inline void tickP4CameraService() {}
#endif
