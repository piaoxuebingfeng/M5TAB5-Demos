#pragma once
#if defined(BOARD_IS_WS_P4_7B)
#include <Arduino.h>
#include "bambu_state.h"
#include "bambu_mqtt.h"
#include <WiFiClientSecure.h>
class MqttRxClient;
class PubSubClient;
// ── Per-connection context ──────────────────────────────────────────────────
struct MqttConn {
  uint8_t slotIndex;
  PrinterConfig config{};
  WiFiClientSecure* tls = nullptr;
  MqttRxClient* io = nullptr;
  PubSubClient* mqtt = nullptr;
  MqttDiag diag{};
  unsigned long lastReconnectAttempt = 0;
  unsigned long lastPushallRequest = 0;
  uint32_t pushallSeqId = 0;
  unsigned long connectTime = 0;
  bool initialPushallSent = false;
  bool gotDataSinceConnect = false;
  bool active = false;
  uint16_t consecutiveFails = 0;
  bool printerUnreachable = false;
  unsigned long disconnectSince = 0;
  bool wasConnected = false;
  unsigned long stalePushallSentMs = 0;
  unsigned long lastRecoveryResolvedMs = 0;
  bool hotFinishArmed = false;
  bool hotFinishHintConsumed = false;
  uint32_t serviceCalls = 0;
#if HAS_HMS_UI
  bool hmsBaselineOpen = false;
  bool hmsBaselineSeal = false;
  uint32_t hmsBaselineOpenMs = 0;   // window age cap; see the close rule
#endif
};
void initConnSlot(uint8_t slot);
void handleConn(MqttConn& conn);
void releaseClients(MqttConn& conn);
bool requestPushall(MqttConn& conn, uint8_t reason);
bool sendLightCtrl(MqttConn& conn, bool on);
void logP4MqttHealth(const MqttConn& conn);
MqttConn& p4MqttConnection(uint8_t slot);
BambuState& p4MqttState(uint8_t slot);
#endif
