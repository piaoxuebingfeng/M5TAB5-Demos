// Board macro matrix. Arduino IDE has no global -D injection, so every
// translation unit pulls it in first (see board_config.h for the rationale).
#include "board_config.h"

#define P4_SETTINGS_NO_ALIAS
#include "p4_cloud_worker.h"
#if defined(BOARD_IS_WS_P4_7B)
#include "spsc_latest.h"
#include "cloud_login.h"
#include "bambu_cloud.h"
#include "settings.h"
#include "p4_runtime.h"
#include "p4_storage_commit.h"
#include "p4_factory_reset.h"
#include "p4_json_allocator.h"
#include <Preferences.h>
#include <esp_heap_caps.h>
#include <freertos/idf_additions.h>
#include <algorithm>
#include <atomic>
#include <cstring>

namespace {
constexpr size_t authCapacity = 8001;
enum class Command : uint8_t {
  LoginPassword, LoginEmail, LoginCode, Printers, Diagnostics, SelfTest, Logout
};
struct Request {
  Command command = Command::Printers;
  CloudRegion region = REGION_CN;
  bool savePassword = false;
  char email[CLOUD_EMAIL_MAX + 1]{};
  char password[CLOUD_PASSWORD_MAX + 1]{};
  char code[32]{};
};
struct Status {
  uint32_t generation = 0;
  uint8_t login = CLOUD_LOGIN_IDLE;
  char message[192]{};
  bool failed = false, savedPassword = false, hasToken = false;
  char email[CLOUD_EMAIL_MAX + 1]{};
  bool pending = false;
  Command resultKind = Command::Printers;
  char result[12000]{};
};
struct Credentials { char token[CLOUD_TOKEN_MAX]{}; char email[CLOUD_EMAIL_MAX + 1]{}; };
struct CameraPublication { char auth[authCapacity]{}; bool enabled = false, automatic = false; };
struct StorageUsage { uint16_t used = 0, free = 0, total = 0; };
struct SlotNoteRequest { uint32_t sequence = 0; char label[16]{}; };
struct SlotNoteResult { uint32_t sequence = 0; bool found = false; char text[48]{}; };
enum class StorageKind : uint8_t { Settings, Token, CameraAuth, CameraDelete, CameraAuto, FactoryReset };
struct StorageRequest {
  uint32_t ticket = 0;
  StorageKind kind = StorageKind::Settings;
  P4UiSettings base{};
  P4UiSettings settings{};
  char payload[authCapacity]{};
  bool enabled = false;
  uint32_t restartDelayMs = 0;
};
struct StorageResult { uint32_t ticket = 0, sequence = 0; bool stored = false; };

// Requests which need ordering or a result use queues, never latest-value
// mailboxes. The cloud task alone executes account HTTPS and NVS operations.
QueueHandle_t networkRequests = nullptr;
QueueHandle_t slotNoteQueue = nullptr;
QueueHandle_t slotNoteReplies = nullptr;
SpscLatest<Status>* statuses = nullptr;
QueueHandle_t storageRequests = nullptr;
QueueHandle_t storageResults = nullptr;
SpscLatest<CameraPublication>* cameraPublication = nullptr;
SpscLatest<StorageUsage> storageUsage;
std::atomic<bool> started{false}, networkBusy{false};
Status* status = nullptr;
// Persistent secrets are large and only accessed by the cloud owner. Keep
// their storage in PSRAM, leaving internal DMA memory to Wi-Fi and crypto.
Credentials* credentials = nullptr;
char* cameraAuth = nullptr;
char* cameraRestoreAuth = nullptr;
char scratchPassword[CLOUD_PASSWORD_MAX + 1]{};
bool cameraEnabled = false, cameraAutomatic = false;
bool savedPasswordPresent = false;
bool loginNeedsTwoFactor = false;
bool loginCredentialsStored = true;
uint32_t settingsRevision = 0;
uint32_t nextUserIdAttempt = 0;
uint8_t userIdFailures = 0;
// Only task() touches these full-size values. Keeping them in PSRAM prevents
// the compiler from combining request and merge temporaries into a task stack
// much larger than its 32 KiB allocation.
P4UiSettings* settingsScratch = nullptr;
P4UiSettings* userIdScratch = nullptr;
StorageRequest* storageScratch = nullptr;
StorageRequest* requestBuildScratch = nullptr;
StorageRequest* readbackScratch = nullptr;
Request* networkScratch = nullptr;
Status* httpStatusScratch = nullptr;
Request* httpRequestScratch = nullptr;
P4UiSettings* publishedSettings = nullptr;
bool settingsPublicationPending = false;

bool publishCommittedSettings(const P4UiSettings& settings) {
  if (!publishedSettings) return false;
  if (!settingsPublicationPending) *publishedSettings = settings;
  settingsPublicationPending = !p4CloudCommitSettings(*publishedSettings);
  return !settingsPublicationPending;
}

void publish() {
  status->savedPassword = savedPasswordPresent;
  status->hasToken = credentials->token[0];
  strlcpy(status->email, credentials->email, sizeof(status->email));
  statuses->update(*status);
}
void publishCamera() {
  CameraPublication* value = cameraPublication->beginWrite();
  if (!value) return;
  strlcpy(value->auth, cameraAuth, sizeof(value->auth));
  value->enabled = cameraEnabled; value->automatic = cameraAutomatic;
  cameraPublication->publish();
}
void resetCloudWorkers() {
  // These queues and mailboxes are reachable from HTTP/UI callers. A partial
  // start must revoke their access before freeing storage, then be retryable
  // as one clean allocation. Nothing inside this guard runs on a live task.
  started.store(false, std::memory_order_release);
  networkBusy.store(false, std::memory_order_release);
  if (credentials) { *credentials = Credentials{}; credentials->~Credentials(); }
  p4CloudPublishToken("");
  auto freeQueue = [](QueueHandle_t& queue) {
    if (queue) vQueueDelete(queue);
    queue = nullptr;
  };
  freeQueue(networkRequests);
  freeQueue(slotNoteQueue);
  freeQueue(slotNoteReplies);
  freeQueue(storageRequests);
  freeQueue(storageResults);
  SpscLatest<Status>::destroyExternal(statuses); statuses = nullptr;
  SpscLatest<CameraPublication>::destroyExternal(cameraPublication); cameraPublication = nullptr;
  auto freeBuffer = [](auto*& buffer) {
    if (buffer) heap_caps_free(buffer);
    buffer = nullptr;
  };
  freeBuffer(status);
  freeBuffer(credentials);
  freeBuffer(cameraAuth);
  freeBuffer(cameraRestoreAuth);
  freeBuffer(settingsScratch);
  freeBuffer(storageScratch);
  freeBuffer(networkScratch);
  freeBuffer(userIdScratch);
  freeBuffer(requestBuildScratch);
  freeBuffer(readbackScratch);
  freeBuffer(httpStatusScratch);
  freeBuffer(httpRequestScratch);
  freeBuffer(publishedSettings);
  memset(scratchPassword, 0, sizeof(scratchPassword));
}
void publishStorageUsage() {
  uint16_t used, freeEntries, total;
  getNvsUsage(used, freeEntries, total);
  storageUsage.update({used, freeEntries, total});
}
bool applyCloudSettings(P4UiSettings& settings) {
  P4StorageCommitter commit;
  return commit.settings(settings, settingsRevision,
                         [](P4UiSettings& value) { return p4StoreSettings(value); },
                         [](const P4UiSettings& value) { return publishCommittedSettings(value); });
}
uint32_t nextSettingsRevision() {
  return P4StorageCommitter::nextRevision(settingsRevision);
}
void fetchUserIds(const char* token, P4UiSettings& settings) {
  if (!token || !token[0]) return;
  for (uint8_t i = 0; i < MAX_PRINTERS; ++i) {
    PrinterConfig& config = settings.printers[i].config;
    if (!isCloudMode(config.mode) || !config.serial[0] || config.cloudUserId[0]) continue;
    if (!cloudExtractUserId(token, config.cloudUserId, sizeof(config.cloudUserId)))
      cloudFetchUserId(token, config.cloudUserId, sizeof(config.cloudUserId), config.region);
  }
}
void refreshCloudUserIds() {
  const uint32_t now = millis();
  if (!credentials->token[0] || int32_t(now - nextUserIdAttempt) < 0) return;
  P4NetworkStatus network;
  if (!p4CloudNetworkSnapshot(network) || !network.wifiOwnerReady ||
      !network.online || network.ap || network.ota) {
    nextUserIdAttempt = now + 5000;
    return;
  }
  P4UiSettings& enriched = *settingsScratch;
  p4BootSettingsSnapshot(enriched);
  P4UiSettings& current = *userIdScratch;
  current = enriched;
  fetchUserIds(credentials->token, enriched);
  bool missing = false;
  for (uint8_t i = 0; i < MAX_PRINTERS; ++i)
    missing |= isCloudMode(enriched.printers[i].config.mode) && enriched.printers[i].config.serial[0] &&
               !enriched.printers[i].config.cloudUserId[0];
  if (memcmp(&current, &enriched, sizeof(P4UiSettings)) != 0) applyCloudSettings(enriched);
  userIdFailures = missing ? std::min<uint8_t>(userIdFailures + 1, 4) : 0;
  nextUserIdAttempt = now + (missing ? (30000UL << userIdFailures) : 30000);
}
bool storeSettingsForAccount(P4UiSettings& value) {
  const uint32_t revision = value.revision;
  value.revision = 0;  // NVS has no durable cross-key revision counter.
  const bool stored = p4StoreSettings(value);
  value.revision = revision;
  return stored;
}
bool storeTokenForAccount(const char* value) {
  if (value && value[0]) return saveCloudToken(value);
  return p4DeleteCloudToken();
}
void publishAccount() {
  *credentials = Credentials{};
  loadCloudToken(credentials->token, sizeof(credentials->token));
  loadCloudEmail(credentials->email, sizeof(credentials->email));
  savedPasswordPresent = loadCloudPassword(scratchPassword, sizeof(scratchPassword));
  memset(scratchPassword, 0, sizeof(scratchPassword));
  p4CloudPublishToken(credentials->token);
  publish();
}
bool storeToken(const char* token) {
  if (!token || !token[0]) return false;
  if (strcmp(credentials->token, token) != 0) {
    P4UiSettings& before = *settingsScratch;
    p4BootSettingsSnapshot(before);
    P4UiSettings& candidateSettings = *userIdScratch;
    candidateSettings = before;
    p4ClearCloudUserIds(candidateSettings, MAX_PRINTERS);
    fetchUserIds(token, candidateSettings);
    const bool stored = p4CommitCloudAccount(token, credentials->token, before,
      candidateSettings, settingsRevision, storeSettingsForAccount, storeTokenForAccount,
      [](const P4UiSettings& value) { return publishCommittedSettings(value); },
      [](P4UiSettings& actual) {
        p4BootSettingsSnapshot(actual);
        status->failed = true;
        strlcpy(status->message, "Cloud account rollback failed; storage was republished.",
                sizeof(status->message));
      });
    publishAccount();
    userIdFailures = 0;
    nextUserIdAttempt = millis();
    return stored;
  }
  strlcpy(credentials->token, token, sizeof(credentials->token));
  p4CloudPublishToken(credentials->token);
  userIdFailures = 0;
  nextUserIdAttempt = millis();
  return true;
}
bool storeLoginCredentials(const char* email, const char* password, bool savePassword) {
  return p4StoreCloudLogin(email, password, savePassword);
}
void finishLogin() {
  status->login = cloudLoginState();
  status->failed = cloudLoginLastFailed();
  strlcpy(status->message, cloudLoginMessage(), sizeof(status->message));
  if (status->login == CLOUD_LOGIN_OK && !loginCredentialsStored) {
    status->failed = true;
    strlcpy(status->message,
            "Signed in, but the login email or password could not be stored.",
            sizeof(status->message));
  }
  if (status->login != CLOUD_LOGIN_OK) return;
  loadCloudToken(credentials->token, sizeof(credentials->token));
  loadCloudEmail(credentials->email, sizeof(credentials->email));
  savedPasswordPresent = loadCloudPassword(scratchPassword, sizeof(scratchPassword));
  memset(scratchPassword, 0, sizeof(scratchPassword));
  loginNeedsTwoFactor = false;
}
void execute(const Request& request) {
  status->result[0] = 0; status->resultKind = request.command;
  switch (request.command) {
    case Command::LoginPassword:
      loginNeedsTwoFactor = false;
      cloudLoginWithPassword(request.email, request.password, request.region);
      if (cloudLoginState() == CLOUD_LOGIN_OK) {
        loginCredentialsStored = storeLoginCredentials(request.email, request.password, request.savePassword);
      } else if (cloudLoginState() == CLOUD_LOGIN_NEED_TFA ||
                 cloudLoginState() == CLOUD_LOGIN_NEED_EMAIL_CODE) {
        loginNeedsTwoFactor = true;
      }
      finishLogin();
      break;
    case Command::LoginEmail:
      loginNeedsTwoFactor = true;
      cloudLoginRequestEmailCode(request.email, request.region); finishLogin(); break;
    case Command::LoginCode:
      cloudLoginSubmitCode(request.code, request.region);
      if (cloudLoginState() == CLOUD_LOGIN_OK)
        // Second-factor sign-ins never persist a reusable password. The login
        // module clears that stale value after this verified email commit.
        loginCredentialsStored = storeLoginCredentials(request.email, nullptr, true);
      finishLogin();
      break;
    case Command::Printers: {
      JsonDocument out(p4JsonAllocator());
      if (!credentials->token[0]) out["message"] = "Sign in or paste a token first.";
      else {
        String response;
        if (!cloudFetchDeviceList(credentials->token, request.region, response))
          out["message"] = "Bambu refused the request - the token may have expired.";
        else {
          JsonDocument filter(p4JsonAllocator());
          filter["devices"][0]["dev_id"] = true;
          filter["devices"][0]["name"] = true;
          filter["devices"][0]["dev_product_name"] = true;
          filter["devices"][0]["dev_model_name"] = true;
          filter["devices"][0]["online"] = true;
          JsonDocument doc(p4JsonAllocator());
          if (deserializeJson(doc, response, DeserializationOption::Filter(filter)))
            out["message"] = "Could not read the account's printer list.";
          else {
            JsonArray printers = out["printers"].to<JsonArray>();
            for (JsonObject device : doc["devices"].as<JsonArray>()) {
              const char* serial = device["dev_id"];
              if (!serial || !serial[0]) continue;
              JsonObject printer = printers.add<JsonObject>();
              printer["serial"] = serial;
              printer["name"] = device["name"].is<const char*>() ? (const char*)device["name"] : serial;
              const char* model = device["dev_product_name"].is<const char*>()
                ? (const char*)device["dev_product_name"] : (const char*)device["dev_model_name"];
              printer["model"] = model ? model : "";
              printer["online"] = device["online"].as<bool>();
            }
          }
        }
      }
      serializeJson(out, status->result, sizeof(status->result));
      break;
    }
    case Command::Diagnostics: {
      String result; cloudDiagnostics(request.region, result);
      strlcpy(status->result, result.c_str(), sizeof(status->result)); break;
    }
    case Command::SelfTest: {
      String result; cloudLoginSelfTest(result);
      strlcpy(status->result, result.c_str(), sizeof(status->result)); break;
    }
    case Command::Logout:
      cloudLoginReset();
      if (!clearCloudToken()) {
        status->failed = true;
        strlcpy(status->message, "Could not erase cloud credentials from storage.",
                  sizeof(status->message));
      } else {
        *credentials = Credentials{}; *status = Status{};
        savedPasswordPresent = false; p4CloudPublishToken("");
      }
      break;
  }
}
__attribute__((noinline)) bool executeSettingsStorage(const StorageRequest& request) {
  P4UiSettings& current = *settingsScratch;
  p4BootSettingsSnapshot(current);
  P4UiSettings& merged = *userIdScratch;
  merged = current;
  p4MergeSettingsInPlace(merged, current, request.base, request.settings, MAX_PRINTERS);
  return applyCloudSettings(merged);
}

__attribute__((noinline)) bool executeTokenStorage(const StorageRequest& request) {
  return storeToken(request.payload);
}

__attribute__((noinline)) bool executeCameraAuthStorage(const StorageRequest& request) {
  Preferences store;
  const size_t length = strlen(request.payload);
  char* verified = readbackScratch->payload;
  const bool stored = store.begin("p4cam", false) &&
    store.putBytes("auth", request.payload, length) == length &&
    store.getBytes("auth", verified, authCapacity) == length &&
    memcmp(verified, request.payload, length) == 0;
  if (stored) {
    strlcpy(cameraAuth, request.payload, authCapacity);
    cameraEnabled = true; cameraAutomatic = true; publishCamera();
  }
  store.end();
  return stored;
}

__attribute__((noinline)) bool executeFactoryResetStorage() {
  // Preserve both namespaces before changing either. Settings and Bambu
  // account credentials share the bambu namespace, so account recovery is
  // required even when the camera namespace is the operation which failed.
  char* oldAuth = readbackScratch->payload;
  memset(oldAuth, 0, authCapacity);
  size_t oldLength = 0;
  bool oldAutomatic = cameraAutomatic;
  const Credentials oldCredentials = *credentials;
  const bool oldHasPassword = savedPasswordPresent;
  char* oldPassword = scratchPassword;
  if (oldHasPassword && !loadCloudPassword(oldPassword, CLOUD_PASSWORD_MAX + 1))
    return false;
  Preferences cameraRead;
  if (cameraRead.begin("p4cam", true)) {
    oldLength = cameraRead.isKey("auth") ? cameraRead.getBytesLength("auth") : 0;
    if (oldLength && oldLength < authCapacity &&
        cameraRead.getBytes("auth", oldAuth, authCapacity) == oldLength)
      oldAuth[oldLength] = 0;
    else oldLength = 0;
    oldAutomatic = cameraRead.getBool("auto", true);
    cameraRead.end();
  }
  Preferences cameraStore;
  const bool cameraCleared = cameraStore.begin("p4cam", false) && cameraStore.clear();
  cameraStore.end();
  if (!cameraCleared) {
    memset(oldPassword, 0, CLOUD_PASSWORD_MAX + 1);
    memset(oldAuth, 0, authCapacity);
    Serial.println("P4 storage: camera clear failed; settings reset was not attempted");
    return false;  // Nothing in the settings/account namespace was touched.
  }
  // Save before p4ResetSettings(), whose successful path replaces actual RAM.
  P4UiSettings& previous = *settingsScratch;
  p4BootSettingsSnapshot(previous);
  const P4FactoryResetResult result = p4FactoryResetStorage(
    [&] {
      P4UiSettings& defaults = *userIdScratch;
      defaults = previous;
      return p4ResetSettings(defaults) && clearCloudToken();
    },
    [&] {
      P4UiSettings& restoring = *userIdScratch;
      restoring = previous;
      restoring.revision = nextSettingsRevision();
      return p4StoreSettings(restoring);
    },
    [&] {
      // Do not skip token restoration because an email/password write failed.
      bool ok = p4StoreCloudLogin(oldCredentials.email, oldPassword, oldHasPassword);
      const bool tokenOk = storeTokenForAccount(oldCredentials.token);
      return ok && tokenOk;
    },
    [&] {
      bool ok = false;
      if (cameraStore.begin("p4cam", false)) {
        ok = true;
        if (oldLength) {
          const bool written = cameraStore.putBytes("auth", oldAuth, oldLength) == oldLength;
          char* verify = cameraRestoreAuth;
          const bool verified = written && cameraStore.getBytes("auth", verify, authCapacity) == oldLength &&
                                memcmp(verify, oldAuth, oldLength) == 0;
          ok = verified;
        }
        const bool automaticWritten = cameraStore.putBool("auto", oldAutomatic) == 1;
        const bool automaticVerified = automaticWritten &&
          cameraStore.getBool("auto", !oldAutomatic) == oldAutomatic;
        ok = ok && automaticVerified;
      }
      cameraStore.end();
      return ok;
    },
    [&] {
      // Read all three owners again after the last restoration attempt. This
      // is also the failure path when the rollback itself could not complete.
      P4UiSettings& actual = *userIdScratch;
      p4BootSettingsSnapshot(actual);
      actual.revision = nextSettingsRevision();
      publishCommittedSettings(actual);
      publishAccount();
      p4CloudCameraReload();
      publishCamera();
    });
  if (result.reset) {
    cloudLoginReset();
    memset(status, 0, sizeof(*status));
    publish();
  } else {
    status->failed = !result.restored;
    strlcpy(status->message, result.restored ? "Factory reset failed; previous settings restored."
                                            : "Factory reset and rollback failed; check storage.",
            sizeof(status->message));
    if (!result.restored) Serial.println("P4 storage: factory reset rollback FAILED");
    publish();
  }
  memset(oldPassword, 0, CLOUD_PASSWORD_MAX + 1);
  memset(oldAuth, 0, authCapacity);
  return result.reset;
}

__attribute__((noinline)) void executeStorage(const StorageRequest& request) {
  // Only a web save owns a restart ticket. An unrelated camera write must
  // never cancel or complete that request.
  static uint32_t storageSequence = 0;
  if (++storageSequence == 0) ++storageSequence;
  bool stored = false;
  switch (request.kind) {
    case StorageKind::Settings:
      stored = executeSettingsStorage(request);
      break;
    case StorageKind::Token:
      stored = executeTokenStorage(request);
      break;
    case StorageKind::CameraAuth:
      stored = executeCameraAuthStorage(request);
      break;
    case StorageKind::CameraDelete: {
      Preferences store;
      if (store.begin("p4cam", false) && store.clear()) {
        memset(cameraAuth, 0, authCapacity);
        cameraEnabled = cameraAutomatic = false; publishCamera();
        stored = true;
      }
      store.end(); break;
    }
    case StorageKind::CameraAuto: {
      Preferences store;
      if (store.begin("p4cam", false) && store.putBool("auto", request.enabled) == 1 &&
          store.getBool("auto", !request.enabled) == request.enabled) {
        cameraAutomatic = request.enabled; publishCamera();
        stored = true;
      }
      store.end(); break;
    }
    case StorageKind::FactoryReset:
      stored = executeFactoryResetStorage();
      break;
  }
  if (request.ticket) {
    if (!p4WebFinishRestart(request.ticket, stored)) return;
    const StorageResult result{request.ticket, storageSequence, stored};
    if (xQueueSend(storageResults, &result, pdMS_TO_TICKS(100)) != pdPASS)
      p4WebCancelRestart(request.ticket);
  }
  publishStorageUsage();
}
void task(void*) {
  publishAccount();
  p4CloudCameraReload();
  // Login and token persistence have the same owner. Save exactly once.
  cloudLoginSetTokenHook([](const char* token) {
    return storeToken(token);
  });
  publish(); publishCamera(); publishStorageUsage();
  uint32_t lastHealth = 0;
  for (;;) {
    if (settingsPublicationPending) publishCommittedSettings(*publishedSettings);
    // Account API work cannot postpone NVS saves or reset requests. Poll
    // storage again after an external request: another HTTP action may have
    // arrived while its TLS operation was in progress.
    for (unsigned i = 0; i < 8; ++i) {
      if (xQueueReceive(storageRequests, storageScratch, 0) != pdPASS) break;
      executeStorage(*storageScratch);
      memset(storageScratch, 0, sizeof(*storageScratch));
    }
    if (uxQueueMessagesWaiting(storageRequests) == 0 &&
        uxQueueMessagesWaiting(networkRequests) == 0 &&
        p4AuxTransportTryAcquire(P4AuxTransportOwner::Cloud)) {
      refreshCloudUserIds();
      p4AuxTransportRelease(P4AuxTransportOwner::Cloud);
    }
    SlotNoteRequest note{};
    if (xQueueReceive(slotNoteQueue, &note, 0) == pdPASS) {
      SlotNoteResult result; result.sequence = note.sequence;
      result.found = loadSlotVersionNote(note.label, result.text, sizeof(result.text));
      xQueueSend(slotNoteReplies, &result, 0);
    }
    Request request{};
    if (uxQueueMessagesWaiting(storageRequests) == 0 &&
        xQueueReceive(networkRequests, &request, 0) == pdPASS) {
      *networkScratch = request;
      status->pending = true; publish();
      // Logout clears local state and must not be postponed by a slow camera.
      if (networkScratch->command == Command::Logout ||
          p4AuxTransportTryAcquire(P4AuxTransportOwner::Cloud)) {
        execute(*networkScratch);
        if (networkScratch->command != Command::Logout)
          p4AuxTransportRelease(P4AuxTransportOwner::Cloud);
      } else {
        status->failed = true;
        strlcpy(status->message, "The camera is busy. Retry this cloud request shortly.",
                sizeof(status->message));
      }
      status->pending = false; status->generation++;
      networkBusy.store(false, std::memory_order_release);
      publish();
      memset(networkScratch, 0, sizeof(*networkScratch));
    }
    if (millis() - lastHealth >= 10000) {
      lastHealth = millis();
      Serial.printf("[cloud-health] uptime=%lu stack_min=%u\n",
                    (unsigned long)(lastHealth / 1000),
                    (unsigned)uxTaskGetStackHighWaterMark(nullptr));
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}
bool postNetwork(const Request& request) {
  if (!started.load(std::memory_order_acquire) || !networkRequests) return false;
  if (networkBusy.exchange(true, std::memory_order_acq_rel)) return false;
  if (xQueueSend(networkRequests, &request, 0) != pdPASS) {
    networkBusy.store(false, std::memory_order_release); return false;
  }
  return true;
}
bool postStorage(const StorageRequest& request) {
  if (!started.load(std::memory_order_acquire) || !storageRequests) return false;
  return xQueueSend(storageRequests, &request, 0) == pdPASS;
}

// These snapshots belong to their single consumers, never to a small task
// stack. Keeping the copy out of line also prevents large mailbox values from
// being folded into an otherwise small caller's stack frame.
__attribute__((noinline)) bool copyCloudStatus(Status& destination) {
  return statuses && statuses->copyLatest(destination);
}

__attribute__((noinline)) bool postBuiltNetwork(Command command, CloudRegion region,
                                               bool savePassword, const char* email,
                                               const char* password, const char* code) {
  if (!started.load(std::memory_order_acquire) || !httpRequestScratch) return false;
  Request* request = httpRequestScratch;
  memset(request, 0, sizeof(*request));
  request->command = command;
  request->region = region;
  request->savePassword = savePassword;
  if (email) strlcpy(request->email, email, sizeof(request->email));
  if (password) strlcpy(request->password, password, sizeof(request->password));
  if (code) strlcpy(request->code, code, sizeof(request->code));
  return postNetwork(*request);
}
}

void p4CloudCameraReload() {
  if (!cameraAuth) return;
  cameraAuth[0] = 0;
  cameraEnabled = cameraAutomatic = false;
  Preferences store;
  if (store.begin("p4cam", true)) {
    const size_t length = store.isKey("auth") ? store.getBytesLength("auth") : 0;
    if (length && length < authCapacity && store.getBytes("auth", cameraAuth, authCapacity) == length) {
      cameraAuth[length] = 0;
      cameraEnabled = !store.isKey("enabled") || store.getBool("enabled", true);
      cameraAutomatic = store.getBool("auto", true);
    }
    store.end();
  }
  Serial.printf("[camera-storage] credentials=%u enabled=%u automatic=%u\n",
                cameraAuth[0] != 0, cameraEnabled, cameraAutomatic);
}

__attribute__((noinline)) bool postBuiltStorage(StorageKind kind, const char* payload,
                                               const P4UiSettings* base, const P4UiSettings* edited,
                                               bool enabled,
                                               uint32_t ticket, uint32_t restartDelayMs) {
  if (!started.load(std::memory_order_acquire) || !requestBuildScratch) return false;
  StorageRequest* request = requestBuildScratch;
  memset(request, 0, sizeof(*request));
  request->kind = kind;
  request->ticket = ticket;
  request->restartDelayMs = restartDelayMs;
  request->enabled = enabled;
  if (base) request->base = *base;
  if (edited) request->settings = *edited;
  if (payload) strlcpy(request->payload, payload, sizeof(request->payload));
  return postStorage(*request);
}

bool p4CloudSaveSettings(const P4UiSettings& base, const P4UiSettings& edited,
                         uint32_t ticket, uint32_t restartDelayMs) {
  return postBuiltStorage(StorageKind::Settings, nullptr, &base, &edited, false,
                          ticket, restartDelayMs);
}
bool p4CloudSaveToken(const char* token) {
  if (!token || !token[0] || strlen(token) >= CLOUD_TOKEN_MAX) return false;
  const uint32_t ticket = p4WebArmRestart();
  if (!postBuiltStorage(StorageKind::Token, token, nullptr, nullptr, false, ticket, 0)) {
    p4WebCancelRestart(ticket);
    return false;
  }
  return p4WebAwaitStorage(ticket);
}
bool p4CloudSubmitCredentials(const char* json, size_t length, String& error) {
  if (!json || !length || length >= authCapacity) { error = "invalid_credentials_size"; return false; }
  if (!postBuiltStorage(StorageKind::CameraAuth, json, nullptr, nullptr, false, 0, 0)) {
    error = "storage_worker_unavailable";
    return false;
  }
  return true;
}
bool p4CloudDeleteCredentials() {
  return postBuiltStorage(StorageKind::CameraDelete, nullptr, nullptr, nullptr, false, 0, 0);
}
bool p4CloudSetAutomatic(bool enabled) {
  return postBuiltStorage(StorageKind::CameraAuto, nullptr, nullptr, nullptr, enabled, 0, 0);
}
bool p4CloudFactoryReset() {
  const uint32_t ticket = p4WebArmRestart();
  if (!postBuiltStorage(StorageKind::FactoryReset, nullptr, nullptr, nullptr, false, ticket, 1000)) {
    p4WebCancelRestart(ticket);
    return false;
  }
  return p4WebAwaitStorage(ticket, 1000);
}
bool p4CloudStartLogin(const char* email, const char* password, bool save, bool emailCode,
                       CloudRegion region, String& error) {
  const bool accepted = postBuiltNetwork(emailCode ? Command::LoginEmail : Command::LoginPassword,
                                         region, save, email, password, nullptr);
  if (!accepted) error = "Cloud request already running.";
  return accepted;
}
bool p4CloudSubmitCode(const char* code, CloudRegion region) {
  return postBuiltNetwork(Command::LoginCode, region, false, nullptr, nullptr, code);
}
bool p4CloudStartPrinters(CloudRegion region) {
  return postBuiltNetwork(Command::Printers, region, false, nullptr, nullptr, nullptr);
}
bool p4CloudStartDiagnostics(CloudRegion region) {
  return postBuiltNetwork(Command::Diagnostics, region, false, nullptr, nullptr, nullptr);
}
bool p4CloudStartSelfTest() {
  return postBuiltNetwork(Command::SelfTest, REGION_CN, false, nullptr, nullptr, nullptr);
}
void p4CloudLogout() {
  postBuiltNetwork(Command::Logout, REGION_CN, false, nullptr, nullptr, nullptr);
}
void p4CloudStatusJson(JsonDocument& out) {
  if (!started.load(std::memory_order_acquire) || !statuses || !httpStatusScratch) {
    out["state"] = "idle"; out["failed"] = true; out["pending"] = false;
    out["message"] = "Cloud service unavailable";
    out["saved_password"] = false; out["has_token"] = false;
    out["email"] = ""; out["generation"] = 0; out["result_kind"] = 0;
    return;
  }
  Status& current = *httpStatusScratch;
  memset(&current, 0, sizeof(current));
  copyCloudStatus(current);
  static const char* names[] = {"idle", "need_tfa", "need_email_code", "ok", "failed"};
  out["state"] = current.login < 5 ? names[current.login] : "idle";
  out["message"] = current.message; out["failed"] = current.failed;
  out["pending"] = current.pending; out["generation"] = current.generation;
  out["saved_password"] = current.savedPassword; out["has_token"] = current.hasToken;
  out["email"] = current.email; out["result_kind"] = uint8_t(current.resultKind);
  if (current.result[0]) out["result"] = serialized(current.result);
}
bool p4CloudCameraSnapshot(char* auth, size_t capacity, bool& enabled, bool& automatic) {
  if (!auth || !capacity || !cameraPublication) { enabled = automatic = false; return false; }
  CameraPublication value{};
  if (!cameraPublication->readNext(value) || strlen(value.auth) >= capacity) return false;
  strlcpy(auth, value.auth, capacity);
  enabled = value.enabled; automatic = value.automatic; return true;
}
void p4CloudStorageUsage(uint16_t& used, uint16_t& freeEntries, uint16_t& total) {
  StorageUsage value{};
  if (storageUsage.copyLatest(value)) {
    used = value.used; freeEntries = value.free; total = value.total;
  } else used = freeEntries = total = 0;
}
bool p4CloudSlotVersion(const char* label, char* buf, size_t capacity) {
  if (!label || !buf || !capacity || !started.load(std::memory_order_acquire)) return false;
  buf[0] = 0;
  static uint32_t nextSequence = 0;
  SlotNoteRequest request; request.sequence = ++nextSequence;
  strlcpy(request.label, label, sizeof(request.label));
  if (xQueueSend(slotNoteQueue, &request, 0) != pdPASS) return false;
  // This legacy metadata lookup may wait only in the HTTP task. The display,
  // MQTT and camera never wait for NVS or each other.
  for (unsigned i = 0; i < 500; ++i) {
    SlotNoteResult result{};
    if (xQueueReceive(slotNoteReplies, &result, 0) == pdPASS && result.sequence == request.sequence) {
      if (!result.found) return false;
      strlcpy(buf, result.text, capacity); return true;
    }
    vTaskDelay(pdMS_TO_TICKS(2));
  }
  return false;
}
bool p4CloudStorageResult(uint32_t& ticket, bool& stored) {
  if (!started.load(std::memory_order_acquire) || !storageResults) return false;
  StorageResult result;
  if (xQueueReceive(storageResults, &result, 0) != pdPASS) return false;
  ticket = result.ticket; stored = result.stored;
  return true;
}
void p4CloudWorkerStart() {
  // This factory is single-called before worker tasks begin. Publish only a
  // completely initialized instance; partial construction never leaves a
  // started flag, an accessible queue or permanently allocated scratch.
  if (started.load(std::memory_order_acquire)) return;
  resetCloudWorkers();
  networkRequests = xQueueCreate(2, sizeof(Request));
  slotNoteQueue = xQueueCreate(1, sizeof(SlotNoteRequest));
  slotNoteReplies = xQueueCreate(1, sizeof(SlotNoteResult));
  statuses = SpscLatest<Status>::createExternal();
  // One bounded FIFO preserves every save/reset/camera operation. Background
  // enrichment yields to these requests rather than having its own queue.
  storageRequests = xQueueCreateWithCaps(6, sizeof(StorageRequest), MALLOC_CAP_SPIRAM);
  storageResults = xQueueCreate(4, sizeof(StorageResult));
  cameraPublication = SpscLatest<CameraPublication>::createExternal();
  status = static_cast<Status*>(heap_caps_calloc(1, sizeof(Status), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (status) new (status) Status;
  credentials = static_cast<Credentials*>(heap_caps_calloc(1, sizeof(Credentials), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (credentials) new (credentials) Credentials;
  cameraAuth = static_cast<char*>(heap_caps_calloc(authCapacity, 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  cameraRestoreAuth = static_cast<char*>(heap_caps_calloc(authCapacity, 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  settingsScratch = static_cast<P4UiSettings*>(heap_caps_calloc(1, sizeof(P4UiSettings), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  storageScratch = static_cast<StorageRequest*>(heap_caps_calloc(1, sizeof(StorageRequest), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  networkScratch = static_cast<Request*>(heap_caps_calloc(1, sizeof(Request), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  userIdScratch = static_cast<P4UiSettings*>(heap_caps_calloc(1, sizeof(P4UiSettings), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  requestBuildScratch = static_cast<StorageRequest*>(heap_caps_calloc(1, sizeof(StorageRequest), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  readbackScratch = static_cast<StorageRequest*>(heap_caps_calloc(1, sizeof(StorageRequest), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  publishedSettings = static_cast<P4UiSettings*>(heap_caps_calloc(1, sizeof(P4UiSettings), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  httpStatusScratch = static_cast<Status*>(heap_caps_calloc(1, sizeof(Status), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  httpRequestScratch = static_cast<Request*>(heap_caps_calloc(1, sizeof(Request), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!networkRequests || !slotNoteQueue || !slotNoteReplies ||
      !statuses || !status || !credentials || !cameraAuth || !cameraRestoreAuth ||
      !storageRequests || !storageResults || !cameraPublication ||
      !settingsScratch || !storageScratch || !networkScratch || !userIdScratch ||
      !requestBuildScratch || !readbackScratch || !publishedSettings ||
      !httpStatusScratch || !httpRequestScratch) {
    Serial.println("P4 cloud: snapshot allocation failed");
    resetCloudWorkers();
    return;
  }
  if (!p4HttpSettingsSnapshot(*settingsScratch)) {
    Serial.println("P4 cloud: initial settings unavailable");
    resetCloudWorkers();
    return;
  }
  settingsRevision = settingsScratch->revision;
  // NVS disables Flash cache during a commit. Its execution stack must stay
  // internal, even though the much larger request/settings buffers use PSRAM.
  constexpr size_t bytes = 16 * 1024;
  auto* stack = static_cast<StackType_t*>(heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  auto* control = static_cast<StaticTask_t*>(heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  if (!stack || !control || !xTaskCreateStatic(task, "p4_cloud", bytes / sizeof(StackType_t), nullptr, 1, stack, control)) {
    if (stack) heap_caps_free(stack);
    if (control) heap_caps_free(control);
    resetCloudWorkers();
    return;
  }
  started.store(true, std::memory_order_release);
}

bool p4CloudTransportBusy() { return p4AuxTransportBusy(); }
bool p4CloudTransportTryAcquire() { return p4AuxTransportTryAcquire(P4AuxTransportOwner::Cloud); }
void p4CloudTransportRelease() { p4AuxTransportRelease(P4AuxTransportOwner::Cloud); }
#endif
