// Board macro matrix. Arduino IDE has no global -D injection, so every
// translation unit pulls it in first (see board_config.h for the rationale).
#include "board_config.h"

#define P4_SETTINGS_NO_ALIAS
#include "wifi_manager.h"
#include <new>
#include "p4_runtime.h"
#include "settings.h"
#include "web_server.h"   // isOtaAutoInProgress: the link-dead reboot must not cut an OTA short
#include "display_ui.h"
#include "config.h"
#include "improv_setup.h"
#include <WiFi.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <esp_heap_caps.h>
#include <atomic>
#if defined(BOARD_IS_WS_P4_7B)
static void beginStaConnectAttempt();
static void stopAP();
static P4UiSettings wifiSettings;
#define P4_SETTINGS_OWNER wifiSettings
#include "p4_legacy_aliases.h"
#else
#include "settings.h"
#endif

static bool apMode = false;
static DNSServer* dnsServer = nullptr;
static unsigned long disconnectTime = 0;
static unsigned long lastReconnectAttempt = 0;
static uint8_t reconnectAttempts = 0;
#if defined(BOARD_IS_WS_P4_7B)
// esp-hosted self-healing timers. See config.h: the coprocessor can die
// without ever delivering a disconnect event, so status alone is untrustworthy
// and every guard here is gated on the netif IP instead.
static unsigned long fakeOnlineSince = 0;   // status=connected but no lease
static uint8_t fakeOnlineForces = 0;        // forced reconnects in that state
static unsigned long noIpSince = 0;         // any state, no lease at all
#endif
static unsigned long lastStaProbe = 0;
static unsigned long probeStartTime = 0;
static bool staProbing = false;
static unsigned long phase3StartTime = 0;
static String apSSID;
static bool splashStaStarted = false;
static unsigned long splashStaStartMs = 0;

// C3 antenna workaround (issue #146): a common batch of ESP32-C3 SuperMini
// clones (and LOLIN C3 Mini v1.0.0) ships with a mis-tuned antenna matching
// network. At the default ~19.5 dBm the PA distorts so badly that beacons and
// association frames never decode on any receiver, even at <1 m - while the
// radio itself reports success, so softAP() logs "AP started" with an SSID
// nobody can see. Capping TX power at 8.5 dBm is the community-established
// fix. Policy: AP/setup mode is always capped (close range by definition, and
// an invisible AP would strand the user); STA boot connect tries full power
// first and falls back to capped on the last attempt - a capped success is
// persisted (wifiTxCapped) so every later connect starts capped. Healthy
// boards keep full power and range.
#if BOARD_NEEDS_WIFI_TX_LIMIT
static void capTxPower() {
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
}
#endif

// --- Failure hint for AP fallback ---
// Release builds log at level E, so the core's own "Reason: N" warning is
// invisible; capture the reasons here. State covers everything since the last
// successful IP, which is exactly the failed stretch that led to AP mode.
static WifiFailInfo failInfo = {};
static std::atomic<uint8_t> staLastReason{0};
static std::atomic<bool> staAuthRejected{false}, staAssociated{false};

#if defined(BOARD_IS_WS_P4_7B)
void p4WifiApplySettings(const P4UiSettings& settings) { wifiSettings = settings; }
bool p4WifiUpdateSettings(const P4UiSettings& settings) {
  const bool changed = memcmp(&wifiSettings.netSettings, &settings.netSettings, sizeof(NetworkSettings)) != 0 ||
                       strcmp(wifiSettings.wifiSSID, settings.wifiSSID) != 0 ||
                       strcmp(wifiSettings.wifiPass, settings.wifiPass) != 0;
  wifiSettings = settings;
  if (changed) {
    stopAP();
    apMode = false;
    WiFi.disconnect(false, false);
    WiFi.mode(WIFI_OFF);
    if (wifiSettings.wifiSSID[0]) beginStaConnectAttempt();
    else startAP();
  }
  return changed;
}
bool p4WifiHasCredentials() { return wifiSettings.wifiSSID[0] != 0; }
#endif

static void onStaEvent(arduino_event_id_t event, arduino_event_info_t info) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
      staAssociated = true;
      break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      staLastReason = 0;
      staAuthRejected = false;
      staAssociated = false;
#if defined(BOARD_IS_WS_P4_7B)
      p4WifiGotIp();
#endif
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED: {
      uint8_t r = info.wifi_sta_disconnected.reason;
      if (r == WIFI_REASON_ASSOC_LEAVE) break;  // our own disconnect() calls
      staLastReason = r;
      if (r == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT || r == WIFI_REASON_HANDSHAKE_TIMEOUT ||
          r == WIFI_REASON_AUTH_FAIL || r == WIFI_REASON_MIC_FAILURE ||
          r == WIFI_REASON_802_1X_AUTH_FAILED) {
        staAuthRejected = true;
      }
      break;
    }
    default:
      break;
  }
}

static void hookStaEvents() {
  static bool hooked = false;
  if (hooked) return;
  WiFi.onEvent(onStaEvent);
  hooked = true;
}

const WifiFailInfo& getWifiFailInfo() {
  return failInfo;
}

WifiFailInfo wifiFailureValue() {
#if defined(BOARD_IS_WS_P4_7B)
  if (isWiFiOwnedByWorker()) return failInfo;
  WifiFailInfo snapshot{};
  p4UiWifiFailureSnapshot(snapshot);
  return snapshot;
#endif
  return failInfo;
}

const char* wifiFailKindName(WifiFailKind k) {
  switch (k) {
    case WIFI_FAIL_NOT_FOUND:     return "not_found";
    case WIFI_FAIL_NAME_MISMATCH: return "name_mismatch";
    case WIFI_FAIL_WEAK:          return "weak";
    case WIFI_FAIL_PASS_SHORT:    return "pass_short";
    case WIFI_FAIL_BAD_PASS:      return "bad_pass";
    case WIFI_FAIL_ENTERPRISE:    return "enterprise";
    case WIFI_FAIL_NO_IP:         return "no_ip";
    case WIFI_FAIL_OTHER:         return "other";
    default:                      return "none";
  }
}

// Scan for the stored SSID and turn scan + captured reasons into one verdict.
// Runs once on the way into AP mode; the STA side must be idle (a connecting
// STA cannot scan). Async scan so the connecting animation keeps running.
static void diagnoseStaFailure() {
  WifiFailInfo fi = {};
  fi.reason = staLastReason;

  WiFi.mode(WIFI_STA);
  WiFi.scanNetworks(true, true);
  int n;
  unsigned long t0 = millis();
  while ((n = WiFi.scanComplete()) == WIFI_SCAN_RUNNING && millis() - t0 < 10000) {
#if defined(BOARD_IS_WS_P4_7B)
    vTaskDelay(pdMS_TO_TICKS(50));
#else
    delay(50);
    updateDisplay();
    flushFrame();  // JC3248W535: push sprite to panel during blocking loop
#endif
  }

  bool exact = false, enterprise = false, secured = false;
  String want = wifiSSID;
  want.trim();
  for (int i = 0; i < n; i++) {
    String s = WiFi.SSID(i);
    if (s == wifiSSID) {
      // Mesh / multi-AP: judge by the best radio the device can hear.
      if (!exact || WiFi.RSSI(i) > fi.rssi) {
        fi.rssi = (int8_t)WiFi.RSSI(i);
        fi.channel = (uint8_t)WiFi.channel(i);
        wifi_auth_mode_t auth = WiFi.encryptionType(i);
        enterprise = (auth == WIFI_AUTH_WPA2_ENTERPRISE);
        secured = (auth != WIFI_AUTH_OPEN);
      }
      exact = true;
    } else if (!fi.nearName[0] && s.length() > 0) {
      String t = s;
      t.trim();
      if (t.equalsIgnoreCase(want)) strlcpy(fi.nearName, s.c_str(), sizeof(fi.nearName));
    }
  }
  if (n > 0) WiFi.scanDelete();

  const size_t passLen = strlen(wifiPass);
  if (n >= 0 && !exact) {
    fi.kind = fi.nearName[0] ? WIFI_FAIL_NAME_MISMATCH : WIFI_FAIL_NOT_FOUND;
  } else if (n < 0) {
    // Scan failed: the reasons alone are all we have.
    fi.kind = staLastReason == WIFI_REASON_NO_AP_FOUND ? WIFI_FAIL_NOT_FOUND
            : staAuthRejected ? WIFI_FAIL_BAD_PASS : WIFI_FAIL_OTHER;
  } else if (fi.rssi < WIFI_RSSI_WEAK) {
    fi.kind = WIFI_FAIL_WEAK;
  } else if (enterprise) {
    fi.kind = WIFI_FAIL_ENTERPRISE;
  } else if (secured && passLen > 0 && passLen < 8) {
    fi.kind = WIFI_FAIL_PASS_SHORT;
  } else if (staAuthRejected) {
    fi.kind = WIFI_FAIL_BAD_PASS;
  } else if (fi.rssi < WIFI_RSSI_MARGINAL) {
    fi.kind = WIFI_FAIL_WEAK;
  } else if (staAssociated) {
    fi.kind = WIFI_FAIL_NO_IP;
  } else {
    fi.kind = WIFI_FAIL_OTHER;
  }
  failInfo = fi;

  Serial.printf("WiFi: failure hint %s (scan %d, rssi %d, ch %u, reason %u%s%s)\n",
                wifiFailKindName(fi.kind), n, fi.rssi, fi.channel, fi.reason,
                fi.nearName[0] ? ", near " : "", fi.nearName);
}

bool isWiFiConnected() {
#if defined(BOARD_IS_WS_P4_7B)
  if (!isWiFiOwnedByWorker()) {
    return p4WifiOnline();
  }
#endif
  return WiFi.status() == WL_CONNECTED;
}

bool isAPMode() {
#if defined(BOARD_IS_WS_P4_7B)
  if (!isWiFiOwnedByWorker()) {
    return p4WifiApMode();
  }
#endif
  return apMode;
}

bool isWiFiOwnedByWorker() {
#if defined(BOARD_IS_WS_P4_7B)
  void* owner = p4WifiTaskHandle();
  return owner && xTaskGetCurrentTaskHandle() == owner;
#else
  return true;
#endif
}

void getWiFiIpText(char* out, size_t size) {
  if (!out || !size) return;
#if defined(BOARD_IS_WS_P4_7B)
  if (!isWiFiOwnedByWorker()) {
    P4NetworkStatus status;
    if (p4NetworkSnapshot(status)) strlcpy(out, status.ip, size);
    else out[0] = 0;
    return;
  }
#endif
  strlcpy(out, WiFi.localIP().toString().c_str(), size);
}

String getAPSSID() {
  return apSSID;
}

static void stopAP() {
  if (dnsServer) {
    dnsServer->stop();
    delete dnsServer;
    dnsServer = nullptr;
  }
}

void startAP() {
  MDNS.end();  // tear down any responder bound to the now-dead STA interface

  // Build SSID from MAC
  uint32_t mac = (uint32_t)(ESP.getEfuseMac() & 0xFFFF);
  char ssidBuf[32];
  snprintf(ssidBuf, sizeof(ssidBuf), "%s%04X", WIFI_AP_PREFIX, mac);
  apSSID = ssidBuf;

  // Use AP+STA so we can probe STA while serving the config portal
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(ssidBuf, WIFI_AP_PASSWORD);
#if BOARD_NEEDS_WIFI_TX_LIMIT
  capTxPower();
  Serial.println("WiFi: TX power capped to 8.5 dBm in AP mode (C3 antenna workaround)");
#endif

  // Captive portal: redirect all DNS to our IP
  if (!dnsServer) {
    dnsServer = new (std::nothrow) DNSServer();
  }
  if (dnsServer) dnsServer->start(53, "*", WiFi.softAPIP());

  apMode = true;
  disconnectTime = 0;
  lastStaProbe = millis();
  staProbing = false;

  Serial.printf("AP started: %s (IP: %s)\n", ssidBuf,
                WiFi.softAPIP().toString().c_str());
#if !defined(BOARD_IS_WS_P4_7B)
  setScreenState(SCREEN_AP_MODE);
#endif
}

static void applyStaticNetworkConfig() {
  if (netSettings.useDHCP || netSettings.staticIP[0] == '\0') return;

  IPAddress ip, gw, sn, dns;
  bool ipOk = ip.fromString(netSettings.staticIP);
  bool gwOk = gw.fromString(netSettings.gateway);
  bool snOk = sn.fromString(netSettings.subnet);

  if (!ipOk || !gwOk || !snOk) {
    Serial.printf("Static IP config invalid, falling back to DHCP (ip=%d gw=%d sn=%d)\n",
                  (int)ipOk, (int)gwOk, (int)snOk);
    return;
  }

  bool customDns = (netSettings.dns[0] != '\0');
  if (customDns) {
    if (!dns.fromString(netSettings.dns)) {
      dns = gw;
      Serial.printf("Static IP: invalid DNS '%s', falling back to gateway %s\n",
                    netSettings.dns, netSettings.gateway);
    }
  } else {
    dns = gw;
  }

  String dnsStr = dns.toString();
  bool applied = WiFi.config(ip, gw, sn, dns);
  if (applied) {
    Serial.printf("Static IP: %s GW: %s SN: %s DNS: %s\n",
                  netSettings.staticIP, netSettings.gateway,
                  netSettings.subnet, dnsStr.c_str());
  } else {
    Serial.printf("Static IP: WiFi.config() failed for IP=%s GW=%s SN=%s DNS=%s\n",
                  netSettings.staticIP, netSettings.gateway,
                  netSettings.subnet, dnsStr.c_str());
  }
}

// Start (or restart) the mDNS responder so the device is reachable at
// <hostname>.local. No-op unless the user enabled it. Safe to call repeatedly:
// MDNS.end() first lets us re-announce after a WiFi drop/reconnect, which the
// ESP32 responder does not always recover on its own.
static void startMDNS() {
  if (!netSettings.mdnsEnabled || netSettings.hostname[0] == '\0') return;

  // The web UI validates too, but never trust stored input.
  char host[32];
  sanitizeHostname(netSettings.hostname, host, sizeof(host));

  MDNS.end();
  if (MDNS.begin(host)) {
    MDNS.addService("http", "tcp", 80);
    Serial.printf("mDNS started: http://%s.local\n", host);
  } else {
    Serial.println("mDNS start failed");
  }
}

static void completeWiFiStartup() {
  Serial.printf("WiFi connected! IP: %s\n",
                WiFi.localIP().toString().c_str());
  stopAP();
  apMode = false;
  disconnectTime = 0;
  reconnectAttempts = 0;
  splashStaStarted = false;

  // Sync time via NTP with automatic DST
  configTzTime(netSettings.timezoneStr, "pool.ntp.org", "time.nist.gov");
  Serial.printf("NTP configured: %s\n", netSettings.timezoneStr);

  startMDNS();

  // Show IP screen for 1.5 seconds if enabled
  if (netSettings.showIPAtStartup) {
#if !defined(BOARD_IS_WS_P4_7B)
    setScreenState(SCREEN_WIFI_CONNECTED);
    unsigned long ipStart = millis();
    while (millis() - ipStart < 1500) {
      updateDisplay();
      flushFrame();  // JC3248W535: push sprite to panel during blocking loop
      delay(50);
    }
#endif
  }
}

static void beginStaConnectAttempt() {
  hookStaEvents();
  // DHCP option 12; the core applies it to the STA netif inside WiFi.mode().
  WiFi.setHostname(netSettings.hostname);
  WiFi.mode(WIFI_STA);
  applyStaticNetworkConfig();
  WiFi.begin(wifiSSID, wifiPass);
#if BOARD_NEEDS_WIFI_TX_LIMIT
  if (wifiTxCapped) capTxPower();
#endif
}

void startWiFiDuringSplash() {
#if defined(BOARD_IS_WS_P4_7B)
  return; // The Wi-Fi owner starts after the splash; the renderer never connects.
#endif
  if (strlen(wifiSSID) == 0) return;

  Serial.printf("WiFi: starting background connect during splash: %s\n", wifiSSID);
  beginStaConnectAttempt();
  splashStaStarted = true;
  splashStaStartMs = millis();
}

void initWiFi() {
#if defined(BOARD_IS_WS_P4_7B)
  if (strlen(wifiSSID) > 0) {
    beginStaConnectAttempt();
    splashStaStarted = true;
    splashStaStartMs = millis();
    disconnectTime = millis();
  } else {
    startAP();
  }
  return;
#endif
  // If we have stored credentials, try STA mode
  if (strlen(wifiSSID) > 0) {
    setScreenState(SCREEN_CONNECTING_WIFI);

    if (splashStaStarted && WiFi.status() == WL_CONNECTED) {
      Serial.println("WiFi connected during splash");
      completeWiFiStartup();
      return;
    }

    for (int attempt = 1; attempt <= 3; attempt++) {
      unsigned long start = millis();

      if (attempt == 1 && splashStaStarted) {
        Serial.printf("Connecting to WiFi: %s (attempt 1/3, continued after splash)\n", wifiSSID);
        start = splashStaStartMs;
      } else {
        Serial.printf("Connecting to WiFi: %s (attempt %d/3)\n", wifiSSID, attempt);
        beginStaConnectAttempt();
      }

#if BOARD_NEEDS_WIFI_TX_LIMIT
      // Last attempt: retry at reduced power in case this board's antenna
      // cannot handle full TX power (see capTxPower above). If it works,
      // remember it so every future connect skips the doomed full-power tries.
      bool cappedAttempt = false;
      if (attempt == 3 && !wifiTxCapped) {
        capTxPower();
        Serial.println("WiFi: retrying at 8.5 dBm TX power (C3 antenna workaround)");
        cappedAttempt = true;
      }
#endif

      while (WiFi.status() != WL_CONNECTED &&
             millis() - start < WIFI_CONNECT_TIMEOUT) {
        delay(100);
        updateDisplay();
        flushFrame();  // JC3248W535: push sprite to panel during blocking loop
      }
      if (WiFi.status() == WL_CONNECTED) {
#if BOARD_NEEDS_WIFI_TX_LIMIT
        if (cappedAttempt) {
          wifiTxCapped = true;
          saveWifiTxCapped();
          Serial.println("WiFi: connected at reduced TX power - cap persisted for future boots");
        }
#endif
        break;
      }

      Serial.println("WiFi attempt failed, retrying...");
      WiFi.disconnect(true);
      splashStaStarted = false;
      delay(1000);
    }

    if (WiFi.status() == WL_CONNECTED) {
      completeWiFiStartup();
      return;
    }

    splashStaStarted = false;
    Serial.println("WiFi connection failed, starting AP");
    diagnoseStaFailure();
  }

  // No credentials or connection failed — bring AP up immediately so the
  // captive portal is reachable without any delay. This matches the legacy
  // behaviour exactly; web-flasher / Improv users get an additional path
  // in parallel (see below) without anyone having to wait for a timeout.
  startAP();

  // For genuinely fresh devices (no SSID stored at all), also open an
  // Improv-Serial listening window in the background. Pumped from
  // handleWiFi() while AP is up, so it costs nothing if no browser is
  // listening; if the user did flash via the web flasher, ESP Web Tools
  // probes Improv ~15s after the install and presents its "Configure
  // WiFi" dialog. Skipped when the user just failed to connect with
  // stored credentials - they need the AP to fix what they typed, not a
  // browser dialog over a port that may not even be connected anymore.
  if (strlen(wifiSSID) == 0) {
    improvSetupBegin(IMPROV_SETUP_WINDOW_MS);
  }
}

// Return the reconnect interval for the current attempt count.
static unsigned long reconnectInterval() {
  if (reconnectAttempts < WIFI_BACKOFF_PHASE2_START) {
    return WIFI_BACKOFF_PHASE1_MS;      // phase 1: 10s
  }
  if (reconnectAttempts < WIFI_BACKOFF_PHASE3_START) {
    return WIFI_BACKOFF_PHASE2_MS;      // phase 2: 30s
  }
  return WIFI_BACKOFF_PHASE3_MS;        // phase 3: 60s, indefinite
}

void refreshWiFiConnection() {
  if (apMode) return;
  lastReconnectAttempt = 0;
  handleWiFi();
}

#if defined(BOARD_IS_WS_P4_7B)
void p4WifiServiceOnline() {
  // Called by the Wi-Fi owner after GOT_IP. No display wait or NVS work here.
  disconnectTime = 0;
  reconnectAttempts = 0;
  lastReconnectAttempt = 0;
  phase3StartTime = 0;
  fakeOnlineSince = 0;
  fakeOnlineForces = 0;
  noIpSince = 0;
  stopAP(); apMode = false; staProbing = false;
  configTzTime(netSettings.timezoneStr, "pool.ntp.org", "time.nist.gov");
  startMDNS();
}
#endif

#if defined(BOARD_IS_WS_P4_7B)
// Last resort for a dead coprocessor link: nothing has produced an IP for a
// long stretch through both offline reconnects and fake-online rebuilds, and
// phase-3 attempts have actually been running. A hosted link that far gone
// does not come back on its own — rebooting reproduces the power-cycle
// recovery. Never during an OTA transfer.
void checkP4LinkDeadReboot() {
  if (!noIpSince || millis() - noIpSince < P4_LINK_DEAD_REBOOT_MS) return;
  if (reconnectAttempts < WIFI_BACKOFF_PHASE3_START + P4_LINK_DEAD_REBOOT_MIN_ATTEMPTS) return;
  if (isOtaAutoInProgress()) return;
  Serial.printf("WiFi: no IP for %lus after %u reconnect attempts — rebooting to recover the hosted link\n",
                (unsigned long)((millis() - noIpSince) / 1000), reconnectAttempts);
  Serial.flush();
  delay(250);
  ESP.restart();
}
#endif

void recoverWiFiTransport(){
#if defined(BOARD_IS_WS_P4_7B)
  static uint32_t last=0;
  if(apMode||(last&&millis()-last<60000))return;
  last=millis();
  Serial.println("WiFi: rebuilding associated link after repeated transport failures");
  WiFi.disconnect(false,false);
  WiFi.mode(WIFI_OFF);
  beginStaConnectAttempt();
#endif
}

void handleWiFi() {
#if defined(BOARD_IS_WS_P4_7B)
  if (apMode) {
    if (dnsServer) dnsServer->processNextRequest();
    if (!staProbing && millis() - lastStaProbe >= WIFI_STA_PROBE_INTERVAL) {
      WiFi.begin(wifiSSID, wifiPass);
      staProbing = true;
      probeStartTime = millis();
    } else if (staProbing && millis() - probeStartTime >= WIFI_STA_PROBE_CHECK_MS) {
      if (WiFi.status() == WL_CONNECTED) {
        stopAP(); WiFi.mode(WIFI_STA); apMode = false; staProbing = false;
        p4WifiServiceOnline();
      } else {
        WiFi.disconnect(false); staProbing = false; lastStaProbe = millis();
      }
    }
    return;
  }
  if (WiFi.status() == WL_CONNECTED) {
    if (WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
      fakeOnlineSince = 0;
      fakeOnlineForces = 0;
      noIpSince = 0;
      if (disconnectTime) p4WifiServiceOnline();
      disconnectTime = 0;
      return;
    }
    // Fake online: the connected flag stands but the lease is gone — the
    // hosted coprocessor stopped delivering events. A plain begin() here is
    // not enough: after a few windows escalate to a full transport rebuild.
    if (!fakeOnlineSince) {
      fakeOnlineSince = millis();
      Serial.println("WiFi: status is connected but the IP is gone — hosted link suspect");
    }
    if (!noIpSince) noIpSince = millis();
    if (millis() - fakeOnlineSince >= P4_FAKE_ONLINE_RECONNECT_MS) {
      lastReconnectAttempt = millis();
      reconnectAttempts++;
      fakeOnlineSince = millis();  // re-arm the window
      if (++fakeOnlineForces >= P4_FAKE_ONLINE_REBUILD_AFTER) {
        fakeOnlineForces = 0;
        Serial.println("WiFi: repeated connected-but-no-IP windows, rebuilding transport");
        recoverWiFiTransport();    // throttled to once per 60s internally
      } else {
        Serial.printf("WiFi: forced reconnect %u/%u under a connected-but-no-IP link\n",
                      fakeOnlineForces, P4_FAKE_ONLINE_REBUILD_AFTER);
        WiFi.disconnect(false);
        WiFi.begin(wifiSSID, wifiPass);
      }
    }
    checkP4LinkDeadReboot();
    return;
  }
  fakeOnlineSince = 0;
  fakeOnlineForces = 0;
  if (!noIpSince) noIpSince = millis();
  if (!disconnectTime) {
    disconnectTime = millis();
    Serial.println("WiFi disconnected, will try to reconnect...");
  }
  if (!lastReconnectAttempt || millis() - lastReconnectAttempt > reconnectInterval()) {
    lastReconnectAttempt = millis();
    reconnectAttempts++;
    Serial.printf("WiFi: reconnect attempt %u (interval %lus, last reason=%u)\n",
                  reconnectAttempts, reconnectInterval() / 1000UL,
                  (unsigned)staLastReason.load());
    WiFi.disconnect(false);
    WiFi.begin(wifiSSID, wifiPass);
  }
  checkP4LinkDeadReboot();
  return;
#endif
  if (apMode) {
    if (dnsServer) dnsServer->processNextRequest();

    // Pump the Improv-Serial listener if its setup window is still open.
    // On success the library has already saved credentials AND established
    // STA via our custom connect callback; restart so the device boots
    // cleanly into the configured WiFi without the dual-mode hangover.
    if (improvSetupTick()) {
      Serial.println("Improv: credentials received, restarting");
      Serial.flush();
      delay(200);  // let any pending serial response reach the browser
      ESP.restart();
    }
    if (improvSetupExpired()) {
      improvSetupEnd();
    }

    // Periodically probe STA to recover without user interaction
    unsigned long now = millis();
    if (!staProbing && now - lastStaProbe >= WIFI_STA_PROBE_INTERVAL) {
      // Start a non-blocking STA probe; result checked after WIFI_STA_PROBE_CHECK_MS
      Serial.println("AP mode: probing STA connection...");
      WiFi.begin(wifiSSID, wifiPass);
      staProbing = true;
      probeStartTime = now;
    } else if (staProbing && now - probeStartTime >= WIFI_STA_PROBE_CHECK_MS) {
      // Check result of the in-flight probe
      if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("STA probe succeeded, IP: %s — leaving AP mode\n",
                      WiFi.localIP().toString().c_str());
        stopAP();
        WiFi.mode(WIFI_STA);
#if BOARD_NEEDS_WIFI_TX_LIMIT
        // AP mode capped the shared radio; healthy boards get full power back
        // for normal operation, capped boards stay at 8.5 dBm.
        if (!wifiTxCapped) WiFi.setTxPower(WIFI_POWER_19_5dBm);
#endif
        apMode = false;
        staProbing = false;
        disconnectTime = 0;
        reconnectAttempts = 0;
        lastReconnectAttempt = 0;
        phase3StartTime = 0;

        configTzTime(netSettings.timezoneStr, "pool.ntp.org", "time.nist.gov");
        startMDNS();
        setScreenState(SCREEN_WIFI_CONNECTED);
      } else {
        Serial.println("STA probe failed, staying in AP mode");
        WiFi.disconnect(false);
        staProbing = false;
        lastStaProbe = now;  // reset the 2-min cycle
      }
    }
    return;
  }

  // In STA mode: check for disconnection
  if (WiFi.status() != WL_CONNECTED) {
    if (disconnectTime == 0) {
      disconnectTime = millis();
      lastReconnectAttempt = 0;
      reconnectAttempts = 0;
      Serial.println("WiFi disconnected, will try to reconnect...");
    }

    // Try to reconnect using the current backoff interval (non-blocking)
    if (lastReconnectAttempt == 0 ||
        millis() - lastReconnectAttempt > reconnectInterval()) {
      lastReconnectAttempt = millis();
      reconnectAttempts++;

      const char* phase = reconnectAttempts <= WIFI_BACKOFF_PHASE2_START ? "phase1"
                        : reconnectAttempts <= WIFI_BACKOFF_PHASE3_START ? "phase2"
                        : "phase3";
      Serial.printf("WiFi reconnect attempt %d (%s, interval %lus)\n",
                    reconnectAttempts, phase, reconnectInterval() / 1000UL);

      // Record when we first enter phase 3
      if (reconnectAttempts == WIFI_BACKOFF_PHASE3_START) {
        phase3StartTime = millis();
      }

      // After phase 3 has run for WIFI_AP_FALLBACK_MS, fall back to AP so
      // the user can reconfigure the device if credentials have changed
      if (reconnectAttempts > WIFI_BACKOFF_PHASE3_START &&
          phase3StartTime > 0 &&
          millis() - phase3StartTime >= WIFI_AP_FALLBACK_MS) {
        Serial.println("Phase 3 timeout — falling back to AP mode");
        WiFi.disconnect();  // stop the reconnect cycle so the scan can run
        diagnoseStaFailure();
        startAP();
        return;
      }

      WiFi.disconnect();
      WiFi.begin(wifiSSID, wifiPass);
#if BOARD_NEEDS_WIFI_TX_LIMIT
      if (wifiTxCapped) capTxPower();
#endif
    }
  } else {
    if (disconnectTime > 0) {
      Serial.println("WiFi reconnected!");
      startMDNS();  // re-announce; responder may not survive a link drop
    }
    disconnectTime = 0;
    reconnectAttempts = 0;
    lastReconnectAttempt = 0;
    phase3StartTime = 0;
  }
}
