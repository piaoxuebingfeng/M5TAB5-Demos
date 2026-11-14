// Board macro matrix. Arduino IDE has no global -D injection, so every
// translation unit pulls it in first (see board_config.h for the rationale).
#include "board_config.h"

#define P4_SETTINGS_NO_ALIAS
#include "p4_runtime.h"
#if defined(BOARD_IS_WS_P4_7B)
#include "settings.h"
#include "wifi_manager.h"
#include "p4_camera_service.h"
#include "web_server.h"
#include "p4_cloud_worker.h"
#include "cloud_login.h"
#include "button.h"
#include "buzzer.h"
#include "led.h"
#include "p4_ui_settings.h"
#include "p4_mqtt_protocol.h"
#include "p4_hosted_memory.h"
#include "p4_tls_memory.h"
#include <WiFi.h>
#include <atomic>
#include <cstring>
#include <new>
#include <esp_heap_caps.h>
#include <freertos/queue.h>
#include <NetworkManager.h>

namespace {
struct SharedSlot {
  SpscLatest<BambuState> stateForUi, stateForCamera, stateForHttp;
  SpscLatest<MqttDiag> diagForHttp;
  SpscLatest<PrinterConfig> configForMqtt, configForCamera, configForHttp;
};
SharedSlot* shared = nullptr;
SpscLatest<P4UiSettings>* settingsForWeb = nullptr;
SpscLatest<P4UiSettings>* settingsForWifi = nullptr;
SpscLatest<P4NetworkStatus> networkForUi, networkForMqtt, networkForCamera, networkForHttp, networkForCloud;
SpscLatest<WifiFailInfo> wifiFailureForUi, wifiFailureForHttp;
SpscLatest<P4UiStatus> uiForHttp, uiForCamera;
struct TokenSnapshot { char value[CLOUD_TOKEN_MAX]{}; };
SpscLatest<TokenSnapshot> tokenForMqtt, tokenForCamera;
QueueHandle_t wifiCommands = nullptr;
std::atomic<TaskHandle_t> wifiTask{nullptr};
std::atomic<uint8_t> networkFlags{0};
std::atomic<bool> wifiStarted{false}, networkReady{false}, otaActive{false};
P4AuxTransportGate auxTransport;
std::atomic<uint32_t> wifiCommandsDropped{0};
std::atomic<uint32_t> wifiRpcTimeouts{0};

SpscLatest<P4UiSettings>* committedSettings = nullptr;
QueueHandle_t brightnessCommands = nullptr, buzzerCommands = nullptr;
P4UiSettings* wifiSettings = nullptr;
P4UiSettings* settingsForUi = nullptr;
P4UiSettings* initialRuntimeSettings = nullptr;
bool pendingUiSettings = false;  // UI owner retries if its scratch allocation fails.
bool coreReady = false;
uint32_t nextCoreRetry = 0;

enum WifiCommand : uint8_t { RebuildTransport = 1, Reconnect = 2, GotIp = 3 };
void postWifi(WifiCommand command) {
  if (!wifiCommands || xQueueSend(wifiCommands, &command, 0) != pdPASS)
    wifiCommandsDropped.fetch_add(1, std::memory_order_relaxed);
}
void publishNetwork() {
  P4NetworkStatus value;
  value.online = WiFi.status() == WL_CONNECTED; value.ap = isAPMode();
  value.ota = otaActive.load(std::memory_order_acquire);
  value.wifiOwnerReady = networkReady.load(std::memory_order_acquire);
  value.wifiState = WiFi.status();
  networkFlags.store(uint8_t(value.online) | (uint8_t(value.ap) << 1), std::memory_order_release);
  // RSSI is a synchronous C6 RPC. It is diagnostic-only: do not poll it in
  // the transport owner, let it delay Wi-Fi recovery, or confuse it with a
  // usable Internet path. An explicit low-rate diagnostic may fill it later.
  value.rssi = 0;
  getWiFiIpText(value.ip, sizeof(value.ip));
  const WifiFailInfo failure = wifiFailureValue();
  wifiFailureForUi.update(failure); wifiFailureForHttp.update(failure);
  networkForUi.update(value); networkForMqtt.update(value);
  networkForCamera.update(value); networkForHttp.update(value); networkForCloud.update(value);
}
void publishConfig(const P4UiSettings& settings, uint8_t slot) {
  if (!shared || slot >= MAX_ACTIVE_PRINTERS) return;
  shared[slot].configForMqtt.update(settings.printers[slot].config);
  shared[slot].configForCamera.update(settings.printers[slot].config);
  shared[slot].configForHttp.update(settings.printers[slot].config);
}

bool publish(const P4UiSettings& settings) {
  if (!shared || !settingsForWeb || !settingsForWifi) return false;
  bool delivered = settingsForWeb->update(settings);
  delivered = settingsForWifi->update(settings) && delivered;
  for (uint8_t i = 0; i < MAX_ACTIVE_PRINTERS; ++i)
    publishConfig(settings, i);
  return delivered;
}
}

void p4SettingsPublish(const P4UiSettings& settings, uint8_t changedPrinters) {
  if (!coreReady) return;
  // A busy mailbox cannot erase this final configuration. The Wi-Fi owner
  // retries the latest committed revision before every transport action;
  // the HTTP owner refreshes the complete value on every request pass.
  publish(settings);
  for (uint8_t i = 0; i < MAX_ACTIVE_PRINTERS; ++i) {
    if (changedPrinters & (1U << i)) p4MqttPost({P4MqttCommand::Configure, i});
  }
}

bool ensureCore() {
  p4TlsMemoryStart();
  if (coreReady) return true;
  if (!shared) {
    shared = static_cast<SharedSlot*>(heap_caps_calloc(MAX_ACTIVE_PRINTERS, sizeof(SharedSlot),
                                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!shared) return false;
    for (uint8_t i = 0; i < MAX_ACTIVE_PRINTERS; ++i) new (&shared[i]) SharedSlot;
  }
  bool coreAllocated = true;
  if (!settingsForWeb) settingsForWeb = SpscLatest<P4UiSettings>::createExternal();
  coreAllocated &= settingsForWeb != nullptr;
  if (!settingsForWifi) settingsForWifi = SpscLatest<P4UiSettings>::createExternal();
  coreAllocated &= settingsForWifi != nullptr;
  if (!committedSettings) committedSettings = SpscLatest<P4UiSettings>::createExternal();
  coreAllocated &= committedSettings != nullptr;
  if (!settingsForUi) settingsForUi = static_cast<P4UiSettings*>(heap_caps_calloc(1,sizeof(P4UiSettings),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
  coreAllocated &= settingsForUi != nullptr;
  if (!coreAllocated) {
    const uint32_t now = millis();
    if (int32_t(now - nextCoreRetry) >= 0) {
      Serial.println("P4 runtime: core allocation incomplete; retrying without restarting workers");
      nextCoreRetry = now + 1000;
    }
    return false;
  }
  coreReady = true;
  return true;
}

void p4RuntimeStart(const P4UiSettings& initialSettings) {
  if (!initialRuntimeSettings) {
    initialRuntimeSettings = static_cast<P4UiSettings*>(heap_caps_calloc(
      1, sizeof(P4UiSettings), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!initialRuntimeSettings) return;
    *initialRuntimeSettings = initialSettings;
  }
  if (!ensureCore()) return;
  // Keep the initial generation in persistent PSRAM until the UI accepts it.
  // Worker construction only starts after that complete publication exists.
  *settingsForUi = *initialRuntimeSettings;
  pendingUiSettings = true;
  initP4CameraService();
  p4WebInitialize(*initialRuntimeSettings);
  p4RuntimeSync();
  if (!pendingUiSettings) {
    heap_caps_free(initialRuntimeSettings);
    initialRuntimeSettings = nullptr;
  }
}

void p4RuntimeStartWorkers() {
  if (!coreReady || pendingUiSettings) return;
  p4WifiWorkerStart();
  p4CloudWorkerStart();
  p4MqttWorkerStart();
  startP4CameraService();
}

bool postUiByte(QueueHandle_t& queue, uint8_t value) {
  // The HTTP owner allocates and writes each queue; the UI owner only reads.
  // No allocation failure or full queue is reported as a completed action.
  if (!queue) queue = xQueueCreate(16, sizeof(uint8_t));
  return queue && xQueueSend(queue, &value, 0) == pdPASS;
}

static bool copyConfig(uint8_t slot, PrinterConfig& config, bool camera) {
  if (!shared || slot >= MAX_ACTIVE_PRINTERS) return false;
  return camera ? shared[slot].configForCamera.copyLatest(config)
                : shared[slot].configForMqtt.copyLatest(config);
}
bool p4WorkerConfigSnapshot(uint8_t slot, PrinterConfig& config) { return copyConfig(slot, config, false); }
bool p4CameraConfigSnapshot(uint8_t slot, PrinterConfig& config) { return copyConfig(slot, config, true); }
bool p4HttpSettingsSnapshot(P4UiSettings& settings) {
  return settingsForWeb && settingsForWeb->copyLatest(settings);
}
bool p4WifiSettingsSnapshot(P4UiSettings& settings) {
  return settingsForWifi && settingsForWifi->copyLatest(settings);
}
bool p4WifiFailureSnapshot(WifiFailInfo& info) {
  return wifiFailureForHttp.copyLatest(info);
}
bool p4UiWifiFailureSnapshot(WifiFailInfo& info) {
  return wifiFailureForUi.copyLatest(info);
}
bool p4HttpPrinterSnapshot(uint8_t slot, PrinterConfig& config, BambuState& state) {
  if (!shared || slot >= MAX_PRINTERS) return false;
  if (slot < MAX_ACTIVE_PRINTERS) {
    shared[slot].configForHttp.copyLatest(config);
    if (shared[slot].stateForHttp.copyLatest(state)) return true;
  }
  // The HTTP owner keeps the last complete values if a concurrent publication
  // wins this bounded read. A missed copy must never erase valid live data.
  return false;
}
bool p4HttpPrinterConfigured(uint8_t slot) {
  if (!shared || slot >= MAX_ACTIVE_PRINTERS) return false;
  PrinterConfig config{};
  return shared[slot].configForHttp.copyLatest(config) && p4PrinterConfigured(config);
}
bool p4PublishMqttSlot(uint8_t slot, const BambuState& state, const MqttDiag& diag) {
  if (!shared || slot >= MAX_ACTIVE_PRINTERS) return false;
  BambuState* ui = shared[slot].stateForUi.beginWrite();
  BambuState* camera = shared[slot].stateForCamera.beginWrite();
  BambuState* http = shared[slot].stateForHttp.beginWrite();
  MqttDiag* diagnostics = shared[slot].diagForHttp.beginWrite();
  if (!ui || !camera || !http || !diagnostics) {
    if (ui) shared[slot].stateForUi.cancelWrite();
    if (camera) shared[slot].stateForCamera.cancelWrite();
    if (http) shared[slot].stateForHttp.cancelWrite();
    if (diagnostics) shared[slot].diagForHttp.cancelWrite();
    return false;  // The producer retries its unchanged final value next loop.
  }
  *ui = state; *camera = state; *http = state; *diagnostics = diag;
  shared[slot].stateForUi.publish(); shared[slot].stateForCamera.publish();
  shared[slot].stateForHttp.publish(); shared[slot].diagForHttp.publish();
  return true;
}
bool p4MqttDiagSnapshot(uint8_t slot, MqttDiag& diag) {
  if (!shared || slot >= MAX_ACTIVE_PRINTERS) return false;
  return shared[slot].diagForHttp.copyLatest(diag);
}
bool p4CameraSnapshot(uint8_t slot, BambuState& state) {
  if (!shared || slot >= MAX_ACTIVE_PRINTERS) return false;
  return shared[slot].stateForCamera.copyLatest(state);
}
void p4CloudPublishToken(const char* token) {
  TokenSnapshot value;
  strlcpy(value.value, token ? token : "", sizeof(value.value));
  tokenForMqtt.update(value); tokenForCamera.update(value);
  memset(&value, 0, sizeof(value));
}
bool p4AuxTransportTryAcquire(P4AuxTransportOwner owner) { return auxTransport.tryAcquire(owner); }
void p4AuxTransportRelease(P4AuxTransportOwner owner) { auxTransport.release(owner); }
bool p4AuxTransportBusy() { return auxTransport.owner() != P4AuxTransportOwner::None; }
bool p4CloudTokenSnapshot(char* token, size_t capacity) {
  if (!token || capacity < CLOUD_TOKEN_MAX) return false;
  TokenSnapshot value{};
  if (!tokenForMqtt.copyLatest(value) || !value.value[0]) return false;
  strlcpy(token, value.value, capacity); return true;
}
bool p4CameraTokenSnapshot(char* token, size_t capacity) {
  if (!token || capacity < CLOUD_TOKEN_MAX) return false;
  TokenSnapshot value{};
  if (!tokenForCamera.copyLatest(value) || !value.value[0]) return false;
  strlcpy(token, value.value, capacity); return true;
}
bool p4NetworkSnapshot(P4NetworkStatus& status) {
  return networkForUi.copyLatest(status);
}
bool p4WifiOnline() { return networkFlags.load(std::memory_order_acquire) == 1; }
bool p4WifiApMode() { return (networkFlags.load(std::memory_order_acquire) & 2) != 0; }
bool p4MqttNetworkSnapshot(P4NetworkStatus& status) {
  return networkForMqtt.copyLatest(status);
}
bool p4CloudNetworkSnapshot(P4NetworkStatus& status) {
  return networkForCloud.copyLatest(status);
}
bool p4CameraNetworkSnapshot(P4NetworkStatus& status) {
  return networkForCamera.copyLatest(status);
}
bool p4HttpNetworkSnapshot(P4NetworkStatus& status) {
  return networkForHttp.copyLatest(status);
}
void p4SetOtaActive(bool active) { otaActive.store(active, std::memory_order_release); }
void requestWiFiTransportRebuild() { postWifi(RebuildTransport); }
void p4WifiReconnect() { postWifi(Reconnect); }
void p4WifiRefreshTime() { postWifi(GotIp); }
// Arduino dispatches Wi-Fi callbacks from its event task, not an ISR.
void p4WifiGotIp() { postWifi(GotIp); }

void p4WifiWorkerStart() {
  if (wifiStarted.exchange(true)) return;
  wifiCommands = xQueueCreate(4, sizeof(uint8_t));
  wifiSettings = static_cast<P4UiSettings*>(heap_caps_calloc(1, sizeof(P4UiSettings),
                                                             MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!wifiCommands || !wifiSettings) {
    if (wifiCommands) { vQueueDelete(wifiCommands); wifiCommands = nullptr; }
    if (wifiSettings) { heap_caps_free(wifiSettings); wifiSettings = nullptr; }
    wifiStarted = false;
    Serial.println("P4 Wi-Fi: worker allocation failed");
    return;
  }
  auto task = [](void*) {
    // Record ownership in the task itself, never in the main task which calls
    // this factory. Wi-Fi helpers use this handle to choose snapshots instead
    // of touching the Arduino Wi-Fi object from another task.
    wifiTask = xTaskGetCurrentTaskHandle();
    uint32_t lastHeartbeat = 0, nextNetworkBegin = 0;
    bool transportInitialized = false;
    P4UiSettings& settings = *wifiSettings;
    if (p4WifiSettingsSnapshot(settings)) p4WifiApplySettings(settings);
    // WiFiSTA::begin() owns the one-time ESP-Hosted/netif/lwIP setup. No
    // socket worker may race that initialization: lwIP's mailbox only becomes
    // valid after Network.begin() has finished, including when Wi-Fi is
    // offline or only AP credentials are present.
    for (;;) {
      if (!transportInitialized) {
        const uint32_t now = millis();
        if (int32_t(now - nextNetworkBegin) < 0) { vTaskDelay(pdMS_TO_TICKS(20)); continue; }
        Serial.println("[net-stage] netif_begin");
        if (!Network.begin()) {
          Serial.println("[net-stage] netif_failed retry=5000ms");
          networkReady.store(false, std::memory_order_release);
          publishNetwork(); nextNetworkBegin = millis() + 5000;
          continue;
        }
        transportInitialized = true;
        p4HostedMemoryStart();
        networkReady.store(true, std::memory_order_release);
        Serial.println("[net-stage] netif_ready");
        initWiFi(); publishNetwork();
        // This is the first socket service; it is safe only after netif/lwIP.
        p4WebServiceStart();
      }
      uint8_t command; bool rebuild = false, reconnect = false, online = false;
      while (xQueueReceive(wifiCommands, &command, 0) == pdPASS) {
        rebuild |= command == RebuildTransport; reconnect |= command == Reconnect; online |= command == GotIp;
      }
      static uint32_t appliedRevision = 0;
      static bool haveSettings = false;
      // A saved Wi-Fi generation takes effect before this iteration's socket
      // handling. Reconnect/GotIp commands must never use stale credentials.
      P4UiSettings candidate{};
      if (p4WifiSettingsSnapshot(candidate)) {
        const bool changed = haveSettings && candidate.revision != appliedRevision;
        if (changed) p4WifiUpdateSettings(candidate);
        settings = candidate;
        appliedRevision = candidate.revision;
        haveSettings = true;
      }
      if (rebuild) recoverWiFiTransport();
      if (reconnect && !isAPMode()) refreshWiFiConnection();
      handleWiFi();
      if (online && WiFi.status() == WL_CONNECTED) { p4WifiServiceOnline(); Serial.println("[net-stage] ip_ready"); }
      else if (online && !p4WifiHasCredentials()) startAP();
      publishNetwork();
      if (networkReady.load(std::memory_order_acquire)) p4WebServiceStart();
      const uint32_t now = millis();
      if (now - lastHeartbeat >= 10000) {
        lastHeartbeat = now;
      Serial.printf("[wifi-health] uptime=%lu status=%u heap=%u dropped=%lu rpc_timeouts=%lu\n",
                      now / 1000, unsigned(WiFi.status()), unsigned(ESP.getFreeHeap()),
                      (unsigned long)wifiCommandsDropped.exchange(0),
                      (unsigned long)wifiRpcTimeouts.load(std::memory_order_acquire));
        char ip[16]{}; getWiFiIpText(ip, sizeof(ip));
        Serial.printf("[net-health] ip=%s bits=%lx internal=%u largest=%u psram=%u\n", ip,
                      (unsigned long)WiFi.STA.getStatusBits(),
                      (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                      (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                      (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        p4HostedMemoryLog();
        p4TlsMemoryLog();
      }
      vTaskDelay(pdMS_TO_TICKS(20));
    }
  };
  constexpr size_t bytes = 32768;
  auto* stack = static_cast<StackType_t*>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  // Retry only before ownership has been published to the Wi-Fi task.
  if (!stack) {
    vQueueDelete(wifiCommands); wifiCommands = nullptr; wifiStarted = false;
    heap_caps_free(wifiSettings); wifiSettings = nullptr;
    Serial.println("P4 Wi-Fi: task allocation failed");
    return;
  }
  static StaticTask_t control;
  if (!xTaskCreateStatic(task, "p4_wifi", bytes / sizeof(StackType_t), nullptr, 2,
                         stack, &control)) {
    heap_caps_free(stack);
    wifiTask = nullptr;
    vQueueDelete(wifiCommands); wifiCommands = nullptr; wifiStarted = false;
    heap_caps_free(wifiSettings); wifiSettings = nullptr;
    Serial.println("P4 Wi-Fi: task allocation failed");
  }
}

void* p4WifiTaskHandle() { return wifiTask.load(std::memory_order_acquire); }

void p4RuntimeSync() {
  // Complete the core before starting any worker. Each worker subsequently
  // retries only its own construction, never resets another live owner.
  if (!ensureCore()) return;
  p4UiServiceStart();
  if (committedSettings) {
    if (!pendingUiSettings) pendingUiSettings = committedSettings->readNext(*settingsForUi);
    if (pendingUiSettings && p4UiApplySettings(*settingsForUi)) pendingUiSettings = false;
  }
  if (!pendingUiSettings) p4RuntimeStartWorkers();
  uint8_t pending = 0;
  if (brightnessCommands && xQueueReceive(brightnessCommands, &pending, 0) == pdPASS) setBacklight(pending);
  if (buzzerCommands && xQueueReceive(buzzerCommands, &pending, 0) == pdPASS)
    buzzerPlay((BuzzerEvent)pending);
  for (uint8_t i = 0; i < MAX_ACTIVE_PRINTERS; ++i) {
    BambuState snapshot{};
    if (!shared[i].stateForUi.readNext(snapshot)) continue;
    BambuState& current = p4UiPrinters()[i].state;
    snapshot.finishBuzzerPlayed = current.finishBuzzerPlayed;
    snapshot.finishEpoch = current.finishEpoch;
    snapshot.finishTimeLatched = current.finishTimeLatched;
    snapshot.doorAcknowledged = current.doorAcknowledged;
    snapshot.lightOffDueMs = 0; snapshot.ctrlCmdSentMs = 0;
    current = snapshot;
    if (snapshot.lastPrintDataMs) {
      static bool firstCore[MAX_ACTIVE_PRINTERS]{};
      if (!firstCore[i]) {
        firstCore[i] = true;
        Serial.printf("[data-stage] slot=%u ui_received state=%s progress=%u\n",
                      i, snapshot.gcodeState, snapshot.progress);
      }
    }
  }
}

bool p4CloudCommitSettings(const P4UiSettings& settings) {
  return committedSettings && committedSettings->update(settings);
}
bool p4UiPreviewBrightness(uint8_t value) { return postUiByte(brightnessCommands, value); }
bool p4UiBuzzer(uint8_t event) { return postUiByte(buzzerCommands, event); }
bool p4UiPreviewLed(const P4LedPreview&) { return false; }
bool p4UiTestLed(const P4LedEffect&) { return false; }
bool p4HttpUiSnapshot(P4UiStatus& status) {
  return uiForHttp.copyLatest(status);
}
bool p4CameraUiSnapshot(P4UiStatus& status) {
  return uiForCamera.copyLatest(status);
}
void p4PublishUiStatus(const P4UiStatus& status) { uiForHttp.update(status); uiForCamera.update(status); }
uint32_t p4LoopStackMinWords() { return p4UiLoopStackMinWords(); }
#endif
