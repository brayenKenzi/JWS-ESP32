// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include <stdint.h>
#include <time.h>

namespace jws {

struct ChronosSnapshot {
  bool enabled = true;
  bool connected = false;
  bool ringing = false;
  bool findingPhone = false;
  uint8_t phoneBattery = 0;
  bool phoneCharging = false;
  char deviceName[24] = "JWS-UPIN-S3";
  char caller[36] = "";
  char notifApp[24] = "";
  char notifTitle[40] = "";
  char notifMsg[88] = "";
  char notifTime[12] = "";
  uint32_t lastNotifMs = 0;
  uint32_t notifCount = 0;
};

class ChronosService {
 public:
  using TimeSyncHandler = void (*)(time_t epoch);

  static ChronosService& instance();

  void begin();
  void setTimeSyncHandler(TimeSyncHandler cb);
  void tick();
  void setEnabled(bool en);
  void findPhone(bool active);
  void musicPlayPause();
  void musicNext();
  void musicPrev();
  void capturePhoto();
  void dismissAlert();
  ChronosSnapshot snapshot();

  // Internal callbacks invoked from ChronosESP32 C-callbacks
  void onConnection(bool state);
  void onNotification(const char* app, const char* title, const char* msg, int hour, int minute);
  void onRinger(const char* caller, bool state);
  void onConfigTimeSync();
  void updateBattery(uint8_t level, bool charging);

 private:
  ChronosService() = default;
  Preferences prefs_;
  portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
  ChronosSnapshot snap_{};
  TimeSyncHandler timeSyncCb_ = nullptr;
  bool initialized_ = false;
  bool bleStarted_ = false;
};

}  // namespace jws
