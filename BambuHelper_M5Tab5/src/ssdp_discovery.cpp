// Board macro matrix. Arduino IDE has no global -D injection, so every
// translation unit pulls it in first (see board_config.h for the rationale).
#include "board_config.h"

#include "ssdp_discovery.h"

#include <WiFi.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>
#if defined(BOARD_IS_WS_P4_7B)
#include "p4_runtime.h"
#include <atomic>
#endif

// ---------------------------------------------------------------------------
//  Discovered-device table
// ---------------------------------------------------------------------------
// P4 uses a bounded worker and publishes a complete result snapshot for HTTP.
// Other boards retain their original main-loop owner.
// No socket is touched by the renderer. This deliberately avoids AsyncUDP + a critical-section spinlock, which
// could deadlock with interrupts disabled during a concurrent save/TLS op.
struct SsdpDevice {
  char serial[20];
  char ip[16];
  char name[24];
  char model[12];
  uint32_t lastSeen;
};

static const uint8_t  SSDP_MAX_DEVICES = 8;
// Listen window per scan. Printers broadcast ~every 5s, alternating dest port
// 1990/2021, so a single port sees a device only ~every 10s. 12s guarantees at
// least one broadcast per device on each port - shorter windows miss the
// less-frequent broadcasters (observed: H2C in cloud mode).
static const uint32_t SSDP_SCAN_MS = 12000;

static SsdpDevice s_devices[SSDP_MAX_DEVICES];

static WiFiUDP s_udp2021;
static WiFiUDP s_udp1990;
#if defined(BOARD_IS_WS_P4_7B)
static std::atomic<bool> s_scanActive{false};
static std::atomic<bool> s_workerRunning{false};
static std::atomic<bool> s_cancel{false};
static std::atomic<int> s_opened{0};
struct SsdpSnapshot { SsdpDevice devices[SSDP_MAX_DEVICES]{}; };
static SpscLatest<SsdpSnapshot> published;
#else
static bool s_scanActive = false;
#endif
static bool          s_listening   = false;  // sockets currently open
static unsigned long s_scanStartMs = 0;

static const IPAddress SSDP_GROUP(239, 255, 255, 250);

// ---------------------------------------------------------------------------
//  Header parsing
// ---------------------------------------------------------------------------

// If `line` begins (case-insensitively) with `key`, copy the trimmed value into
// `out` and return true.
static bool headerValue(const char* line, const char* key, char* out, size_t outLen) {
  size_t klen = strlen(key);
  if (strncasecmp(line, key, klen) != 0) return false;
  const char* v = line + klen;
  while (*v == ' ' || *v == '\t') v++;
  strlcpy(out, v, outLen);
  size_t n = strlen(out);
  while (n > 0 && (out[n-1] == ' ' || out[n-1] == '\t' ||
                   out[n-1] == '\r' || out[n-1] == '\n')) {
    out[--n] = '\0';
  }
  return true;
}

// Reduce a Location value to a bare IPv4: strip scheme, trailing :port / path.
static void normalizeIp(const char* in, char* out, size_t outLen) {
  const char* p = in;
  if (strncasecmp(p, "http://", 7) == 0)       p += 7;
  else if (strncasecmp(p, "https://", 8) == 0) p += 8;
  size_t j = 0;
  while (*p && *p != ':' && *p != '/' && *p != ' ' && j < outLen - 1) {
    out[j++] = *p++;
  }
  out[j] = '\0';
}

static void upsertDevice(const char* serial, const char* ip,
                         const char* name, const char* model) {
  if (!serial || serial[0] == '\0') return;

  char up[20];
  strlcpy(up, serial, sizeof(up));
  for (char* c = up; *c; c++) *c = toupper((unsigned char)*c);

  int slot = -1, freeSlot = -1, oldestSlot = 0;
  uint32_t oldest = UINT32_MAX;
  for (int i = 0; i < SSDP_MAX_DEVICES; i++) {
    if (s_devices[i].serial[0] == '\0') {
      if (freeSlot < 0) freeSlot = i;
      continue;
    }
    if (strcmp(s_devices[i].serial, up) == 0) { slot = i; break; }
    if (s_devices[i].lastSeen < oldest) { oldest = s_devices[i].lastSeen; oldestSlot = i; }
  }
  if (slot < 0) slot = (freeSlot >= 0) ? freeSlot : oldestSlot;

  SsdpDevice& d = s_devices[slot];
  strlcpy(d.serial, up, sizeof(d.serial));
  if (ip)    strlcpy(d.ip, ip, sizeof(d.ip));
  if (name)  strlcpy(d.name, name, sizeof(d.name));
  if (model) strlcpy(d.model, model, sizeof(d.model));
  d.lastSeen = millis();
}

// Parse one SSDP packet payload (already null-terminated in `buf`).
static void parsePacket(char* buf) {
  char serial[20] = {0};
  char loc[64]    = {0};
  char name[24]   = {0};
  char model[12]  = {0};

  char* save = nullptr;
  for (char* line = strtok_r(buf, "\r\n", &save); line;
       line = strtok_r(nullptr, "\r\n", &save)) {
    headerValue(line, "USN:", serial, sizeof(serial)) ||
    headerValue(line, "Location:", loc, sizeof(loc)) ||
    headerValue(line, "DevName.bambu.com:", name, sizeof(name)) ||
    headerValue(line, "DevModel.bambu.com:", model, sizeof(model));
  }

  if (serial[0] == '\0') return;  // not a Bambu device announcement

  char ip[16] = {0};
  if (loc[0]) normalizeIp(loc, ip, sizeof(ip));
  upsertDevice(serial, ip, name, model);
}

// Drain all currently-buffered datagrams on one socket.
static void drainSocket(WiFiUDP& udp) {
  char buf[600];
  int sz;
  while ((sz = udp.parsePacket()) > 0) {
    int n = udp.read((uint8_t*)buf, sizeof(buf) - 1);
    if (n <= 0) break;
    buf[n] = '\0';
    parsePacket(buf);
  }
}

// ---------------------------------------------------------------------------
//  Public API
// ---------------------------------------------------------------------------
static void closeSockets() {
  if (!s_listening) return;
  s_udp2021.stop();
  s_udp1990.stop();
  s_listening = false;
}

int ssdpStartScan() {
#if defined(BOARD_IS_WS_P4_7B)
  // All P4 SSDP socket operations, including stop, have this one stack owner.
  // Starting another scan must wait for the previous bounded run to retire.
  if (s_workerRunning) return 0;
  P4NetworkStatus network{};
  if (!p4HttpNetworkSnapshot(network) || !network.online || network.ap) return 0;
  s_cancel = false; s_scanActive = false; s_listening = false;
  s_opened = 0;
  if (s_workerRunning.exchange(true)) return 0;
  auto worker = [](void*) {
    memset(s_devices, 0, sizeof(s_devices));
    published.update(SsdpSnapshot{});
    s_opened = (s_udp2021.beginMulticast(SSDP_GROUP, 2021) ? 1 : 0) +
               (s_udp1990.beginMulticast(SSDP_GROUP, 1990) ? 1 : 0);
    s_listening = s_opened > 0;
    s_scanActive = s_listening;
    s_scanStartMs = millis();
    while (!s_cancel && s_scanActive && millis() - s_scanStartMs <= SSDP_SCAN_MS) {
      drainSocket(s_udp2021);
      drainSocket(s_udp1990);
      SsdpSnapshot snapshot;
      memcpy(snapshot.devices, s_devices, sizeof(s_devices));
      published.update(snapshot);
      vTaskDelay(pdMS_TO_TICKS(10));
    }
    closeSockets();
    s_scanActive = false;
    s_cancel = false;
    s_workerRunning = false;
    vTaskDelete(nullptr);
  };
  if (xTaskCreate(worker, "p4_ssdp", 4096, nullptr, 1, nullptr) != pdPASS) {
    s_workerRunning = false;
    return 0;
  }
  for (unsigned i = 0; i < 100 && s_opened == 0; ++i) vTaskDelay(pdMS_TO_TICKS(2));
  return s_opened;
#else
  closeSockets();
  memset(s_devices, 0, sizeof(s_devices));
  s_scanActive = false;

  if (WiFi.status() != WL_CONNECTED) return 0;

  int opened = 0;
  if (s_udp2021.beginMulticast(SSDP_GROUP, 2021)) opened++;
  if (s_udp1990.beginMulticast(SSDP_GROUP, 1990)) opened++;
  if (opened == 0) { closeSockets(); return 0; }

  s_listening   = true;
  s_scanActive  = true;
  s_scanStartMs = millis();
  return opened;
#endif
}

void ssdpStopScan() {
#if defined(BOARD_IS_WS_P4_7B)
  // The SSDP worker closes its own sockets on the next tick.
  if (s_workerRunning) {
    // Block only the HTTP owner until the socket owner has retired. This
    // prevents ssdpStartScan() returning while another scan is still active.
    s_cancel = true;
    for (unsigned i = 0; i < 500 && s_workerRunning; ++i) vTaskDelay(pdMS_TO_TICKS(2));
    if (s_workerRunning) return;
    s_cancel = false;
  }
  s_scanActive = false;
  return;
#endif
  closeSockets();
  s_scanActive = false;
}

void ssdpTick() {
#if defined(BOARD_IS_WS_P4_7B)
  return; // The bounded SSDP worker owns its sockets and timeout.
#endif
  if (!s_scanActive) return;
  drainSocket(s_udp2021);
  drainSocket(s_udp1990);
  if (millis() - s_scanStartMs > SSDP_SCAN_MS) {
    ssdpStopScan();
  }
}

bool ssdpScanActive() { return s_scanActive; }

void ssdpScanResultJson(String& out) {
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
#if defined(BOARD_IS_WS_P4_7B)
  SsdpSnapshot snapshot{};
  if (!published.copyLatest(snapshot)) { serializeJson(doc, out); return; }
  const SsdpDevice* devices = snapshot.devices;
#else
  const SsdpDevice* devices = s_devices;
#endif
  for (int i = 0; i < SSDP_MAX_DEVICES; i++) {
    if (devices[i].serial[0] == '\0') continue;
    JsonObject o = arr.add<JsonObject>();
    o["serial"] = devices[i].serial;
    o["ip"]     = devices[i].ip;
    o["name"]   = devices[i].name;
    o["model"]  = devices[i].model;
  }
  serializeJson(doc, out);
}
