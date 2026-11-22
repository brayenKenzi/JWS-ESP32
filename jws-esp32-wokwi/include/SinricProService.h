// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <string.h>
#include "RelayControlService.h"

#ifdef JWS_DESKTOP_S3
#include <SinricPro.h>
#include <SinricProSwitch.h>
#endif

namespace jws {

struct SinricStatus {
  bool enabled = false;
  bool configured = false;
  bool connected = false;
  bool hasAppKey = false;
  bool hasAppSecret = false;
  char deviceId1[32] = "";
  char deviceId2[32] = "";
};

class SinricProService {
 public:
  static SinricProService& instance() {
    static SinricProService svc;
    return svc;
  }

  void begin() {
    if (initialized_) return;
    initialized_ = true;

    prefs_.begin("jws_sinric", false);
    enabled_ = prefs_.getBool("enabled", false);
    copySafe(appKey_, sizeof(appKey_), prefs_.getString("app_key", "").c_str());
    copySafe(appSecret_, sizeof(appSecret_), prefs_.getString("app_sec", "").c_str());
    copySafe(deviceId1_, sizeof(deviceId1_), prefs_.getString("dev1", "").c_str());
    copySafe(deviceId2_, sizeof(deviceId2_), prefs_.getString("dev2", "").c_str());

    RelayControlService::instance().setChangeCallback(&SinricProService::onLocalRelayChanged);
    syncRelaySinricState();
    Serial.printf("[SINRIC] Loaded config: enabled=%d configured=%d dev1='%s' dev2='%s'\n",
                  enabled_ ? 1 : 0, isConfigured() ? 1 : 0, deviceId1_, deviceId2_);
  }

  void tick() {
#ifdef JWS_DESKTOP_S3
    if (!enabled_ || !isConfigured()) return;
    if (WiFi.status() != WL_CONNECTED) {
      if (connected_) {
        connected_ = false;
        syncRelaySinricState();
      }
      return;
    }

    if (!started_) {
      startClient();
    }
    if (started_) {
      SinricPro.handle();
    }
#endif
  }

  void configure(bool enabled,
                 const char* appKey,
                 const char* appSecret,
                 const char* dev1,
                 const char* dev2,
                 bool clearSecrets = false) {
    enabled_ = enabled;
    if (clearSecrets) {
      appKey_[0] = '\0';
      appSecret_[0] = '\0';
      deviceId1_[0] = '\0';
      deviceId2_[0] = '\0';
    } else {
      if (appKey && appKey[0] != '\0') copySafe(appKey_, sizeof(appKey_), appKey);
      if (appSecret && appSecret[0] != '\0') copySafe(appSecret_, sizeof(appSecret_), appSecret);
      if (dev1 != nullptr) copySafe(deviceId1_, sizeof(deviceId1_), dev1);
      if (dev2 != nullptr) copySafe(deviceId2_, sizeof(deviceId2_), dev2);
    }

    prefs_.putBool("enabled", enabled_);
    prefs_.putString("app_key", appKey_);
    prefs_.putString("app_sec", appSecret_);
    prefs_.putString("dev1", deviceId1_);
    prefs_.putString("dev2", deviceId2_);

#ifdef JWS_DESKTOP_S3
    if (started_) {
      SinricPro.stop();
      started_ = false;
      connected_ = false;
    }
    if (enabled_ && isConfigured() && WiFi.status() == WL_CONNECTED) {
      startClient();
    }
#endif
    syncRelaySinricState();
  }

  SinricStatus status() const {
    SinricStatus s;
    s.enabled = enabled_;
    s.configured = isConfigured();
    s.connected = connected_;
    s.hasAppKey = (appKey_[0] != '\0');
    s.hasAppSecret = (appSecret_[0] != '\0');
    copySafe(s.deviceId1, sizeof(s.deviceId1), deviceId1_);
    copySafe(s.deviceId2, sizeof(s.deviceId2), deviceId2_);
    return s;
  }

 private:
  SinricProService() = default;

  bool isConfigured() const {
    return appKey_[0] != '\0' && appSecret_[0] != '\0' &&
           (deviceId1_[0] != '\0' || deviceId2_[0] != '\0');
  }

  void syncRelaySinricState() const {
    RelayControlService::instance().setSinricState(
        enabled_ && isConfigured(), connected_, deviceId1_[0] != '\0', deviceId2_[0] != '\0');
  }

  static void copySafe(char* dst, size_t cap, const char* src) {
    if (!dst || cap == 0) return;
    if (!src) {
      dst[0] = '\0';
      return;
    }
    strncpy(dst, src, cap - 1);
    dst[cap - 1] = '\0';
  }

  static void onLocalRelayChanged(uint8_t channel, bool on) {
#ifdef JWS_DESKTOP_S3
    auto& self = SinricProService::instance();
    if (!self.started_ || !self.connected_) return;
    if (channel == 1 && self.deviceId1_[0] != '\0') {
      SinricProSwitch& sw1 = SinricPro[self.deviceId1_];
      sw1.sendPowerStateEvent(on);
    } else if (channel == 2 && self.deviceId2_[0] != '\0') {
      SinricProSwitch& sw2 = SinricPro[self.deviceId2_];
      sw2.sendPowerStateEvent(on);
    }
#else
    (void)channel;
    (void)on;
#endif
  }

#ifdef JWS_DESKTOP_S3
  void startClient() {
    if (started_ || !isConfigured()) return;

    if (deviceId1_[0] != '\0') {
      SinricProSwitch& sw1 = SinricPro[deviceId1_];
      sw1.onPowerState([](const String& deviceId, bool& state) -> bool {
        (void)deviceId;
        RelayControlService::instance().setRelay1(state, false);
        Serial.printf("[SINRIC] Google Home -> Relay 1 = %s\n", state ? "ON" : "OFF");
        return true;
      });
    }

    if (deviceId2_[0] != '\0') {
      SinricProSwitch& sw2 = SinricPro[deviceId2_];
      sw2.onPowerState([](const String& deviceId, bool& state) -> bool {
        (void)deviceId;
        RelayControlService::instance().setRelay2(state, false);
        Serial.printf("[SINRIC] Google Home -> Relay 2 = %s\n", state ? "ON" : "OFF");
        return true;
      });
    }

    SinricPro.onConnected([this]() {
      connected_ = true;
      syncRelaySinricState();
      Serial.println("[SINRIC] Connected to Sinric Pro (Google Home Ready!)");
    });
    SinricPro.onDisconnected([this]() {
      connected_ = false;
      syncRelaySinricState();
      Serial.println("[SINRIC] Disconnected from Sinric Pro");
    });

    SinricPro.restoreDeviceStates(false);
    SinricPro.begin(appKey_, appSecret_);
    started_ = true;
    Serial.println("[SINRIC] Client started, connecting to Sinric Pro cloud...");
  }
#endif

  Preferences prefs_;
  bool initialized_ = false;
  bool enabled_ = false;
  bool started_ = false;
  bool connected_ = false;
  char appKey_[64] = "";
  char appSecret_[96] = "";
  char deviceId1_[32] = "";
  char deviceId2_[32] = "";
};

}  // namespace jws
