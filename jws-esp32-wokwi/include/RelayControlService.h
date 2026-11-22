// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <Arduino.h>
#include <stdint.h>

namespace jws {

struct RelaySnapshot {
  bool relay1 = false; // GPIO 15, Active-HIGH (HIGH = ON)
  bool relay2 = false; // GPIO 16, Active-LOW  (LOW  = ON)
  bool sinricConfigured = false;
  bool sinricConnected = false;
  bool hasDev1 = false;
  bool hasDev2 = false;
};

class RelayControlService {
 public:
  static constexpr int PinRelay1 = 15; // Active-HIGH
  static constexpr int PinRelay2 = 16; // Active-LOW

  static RelayControlService& instance() {
    static RelayControlService svc;
    return svc;
  }

  void begin() {
    if (initialized_) return;
    initialized_ = true;
#ifdef JWS_DESKTOP_S3
    // Pre-write safe OFF levels BEFORE switching to OUTPUT so Active-LOW Relay 2 never clicks on boot!
    digitalWrite(PinRelay1, LOW);
    pinMode(PinRelay1, OUTPUT);

    digitalWrite(PinRelay2, HIGH);
    pinMode(PinRelay2, OUTPUT);

    Serial.println("[RELAY] Ready: R1=GPIO15(Act-HIGH) R2=GPIO16(Act-LOW)");
#endif
  }

  RelaySnapshot snapshot() {
    portENTER_CRITICAL(&mux_);
    RelaySnapshot s = snap_;
    portEXIT_CRITICAL(&mux_);
    return s;
  }

  using RelayChangeCb = void (*)(uint8_t channel, bool on);

  void setChangeCallback(RelayChangeCb cb) {
    changeCb_ = cb;
  }

  void setRelay1(bool on, bool notifyRemote = true) {
    portENTER_CRITICAL(&mux_);
    snap_.relay1 = on;
    portEXIT_CRITICAL(&mux_);
#ifdef JWS_DESKTOP_S3
    digitalWrite(PinRelay1, on ? HIGH : LOW);
#endif
    Serial.printf("[RELAY] CH1 (GPIO15 Active-HIGH) -> %s\n", on ? "ON" : "OFF");
    if (notifyRemote && changeCb_) changeCb_(1, on);
  }

  void setRelay2(bool on, bool notifyRemote = true) {
    portENTER_CRITICAL(&mux_);
    snap_.relay2 = on;
    portEXIT_CRITICAL(&mux_);
#ifdef JWS_DESKTOP_S3
    digitalWrite(PinRelay2, on ? LOW : HIGH);
#endif
    Serial.printf("[RELAY] CH2 (GPIO16 Active-LOW) -> %s\n", on ? "ON" : "OFF");
    if (notifyRemote && changeCb_) changeCb_(2, on);
  }

  void toggleRelay1() { setRelay1(!snapshot().relay1); }
  void toggleRelay2() { setRelay2(!snapshot().relay2); }

  void setAll(bool on) {
    setRelay1(on);
    setRelay2(on);
  }

  void setSinricState(bool configured, bool connected, bool hasDev1, bool hasDev2) {
    portENTER_CRITICAL(&mux_);
    snap_.sinricConfigured = configured;
    snap_.sinricConnected = connected;
    snap_.hasDev1 = hasDev1;
    snap_.hasDev2 = hasDev2;
    portEXIT_CRITICAL(&mux_);
  }

 private:
  RelayControlService() = default;
  portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
  RelaySnapshot snap_{};
  RelayChangeCb changeCb_ = nullptr;
  bool initialized_ = false;
};

}  // namespace jws
