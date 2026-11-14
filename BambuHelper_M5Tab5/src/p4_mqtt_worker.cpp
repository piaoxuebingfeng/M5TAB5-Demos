// Board macro matrix. Arduino IDE has no global -D injection, so every
// translation unit pulls it in first (see board_config.h for the rationale).
#include "board_config.h"

#include <Arduino.h>
#include <PubSubClient.h>
#include "p4_runtime.h"
#if defined(BOARD_IS_WS_P4_7B)
#include "p4_mqtt_protocol.h"
#include "p4_mqtt_lifecycle.h"
#include "p4_mqtt_commands.h"
#include "p4_snapshot_diff.h"
#include <esp_heap_caps.h>
#include <atomic>

namespace {
MqttConn connections[MAX_ACTIVE_PRINTERS]{};
BambuState states[MAX_ACTIVE_PRINTERS]{};
P4MqttCommands<MAX_ACTIVE_PRINTERS> commands;
P4MqttLifecycle<MAX_ACTIVE_PRINTERS> lifecycle;
bool wasStopped[MAX_ACTIVE_PRINTERS]{};
StaticTask_t taskStorage;
std::atomic<bool> started{false};
StaticTask_t* taskControl = nullptr;
std::atomic<int8_t> lightPending[MAX_ACTIVE_PRINTERS]{};
P4SnapshotDiff<BambuState, MqttDiag> published[MAX_ACTIVE_PRINTERS];
uint32_t firstPublished[MAX_ACTIVE_PRINTERS]{};

void publish(uint8_t slot) {
  const MqttDiag& diag = connections[slot].diag;
  // MQTT sockets can report thousands of unchanged idle iterations. Only
  // complete values which actually changed need copying into three mailboxes.
  if (!published[slot].changed(states[slot], diag)) return;
  if (!p4PublishMqttSlot(slot, states[slot], diag)) published[slot].invalidate();
  if (states[slot].lastPrintDataMs && !firstPublished[slot]) {
    firstPublished[slot] = states[slot].lastPrintDataMs;
    Serial.printf("[data-stage] slot=%u mqtt_published messages=%lu progress=%u\n",
                  slot, (unsigned long)diag.messagesRx, states[slot].progress);
  }
}
void stop(uint8_t slot) {
  MqttConn& conn = connections[slot];
  if (conn.mqtt && conn.mqtt->connected()) conn.mqtt->disconnect();
  conn.active = false; states[slot].connected = false;
  conn.disconnectSince = 0; conn.wasConnected = false;
}
void execute(uint8_t slot, P4MqttCommand::Kind kind) {
  if (slot >= MAX_ACTIVE_PRINTERS) return;
  MqttConn& conn = connections[slot];
  switch (kind) {
    case P4MqttCommand::Stop:
      stop(slot); releaseClients(conn); break;
    case P4MqttCommand::Configure:
      stop(slot); releaseClients(conn); initConnSlot(slot); break;
    case P4MqttCommand::Reconnect:
      stop(slot); conn.consecutiveFails = 0; conn.lastReconnectAttempt = 0;
      conn.active = p4PrinterConfigured(conn.config); break;
    case P4MqttCommand::Refresh:
      if (conn.active && conn.mqtt && conn.mqtt->connected()) requestPushall(conn, PUSHALL_MANUAL);
      else { conn.consecutiveFails = 0; conn.lastReconnectAttempt = 0; conn.disconnectSince = 0; }
      break;
    case P4MqttCommand::Resume: break;
  }
}
void drainCommands(bool online) {
  for (uint8_t slot = 0; slot < MAX_ACTIVE_PRINTERS; ++slot) {
    const bool stopped = lifecycle.stopped(slot) || !online;
    if (stopped) {
      commands.discardTransient(slot);
      connections[slot].active = false;
      if (!wasStopped[slot]) {
        stop(slot); releaseClients(connections[slot]); publish(slot);
        wasStopped[slot] = true;
      }
      // A configuration saved while offline/paused must still be applied on
      // resume. Recovery/refresh requests from the old transport are stale.
      continue;
    }
    if (wasStopped[slot]) {
      // Resume is a new generation, not permission to replay stale recovery
      // or refresh work. Configure belongs to persisted settings: retain it
      // so a save while offline/OTA is applied when the link comes back.
      commands.discardTransient(slot);
      stop(slot); releaseClients(connections[slot]); initConnSlot(slot);
      wasStopped[slot] = false; publish(slot);
    }
    const bool configure = commands.takeConfigure(slot);
    const bool reconnect = commands.takeReconnect(slot);
    if (lifecycle.stopped(slot)) {
      // A stop can arrive after resume was observed but before these flags
      // were drained. Never reopen or pushall into that stop generation.
      stop(slot); releaseClients(connections[slot]); publish(slot);
      wasStopped[slot] = true;
      continue;
    }
    if (configure) execute(slot, P4MqttCommand::Configure);
    if (reconnect) execute(slot, P4MqttCommand::Reconnect);
    if (commands.takeRefresh(slot)) execute(slot, P4MqttCommand::Refresh);
    int8_t light = lightPending[slot].load(std::memory_order_acquire);
    if (light && connections[slot].active && connections[slot].mqtt &&
        connections[slot].mqtt->connected() && sendLightCtrl(connections[slot], light > 0)) {
      // A newer opposite command can arrive while publish() is in progress.
      lightPending[slot].compare_exchange_strong(light, 0, std::memory_order_acq_rel);
    }
  }
}
void task(void*) {
  for (uint8_t i = 0; i < MAX_ACTIVE_PRINTERS; ++i) { initConnSlot(i); publish(i); }
  uint32_t lastHealth = 0;
  P4NetworkStatus network{};
  bool haveNetwork = false;
  for (;;) {
    // A bounded mailbox miss is not a Wi-Fi disconnect. Retain the last
    // complete state until the owner publishes the next real transition.
    haveNetwork = p4MqttNetworkSnapshot(network) || haveNetwork;
    const bool online = haveNetwork && network.wifiOwnerReady &&
                        network.online && !network.ap && !network.ota;
    drainCommands(online);
    for (uint8_t i = 0; i < MAX_ACTIVE_PRINTERS; ++i) {
      if (!online || lifecycle.stopped(i)) {
      // Keep the configured slot armed. A temporary link loss must not turn
      // into a permanently disabled connection after Wi-Fi recovers.
        if (connections[i].mqtt && connections[i].mqtt->connected()) connections[i].mqtt->disconnect();
        states[i].connected = false; connections[i].wasConnected = false;
        connections[i].disconnectSince = 0;
      } else if (connections[i].active) handleConn(connections[i]);
      publish(i);
    }
    const uint32_t now = millis();
    if (now - lastHealth >= 10000) {
      lastHealth = now;
      Serial.printf("[mqtt-health] uptime=%lu online=%d heap=%u stack_min=%u\n",
        now / 1000, online, (unsigned)ESP.getFreeHeap(),
        (unsigned)uxTaskGetStackHighWaterMark(nullptr));
      for (uint8_t i = 0; i < MAX_ACTIVE_PRINTERS; ++i)
        if (p4PrinterConfigured(connections[i].config)) logP4MqttHealth(connections[i]);
    }
    vTaskDelay(pdMS_TO_TICKS(3));
  }
}
}

MqttConn& p4MqttConnection(uint8_t slot) { return connections[slot]; }
BambuState& p4MqttState(uint8_t slot) { return states[slot]; }
bool p4MqttPost(const P4MqttCommand& command) {
  if (!started.load(std::memory_order_acquire) || command.slot >= MAX_ACTIVE_PRINTERS) return false;
  if (command.kind == P4MqttCommand::Resume) lifecycle.resume();
  else if (command.kind == P4MqttCommand::Stop) lifecycle.stop(command.slot);
  else if (command.kind == P4MqttCommand::Configure) commands.configure(command.slot);
  else if (command.kind == P4MqttCommand::Reconnect) commands.reconnect(command.slot);
  else if (command.kind == P4MqttCommand::LightOn || command.kind == P4MqttCommand::LightOff)
    lightPending[command.slot].store(command.kind == P4MqttCommand::LightOn ? 1 : -1,
                                     std::memory_order_release);
  else commands.refresh(command.slot);
  return true;
}
void p4MqttWorkerStart() {
  // The single startup owner publishes complete task ownership before any
  // command can reach the worker. A failed attempt owns no resources.
  if (started.load(std::memory_order_acquire) || taskControl) return;
  // The P4 FreeRTOS port requires its TCB in internal RAM. Only the task's
  // large data buffers and execution stack belong in PSRAM.
  StaticTask_t* control = static_cast<StaticTask_t*>(
    heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  if (!control) return;
  constexpr size_t stackBytes = 32768;
  auto* stack = static_cast<StackType_t*>(heap_caps_malloc(stackBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!stack || !xTaskCreateStatic(task, "bambu_mqtt", stackBytes / sizeof(StackType_t),
      nullptr, 3, stack, control)) {
    if (stack) heap_caps_free(stack);
    heap_caps_free(control);
    Serial.println("P4 MQTT: task allocation failed; retry pending");
    return;
  }
  taskControl = control;
  started.store(true, std::memory_order_release);
}
#endif
