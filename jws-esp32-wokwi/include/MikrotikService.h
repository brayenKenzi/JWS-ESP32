// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <Arduino.h>
#include <MD5Builder.h>
#include <Preferences.h>
#include <WiFi.h>
#include <atomic>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

namespace jws {

struct MikrotikInterface {
  char name[20] = "";
  char label[20] = "";
  bool running = false;
  float rxMbps = 0.0f;
  float txMbps = 0.0f;
  uint32_t rxPps = 0;
  uint32_t txPps = 0;
  float peakRxMbps = 0.0f;
  float peakTxMbps = 0.0f;
  float rxHistory[12] = {0};
  float txHistory[12] = {0};
};

struct MikrotikClientUser {
  char username[20] = "";
  char address[16] = "";
  char uptime[12] = "";
  float rxMbps = 0.0f;
  float txMbps = 0.0f;
};

struct MikrotikFeed {
  bool connected = false;
  char host[32] = "192.168.88.1";
  uint16_t port = 8728;
  char user[24] = "admin";
  char pass[32] = "";
  char interfaceCsv[64] = "ether1,ether2,ether3,ether4";
  char identity[24] = "CCR-CORE-NOC";
  char boardName[20] = "CCR2004-1G-12S+";
  char uptime[20] = "18d 07:42:19";
  uint8_t cpuLoad = 18;
  uint8_t memPercent = 32;
  uint8_t tempC = 41;
  MikrotikInterface ifaces[4];
  int totalPppoe = 24;
  int pppoeCount = 5;
  MikrotikClientUser pppoeUsers[15];
  int totalHotspot = 38;
  int hotspotCount = 5;
  MikrotikClientUser hotspotUsers[15];
  uint32_t lastUpdateMs = 0;
};

class MikrotikService {
 public:
  static MikrotikService& instance() {
    static MikrotikService s;
    return s;
  }

  void begin() {
    prefs_.begin("jws_mtik", false);
    String h = prefs_.getString("host", "192.168.88.1");
    uint16_t p = prefs_.getUShort("port", 8728);
    String u = prefs_.getString("user", "admin");
    String pw = prefs_.getString("pass", "");
    String csv = prefs_.getString("ifaces", "ether1,ether2,ether3,ether4");

    portENTER_CRITICAL(&cfgMux_);
    copySafe(pendingHost_, sizeof(pendingHost_), h.c_str());
    pendingPort_ = (p == 0) ? 8728 : p;
    copySafe(pendingUser_, sizeof(pendingUser_), u.c_str());
    copySafe(pendingPass_, sizeof(pendingPass_), pw.c_str());
    copySafe(pendingIfaces_, sizeof(pendingIfaces_), csv.c_str());

    copySafe(feed_.host, sizeof(feed_.host), pendingHost_);
    feed_.port = pendingPort_;
    copySafe(feed_.user, sizeof(feed_.user), pendingUser_);
    copySafe(feed_.pass, sizeof(feed_.pass), pendingPass_);
    copySafe(feed_.interfaceCsv, sizeof(feed_.interfaceCsv), pendingIfaces_);
    portEXIT_CRITICAL(&cfgMux_);

    if (feed_.pass[0] == '\0') {
      initDefaultTelemetry();
    } else {
      resetToLiveStandby();
    }
    parseInterfacesCsv();

    if (taskHandle_ == nullptr) {
      xTaskCreatePinnedToCore(taskEntry, "mtik_bg", 8192, this, 1, &taskHandle_, 0);
    }
  }

  const MikrotikFeed& feed() const { return feed_; }

  void configure(const char* host, uint16_t port, const char* user,
                 const char* pass, const char* ifacesCsv) {
    portENTER_CRITICAL(&cfgMux_);
    if (host && host[0]) copySafe(pendingHost_, sizeof(pendingHost_), host);
    pendingPort_ = (port == 0) ? 8728 : port;
    if (user && user[0]) copySafe(pendingUser_, sizeof(pendingUser_), user);
    if (pass) copySafe(pendingPass_, sizeof(pendingPass_), pass);
    if (ifacesCsv && ifacesCsv[0]) {
      copySafe(pendingIfaces_, sizeof(pendingIfaces_), ifacesCsv);
    }

    copySafe(feed_.host, sizeof(feed_.host), pendingHost_);
    feed_.port = pendingPort_;
    copySafe(feed_.user, sizeof(feed_.user), pendingUser_);
    copySafe(feed_.pass, sizeof(feed_.pass), pendingPass_);
    copySafe(feed_.interfaceCsv, sizeof(feed_.interfaceCsv), pendingIfaces_);
    portEXIT_CRITICAL(&cfgMux_);

    prefs_.putString("host", feed_.host);
    prefs_.putUShort("port", feed_.port);
    prefs_.putString("user", feed_.user);
    prefs_.putString("pass", feed_.pass);
    prefs_.putString("ifaces", feed_.interfaceCsv);

    configChanged_.store(true, std::memory_order_release);
  }

  void tick(uint32_t nowMs, bool isViewingNoc) {
    viewingNoc_.store(isViewingNoc, std::memory_order_relaxed);
    if (feed_.pass[0] == '\0') {
      const uint32_t intervalMs = isViewingNoc ? 1000UL : 10000UL;
      if (lastDemoMs_ == 0 || static_cast<uint32_t>(nowMs - lastDemoMs_) >= intervalMs) {
        lastDemoMs_ = nowMs;
        animateDemoTelemetry(nowMs);
        feed_.lastUpdateMs = nowMs;
      }
    }
  }

 private:
  struct SimpleQueueEntry {
    char name[24] = "";
    char targetIp[18] = "";
    float txMbps = 0.0f;
    float rxMbps = 0.0f;
  };

  struct ByteCacheEntry {
    char key[24] = "";
    uint64_t rxBytes = 0;
    uint64_t txBytes = 0;
    uint32_t lastMs = 0;
    float rxMbps = 0.0f;
    float txMbps = 0.0f;
    bool valid = false;
  };

  Preferences prefs_;
  MikrotikFeed feed_;
  WiFiClient apiClient_;
  bool loggedIn_ = false;
  bool hadLiveConnection_ = false;

  TaskHandle_t taskHandle_ = nullptr;
  std::atomic<bool> viewingNoc_{false};
  std::atomic<bool> configChanged_{false};
  portMUX_TYPE cfgMux_ = portMUX_INITIALIZER_UNLOCKED;

  char pendingHost_[32] = "192.168.88.1";
  uint16_t pendingPort_ = 8728;
  char pendingUser_[24] = "admin";
  char pendingPass_[32] = "";
  char pendingIfaces_[64] = "ether1,ether2,ether3,ether4";

  uint32_t lastDemoMs_ = 0;
  uint32_t lastPollMs_ = 0;
  uint32_t lastAttemptMs_ = 0;
  uint32_t demoTickCount_ = 0;

  static void taskEntry(void* arg) {
    static_cast<MikrotikService*>(arg)->backgroundLoop();
  }

  void backgroundLoop() {
    for (;;) {
      const uint32_t nowMs = millis();

      if (configChanged_.exchange(false, std::memory_order_acq_rel)) {
        disconnectApi();
        portENTER_CRITICAL(&cfgMux_);
        copySafe(feed_.host, sizeof(feed_.host), pendingHost_);
        feed_.port = pendingPort_;
        copySafe(feed_.user, sizeof(feed_.user), pendingUser_);
        copySafe(feed_.pass, sizeof(feed_.pass), pendingPass_);
        copySafe(feed_.interfaceCsv, sizeof(feed_.interfaceCsv), pendingIfaces_);
        portEXIT_CRITICAL(&cfgMux_);

        if (feed_.pass[0] != '\0') {
          resetToLiveStandby();
        } else {
          initDefaultTelemetry();
        }
        parseInterfacesCsv();
        lastPollMs_ = 0;
        lastAttemptMs_ = 0;
      }

      const bool hasConfig = (feed_.host[0] != '\0') && (feed_.pass[0] != '\0');
      const uint32_t intervalMs = viewingNoc_.load(std::memory_order_relaxed) ? 1200UL : 10000UL;

      if (hasConfig && (lastPollMs_ == 0 || static_cast<uint32_t>(nowMs - lastPollMs_) >= intervalMs)) {
        lastPollMs_ = nowMs;
        if (WiFi.status() == WL_CONNECTED) {
          if (loggedIn_ || lastAttemptMs_ == 0 ||
              static_cast<uint32_t>(nowMs - lastAttemptMs_) >= 10000UL) {
            lastAttemptMs_ = nowMs;
            if (!pollRouterOs(nowMs)) {
              disconnectApi();
            } else {
              feed_.lastUpdateMs = nowMs;
            }
          }
        } else {
          disconnectApi();
        }
      }

      vTaskDelay(pdMS_TO_TICKS(100));
    }
  }

  // Per-interface byte/packet counters for real-time hardware delta rate calculation
  uint64_t prevIfRxBytes_[4] = {0};
  uint64_t prevIfTxBytes_[4] = {0};
  uint64_t prevIfRxPkts_[4] = {0};
  uint64_t prevIfTxPkts_[4] = {0};
  bool hasPrevIfSample_[4] = {false};
  uint32_t prevIfSampleMs_ = 0;

  // Lookup tables stored in singleton state (keeps loopTask stack usage low)
  SimpleQueueEntry queueCache_[48];
  int queueCount_ = 0;

  ByteCacheEntry pppIfCache_[32];
  int pppIfCacheCount_ = 0;

  ByteCacheEntry hsByteCache_[32];
  int hsByteCacheCount_ = 0;

  MikrotikClientUser sortScratch_[32];

  static void copySafe(char* dst, size_t cap, const char* src) {
    if (!dst || cap == 0) return;
    if (!src) {
      dst[0] = '\0';
      return;
    }
    strncpy(dst, src, cap - 1);
    dst[cap - 1] = '\0';
  }

  static bool startsWith(const char* s, const char* prefix) {
    return strncmp(s, prefix, strlen(prefix)) == 0;
  }

  static bool ciStartsWith(const char* s, const char* prefix) {
    return strncasecmp(s, prefix, strlen(prefix)) == 0;
  }

  void disconnectApi() {
    if (apiClient_.connected()) {
      apiClient_.stop();
    }
    loggedIn_ = false;
    feed_.connected = false;
  }

  void clearCountersAndCaches() {
    for (int i = 0; i < 4; ++i) {
      prevIfRxBytes_[i] = 0;
      prevIfTxBytes_[i] = 0;
      prevIfRxPkts_[i] = 0;
      prevIfTxPkts_[i] = 0;
      hasPrevIfSample_[i] = false;
      feed_.ifaces[i].rxMbps = 0.0f;
      feed_.ifaces[i].txMbps = 0.0f;
      feed_.ifaces[i].rxPps = 0;
      feed_.ifaces[i].txPps = 0;
      feed_.ifaces[i].peakRxMbps = 0.0f;
      feed_.ifaces[i].peakTxMbps = 0.0f;
      for (int h = 0; h < 12; ++h) {
        feed_.ifaces[i].rxHistory[h] = 0.0f;
        feed_.ifaces[i].txHistory[h] = 0.0f;
      }
    }
    prevIfSampleMs_ = 0;
    queueCount_ = 0;
    pppIfCacheCount_ = 0;
    hsByteCacheCount_ = 0;
    for (int i = 0; i < 32; ++i) {
      pppIfCache_[i] = ByteCacheEntry{};
      hsByteCache_[i] = ByteCacheEntry{};
    }
  }

  void resetToLiveStandby() {
    feed_.connected = false;
    hadLiveConnection_ = false;
    copySafe(feed_.identity, sizeof(feed_.identity), feed_.host);
    copySafe(feed_.boardName, sizeof(feed_.boardName), "RouterOS");
    copySafe(feed_.uptime, sizeof(feed_.uptime), "Offline");
    feed_.cpuLoad = 0;
    feed_.memPercent = 0;
    feed_.tempC = 0;
    feed_.totalPppoe = 0;
    feed_.pppoeCount = 0;
    feed_.totalHotspot = 0;
    feed_.hotspotCount = 0;
    for (int i = 0; i < 4; ++i) {
      feed_.ifaces[i].running = false;
    }
    for (int i = 0; i < 15; ++i) {
      feed_.pppoeUsers[i] = MikrotikClientUser{};
      feed_.hotspotUsers[i] = MikrotikClientUser{};
    }
    clearCountersAndCaches();
  }

  void initDefaultTelemetry() {
    copySafe(feed_.identity, sizeof(feed_.identity), "CCR-DEMO-NOC");
    copySafe(feed_.boardName, sizeof(feed_.boardName), "CCR2004-1G-12S+");
    copySafe(feed_.uptime, sizeof(feed_.uptime), "18d 07:42:19");
    feed_.cpuLoad = 18;
    feed_.memPercent = 32;
    feed_.tempC = 41;

    const float baseRx[4] = {184.6f, 62.3f, 241.8f, 18.4f};
    const float baseTx[4] = {42.1f, 15.8f, 58.9f, 6.2f};
    const uint32_t basePps[4] = {21400, 7800, 28900, 2450};

    for (int i = 0; i < 4; ++i) {
      auto& iface = feed_.ifaces[i];
      iface.running = true;
      iface.rxMbps = baseRx[i];
      iface.txMbps = baseTx[i];
      iface.rxPps = basePps[i];
      iface.txPps = basePps[i] / 3;
      iface.peakRxMbps = baseRx[i] * 1.35f;
      iface.peakTxMbps = baseTx[i] * 1.35f;
      for (int h = 0; h < 12; ++h) {
        const float wave = 0.85f + 0.15f * sinf(static_cast<float>(h + i) * 0.8f);
        iface.rxHistory[h] = baseRx[i] * wave;
        iface.txHistory[h] = baseTx[i] * wave;
      }
    }

    feed_.totalPppoe = 24;
    feed_.pppoeCount = 5;
    const char* pppNames[5] = {"pppoe-rumah01", "pppoe-warung", "pppoe-kantor", "pppoe-kost88", "pppoe-klinik"};
    const char* pppIps[5] = {"10.10.1.12", "10.10.1.24", "10.10.1.35", "10.10.1.48", "10.10.1.61"};
    const char* pppUps[5] = {"4d 11:20", "1d 04:15", "6d 19:02", "08:45:12", "2d 01:33"};
    const float pppRx[5] = {24.8f, 18.2f, 42.5f, 11.4f, 8.9f};
    const float pppTx[5] = {5.2f, 3.8f, 12.1f, 2.4f, 1.9f};
    for (int i = 0; i < 5; ++i) {
      copySafe(feed_.pppoeUsers[i].username, sizeof(feed_.pppoeUsers[i].username), pppNames[i]);
      copySafe(feed_.pppoeUsers[i].address, sizeof(feed_.pppoeUsers[i].address), pppIps[i]);
      copySafe(feed_.pppoeUsers[i].uptime, sizeof(feed_.pppoeUsers[i].uptime), pppUps[i]);
      feed_.pppoeUsers[i].rxMbps = pppRx[i];
      feed_.pppoeUsers[i].txMbps = pppTx[i];
    }

    feed_.totalHotspot = 38;
    feed_.hotspotCount = 5;
    const char* hsNames[5] = {"tamu-vip01", "kasir-cafe", "hs-voucher22", "android-budi", "laptop-rina"};
    const char* hsIps[5] = {"172.16.2.15", "172.16.2.28", "172.16.2.41", "172.16.2.77", "172.16.2.93"};
    const char* hsUps[5] = {"02:14:09", "05:30:44", "00:48:15", "01:12:50", "03:05:18"};
    const float hsRx[5] = {8.4f, 3.2f, 14.6f, 5.1f, 6.8f};
    const float hsTx[5] = {1.6f, 0.7f, 2.9f, 1.1f, 1.4f};
    for (int i = 0; i < 5; ++i) {
      copySafe(feed_.hotspotUsers[i].username, sizeof(feed_.hotspotUsers[i].username), hsNames[i]);
      copySafe(feed_.hotspotUsers[i].address, sizeof(feed_.hotspotUsers[i].address), hsIps[i]);
      copySafe(feed_.hotspotUsers[i].uptime, sizeof(feed_.hotspotUsers[i].uptime), hsUps[i]);
      feed_.hotspotUsers[i].rxMbps = hsRx[i];
      feed_.hotspotUsers[i].txMbps = hsTx[i];
    }
  }

  void parseInterfacesCsv() {
    const char* defaults[4] = {"ether1", "ether2", "ether3", "ether4"};
    char buf[64];
    copySafe(buf, sizeof(buf), feed_.interfaceCsv);

    int idx = 0;
    char* saveptr = nullptr;
    char* tok = strtok_r(buf, ",", &saveptr);
    while (tok && idx < 4) {
      while (*tok == ' ') tok++;
      size_t len = strlen(tok);
      while (len > 0 && tok[len - 1] == ' ') {
        tok[--len] = '\0';
      }
      if (tok[0] != '\0') {
        copySafe(feed_.ifaces[idx].name, sizeof(feed_.ifaces[idx].name), tok);
        copySafe(feed_.ifaces[idx].label, sizeof(feed_.ifaces[idx].label), tok);
        idx++;
      }
      tok = strtok_r(nullptr, ",", &saveptr);
    }
    for (int i = idx; i < 4; ++i) {
      copySafe(feed_.ifaces[i].name, sizeof(feed_.ifaces[i].name), defaults[i]);
      copySafe(feed_.ifaces[i].label, sizeof(feed_.ifaces[i].label), defaults[i]);
    }
  }

  static void pushHistory(MikrotikInterface& iface, float rx, float tx) {
    for (int i = 0; i < 11; ++i) {
      iface.rxHistory[i] = iface.rxHistory[i + 1];
      iface.txHistory[i] = iface.txHistory[i + 1];
    }
    iface.rxHistory[11] = rx;
    iface.txHistory[11] = tx;
    iface.rxMbps = rx;
    iface.txMbps = tx;
    if (rx > iface.peakRxMbps) iface.peakRxMbps = rx;
    if (tx > iface.peakTxMbps) iface.peakTxMbps = tx;
  }

  void animateDemoTelemetry(uint32_t nowMs) {
    demoTickCount_++;
    const float t = static_cast<float>(nowMs) / 1000.0f;
    feed_.cpuLoad = static_cast<uint8_t>(14 + static_cast<int>(8.0f * (0.5f + 0.5f * sinf(t * 0.4f))));
    feed_.memPercent = 31 + (demoTickCount_ % 3);
    feed_.tempC = 40 + (demoTickCount_ % 2);

    const float anchorsRx[4] = {185.0f, 64.0f, 248.0f, 19.0f};
    const float anchorsTx[4] = {44.0f, 16.5f, 61.0f, 6.8f};

    for (int i = 0; i < 4; ++i) {
      const float wobble = sinf(t * 0.7f + i * 1.4f) * 0.18f + cosf(t * 1.3f + i) * 0.08f;
      float rx = anchorsRx[i] * (1.0f + wobble);
      float tx = anchorsTx[i] * (1.0f + wobble * 0.85f);
      if (rx < 0.2f) rx = 0.2f;
      if (tx < 0.1f) tx = 0.1f;
      feed_.ifaces[i].running = true;
      feed_.ifaces[i].rxPps = static_cast<uint32_t>(rx * 115.0f);
      feed_.ifaces[i].txPps = static_cast<uint32_t>(tx * 95.0f);
      pushHistory(feed_.ifaces[i], rx, tx);
    }

    for (int i = 0; i < feed_.pppoeCount && i < 15; ++i) {
      float delta = 0.85f + 0.30f * fabsf(sinf(t * 0.5f + i * 1.1f));
      feed_.pppoeUsers[i].rxMbps = 8.0f + (i * 5.2f) * delta;
      feed_.pppoeUsers[i].txMbps = 1.5f + (i * 1.4f) * delta;
    }
    for (int i = 0; i < feed_.hotspotCount && i < 15; ++i) {
      float delta = 0.80f + 0.40f * fabsf(cosf(t * 0.6f + i * 1.3f));
      feed_.hotspotUsers[i].rxMbps = 2.5f + ((4 - (i % 5)) * 2.4f) * delta;
      feed_.hotspotUsers[i].txMbps = 0.6f + ((4 - (i % 5)) * 0.5f) * delta;
    }
  }

  // ---- MikroTik RouterOS API Binary Protocol (TCP Port 8728) ----

  static void writeApiWord(WiFiClient& client, const char* word) {
    const uint32_t len = word ? static_cast<uint32_t>(strlen(word)) : 0;
    if (len < 0x80) {
      client.write(static_cast<uint8_t>(len));
    } else if (len < 0x4000) {
      const uint32_t enc = len | 0x8000;
      uint8_t b[2] = {static_cast<uint8_t>((enc >> 8) & 0xFF), static_cast<uint8_t>(enc & 0xFF)};
      client.write(b, 2);
    } else {
      const uint32_t enc = len | 0xC00000;
      uint8_t b[3] = {static_cast<uint8_t>((enc >> 16) & 0xFF),
                      static_cast<uint8_t>((enc >> 8) & 0xFF),
                      static_cast<uint8_t>(enc & 0xFF)};
      client.write(b, 3);
    }
    if (len > 0) {
      client.write(reinterpret_cast<const uint8_t*>(word), len);
    }
  }

  static void sendSentence(WiFiClient& client, const char* w0, const char* w1 = nullptr,
                           const char* w2 = nullptr, const char* w3 = nullptr) {
    if (w0) writeApiWord(client, w0);
    if (w1) writeApiWord(client, w1);
    if (w2) writeApiWord(client, w2);
    if (w3) writeApiWord(client, w3);
    client.write(static_cast<uint8_t>(0));
  }

  static int readByteWithTimeout(WiFiClient& client, uint32_t timeoutMs = 400) {
    const uint32_t start = millis();
    while (!client.available()) {
      if (!client.connected() || static_cast<uint32_t>(millis() - start) >= timeoutMs) {
        return -1;
      }
      vTaskDelay(pdMS_TO_TICKS(2));
    }
    return client.read();
  }

  static int readApiWord(WiFiClient& client, char* out, size_t maxCap) {
    int c0 = readByteWithTimeout(client);
    if (c0 < 0) return -1;
    uint32_t len = static_cast<uint32_t>(c0);
    if ((c0 & 0x80) != 0) {
      int c1 = readByteWithTimeout(client);
      if (c1 < 0) return -1;
      if ((c0 & 0xC0) == 0x80) {
        len = ((c0 & 0x3F) << 8) | c1;
      } else if ((c0 & 0xE0) == 0xC0) {
        int c2 = readByteWithTimeout(client);
        if (c2 < 0) return -1;
        len = ((c0 & 0x1F) << 16) | (c1 << 8) | c2;
      } else {
        int c2 = readByteWithTimeout(client);
        int c3 = readByteWithTimeout(client);
        if (c2 < 0 || c3 < 0) return -1;
        len = ((c0 & 0x0F) << 24) | (c1 << 16) | (c2 << 8) | c3;
      }
    }

    size_t written = 0;
    for (uint32_t i = 0; i < len; ++i) {
      int ch = readByteWithTimeout(client);
      if (ch < 0) return -1;
      if (out && written + 1 < maxCap) {
        out[written++] = static_cast<char>(ch);
      }
    }
    if (out && maxCap > 0) out[written] = '\0';
    return static_cast<int>(len);
  }

  static uint8_t hexNibble(char c) {
    if (c >= '0' && c <= '9') return static_cast<uint8_t>(c - '0');
    if (c >= 'a' && c <= 'f') return static_cast<uint8_t>(10 + (c - 'a'));
    if (c >= 'A' && c <= 'F') return static_cast<uint8_t>(10 + (c - 'A'));
    return 0;
  }

  bool apiLogin(WiFiClient& client) {
    char wUser[40];
    char wPass[48];
    snprintf(wUser, sizeof(wUser), "=name=%s", feed_.user);
    snprintf(wPass, sizeof(wPass), "=password=%s", feed_.pass);
    sendSentence(client, "/login", wUser, wPass);

    char word[128];
    char retHex[40] = "";
    bool ok = false;
    while (true) {
      int len = readApiWord(client, word, sizeof(word));
      if (len < 0) return false;
      if (len == 0) {
        if (ok) break;
        continue;
      }
      if (strcmp(word, "!done") == 0) ok = true;
      else if (strcmp(word, "!trap") == 0 || strcmp(word, "!fatal") == 0) return false;
      else if (startsWith(word, "=ret=")) copySafe(retHex, sizeof(retHex), word + 5);
    }

    // RouterOS v6.43+ & v7 complete on first !done with empty retHex
    if (retHex[0] == '\0') {
      return true;
    }

    // Legacy RouterOS (<6.43) MD5 challenge-response
    if (strlen(retHex) != 32) return false;
    uint8_t challenge[16];
    for (int i = 0; i < 16; ++i) {
      challenge[i] = static_cast<uint8_t>((hexNibble(retHex[i * 2]) << 4) |
                                          hexNibble(retHex[i * 2 + 1]));
    }
    MD5Builder md5;
    md5.begin();
    uint8_t zero = 0x00;
    uint8_t passBuf[32];
    const size_t passLen = strlen(feed_.pass);
    const size_t safePassLen = (passLen < sizeof(passBuf)) ? passLen : sizeof(passBuf);
    memcpy(passBuf, feed_.pass, safePassLen);
    md5.add(&zero, 1);
    md5.add(passBuf, static_cast<uint16_t>(safePassLen));
    md5.add(challenge, 16);
    md5.calculate();

    char wResp[48];
    snprintf(wResp, sizeof(wResp), "=response=00%s", md5.toString().c_str());
    sendSentence(client, "/login", wUser, wResp);

    bool ok2 = false;
    while (true) {
      int len = readApiWord(client, word, sizeof(word));
      if (len < 0) return false;
      if (len == 0) {
        if (ok2) return true;
        continue;
      }
      if (strcmp(word, "!done") == 0) ok2 = true;
      else if (strcmp(word, "!trap") == 0 || strcmp(word, "!fatal") == 0) return false;
    }
  }

  bool querySystemIdentity(WiFiClient& client) {
    sendSentence(client, "/system/identity/print");
    char word[96];
    bool done = false;
    bool hadErr = false;
    while (true) {
      int len = readApiWord(client, word, sizeof(word));
      if (len < 0) return false;
      if (len == 0) {
        if (done) break;
        continue;
      }
      if (strcmp(word, "!done") == 0) done = true;
      else if (strcmp(word, "!trap") == 0) {
        hadErr = true;
        done = true;
      } else if (startsWith(word, "=name=")) {
        copySafe(feed_.identity, sizeof(feed_.identity), word + 6);
      }
    }
    return !hadErr || done;
  }

  bool querySystemResource(WiFiClient& client) {
    sendSentence(client, "/system/resource/print", "=.proplist=uptime,cpu-load,free-memory,total-memory,board-name");
    char word[160];
    uint64_t freeMem = 0, totalMem = 0;
    bool done = false;
    while (true) {
      int len = readApiWord(client, word, sizeof(word));
      if (len < 0) return false;
      if (len == 0) {
        if (done) break;
        continue;
      }
      if (strcmp(word, "!done") == 0 || strcmp(word, "!trap") == 0) done = true;
      else if (startsWith(word, "=uptime=")) copySafe(feed_.uptime, sizeof(feed_.uptime), word + 8);
      else if (startsWith(word, "=board-name=")) copySafe(feed_.boardName, sizeof(feed_.boardName), word + 12);
      else if (startsWith(word, "=cpu-load=")) feed_.cpuLoad = static_cast<uint8_t>(atoi(word + 10));
      else if (startsWith(word, "=free-memory=")) freeMem = strtoull(word + 13, nullptr, 10);
      else if (startsWith(word, "=total-memory=")) totalMem = strtoull(word + 14, nullptr, 10);
    }
    if (totalMem > 0 && totalMem >= freeMem) {
      feed_.memPercent = static_cast<uint8_t>(((totalMem - freeMem) * 100ULL) / totalMem);
    }
    return true;
  }

  void updatePppByteCache(const char* username, uint64_t rxBytes, uint64_t txBytes, uint32_t nowMs) {
    if (!username || username[0] == '\0') return;
    int slot = -1;
    for (int i = 0; i < pppIfCacheCount_; ++i) {
      if (strcmp(pppIfCache_[i].key, username) == 0) {
        slot = i;
        break;
      }
    }
    if (slot < 0) {
      if (pppIfCacheCount_ < 32) {
        slot = pppIfCacheCount_++;
      } else {
        // Reuse slot with oldest timestamp
        slot = 0;
        for (int i = 1; i < 32; ++i) {
          if (pppIfCache_[i].lastMs < pppIfCache_[slot].lastMs) slot = i;
        }
      }
      pppIfCache_[slot] = ByteCacheEntry{};
      copySafe(pppIfCache_[slot].key, sizeof(pppIfCache_[slot].key), username);
    }

    auto& e = pppIfCache_[slot];
    if (e.valid && nowMs > e.lastMs) {
      const uint32_t dtMs = nowMs - e.lastMs;
      if (dtMs >= 200) {
        // RouterOS pppoe-in TX-byte = Download to subscriber; RX-byte = Upload from subscriber
        e.rxMbps = (txBytes >= e.txBytes)
                       ? (static_cast<float>(txBytes - e.txBytes) * 8.0f) / (static_cast<float>(dtMs) * 1000.0f)
                       : 0.0f;
        e.txMbps = (rxBytes >= e.rxBytes)
                       ? (static_cast<float>(rxBytes - e.rxBytes) * 8.0f) / (static_cast<float>(dtMs) * 1000.0f)
                       : 0.0f;
      }
    }
    e.rxBytes = rxBytes;
    e.txBytes = txBytes;
    e.lastMs = nowMs;
    e.valid = true;
  }

  bool queryInterfaces(WiFiClient& client, uint32_t nowMs) {
    sendSentence(client, "/interface/print",
                 "=.proplist=name,default-name,type,running,rx-byte,tx-byte,rx-packet,tx-packet");
    char word[160];
    char curName[28] = "";
    char curDefaultName[28] = "";
    char curType[20] = "";
    bool curRun = false;
    uint64_t curRxB = 0, curTxB = 0;
    uint64_t curRxP = 0, curTxP = 0;
    bool inRe = false;
    bool done = false;

    const uint32_t dtMs = (prevIfSampleMs_ > 0 && nowMs > prevIfSampleMs_) ? (nowMs - prevIfSampleMs_) : 0;

    while (true) {
      int len = readApiWord(client, word, sizeof(word));
      if (len < 0) return false;
      if (len == 0) {
        if (inRe && curName[0]) {
          // 1. Match configured physical/logical interfaces (supports renamed ports via default-name & prefix)
          for (int i = 0; i < 4; ++i) {
            const char* cfg = feed_.ifaces[i].name;
            if (cfg[0] == '\0') continue;
            const bool exactName = (strcasecmp(curName, cfg) == 0);
            const bool exactDefault = (curDefaultName[0] != '\0' && strcasecmp(curDefaultName, cfg) == 0);
            const bool prefixMatch = ciStartsWith(curName, cfg);
            if (exactName || exactDefault || prefixMatch) {
              feed_.ifaces[i].running = curRun;
              copySafe(feed_.ifaces[i].label, sizeof(feed_.ifaces[i].label), curName);

              if (hasPrevIfSample_[i] && dtMs >= 200) {
                const float rxM = (curRxB >= prevIfRxBytes_[i])
                                      ? (static_cast<float>(curRxB - prevIfRxBytes_[i]) * 8.0f) /
                                            (static_cast<float>(dtMs) * 1000.0f)
                                      : 0.0f;
                const float txM = (curTxB >= prevIfTxBytes_[i])
                                      ? (static_cast<float>(curTxB - prevIfTxBytes_[i]) * 8.0f) /
                                            (static_cast<float>(dtMs) * 1000.0f)
                                      : 0.0f;
                feed_.ifaces[i].rxPps = (curRxP >= prevIfRxPkts_[i])
                                            ? static_cast<uint32_t>(((curRxP - prevIfRxPkts_[i]) * 1000ULL) / dtMs)
                                            : 0;
                feed_.ifaces[i].txPps = (curTxP >= prevIfTxPkts_[i])
                                            ? static_cast<uint32_t>(((curTxP - prevIfTxPkts_[i]) * 1000ULL) / dtMs)
                                            : 0;
                pushHistory(feed_.ifaces[i], rxM, txM);
              }
              prevIfRxBytes_[i] = curRxB;
              prevIfTxBytes_[i] = curTxB;
              prevIfRxPkts_[i] = curRxP;
              prevIfTxPkts_[i] = curTxP;
              hasPrevIfSample_[i] = true;
            }
          }

          // 2. Capture dynamic PPPoE subscriber interfaces (<pppoe-user> or type=pppoe-in)
          if (startsWith(curName, "<pppoe-") || strcmp(curType, "pppoe-in") == 0) {
            char pppUser[24] = "";
            if (startsWith(curName, "<pppoe-")) {
              copySafe(pppUser, sizeof(pppUser), curName + 7);
              size_t uLen = strlen(pppUser);
              if (uLen > 0 && pppUser[uLen - 1] == '>') {
                pppUser[uLen - 1] = '\0';
              }
            } else {
              copySafe(pppUser, sizeof(pppUser), curName);
            }
            updatePppByteCache(pppUser, curRxB, curTxB, nowMs);
          }
        }
        inRe = false;
        curName[0] = '\0';
        curDefaultName[0] = '\0';
        curType[0] = '\0';
        curRun = false;
        curRxB = 0;
        curTxB = 0;
        curRxP = 0;
        curTxP = 0;
        if (done) break;
        continue;
      }

      if (strcmp(word, "!re") == 0) {
        inRe = true;
      } else if (strcmp(word, "!done") == 0 || strcmp(word, "!trap") == 0) {
        done = true;
      } else if (inRe) {
        if (startsWith(word, "=name=")) copySafe(curName, sizeof(curName), word + 6);
        else if (startsWith(word, "=default-name=")) copySafe(curDefaultName, sizeof(curDefaultName), word + 14);
        else if (startsWith(word, "=type=")) copySafe(curType, sizeof(curType), word + 6);
        else if (startsWith(word, "=running=")) curRun = (strcmp(word + 9, "true") == 0);
        else if (startsWith(word, "=rx-byte=")) curRxB = strtoull(word + 9, nullptr, 10);
        else if (startsWith(word, "=tx-byte=")) curTxB = strtoull(word + 9, nullptr, 10);
        else if (startsWith(word, "=rx-packet=")) curRxP = strtoull(word + 11, nullptr, 10);
        else if (startsWith(word, "=tx-packet=")) curTxP = strtoull(word + 11, nullptr, 10);
      }
    }

    prevIfSampleMs_ = nowMs;
    return true;
  }

  bool querySimpleQueues(WiFiClient& client) {
    sendSentence(client, "/queue/simple/print", "=.proplist=name,target,rate");
    char word[160];
    queueCount_ = 0;
    SimpleQueueEntry cur;
    bool inRe = false;
    bool done = false;

    while (true) {
      int len = readApiWord(client, word, sizeof(word));
      if (len < 0) return false;
      if (len == 0) {
        if (inRe && (cur.name[0] || cur.targetIp[0]) && queueCount_ < 48) {
          queueCache_[queueCount_++] = cur;
        }
        inRe = false;
        cur = SimpleQueueEntry{};
        if (done) break;
        continue;
      }

      if (strcmp(word, "!re") == 0) {
        inRe = true;
      } else if (strcmp(word, "!done") == 0 || strcmp(word, "!trap") == 0) {
        done = true;
      } else if (inRe) {
        if (startsWith(word, "=name=")) {
          const char* rawName = word + 6;
          if (startsWith(rawName, "<pppoe-")) {
            copySafe(cur.name, sizeof(cur.name), rawName + 7);
            size_t nLen = strlen(cur.name);
            if (nLen > 0 && cur.name[nLen - 1] == '>') cur.name[nLen - 1] = '\0';
          } else if (startsWith(rawName, "<hotspot-")) {
            copySafe(cur.name, sizeof(cur.name), rawName + 9);
            size_t nLen = strlen(cur.name);
            if (nLen > 0 && cur.name[nLen - 1] == '>') cur.name[nLen - 1] = '\0';
          } else {
            copySafe(cur.name, sizeof(cur.name), rawName);
          }
        } else if (startsWith(word, "=target=")) {
          copySafe(cur.targetIp, sizeof(cur.targetIp), word + 8);
          char* slash = strchr(cur.targetIp, '/');
          if (slash) *slash = '\0';
        } else if (startsWith(word, "=rate=")) {
          // RouterOS =rate= format: "<upload_bps>/<download_bps>"
          const char* rateStr = word + 6;
          const char* slash = strchr(rateStr, '/');
          if (slash) {
            cur.txMbps = static_cast<float>(strtoull(rateStr, nullptr, 10)) / 1000000.0f;
            cur.rxMbps = static_cast<float>(strtoull(slash + 1, nullptr, 10)) / 1000000.0f;
          }
        }
      }
    }
    return true;
  }

  bool findQueueRate(const char* username, const char* address, float& outRxMbps, float& outTxMbps) const {
    for (int i = 0; i < queueCount_; ++i) {
      const bool matchIp = (address && address[0] && queueCache_[i].targetIp[0] &&
                            strcmp(queueCache_[i].targetIp, address) == 0);
      const bool matchName = (username && username[0] && queueCache_[i].name[0] &&
                              strcmp(queueCache_[i].name, username) == 0);
      if (matchIp || matchName) {
        outRxMbps = queueCache_[i].rxMbps;
        outTxMbps = queueCache_[i].txMbps;
        return true;
      }
    }
    return false;
  }

  static void sortUsersByTraffic(MikrotikClientUser* arr, int count) {
    for (int i = 0; i < count - 1; ++i) {
      int best = i;
      float bestTotal = arr[i].rxMbps + arr[i].txMbps;
      for (int j = i + 1; j < count; ++j) {
        const float tot = arr[j].rxMbps + arr[j].txMbps;
        if (tot > bestTotal) {
          best = j;
          bestTotal = tot;
        }
      }
      if (best != i) {
        MikrotikClientUser tmp = arr[i];
        arr[i] = arr[best];
        arr[best] = tmp;
      }
    }
  }

  bool queryPppoeActive(WiFiClient& client) {
    sendSentence(client, "/ppp/active/print", "=.proplist=name,address,uptime");
    char word[128];
    int total = 0;
    int collected = 0;
    MikrotikClientUser cur;
    bool inRe = false;
    bool done = false;

    while (true) {
      int len = readApiWord(client, word, sizeof(word));
      if (len < 0) return false;
      if (len == 0) {
        if (inRe && cur.username[0]) {
          total++;
          if (collected < 32) {
            bool gotRate = false;
            for (int c = 0; c < pppIfCacheCount_; ++c) {
              if (strcmp(pppIfCache_[c].key, cur.username) == 0) {
                cur.rxMbps = pppIfCache_[c].rxMbps;
                cur.txMbps = pppIfCache_[c].txMbps;
                gotRate = true;
                break;
              }
            }
            if (!gotRate || (cur.rxMbps == 0.0f && cur.txMbps == 0.0f)) {
              float qRx = 0.0f, qTx = 0.0f;
              if (findQueueRate(cur.username, cur.address, qRx, qTx)) {
                cur.rxMbps = qRx;
                cur.txMbps = qTx;
              }
            }
            sortScratch_[collected++] = cur;
          }
        }
        inRe = false;
        cur = MikrotikClientUser{};
        if (done) break;
        continue;
      }

      if (strcmp(word, "!re") == 0) inRe = true;
      else if (strcmp(word, "!done") == 0 || strcmp(word, "!trap") == 0) done = true;
      else if (inRe) {
        if (startsWith(word, "=name=")) copySafe(cur.username, sizeof(cur.username), word + 6);
        else if (startsWith(word, "=address=")) copySafe(cur.address, sizeof(cur.address), word + 9);
        else if (startsWith(word, "=uptime=")) copySafe(cur.uptime, sizeof(cur.uptime), word + 8);
      }
    }

    sortUsersByTraffic(sortScratch_, collected);
    feed_.totalPppoe = total;
    const int stored = (collected > 15) ? 15 : collected;
    feed_.pppoeCount = stored;
    for (int i = 0; i < stored; ++i) {
      feed_.pppoeUsers[i] = sortScratch_[i];
    }
    for (int i = stored; i < 15; ++i) {
      feed_.pppoeUsers[i] = MikrotikClientUser{};
    }
    return true;
  }

  void computeHotspotDelta(const char* key, uint64_t bytesIn, uint64_t bytesOut,
                           uint32_t nowMs, float& outRxMbps, float& outTxMbps) {
    outRxMbps = 0.0f;
    outTxMbps = 0.0f;
    if (!key || key[0] == '\0') return;

    int slot = -1;
    for (int i = 0; i < hsByteCacheCount_; ++i) {
      if (strcmp(hsByteCache_[i].key, key) == 0) {
        slot = i;
        break;
      }
    }
    if (slot < 0) {
      if (hsByteCacheCount_ < 32) {
        slot = hsByteCacheCount_++;
      } else {
        slot = 0;
        for (int i = 1; i < 32; ++i) {
          if (hsByteCache_[i].lastMs < hsByteCache_[slot].lastMs) slot = i;
        }
      }
      hsByteCache_[slot] = ByteCacheEntry{};
      copySafe(hsByteCache_[slot].key, sizeof(hsByteCache_[slot].key), key);
    }

    auto& e = hsByteCache_[slot];
    if (e.valid && nowMs > e.lastMs) {
      const uint32_t dtMs = nowMs - e.lastMs;
      if (dtMs >= 200) {
        // Hotspot: bytes-out = bytes sent to user (Download), bytes-in = bytes uploaded by user
        outRxMbps = (bytesOut >= e.txBytes)
                        ? (static_cast<float>(bytesOut - e.txBytes) * 8.0f) / (static_cast<float>(dtMs) * 1000.0f)
                        : 0.0f;
        outTxMbps = (bytesIn >= e.rxBytes)
                        ? (static_cast<float>(bytesIn - e.rxBytes) * 8.0f) / (static_cast<float>(dtMs) * 1000.0f)
                        : 0.0f;
      }
    }
    e.rxBytes = bytesIn;
    e.txBytes = bytesOut;
    e.lastMs = nowMs;
    e.valid = true;
  }

  bool queryHotspotActive(WiFiClient& client, uint32_t nowMs) {
    sendSentence(client, "/ip/hotspot/active/print",
                 "=.proplist=user,name,address,uptime,bytes-in,bytes-out");
    char word[128];
    int total = 0;
    int collected = 0;
    MikrotikClientUser cur;
    uint64_t curIn = 0, curOut = 0;
    bool inRe = false;
    bool done = false;

    while (true) {
      int len = readApiWord(client, word, sizeof(word));
      if (len < 0) return false;
      if (len == 0) {
        if (inRe && (cur.username[0] || cur.address[0])) {
          total++;
          if (collected < 32) {
            float dRx = 0.0f, dTx = 0.0f;
            const char* cacheKey = cur.username[0] ? cur.username : cur.address;
            computeHotspotDelta(cacheKey, curIn, curOut, nowMs, dRx, dTx);

            float qRx = 0.0f, qTx = 0.0f;
            if (findQueueRate(cur.username, cur.address, qRx, qTx) && (qRx > 0.0f || qTx > 0.0f)) {
              cur.rxMbps = qRx;
              cur.txMbps = qTx;
            } else {
              cur.rxMbps = dRx;
              cur.txMbps = dTx;
            }
            sortScratch_[collected++] = cur;
          }
        }
        inRe = false;
        cur = MikrotikClientUser{};
        curIn = 0;
        curOut = 0;
        if (done) break;
        continue;
      }

      if (strcmp(word, "!re") == 0) inRe = true;
      else if (strcmp(word, "!done") == 0 || strcmp(word, "!trap") == 0) done = true;
      else if (inRe) {
        if (startsWith(word, "=user=")) copySafe(cur.username, sizeof(cur.username), word + 6);
        else if (startsWith(word, "=name=") && cur.username[0] == '\0') {
          copySafe(cur.username, sizeof(cur.username), word + 6);
        } else if (startsWith(word, "=address=")) copySafe(cur.address, sizeof(cur.address), word + 9);
        else if (startsWith(word, "=uptime=")) copySafe(cur.uptime, sizeof(cur.uptime), word + 8);
        else if (startsWith(word, "=bytes-in=")) curIn = strtoull(word + 10, nullptr, 10);
        else if (startsWith(word, "=bytes-out=")) curOut = strtoull(word + 11, nullptr, 10);
      }
    }

    sortUsersByTraffic(sortScratch_, collected);
    feed_.totalHotspot = total;
    const int stored = (collected > 15) ? 15 : collected;
    feed_.hotspotCount = stored;
    for (int i = 0; i < stored; ++i) {
      feed_.hotspotUsers[i] = sortScratch_[i];
    }
    for (int i = stored; i < 15; ++i) {
      feed_.hotspotUsers[i] = MikrotikClientUser{};
    }
    return true;
  }

  bool pollRouterOs(uint32_t nowMs) {
    if (!apiClient_.connected() || !loggedIn_) {
      disconnectApi();
      if (!apiClient_.connect(feed_.host, feed_.port, 800)) {
        return false;
      }
      apiClient_.setNoDelay(true);
      if (!apiLogin(apiClient_)) {
        disconnectApi();
        return false;
      }
      loggedIn_ = true;
      if (!hadLiveConnection_) {
        hadLiveConnection_ = true;
        clearCountersAndCaches();
        feed_.totalPppoe = 0;
        feed_.pppoeCount = 0;
        feed_.totalHotspot = 0;
        feed_.hotspotCount = 0;
      }
    }

    // Drain any stray bytes before issuing new synchronous queries
    while (apiClient_.available()) {
      apiClient_.read();
    }

    feed_.connected = true;
    if (!querySystemIdentity(apiClient_)) return false;
    vTaskDelay(pdMS_TO_TICKS(5));
    if (!querySystemResource(apiClient_)) return false;
    vTaskDelay(pdMS_TO_TICKS(5));
    if (!queryInterfaces(apiClient_, nowMs)) return false;
    vTaskDelay(pdMS_TO_TICKS(5));
    if (!querySimpleQueues(apiClient_)) return false;
    vTaskDelay(pdMS_TO_TICKS(5));
    if (!queryPppoeActive(apiClient_)) return false;
    vTaskDelay(pdMS_TO_TICKS(5));
    if (!queryHotspotActive(apiClient_, nowMs)) return false;
    return true;
  }
};

}  // namespace jws
