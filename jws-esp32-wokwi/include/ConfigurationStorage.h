// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include <Interfaces.h>
#include <string.h>

class PersistentConfigurationStore final : public jws::ConfigurationStore {
 public:
  explicit PersistentConfigurationStore(const jws::Configuration& initial)
      : fallback_(initial), current_(initial) {}

  bool begin() {
    ready_ = prefs_.begin("jws_cfg", false);
    if (!ready_) {
      current_ = fallback_;
      return false;
    }
    StoredPayload payload{};
    const size_t readLen = prefs_.getBytes("cfg_v1", &payload, sizeof(payload));
    if (readLen == sizeof(payload) && payload.magic == Magic &&
        payload.checksum == computeChecksum(payload.config) &&
        jws::validConfiguration(payload.config)) {
      current_ = payload.config;
      return true;
    }
    current_ = fallback_;
    return saveToNvs(current_);
  }

  bool load(jws::Configuration& configuration) override {
    if (!jws::validConfiguration(current_)) {
      if (!jws::validConfiguration(fallback_)) return false;
      current_ = fallback_;
    }
    configuration = current_;
    return true;
  }

  bool save(const jws::Configuration& configuration) override {
    if (!jws::validConfiguration(configuration) || !ready_) return false;
    if (!saveToNvs(configuration)) return false;
    current_ = configuration;
    return true;
  }

 private:
  static constexpr uint32_t Magic = 0x4A575333UL;  // "JWS3"
  struct StoredPayload {
    uint32_t magic = Magic;
    uint32_t checksum = 0;
    jws::Configuration config{};
  };

  static uint32_t computeChecksum(const jws::Configuration& cfg) {
    const auto* bytes = reinterpret_cast<const uint8_t*>(&cfg);
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < sizeof(jws::Configuration); ++i) {
      hash ^= bytes[i];
      hash *= 16777619u;
    }
    return hash;
  }

  bool saveToNvs(const jws::Configuration& configuration) {
    if (!ready_) return false;
    StoredPayload payload{};
    payload.magic = Magic;
    payload.checksum = computeChecksum(configuration);
    payload.config = configuration;
    return prefs_.putBytes("cfg_v1", &payload, sizeof(payload)) == sizeof(payload);
  }

  Preferences prefs_;
  jws::Configuration fallback_;
  jws::Configuration current_;
  bool ready_ = false;
};
