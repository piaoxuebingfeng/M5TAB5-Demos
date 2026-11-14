#pragma once
#if defined(BOARD_IS_WS_P4_7B)
#include <Arduino.h>
#include <ArduinoJson.h>
#include "bambu_state.h"
#include "p4_ui_state.h"

void p4CloudWorkerStart();
void p4CloudCameraReload();
// Optional cloud and camera transfers are serialized without blocking.
bool p4CloudTransportBusy();
bool p4CloudTransportTryAcquire();
void p4CloudTransportRelease();
bool p4CloudSaveSettings(const P4UiSettings& base, const P4UiSettings& edited,
                         uint32_t ticket, uint32_t restartDelayMs = 0);
bool p4CloudStorageResult(uint32_t& ticket, bool& stored);
bool p4CloudFactoryReset();
// HTTP posts complete immutable requests. TLS, cloud login and NVS belong to
// the persistent cloud/storage owner.
bool p4CloudDeleteCredentials();
bool p4CloudSetAutomatic(bool enabled);
bool p4CloudSubmitCredentials(const char* json, size_t length, String& error);
bool p4CloudSaveToken(const char* token);
bool p4CloudStartLogin(const char* email, const char* password, bool save, bool emailCode,
                       CloudRegion region, String& error);
bool p4CloudSubmitCode(const char* code, CloudRegion region);
bool p4CloudStartPrinters(CloudRegion region);
bool p4CloudStartDiagnostics(CloudRegion region);
bool p4CloudStartSelfTest();
void p4CloudLogout();
// JSON includes the small cloud status object plus either printers or the
// diagnostic/self-test result. The producer never touches WebServer or UI data.
void p4CloudStatusJson(JsonDocument& out);
bool p4CloudCameraSnapshot(char* auth, size_t capacity, bool& enabled, bool& automatic);
void p4CloudStorageUsage(uint16_t& used, uint16_t& freeEntries, uint16_t& total);
bool p4CloudSlotVersion(const char* label, char* buf, size_t capacity);
#endif
