// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <Arduino.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <JwsApp.h>
#include "KemenagService.h"
#include "MikrotikService.h"
#include "XiaozhiService.h"
#include "NetLock.h"
#include "RelayControlService.h"
#include "SinricProService.h"
#include "ChronosService.h"
#include "DashboardAssets.h"
#include <esp_system.h>
#include <stdio.h>
#include <string.h>
#include <functional>

class WebController {
 public:
  WebController(jws::JwsApp& app, KemenagService& timetable)
      : app_(app), timetable_(timetable), server_(80) {}

  void setTimeHandler(std::function<void(time_t)> handler) { onSetTime_ = handler; }
  void setWifiHandler(std::function<void(const String&, const String&)> handler) { onSetWifi_ = handler; }

  bool begin() {
    generateCsrfToken();
    const char* headerKeys[] = {"X-JWS-Token", "Content-Type"};
    server_.collectHeaders(headerKeys, 2);

    server_.on("/", HTTP_GET, [this]() {
      sendGzipAsset("text/html; charset=utf-8", dashboardIndexGz, dashboardIndexGzLen);
    });
    server_.on("/style.css", HTTP_GET, [this]() {
      sendGzipAsset("text/css; charset=utf-8", dashboardCssGz, dashboardCssGzLen);
    });
    server_.on("/app.js", HTTP_GET, [this]() {
      sendGzipAsset("application/javascript; charset=utf-8", dashboardJsGz, dashboardJsGzLen);
    });
    server_.on("/api/v1/config", HTTP_GET, [this]() { handleGetConfig(); });
    server_.on("/api/v1/config", HTTP_PUT, [this]() { handlePutConfig(); });
    server_.on("/api/v1/status", HTTP_GET, [this]() { handleGetStatus(); });
    server_.on("/api/v1/sync", HTTP_POST, [this]() { handlePostSync(); });
    server_.on("/api/v1/time", HTTP_POST, [this]() { handlePostTime(); });
    server_.on("/api/v1/wifi", HTTP_GET, [this]() { handleGetWifi(); });
    server_.on("/api/v1/wifi", HTTP_POST, [this]() { handlePostWifi(); });
    server_.on("/api/v1/scene", HTTP_POST, [this]() { handlePostScene(); });
    server_.on("/api/v1/slide", HTTP_POST, [this]() { handlePostSlide(); });
    server_.on("/api/v1/theme", HTTP_GET, [this]() { handleGetTheme(); });
    server_.on("/api/v1/theme", HTTP_POST, [this]() { handlePostTheme(); });
    server_.on("/api/v1/stocks", HTTP_GET, [this]() { handleGetStocks(); });
    server_.on("/api/v1/stocks", HTTP_POST, [this]() { handlePostStocks(); });
    server_.on("/api/v1/mikrotik", HTTP_GET, [this]() { handleGetMikrotik(); });
    server_.on("/api/v1/mikrotik", HTTP_POST, [this]() { handlePostMikrotik(); });
    server_.on("/api/v1/xiaozhi", HTTP_GET, [this]() { handleGetXiaozhi(); });
    server_.on("/api/v1/xiaozhi", HTTP_POST, [this]() { handlePostXiaozhi(); });
    server_.on("/api/v1/relay", HTTP_GET, [this]() { handleGetRelay(); });
    server_.on("/api/v1/relay", HTTP_POST, [this]() { handlePostRelay(); });
    server_.on("/api/v1/sinric", HTTP_GET, [this]() { handleGetSinric(); });
    server_.on("/api/v1/sinric", HTTP_POST, [this]() { handlePostSinric(); });
    server_.on("/api/v1/chronos", HTTP_GET, [this]() { handleGetChronos(); });
    server_.on("/api/v1/chronos", HTTP_POST, [this]() { handlePostChronos(); });
    server_.on("/api/v1/system/reboot", HTTP_POST, [this]() { handlePostReboot(); });
    server_.onNotFound([this]() {
      addSecurityHeaders();
      server_.send(404, "application/json", "{\"error\":\"Endpoint tidak ditemukan\"}");
    });

    server_.begin();
    return true;
  }

  void tick() {
    server_.handleClient();
  }

 private:
  void generateCsrfToken() {
    const uint32_t r1 = esp_random();
    const uint32_t r2 = esp_random();
    const uint32_t r3 = esp_random();
    const uint32_t r4 = esp_random();
    snprintf(csrfToken_, sizeof(csrfToken_), "%08lx%08lx%08lx%08lx",
             static_cast<unsigned long>(r1), static_cast<unsigned long>(r2),
             static_cast<unsigned long>(r3), static_cast<unsigned long>(r4));
  }

  void addSecurityHeaders() {
    server_.sendHeader("X-Content-Type-Options", "nosniff");
    server_.sendHeader("X-Frame-Options", "DENY");
    server_.sendHeader("Referrer-Policy", "no-referrer");
    server_.sendHeader(
        "Content-Security-Policy",
        "default-src 'self'; script-src 'self'; style-src 'self' 'unsafe-inline'; img-src 'self' data:; connect-src 'self'; object-src 'none'; frame-ancestors 'none'");
    server_.sendHeader("Cache-Control", "no-store");
  }

  void sendGzipAsset(const char* contentType, const uint8_t* data, size_t len) {
    addSecurityHeaders();
    server_.sendHeader("Content-Encoding", "gzip");
    server_.send_P(200, contentType, reinterpret_cast<const char*>(data), len);
  }

  bool verifyCsrf() {
    if (!server_.hasHeader("X-JWS-Token")) return false;
    const String provided = server_.header("X-JWS-Token");
    return provided.length() == 32 && strcmp(provided.c_str(), csrfToken_) == 0;
  }

  static const char* clockSourceLabel(jws::ClockSource src) {
    switch (src) {
      case jws::ClockSource::Ntp: return "NTP";
      case jws::ClockSource::Holdover: return "HOLD";
      case jws::ClockSource::Mock: return "MOCK";
      default: return "WAIT";
    }
  }

  void populateConfigJson(JsonDocument& doc) {
    const auto& cfg = app_.configuration();
    doc["revision"] = cfg.revision;
    doc["csrfToken"] = csrfToken_;
    doc["mosqueName"] = cfg.mosqueName;
    doc["runningText"] = cfg.runningText;
    doc["runningTextEnabled"] = cfg.runningTextEnabled;
    doc["scrollIntervalMs"] = cfg.scrollIntervalMs;
    doc["panelCount"] = cfg.panelCount;
    doc["panelRows"] = cfg.panelRows;
    doc["calligraphyEnabled"] = cfg.calligraphyEnabled;
    doc["brightness"] = cfg.brightness;
    doc["hijriOffsetDays"] = cfg.hijriOffsetDays;
    doc["adzanDurationMinutes"] = cfg.adzanDurationMinutes;
    doc["khatibName"] = cfg.khatibName;
    doc["imamName"] = cfg.imamName;
    doc["muadzinName"] = cfg.muadzinName;
    doc["bilalName"] = cfg.bilalName;
    doc["khotbahEnabled"] = cfg.khotbahEnabled;
    doc["qrEnabled"] = cfg.qrEnabled;
    doc["qrText"] = cfg.qrText;
    doc["autoSleepEnabled"] = cfg.autoSleepEnabled;
    doc["sleepHour"] = cfg.sleepHour;
    doc["wakeHour"] = cfg.wakeHour;
    doc["audioVolume"] = cfg.audioVolume;
    doc["buzzerEnabled"] = cfg.buzzerEnabled;
    doc["tarhimOffsetMinutes"] = cfg.tarhimOffsetMinutes;

    JsonObject iqomahObj = doc.createNestedObject("iqomah");
    for (int i = 0; i < 5; ++i) {
      iqomahObj[jws::iqomahKey(i)] = cfg.iqomahMinutes[i];
    }

    JsonObject loc = doc.createNestedObject("location");
    loc["area"] = "Mongkonai Barat, Kota Kotamobagu";
    loc["city"] = "KOTA KOTAMOBAGU";
    loc["province"] = "SULAWESI UTARA";
    loc["timezone"] = "WITA (UTC+08:00)";
    loc["utcOffsetMinutes"] = cfg.calculation.utcOffsetMinutes;
    loc["latitude"] = cfg.calculation.latitude;
    loc["longitude"] = cfg.calculation.longitude;

    JsonObject corr = doc.createNestedObject("corrections");
    for (int i = 0; i < jws::PrayerCount; ++i) {
      corr[jws::prayerKey(i)] = cfg.corrections[i];
    }
  }

  void handleGetConfig() {
    StaticJsonDocument<1536> doc;
    populateConfigJson(doc);
    String out;
    serializeJson(doc, out);
    addSecurityHeaders();
    server_.send(200, "application/json", out);
  }

  void handleGetStatus() {
    StaticJsonDocument<2560> doc;
    const auto& frame = app_.frame();
    const auto& cfg = app_.configuration();

    doc["revision"] = cfg.revision;
    doc["mosqueName"] = cfg.mosqueName;
    doc["runningText"] = cfg.runningText;
    doc["runningTextEnabled"] = cfg.runningTextEnabled;
    doc["scrollIntervalMs"] = cfg.scrollIntervalMs;
    doc["panelCount"] = cfg.panelCount;
    doc["panelRows"] = cfg.panelRows;
    doc["calligraphyEnabled"] = cfg.calligraphyEnabled;
    doc["brightness"] = cfg.brightness;
    doc["hijriOffsetDays"] = cfg.hijriOffsetDays;
    doc["adzanDurationMinutes"] = cfg.adzanDurationMinutes;
    doc["khatibName"] = cfg.khatibName;
    doc["imamName"] = cfg.imamName;
    doc["muadzinName"] = cfg.muadzinName;
    doc["bilalName"] = cfg.bilalName;
    doc["khotbahEnabled"] = cfg.khotbahEnabled;
    doc["qrEnabled"] = cfg.qrEnabled;
    doc["qrText"] = cfg.qrText;
    doc["autoSleepEnabled"] = cfg.autoSleepEnabled;
    doc["sleepHour"] = cfg.sleepHour;
    doc["wakeHour"] = cfg.wakeHour;
    doc["audioVolume"] = cfg.audioVolume;
    doc["buzzerEnabled"] = cfg.buzzerEnabled;
    doc["tarhimOffsetMinutes"] = cfg.tarhimOffsetMinutes;

    JsonObject iqomahObj = doc.createNestedObject("iqomah");
    for (int i = 0; i < 5; ++i) {
      iqomahObj[jws::iqomahKey(i)] = cfg.iqomahMinutes[i];
    }

    JsonObject clockObj = doc.createNestedObject("clock");
    clockObj["valid"] = frame.clock.valid;
    clockObj["source"] = clockSourceLabel(frame.clock.source);
    if (frame.clock.valid) {
      char dateBuf[16];
      char timeBuf[12];
      char hijriBuf[40];
      snprintf(dateBuf, sizeof(dateBuf), "%04d-%02d-%02d",
               frame.clock.date.year, frame.clock.date.month, frame.clock.date.day);
      snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d:%02d",
               frame.clock.hour, frame.clock.minute, frame.clock.second);
      const auto hijri = jws::gregorianToHijri(frame.clock.date, cfg.hijriOffsetDays);
      snprintf(hijriBuf, sizeof(hijriBuf), "%d %s %dH",
               hijri.day, jws::hijriMonthName(hijri.month), hijri.year);
      clockObj["date"] = dateBuf;
      clockObj["time"] = timeBuf;
      clockObj["hijri"] = hijriBuf;
      clockObj["pasaran"] = jws::pasaranName(frame.clock.date);
      clockObj["moonPhase"] = jws::moonPhaseName(frame.clock.date);
    } else {
      clockObj["date"] = "----:--:--";
      clockObj["time"] = "--:--:--";
      clockObj["hijri"] = "--";
      clockObj["pasaran"] = "--";
      clockObj["moonPhase"] = "--";
    }

    char srcName[32], syncState[24], syncMsg[96], fetchedAt[32];
    timetable_.copyStatus(srcName, syncState, syncMsg, fetchedAt);

    JsonObject schedObj = doc.createNestedObject("schedule");
    schedObj["valid"] = frame.schedule.valid;
    schedObj["baseValid"] = frame.baseSchedule.valid;
    schedObj["official"] = frame.officialSchedule;
    schedObj["authority"] = "Kementerian Agama RI (Bimas Islam)";
    schedObj["region"] = "Kota Kotamobagu, Sulawesi Utara";
    schedObj["source"] = srcName;
    schedObj["fetchedAt"] = fetchedAt;
    schedObj["syncState"] = syncState;
    schedObj["syncMessage"] = syncMsg;

    JsonArray items = schedObj.createNestedArray("prayers");
    for (int i = 0; i < jws::PrayerCount; ++i) {
      JsonObject item = items.createNestedObject();
      char baseStr[6];
      char finalStr[6];
      jws::formatMinute(frame.baseSchedule.valid ? frame.baseSchedule.minutes[i] : -1, baseStr);
      jws::formatMinute(frame.schedule.valid ? frame.schedule.minutes[i] : -1, finalStr);
      item["key"] = jws::prayerKey(i);
      item["name"] = jws::prayerName(static_cast<jws::Prayer>(i));
      item["baseMinute"] = frame.baseSchedule.valid ? frame.baseSchedule.minutes[i] : -1;
      item["baseTime"] = baseStr;
      item["correction"] = cfg.corrections[i];
      item["finalMinute"] = frame.schedule.valid ? frame.schedule.minutes[i] : -1;
      item["finalTime"] = finalStr;
    }

    char dhuhaStr[6];
    jws::formatMinute(jws::dhuhaMinute(frame.schedule, 20), dhuhaStr);
    schedObj["dhuhaTime"] = dhuhaStr;

    const auto nextInfo = frame.clock.valid
        ? jws::nextPrayerCountdown(frame.schedule, frame.clock.hour, frame.clock.minute, frame.clock.second)
        : jws::NextPrayerInfo{};
    char cdBuf[10];
    jws::formatCountdown(nextInfo.valid ? nextInfo.secondsRemaining : -1, cdBuf);
    JsonObject nextObj = schedObj.createNestedObject("nextPrayer");
    nextObj["valid"] = nextInfo.valid;
    nextObj["name"] = nextInfo.valid ? jws::prayerName(nextInfo.prayer) : "--";
    nextObj["secondsRemaining"] = nextInfo.secondsRemaining;
    nextObj["countdown"] = cdBuf;

    JsonObject sysObj = doc.createNestedObject("system");
    sysObj["freeHeapKb"] = static_cast<uint32_t>(ESP.getFreeHeap() / 1024U);
    sysObj["minFreeHeapKb"] = static_cast<uint32_t>(ESP.getMinFreeHeap() / 1024U);
    sysObj["psramTotalKb"] = static_cast<uint32_t>(ESP.getPsramSize() / 1024U);
    sysObj["psramFreeKb"] = static_cast<uint32_t>(ESP.getFreePsram() / 1024U);
    sysObj["wifiRssi"] = (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : 0;
    sysObj["ip"] = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
    sysObj["uptimeSec"] = static_cast<uint32_t>(millis() / 1000UL);
    sysObj["resetReason"] = jws::resetReasonText(esp_reset_reason());

    String out;
    serializeJson(doc, out);
    addSecurityHeaders();
    server_.send(200, "application/json", out);
  }

  void handlePutConfig() {
    addSecurityHeaders();
    if (!verifyCsrf()) {
      server_.send(403, "application/json",
                   "{\"error\":\"Token sesi X-JWS-Token tidak valid atau kadaluarsa.\"}");
      return;
    }
    const uint32_t nowMs = millis();
    if (lastWriteMs_ != 0 && static_cast<uint32_t>(nowMs - lastWriteMs_) < 1000) {
      server_.send(429, "application/json",
                   "{\"error\":\"Terlalu banyak permintaan simpan. Coba lagi sebentar.\"}");
      return;
    }
    const String body = server_.arg("plain");
    if (body.length() == 0 || body.length() > 1536) {
      server_.send(413, "application/json",
                   "{\"error\":\"Ukuran body JSON tidak valid (maksimal 1536 byte).\"}");
      return;
    }

    StaticJsonDocument<1536> doc;
    if (deserializeJson(doc, body)) {
      server_.send(400, "application/json", "{\"error\":\"Format JSON tidak valid.\"}");
      return;
    }

    const auto& current = app_.configuration();
    if (!doc["revision"].is<uint32_t>()) {
      server_.send(400, "application/json", "{\"error\":\"Field revision wajib berupa angka.\"}");
      return;
    }
    const uint32_t clientRev = doc["revision"].as<uint32_t>();
    if (clientRev != current.revision) {
      StaticJsonDocument<256> conflict;
      conflict["error"] = "Revisi konfigurasi sudah berubah di perangkat. Muat ulang data.";
      conflict["currentRevision"] = current.revision;
      String out;
      serializeJson(conflict, out);
      server_.send(409, "application/json", out);
      return;
    }

    if (!doc["mosqueName"].is<const char*>() || !doc["runningText"].is<const char*>() ||
        !doc["runningTextEnabled"].is<bool>() || !doc["scrollIntervalMs"].is<int>() ||
        !doc["corrections"].is<JsonObject>()) {
      server_.send(400, "application/json",
                   "{\"error\":\"Field konfigurasi tidak lengkap atau tipe tidak sesuai.\"}");
      return;
    }

    const char* nameStr = doc["mosqueName"].as<const char*>();
    const char* textStr = doc["runningText"].as<const char*>();
    const int scrollMs = doc["scrollIntervalMs"].as<int>();
    if (strlen(nameStr) == 0 || strlen(nameStr) >= sizeof(current.mosqueName)) {
      server_.send(400, "application/json",
                   "{\"error\":\"Nama masjid wajib 1..64 karakter ASCII.\"}");
      return;
    }
    if (strlen(textStr) >= sizeof(current.runningText)) {
      server_.send(400, "application/json",
                   "{\"error\":\"Running text maksimal 256 karakter ASCII.\"}");
      return;
    }
    if (scrollMs < 40 || scrollMs > 250) {
      server_.send(400, "application/json",
                   "{\"error\":\"Interval running text harus 40..250 ms.\"}");
      return;
    }

    jws::Configuration next = current;
    next.revision = current.revision + 1;
    memset(next.mosqueName, 0, sizeof(next.mosqueName));
    strncpy(next.mosqueName, nameStr, sizeof(next.mosqueName) - 1);
    memset(next.runningText, 0, sizeof(next.runningText));
    strncpy(next.runningText, textStr, sizeof(next.runningText) - 1);
    next.runningTextEnabled = doc["runningTextEnabled"].as<bool>();
    next.scrollIntervalMs = static_cast<uint16_t>(scrollMs);
    if (doc.containsKey("panelCount")) {
      if (!doc["panelCount"].is<int>()) {
        server_.send(400, "application/json",
                     "{\"error\":\"Jumlah panel P5 harus berupa angka 1..4.\"}");
        return;
      }
      const int panels = doc["panelCount"].as<int>();
      if (panels < 1 || panels > 4) {
        server_.send(400, "application/json",
                     "{\"error\":\"Jumlah panel P5 harus antara 1 sampai 4 panel.\"}");
        return;
      }
      next.panelCount = static_cast<uint8_t>(panels);
    }
    if (doc.containsKey("calligraphyEnabled")) {
      if (!doc["calligraphyEnabled"].is<bool>()) {
        server_.send(400, "application/json",
                     "{\"error\":\"Field calligraphyEnabled harus berupa boolean.\"}");
        return;
      }
      next.calligraphyEnabled = doc["calligraphyEnabled"].as<bool>();
    }
    if (doc.containsKey("panelRows")) {
      const int rows = doc["panelRows"].as<int>();
      if (rows < 1 || rows > 2) {
        server_.send(400, "application/json", "{\"error\":\"Baris panel harus 1 atau 2.\"}");
        return;
      }
      next.panelRows = static_cast<uint8_t>(rows);
    }
    if (doc.containsKey("brightness")) {
      const int bri = doc["brightness"].as<int>();
      if (bri < 5 || bri > 100) {
        server_.send(400, "application/json", "{\"error\":\"Kecerahan harus 5..100%.\"}");
        return;
      }
      next.brightness = static_cast<uint8_t>(bri);
    }
    if (doc.containsKey("hijriOffsetDays")) {
      const int hOff = doc["hijriOffsetDays"].as<int>();
      if (hOff < -2 || hOff > 2) {
        server_.send(400, "application/json", "{\"error\":\"Koreksi Hijriah harus -2..+2 hari.\"}");
        return;
      }
      next.hijriOffsetDays = static_cast<int8_t>(hOff);
    }
    if (doc.containsKey("adzanDurationMinutes")) {
      const int adzanMin = doc["adzanDurationMinutes"].as<int>();
      if (adzanMin < 1 || adzanMin > 10) {
        server_.send(400, "application/json", "{\"error\":\"Durasi adzan harus 1..10 menit.\"}");
        return;
      }
      next.adzanDurationMinutes = static_cast<uint8_t>(adzanMin);
    }
    if (doc["iqomah"].is<JsonObject>()) {
      JsonObject iqObj = doc["iqomah"].as<JsonObject>();
      for (int i = 0; i < 5; ++i) {
        const char* k = jws::iqomahKey(i);
        if (iqObj.containsKey(k)) {
          const int iqVal = iqObj[k].as<int>();
          if (iqVal < 1 || iqVal > 30) {
            server_.send(400, "application/json", "{\"error\":\"Durasi Iqomah harus 1..30 menit.\"}");
            return;
          }
          next.iqomahMinutes[i] = static_cast<uint8_t>(iqVal);
        }
      }
    }
    auto copyOptionalText = [](char* dst, size_t cap, JsonVariantConst v) -> bool {
      if (v.isNull()) return true;
      if (!v.is<const char*>()) return false;
      const char* s = v.as<const char*>();
      if (strlen(s) >= cap) return false;
      memset(dst, 0, cap);
      strncpy(dst, s, cap - 1);
      return true;
    };
    if (!copyOptionalText(next.khatibName, sizeof(next.khatibName), doc["khatibName"]) ||
        !copyOptionalText(next.imamName, sizeof(next.imamName), doc["imamName"]) ||
        !copyOptionalText(next.muadzinName, sizeof(next.muadzinName), doc["muadzinName"]) ||
        !copyOptionalText(next.bilalName, sizeof(next.bilalName), doc["bilalName"]) ||
        !copyOptionalText(next.qrText, sizeof(next.qrText), doc["qrText"])) {
      server_.send(400, "application/json", "{\"error\":\"Teks petugas atau QR melampaui batas panjang.\"}");
      return;
    }
    if (doc.containsKey("khotbahEnabled") && doc["khotbahEnabled"].is<bool>()) {
      next.khotbahEnabled = doc["khotbahEnabled"].as<bool>();
    }
    if (doc.containsKey("qrEnabled") && doc["qrEnabled"].is<bool>()) {
      next.qrEnabled = doc["qrEnabled"].as<bool>();
    }
    if (doc.containsKey("autoSleepEnabled") && doc["autoSleepEnabled"].is<bool>()) {
      next.autoSleepEnabled = doc["autoSleepEnabled"].as<bool>();
    }
    if (doc.containsKey("sleepHour")) {
      const int sh = doc["sleepHour"].as<int>();
      if (sh < 0 || sh > 23) {
        server_.send(400, "application/json", "{\"error\":\"Jam tidur harus 0..23.\"}");
        return;
      }
      next.sleepHour = static_cast<uint8_t>(sh);
    }
    if (doc.containsKey("wakeHour")) {
      const int wh = doc["wakeHour"].as<int>();
      if (wh < 0 || wh > 23) {
        server_.send(400, "application/json", "{\"error\":\"Jam bangun harus 0..23.\"}");
        return;
      }
      next.wakeHour = static_cast<uint8_t>(wh);
    }
    if (doc.containsKey("audioVolume")) {
      const int vol = doc["audioVolume"].as<int>();
      if (vol < 0 || vol > 30) {
        server_.send(400, "application/json", "{\"error\":\"Volume audio harus 0..30.\"}");
        return;
      }
      next.audioVolume = static_cast<uint8_t>(vol);
    }
    if (doc.containsKey("buzzerEnabled") && doc["buzzerEnabled"].is<bool>()) {
      next.buzzerEnabled = doc["buzzerEnabled"].as<bool>();
    }
    if (doc.containsKey("tarhimOffsetMinutes")) {
      const int toff = doc["tarhimOffsetMinutes"].as<int>();
      if (toff < -15 || toff > 0) {
        server_.send(400, "application/json", "{\"error\":\"Offset tarhim harus -15..0 menit.\"}");
        return;
      }
      next.tarhimOffsetMinutes = static_cast<int8_t>(toff);
    }

    JsonObject corrObj = doc["corrections"].as<JsonObject>();
    for (int i = 0; i < jws::PrayerCount; ++i) {
      const char* key = jws::prayerKey(i);
      if (!corrObj[key].is<int>()) {
        server_.send(400, "application/json",
                     "{\"error\":\"Koreksi setiap waktu sholat wajib berupa angka bulat.\"}");
        return;
      }
      const int val = corrObj[key].as<int>();
      if (val < -30 || val > 30) {
        server_.send(400, "application/json",
                     "{\"error\":\"Koreksi waktu sholat harus dalam rentang -30 sampai +30 menit.\"}");
        return;
      }
      next.corrections[i] = static_cast<int16_t>(val);
    }

    if (!jws::validConfiguration(next)) {
      server_.send(400, "application/json",
                   "{\"error\":\"Karakter harus berupa ASCII yang dapat dicetak (32..126).\"}");
      return;
    }

    if (!app_.applyConfiguration(next)) {
      server_.send(400, "application/json",
                   "{\"error\":\"Gagal menerapkan konfigurasi: periksa urutan waktu sholat atau NVS.\"}");
      return;
    }

    lastWriteMs_ = nowMs;
    app_.tick(nowMs);

    StaticJsonDocument<1536> resp;
    populateConfigJson(resp);
    resp["message"] = "Konfigurasi dan revisi waktu sholat berhasil disimpan ke NVS.";
    String out;
    serializeJson(resp, out);
    server_.send(200, "application/json", out);
  }

  void handlePostSync() {
    addSecurityHeaders();
    if (!verifyCsrf()) {
      server_.send(403, "application/json",
                   "{\"error\":\"Token sesi X-JWS-Token tidak valid.\"}");
      return;
    }
    char reason[96] = "";
    const bool queued = timetable_.queueSync(millis(), reason);
    StaticJsonDocument<256> resp;
    resp["queued"] = queued;
    resp["message"] = reason;
    String out;
    serializeJson(resp, out);
    server_.send(queued ? 202 : 409, "application/json", out);
  }

  void handlePostTime() {
    addSecurityHeaders();
    if (!verifyCsrf()) {
      server_.send(403, "application/json", "{\"error\":\"Token sesi X-JWS-Token tidak valid.\"}");
      return;
    }
    if (!server_.hasArg("plain")) {
      server_.send(400, "application/json", "{\"error\":\"Payload kosong.\"}");
      return;
    }
    StaticJsonDocument<256> doc;
    const DeserializationError err = deserializeJson(doc, server_.arg("plain"));
    if (err) {
      server_.send(400, "application/json", "{\"error\":\"Format JSON tidak valid.\"}");
      return;
    }
    const uint32_t epoch = doc["epoch"] | 0U;
    if (epoch < 1700000000U) {
      server_.send(400, "application/json", "{\"error\":\"Nilai epoch waktu tidak valid.\"}");
      return;
    }
    if (onSetTime_) {
      onSetTime_(static_cast<time_t>(epoch));
    }
    app_.invalidateSchedule();
    server_.send(200, "application/json", "{\"success\":true,\"message\":\"Jam dan tanggal perangkat berhasil disinkronkan dengan HP.\"}");
  }

  void handleGetWifi() {
    addSecurityHeaders();
    StaticJsonDocument<256> doc;
    doc["connected"] = (WiFi.status() == WL_CONNECTED);
    doc["ip"] = WiFi.localIP().toString();
    doc["ssid"] = WiFi.SSID();
    doc["ap_ip"] = WiFi.softAPIP().toString();
    doc["rssi"] = WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0;
    String out;
    serializeJson(doc, out);
    server_.send(200, "application/json", out);
  }

  void handlePostWifi() {
    addSecurityHeaders();
    if (!verifyCsrf()) {
      server_.send(403, "application/json", "{\"error\":\"Token sesi X-JWS-Token tidak valid.\"}");
      return;
    }
    if (!server_.hasArg("plain")) {
      server_.send(400, "application/json", "{\"error\":\"Payload kosong.\"}");
      return;
    }
    StaticJsonDocument<256> doc;
    const DeserializationError err = deserializeJson(doc, server_.arg("plain"));
    if (err) {
      server_.send(400, "application/json", "{\"error\":\"Format JSON tidak valid.\"}");
      return;
    }
    const char* ssid = doc["ssid"] | "";
    const char* pass = doc["password"] | "";
    if (strlen(ssid) == 0) {
      server_.send(400, "application/json", "{\"error\":\"Nama WiFi (SSID) tidak boleh kosong.\"}");
      return;
    }
    if (onSetWifi_) {
      onSetWifi_(String(ssid), String(pass));
    }
    server_.send(200, "application/json", "{\"success\":true,\"message\":\"Menghubungkan ke WiFi... Silakan periksa status dalam beberapa detik.\"}");
  }

  void handlePostScene() {
    addSecurityHeaders();
    if (!verifyCsrf()) {
      server_.send(403, "application/json", "{\"error\":\"Token sesi X-JWS-Token tidak valid.\"}");
      return;
    }
    if (!server_.hasArg("plain")) {
      server_.send(400, "application/json", "{\"error\":\"Payload kosong.\"}");
      return;
    }
    StaticJsonDocument<128> doc;
    const DeserializationError err = deserializeJson(doc, server_.arg("plain"));
    if (err) {
      server_.send(400, "application/json", "{\"error\":\"Format JSON tidak valid.\"}");
      return;
    }
    const char* scStr = doc["scene"] | "prayer";
    uint8_t scId = 0;
    if (strcmp(scStr, "adzan") == 0) scId = 1;
    else if (strcmp(scStr, "iqomah") == 0) scId = 2;
    else if (strcmp(scStr, "khotbah") == 0) scId = 3;
    else if (strcmp(scStr, "qr") == 0) scId = 4;
    else if (strcmp(scStr, "sholat") == 0) scId = 5;
    else scId = 0;

    app_.setScene(scId, 25000); // Tampilkan selama 25 detik di layar hardware
    server_.send(200, "application/json", "{\"success\":true,\"message\":\"Layar hardware berhasil diganti ke scene yang dipilih.\"}");
  }

  void handlePostSlide() {
    addSecurityHeaders();
    if (!verifyCsrf()) {
      server_.send(403, "application/json", "{\"error\":\"Token sesi tidak valid\"}");
      return;
    }
    if (!server_.hasArg("plain")) {
      server_.send(400, "application/json", "{\"error\":\"Payload kosong\"}");
      return;
    }
    StaticJsonDocument<128> doc;
    if (deserializeJson(doc, server_.arg("plain"))) {
      server_.send(400, "application/json", "{\"error\":\"Format JSON tidak valid\"}");
      return;
    }
    uint8_t slId = doc["slide"] | 0;
    app_.setSlide(slId);
    server_.send(200, "application/json", "{\"success\":true,\"message\":\"Slide layar hardware berhasil diubah.\"}");
  }

  void handleGetTheme() {
    addSecurityHeaders();
    StaticJsonDocument<128> doc;
    doc["theme"] = app_.getTheme();
    String out;
    serializeJson(doc, out);
    server_.send(200, "application/json", out);
  }

  void handlePostTheme() {
    addSecurityHeaders();
    if (!verifyCsrf()) {
      server_.send(403, "application/json", "{\"error\":\"Token sesi tidak valid\"}");
      return;
    }
    if (!server_.hasArg("plain")) {
      server_.send(400, "application/json", "{\"error\":\"Payload kosong\"}");
      return;
    }
    StaticJsonDocument<128> doc;
    if (deserializeJson(doc, server_.arg("plain"))) {
      server_.send(400, "application/json", "{\"error\":\"Format JSON tidak valid\"}");
      return;
    }
    uint8_t thId = doc["theme"] | 0;
    app_.setTheme(thId);
    server_.send(200, "application/json", "{\"success\":true,\"message\":\"Tema warna layar berhasil diubah.\"}");
  }

  void handleGetStocks() {
    addSecurityHeaders();
    StaticJsonDocument<256> doc;
    doc["symbols"] = jws::OnlineFeedService::instance().stocks().watchlistSymbols;
    String out;
    serializeJson(doc, out);
    server_.send(200, "application/json", out);
  }

  void handlePostStocks() {
    addSecurityHeaders();
    if (!verifyCsrf()) {
      server_.send(403, "application/json", "{\"error\":\"Token sesi tidak valid\"}");
      return;
    }
    if (!server_.hasArg("plain")) {
      server_.send(400, "application/json", "{\"error\":\"Payload kosong\"}");
      return;
    }
    StaticJsonDocument<256> doc;
    if (deserializeJson(doc, server_.arg("plain"))) {
      server_.send(400, "application/json", "{\"error\":\"Format JSON tidak valid\"}");
      return;
    }
    const char* syms = doc["symbols"] | "BBCA,BBRI,TLKM,ASII";
    jws::OnlineFeedService::instance().setWatchlist(syms);
    server_.send(200, "application/json", "{\"success\":true,\"message\":\"Watchlist saham berhasil disimpan dan sedang diupdate.\"}");
  }

  void handleGetMikrotik() {
    addSecurityHeaders();
    const auto& mt = jws::MikrotikService::instance().feed();
    StaticJsonDocument<512> doc;
    doc["host"] = mt.host;
    doc["port"] = mt.port;
    doc["user"] = mt.user;
    doc["hasPassword"] = (mt.pass[0] != '\0');
    doc["ifaces"] = mt.interfaceCsv;
    doc["connected"] = mt.connected;
    doc["identity"] = mt.identity;
    doc["cpuLoad"] = mt.cpuLoad;
    doc["totalPppoe"] = mt.totalPppoe;
    doc["totalHotspot"] = mt.totalHotspot;
    String out;
    serializeJson(doc, out);
    server_.send(200, "application/json", out);
  }

  void handlePostMikrotik() {
    addSecurityHeaders();
    if (!verifyCsrf()) {
      server_.send(403, "application/json", "{\"error\":\"Token sesi tidak valid\"}");
      return;
    }
    if (!server_.hasArg("plain")) {
      server_.send(400, "application/json", "{\"error\":\"Payload kosong\"}");
      return;
    }
    StaticJsonDocument<512> doc;
    if (deserializeJson(doc, server_.arg("plain"))) {
      server_.send(400, "application/json", "{\"error\":\"Format JSON tidak valid\"}");
      return;
    }
    const auto& cur = jws::MikrotikService::instance().feed();
    const char* host = doc["host"] | cur.host;
    int portVal = doc["port"] | static_cast<int>(cur.port);
    if (portVal < 1 || portVal > 65535) portVal = 8728;
    const char* user = doc["user"] | cur.user;
    const char* pass = doc["pass"] | "";
    const char* ifaces = doc["ifaces"] | cur.interfaceCsv;

    // Keep previous password if input was left blank (already configured)
    const char* effectivePass = (pass && pass[0] != '\0') ? pass : cur.pass;
    jws::MikrotikService::instance().configure(host, static_cast<uint16_t>(portVal), user, effectivePass, ifaces);
    server_.send(200, "application/json", "{\"success\":true,\"message\":\"Konfigurasi MikroTik NOC berhasil disimpan dan mulai sinkron!\"}");
  }

  void handleGetXiaozhi() {
    addSecurityHeaders();
    const jws::XiaozhiSnapshot snap = jws::XiaozhiService::instance().snapshot();
    StaticJsonDocument<1024> doc;
    doc["agentName"] = snap.agentName;
    doc["agentPrompt"] = snap.agentPrompt;
    doc["apiBaseUrl"] = snap.apiBaseUrl;
    doc["apiModel"] = snap.apiModel;
    doc["query"] = snap.userQuery;
    doc["reply1"] = snap.replyLine1;
    doc["reply2"] = snap.replyLine2;
    doc["reply3"] = snap.replyLine3;
    doc["state"] = static_cast<int>(snap.state);
    doc["micLevel"] = snap.micLevel;
    doc["micReady"] = snap.micHardwareReady;
    doc["hasApiKey"] = snap.hasApiKey;
    String out;
    serializeJson(doc, out);
    server_.send(200, "application/json", out);
  }

  void handlePostXiaozhi() {
    addSecurityHeaders();
    if (!verifyCsrf()) {
      server_.send(403, "application/json", "{\"error\":\"Token sesi tidak valid\"}");
      return;
    }
    if (!server_.hasArg("plain")) {
      server_.send(400, "application/json", "{\"error\":\"Payload kosong\"}");
      return;
    }
    StaticJsonDocument<1024> doc;
    if (deserializeJson(doc, server_.arg("plain"))) {
      server_.send(400, "application/json", "{\"error\":\"Format JSON tidak valid\"}");
      return;
    }
    const char* agName = doc.containsKey("agentName") ? (doc["agentName"] | "") : nullptr;
    const char* agPrompt = doc.containsKey("agentPrompt") ? (doc["agentPrompt"] | "") : nullptr;
    const char* apiKey = doc.containsKey("apiKey") ? (doc["apiKey"] | "") : nullptr;
    const char* apiBase = doc.containsKey("apiBaseUrl") ? (doc["apiBaseUrl"] | "") : nullptr;
    const char* apiModel = doc.containsKey("apiModel") ? (doc["apiModel"] | "") : nullptr;
    const bool clearKey = doc["clearApiKey"] | false;
    if (agName != nullptr || agPrompt != nullptr || apiKey != nullptr ||
        apiBase != nullptr || apiModel != nullptr || clearKey) {
      jws::XiaozhiService::instance().setAgentConfig(agName, agPrompt, apiKey, clearKey, apiBase, apiModel);
    }

    if (doc["recordVoice"] | false) {
      app_.setSlide(6);
      jws::XiaozhiService::instance().triggerVoiceCapture();
      server_.send(200, "application/json",
                   "{\"success\":true,\"message\":\"Merekam suara 3 detik dari mic INMP441 (cek layar TFT)...\"}");
      return;
    }

    const char* question = doc["question"] | "";
    if (question && question[0] != '\0') {
      jws::XiaozhiService::instance().askQuestion(question);
      app_.setSlide(6); // Pindah layar TFT ke Xiaozhi Islami AI
      server_.send(200, "application/json",
                   "{\"success\":true,\"message\":\"Pertanyaan dikirim ke Xiaozhi AI (cek layar TFT)!\"}");
      return;
    }
    server_.send(200, "application/json",
                 "{\"success\":true,\"message\":\"Profil Agent & pengaturan Xiaozhi AI berhasil disimpan.\"}");
  }

  void handleGetRelay() {
    addSecurityHeaders();
    const auto st = jws::RelayControlService::instance().snapshot();
    StaticJsonDocument<256> doc;
    doc["relay1"] = st.relay1;
    doc["relay2"] = st.relay2;
    String out;
    serializeJson(doc, out);
    server_.send(200, "application/json", out);
  }

  void handlePostRelay() {
    addSecurityHeaders();
    if (!verifyCsrf()) {
      server_.send(403, "application/json", "{\"error\":\"Token sesi tidak valid\"}");
      return;
    }
    if (!server_.hasArg("plain")) {
      server_.send(400, "application/json", "{\"error\":\"Payload kosong\"}");
      return;
    }
    StaticJsonDocument<256> doc;
    if (deserializeJson(doc, server_.arg("plain"))) {
      server_.send(400, "application/json", "{\"error\":\"Format JSON tidak valid\"}");
      return;
    }
    auto& rc = jws::RelayControlService::instance();
    if (doc.containsKey("all")) {
      rc.setAll(doc["all"].as<bool>());
    }
    if (doc.containsKey("relay1")) rc.setRelay1(doc["relay1"].as<bool>());
    if (doc.containsKey("relay2")) rc.setRelay2(doc["relay2"].as<bool>());
    handleGetRelay();
  }

  void handleGetSinric() {
    addSecurityHeaders();
    const auto st = jws::SinricProService::instance().status();
    StaticJsonDocument<512> doc;
    doc["enabled"] = st.enabled;
    doc["configured"] = st.configured;
    doc["connected"] = st.connected;
    doc["hasAppKey"] = st.hasAppKey;
    doc["hasAppSecret"] = st.hasAppSecret;
    doc["deviceId1"] = st.deviceId1;
    doc["deviceId2"] = st.deviceId2;
    String out;
    serializeJson(doc, out);
    server_.send(200, "application/json", out);
  }

  void handlePostSinric() {
    addSecurityHeaders();
    if (!verifyCsrf()) {
      server_.send(403, "application/json", "{\"error\":\"Token sesi tidak valid\"}");
      return;
    }
    if (!server_.hasArg("plain")) {
      server_.send(400, "application/json", "{\"error\":\"Payload kosong\"}");
      return;
    }
    StaticJsonDocument<768> doc;
    if (deserializeJson(doc, server_.arg("plain"))) {
      server_.send(400, "application/json", "{\"error\":\"Format JSON tidak valid\"}");
      return;
    }
    const bool enabled = doc["enabled"] | true;
    const bool clearSecrets = doc["clearSecrets"] | false;
    const char* appKey = doc.containsKey("appKey") ? (doc["appKey"] | "") : nullptr;
    const char* appSecret = doc.containsKey("appSecret") ? (doc["appSecret"] | "") : nullptr;
    const char* dev1 = doc.containsKey("deviceId1") ? (doc["deviceId1"] | "") : nullptr;
    const char* dev2 = doc.containsKey("deviceId2") ? (doc["deviceId2"] | "") : nullptr;
    jws::SinricProService::instance().configure(enabled, appKey, appSecret, dev1, dev2, clearSecrets);
    handleGetSinric();
  }

  void handleGetChronos() {
    addSecurityHeaders();
    const auto cs = jws::ChronosService::instance().snapshot();
    StaticJsonDocument<512> doc;
    doc["enabled"] = cs.enabled;
    doc["connected"] = cs.connected;
    doc["ringing"] = cs.ringing;
    doc["findingPhone"] = cs.findingPhone;
    doc["phoneBattery"] = cs.phoneBattery;
    doc["phoneCharging"] = cs.phoneCharging;
    doc["deviceName"] = cs.deviceName;
    doc["notifApp"] = cs.notifApp;
    doc["notifTitle"] = cs.notifTitle;
    doc["notifMsg"] = cs.notifMsg;
    doc["notifTime"] = cs.notifTime;
    doc["notifCount"] = cs.notifCount;
    String out;
    serializeJson(doc, out);
    server_.send(200, "application/json", out);
  }

  void handlePostChronos() {
    addSecurityHeaders();
    if (!verifyCsrf()) {
      server_.send(403, "application/json", "{\"error\":\"Token sesi tidak valid\"}");
      return;
    }
    if (!server_.hasArg("plain")) {
      server_.send(400, "application/json", "{\"error\":\"Payload kosong\"}");
      return;
    }
    StaticJsonDocument<256> doc;
    if (deserializeJson(doc, server_.arg("plain"))) {
      server_.send(400, "application/json", "{\"error\":\"Format JSON tidak valid\"}");
      return;
    }
    auto& ch = jws::ChronosService::instance();
    if (doc.containsKey("enabled")) {
      ch.setEnabled(doc["enabled"].as<bool>());
    }
    if (doc.containsKey("findPhone")) {
      ch.findPhone(doc["findPhone"].as<bool>());
    }
    if (doc.containsKey("music")) {
      const char* m = doc["music"] | "";
      if (strcmp(m, "play") == 0) ch.musicPlayPause();
      else if (strcmp(m, "next") == 0) ch.musicNext();
      else if (strcmp(m, "prev") == 0) ch.musicPrev();
    }
    if (doc["camera"] | false) {
      ch.capturePhoto();
    }
    handleGetChronos();
  }

  void handlePostReboot() {
    addSecurityHeaders();
    if (!verifyCsrf()) {
      server_.send(403, "application/json", "{\"error\":\"Token sesi tidak valid\"}");
      return;
    }
    server_.send(200, "application/json", "{\"success\":true,\"message\":\"ESP32-S3 sedang restart...\"}");
    delay(250);
    ESP.restart();
  }

  jws::JwsApp& app_;
  KemenagService& timetable_;
  WebServer server_;
  char csrfToken_[33] = "";
  uint32_t lastWriteMs_ = 0;
  std::function<void(time_t)> onSetTime_;
  std::function<void(const String&, const String&)> onSetWifi_;
};
