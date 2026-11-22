// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <Interfaces.h>

// Initial defaults; dashboard changes are saved in simulated ESP32 NVS.
inline jws::Configuration defaultConfiguration() {
  jws::Configuration c;
  c.calculation.latitude = 0.7373333333;    // Kemenag reference point for Kota Kotamobagu
  c.calculation.longitude = 124.3166527778;
  c.calculation.utcOffsetMinutes = 480;  // WITA / UTC+08:00, no DST
  c.calculation.fajrAngle = 20;
  c.calculation.ishaAngle = 18;
  c.calculation.ihtiyatMinutes = 0;  // Official tables receive only explicit per-prayer corrections.
  c.calculation.imsakMinutes = 10;
  c.calculation.asrShadowFactor = 1;
  return c;
}
