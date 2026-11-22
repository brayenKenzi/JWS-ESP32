// SPDX-License-Identifier: GPL-3.0-only
#include "ChronosService.h"
#include <string.h>

#ifdef JWS_DESKTOP_S3
#include <ChronosESP32.h>
#endif

namespace jws {

namespace {
void copySafeStr(char* dst, size_t cap, const char* src) {
  if (!dst || cap == 0) return;
  if (!src) {
    dst[0] = '\0';
    return;
  }
  strncpy(dst, src, cap - 1);
  dst[cap - 1] = '\0';
}

void cleanAscii(char* s) {
  if (!s) return;
  for (size_t i = 0; s[i] != '\0'; ++i) {
    unsigned char c = static_cast<unsigned char>(s[i]);
    if (c == '\n' || c == '\r' || c == '\t') {
      s[i] = ' ';
    } else if (c < 0x20 || c >= 0x7F) {
      s[i] = ' ';
    }
  }
}

#ifdef JWS_DESKTOP_S3
ChronosESP32 gWatch("JWS-UPIN-S3");

void cbConnection(bool state) {
  ChronosService::instance().onConnection(state);
}

void cbNotification(Notification notif) {
  ChronosService::instance().onNotification(
      notif.app.c_str(), notif.title.c_str(), notif.message.c_str(),
      gWatch.getHourC(), gWatch.getMinute());
}

void cbRinger(String caller, bool state) {
  ChronosService::instance().onRinger(caller.c_str(), state);
}

void cbConfig(Config config, uint32_t a, uint32_t b) {
  (void)a;
  (void)b;
  if (config == CF_TIME) {
    ChronosService::instance().onConfigTimeSync();
  }
  ChronosService::instance().updateBattery(gWatch.getPhoneBattery(), gWatch.isPhoneCharging());
}
#endif
}  // namespace

ChronosService& ChronosService::instance() {
  static ChronosService svc;
  return svc;
}

void ChronosService::begin() {
  if (initialized_) return;
  initialized_ = true;

  prefs_.begin("jws_chronos", false);
  const bool en = prefs_.getBool("enabled", true);
  portENTER_CRITICAL(&mux_);
  snap_.enabled = en;
  copySafeStr(snap_.deviceName, sizeof(snap_.deviceName), "JWS-UPIN-S3");
  portEXIT_CRITICAL(&mux_);

#ifdef JWS_DESKTOP_S3
  if (en && !bleStarted_) {
    gWatch.setConnectionCallback(cbConnection);
    gWatch.setNotificationCallback(cbNotification);
    gWatch.setRingerCallback(cbRinger);
    gWatch.setConfigurationCallback(cbConfig);
    gWatch.begin();
    gWatch.set24Hour(true);
    bleStarted_ = true;
    Serial.println("[CHRONOS] BLE Companion active: Name='JWS-UPIN-S3' (Ready for Chronos app)");
  }
#endif
}

void ChronosService::setTimeSyncHandler(TimeSyncHandler cb) {
  timeSyncCb_ = cb;
}

void ChronosService::tick() {
#ifdef JWS_DESKTOP_S3
  if (!bleStarted_) return;
  gWatch.loop();
  const bool conn = gWatch.isConnected();
  const uint8_t bat = gWatch.getPhoneBattery();
  const bool chg = gWatch.isPhoneCharging();
  portENTER_CRITICAL(&mux_);
  snap_.connected = conn;
  snap_.phoneBattery = bat;
  snap_.phoneCharging = chg;
  portEXIT_CRITICAL(&mux_);
#endif
}

void ChronosService::setEnabled(bool en) {
  portENTER_CRITICAL(&mux_);
  snap_.enabled = en;
  portEXIT_CRITICAL(&mux_);
  prefs_.putBool("enabled", en);
#ifdef JWS_DESKTOP_S3
  if (en && !bleStarted_) {
    gWatch.setConnectionCallback(cbConnection);
    gWatch.setNotificationCallback(cbNotification);
    gWatch.setRingerCallback(cbRinger);
    gWatch.setConfigurationCallback(cbConfig);
    gWatch.begin();
    gWatch.set24Hour(true);
    bleStarted_ = true;
    Serial.println("[CHRONOS] BLE Companion enabled & started");
  }
#endif
}

void ChronosService::findPhone(bool active) {
  portENTER_CRITICAL(&mux_);
  snap_.findingPhone = active;
  portEXIT_CRITICAL(&mux_);
#ifdef JWS_DESKTOP_S3
  if (bleStarted_ && gWatch.isConnected()) {
    gWatch.findPhone(active);
    Serial.printf("[CHRONOS] Find Phone -> %s\n", active ? "RINGING" : "STOP");
  }
#endif
}

void ChronosService::musicPlayPause() {
#ifdef JWS_DESKTOP_S3
  if (bleStarted_ && gWatch.isConnected()) {
    gWatch.musicControl(MUSIC_PLAY);
    Serial.println("[CHRONOS] Music Play/Pause");
  }
#endif
}

void ChronosService::musicNext() {
#ifdef JWS_DESKTOP_S3
  if (bleStarted_ && gWatch.isConnected()) {
    gWatch.musicControl(MUSIC_NEXT);
    Serial.println("[CHRONOS] Music Next");
  }
#endif
}

void ChronosService::musicPrev() {
#ifdef JWS_DESKTOP_S3
  if (bleStarted_ && gWatch.isConnected()) {
    gWatch.musicControl(MUSIC_PREVIOUS);
    Serial.println("[CHRONOS] Music Previous");
  }
#endif
}

void ChronosService::capturePhoto() {
#ifdef JWS_DESKTOP_S3
  if (bleStarted_ && gWatch.isConnected()) {
    gWatch.capturePhoto();
    Serial.println("[CHRONOS] Camera Shutter Triggered");
  }
#endif
}

void ChronosService::dismissAlert() {
  portENTER_CRITICAL(&mux_);
  snap_.ringing = false;
  snap_.lastNotifMs = 0;
  portEXIT_CRITICAL(&mux_);
}

ChronosSnapshot ChronosService::snapshot() {
  portENTER_CRITICAL(&mux_);
  ChronosSnapshot s = snap_;
  portEXIT_CRITICAL(&mux_);
  return s;
}

void ChronosService::onConnection(bool state) {
  portENTER_CRITICAL(&mux_);
  snap_.connected = state;
  if (!state) {
    snap_.ringing = false;
    snap_.findingPhone = false;
  }
  portEXIT_CRITICAL(&mux_);
  Serial.printf("[CHRONOS] Phone BLE %s\n", state ? "CONNECTED" : "DISCONNECTED");
}

void ChronosService::onNotification(const char* app, const char* title, const char* msg, int hour, int minute) {
  char aBuf[24], tBuf[40], mBuf[88], tmBuf[12];
  copySafeStr(aBuf, sizeof(aBuf), (app && app[0]) ? app : "Notifikasi HP");
  copySafeStr(tBuf, sizeof(tBuf), (title && title[0]) ? title : "Pesan Baru");
  copySafeStr(mBuf, sizeof(mBuf), (msg && msg[0]) ? msg : "-");
  cleanAscii(aBuf);
  cleanAscii(tBuf);
  cleanAscii(mBuf);
  snprintf(tmBuf, sizeof(tmBuf), "%02d:%02d", hour, minute);

  portENTER_CRITICAL(&mux_);
  copySafeStr(snap_.notifApp, sizeof(snap_.notifApp), aBuf);
  copySafeStr(snap_.notifTitle, sizeof(snap_.notifTitle), tBuf);
  copySafeStr(snap_.notifMsg, sizeof(snap_.notifMsg), mBuf);
  copySafeStr(snap_.notifTime, sizeof(snap_.notifTime), tmBuf);
  snap_.lastNotifMs = millis();
  ++snap_.notifCount;
  portEXIT_CRITICAL(&mux_);

  Serial.printf("[CHRONOS] Notif #%lu [%s] %s: %s\n",
                static_cast<unsigned long>(snap_.notifCount), aBuf, tBuf, mBuf);
}

void ChronosService::onRinger(const char* caller, bool state) {
  char cBuf[36];
  copySafeStr(cBuf, sizeof(cBuf), (caller && caller[0]) ? caller : "Panggilan Masuk");
  cleanAscii(cBuf);

  portENTER_CRITICAL(&mux_);
  snap_.ringing = state;
  copySafeStr(snap_.caller, sizeof(snap_.caller), cBuf);
  if (state) {
    snap_.lastNotifMs = millis();
  }
  portEXIT_CRITICAL(&mux_);

  Serial.printf("[CHRONOS] Incoming Call %s: %s\n", state ? "RINGING" : "ENDED", cBuf);
}

void ChronosService::onConfigTimeSync() {
  time_t now = time(nullptr);
  if (now > 1700000000L && timeSyncCb_) {
    // ChronosESP32 applies local time offset into settimeofday; convert back to UTC epoch for NtpClockProvider
    // (NtpClockProvider adds utcOffsetMinutes = 480 minutes = 28800s)
    timeSyncCb_(now - 28800L);
    Serial.println("[CHRONOS] BLE Time synced from smartphone!");
  }
}

void ChronosService::updateBattery(uint8_t level, bool charging) {
  portENTER_CRITICAL(&mux_);
  snap_.phoneBattery = level;
  snap_.phoneCharging = charging;
  portEXIT_CRITICAL(&mux_);
}

}  // namespace jws
