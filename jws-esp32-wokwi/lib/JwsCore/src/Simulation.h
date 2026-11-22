// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "Interfaces.h"

namespace jws {
// RAM only: deliberately does not pretend to survive a restart.
class MemoryConfigurationStore final : public ConfigurationStore {
 public:
  explicit MemoryConfigurationStore(Configuration initial = {}) : value_(initial) {}
  bool load(Configuration& config) override { config = value_; return validConfiguration(config); }
  bool save(const Configuration& config) override {
    if (!validConfiguration(config)) return false;
    value_ = config;
    return true;
  }
 private:
  Configuration value_;
};
// Deterministic local wall clock; starts at 2026-11-03 23:59:50.
// Epoch is advanced with unsigned deltas so millis() wrap is supported.
class MockClockProvider final : public ClockProvider {
 public:
  void begin(const Configuration& configuration) override;
  void tick(uint32_t nowMs) override;
  ClockReading now() const override;
 private:
  int64_t localSeconds_ = 0;
  uint32_t previousMs_ = 0;
  uint32_t remainingMs_ = 0;
};
}  // namespace jws
