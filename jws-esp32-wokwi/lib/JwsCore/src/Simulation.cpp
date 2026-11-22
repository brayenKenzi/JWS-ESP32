// SPDX-License-Identifier: GPL-3.0-only
#include "Simulation.h"
#include <time.h>

namespace jws {
void MockClockProvider::begin(const Configuration&) {
  localSeconds_ = 1793750390LL;  // 2026-11-03 23:59:50 encoded as UTC for local arithmetic.
  previousMs_ = 0;
  remainingMs_ = 0;
}
void MockClockProvider::tick(uint32_t nowMs) {
  const uint64_t elapsed = static_cast<uint32_t>(nowMs - previousMs_) + static_cast<uint64_t>(remainingMs_);
  previousMs_ = nowMs;
  localSeconds_ += elapsed / 1000;
  remainingMs_ = elapsed % 1000;
}
ClockReading MockClockProvider::now() const {
  const time_t epoch = static_cast<time_t>(localSeconds_);
  struct tm local{};
#ifdef _WIN32
  gmtime_s(&local, &epoch);
#else
  gmtime_r(&epoch, &local);
#endif
  ClockReading reading;
  reading.date = {local.tm_year + 1900, local.tm_mon + 1, local.tm_mday};
  reading.hour = local.tm_hour;
  reading.minute = local.tm_min;
  reading.second = local.tm_sec;
  reading.source = ClockSource::Mock;
  reading.valid = true;
  return reading;
}
}  // namespace jws
