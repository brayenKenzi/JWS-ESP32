// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include "NetLock.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Interfaces.h>
#include <Timetable.h>
#include "KemenagData.h"
#include "KemenagRootCa.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <atomic>
#include <stdio.h>
#include <string.h>

class KemenagService final : public jws::ScheduleProvider {
 public:
  struct CachedMonth {
    uint32_t magic = 0x4B4D4E47UL;  // "KMNG"
    int16_t year = 0;
    int8_t month = 0;
    int8_t dayCount = 0;
    uint16_t minutes[31][jws::PrayerCount]{};
    char fetchedAt[32] = "";
  };

  KemenagService() = default;

  bool begin() {
    mutex_ = xSemaphoreCreateMutex();
    if (!mutex_) return false;
    nvsReady_ = prefs_.begin("jws_kmn", false);
    if (nvsReady_) {
      CachedMonth stored{};
      if (prefs_.getBytes("month_v1", &stored, sizeof(stored)) == sizeof(stored) &&
          validCachedMonth(stored)) {
        cached_ = stored;
        hasCache_ = true;
      }
    }
    snprintf(lastMessage_, sizeof(lastMessage_),
             "Tabel resmi Kemenag Kota Kotamobagu %d siap", jws::KemenagBuiltInYear);
    return true;
  }

  void tick(const jws::ClockReading& clock, uint32_t) {
    if (!mutex_) return;
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(5)) == pdTRUE) {
      lastClock_ = clock;
      if (pendingCacheCommit_) {
        pendingCacheCommit_ = false;
        if (nvsReady_) {
          prefs_.putBytes("month_v1", &cached_, sizeof(cached_));
        }
        ++revision_;
      }
      xSemaphoreGive(mutex_);
    }
  }

  jws::Schedule getSchedule(jws::Date date, const jws::Configuration&) override {
    jws::Schedule result;
    result.date = date;
    if (!jws::validDate(date)) {
      lastOfficial_.store(false, std::memory_order_relaxed);
      return result;
    }
    if (mutex_ && xSemaphoreTake(mutex_, pdMS_TO_TICKS(10)) == pdTRUE) {
      if (hasCache_ && cached_.year == date.year && cached_.month == date.month &&
          date.day >= 1 && date.day <= cached_.dayCount) {
        result.valid = true;
        for (int i = 0; i < jws::PrayerCount; ++i) {
          result.minutes[i] = cached_.minutes[date.day - 1][i];
        }
        lastOfficial_.store(true, std::memory_order_relaxed);
        activeSource_ = "kemenag_cache";
        xSemaphoreGive(mutex_);
        return result;
      }
      xSemaphoreGive(mutex_);
    }
    result = jws::lookupAnnual(
        date, jws::KemenagBuiltInYear, jws::KemenagBuiltInMinutes,
        sizeof(jws::KemenagBuiltInMinutes) / sizeof(jws::KemenagBuiltInMinutes[0]));
    lastOfficial_.store(result.valid, std::memory_order_relaxed);
    activeSource_ = result.valid ? "kemenag_builtin" : "unavailable";
    return result;
  }

  bool isOfficial() const override {
    return lastOfficial_.load(std::memory_order_relaxed);
  }

  uint32_t revision() const {
    return revision_.load(std::memory_order_acquire);
  }

  bool queueSync(uint32_t nowMs, char (&reason)[96]) {
    if (syncRunning_.load(std::memory_order_acquire)) {
      snprintf(reason, sizeof(reason), "Sinkronisasi Kemenag sedang berlangsung.");
      return false;
    }
    if (lastSyncAttemptMs_ != 0 && static_cast<uint32_t>(nowMs - lastSyncAttemptMs_) < 15000) {
      snprintf(reason, sizeof(reason), "Tunggu 15 detik sebelum meminta sinkronisasi ulang.");
      return false;
    }
    jws::ClockReading clockSnap{};
    if (mutex_ && xSemaphoreTake(mutex_, pdMS_TO_TICKS(10)) == pdTRUE) {
      clockSnap = lastClock_;
      xSemaphoreGive(mutex_);
    }
    if (!clockSnap.valid || clockSnap.source != jws::ClockSource::Ntp) {
      snprintf(reason, sizeof(reason),
               "Sinkronisasi HTTPS memerlukan jam NTP aktif agar sertifikat TLS valid.");
      if (mutex_ && xSemaphoreTake(mutex_, pdMS_TO_TICKS(10)) == pdTRUE) {
        snprintf(syncState_, sizeof(syncState_), "waiting_ntp");
        snprintf(lastMessage_, sizeof(lastMessage_), "%s", reason);
        xSemaphoreGive(mutex_);
      }
      return false;
    }
    if (WiFi.status() != WL_CONNECTED) {
      snprintf(reason, sizeof(reason), "Wi-Fi belum terhubung.");
      return false;
    }
    lastSyncAttemptMs_ = nowMs;
    syncYear_ = clockSnap.date.year;
    syncMonth_ = clockSnap.date.month;
    syncRunning_.store(true, std::memory_order_release);
    if (mutex_ && xSemaphoreTake(mutex_, pdMS_TO_TICKS(10)) == pdTRUE) {
      snprintf(syncState_, sizeof(syncState_), "syncing");
      snprintf(lastMessage_, sizeof(lastMessage_),
               "Mengambil jadwal Kemenag %04d-%02d...", syncYear_, syncMonth_);
      xSemaphoreGive(mutex_);
    }
    BaseType_t created = xTaskCreatePinnedToCore(
        syncTaskEntry, "kemenag_sync", 8192, this, 1, nullptr, 0);
    if (created != pdPASS) {
      syncRunning_.store(false, std::memory_order_release);
      snprintf(reason, sizeof(reason), "Gagal membuat task sinkronisasi.");
      return false;
    }
    snprintf(reason, sizeof(reason), "Sinkronisasi Kemenag dijadwalkan.");
    return true;
  }

  void copyStatus(char (&sourceOut)[32], char (&stateOut)[24], char (&msgOut)[96],
                  char (&fetchedOut)[32]) const {
    snprintf(sourceOut, sizeof(sourceOut), "%s", activeSource_);
    snprintf(fetchedOut, sizeof(fetchedOut), "%s", jws::KemenagFetchedAt);
    if (mutex_ && xSemaphoreTake(mutex_, pdMS_TO_TICKS(10)) == pdTRUE) {
      snprintf(stateOut, sizeof(stateOut), "%s", syncState_);
      snprintf(msgOut, sizeof(msgOut), "%s", lastMessage_);
      if (hasCache_ && cached_.fetchedAt[0] != '\0') {
        snprintf(fetchedOut, sizeof(fetchedOut), "%s", cached_.fetchedAt);
      }
      xSemaphoreGive(mutex_);
    } else {
      snprintf(stateOut, sizeof(stateOut), "idle");
      snprintf(msgOut, sizeof(msgOut), "Tabel resmi Kemenag siap");
    }
  }

 private:
  static int daysInMonth(int year, int month) {
    static const int lengths[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12) return 0;
    const bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
    return lengths[month - 1] + (month == 2 && leap ? 1 : 0);
  }

  static bool parseHhMm(const char* str, uint16_t& minuteOut) {
    if (!str || strlen(str) != 5 || str[2] != ':') return false;
    for (int i = 0; i < 5; ++i) {
      if (i == 2) continue;
      if (str[i] < '0' || str[i] > '9') return false;
    }
    const int hh = (str[0] - '0') * 10 + (str[1] - '0');
    const int mm = (str[3] - '0') * 10 + (str[4] - '0');
    if (hh < 0 || hh > 23 || mm < 0 || mm > 59) return false;
    minuteOut = static_cast<uint16_t>(hh * 60 + mm);
    return true;
  }

  static bool validCachedMonth(const CachedMonth& m) {
    if (m.magic != 0x4B4D4E47UL || m.year < 2000 || m.year > 2099) return false;
    const int expectedDays = daysInMonth(m.year, m.month);
    if (expectedDays == 0 || m.dayCount != expectedDays) return false;
    for (int d = 0; d < m.dayCount; ++d) {
      for (int i = 0; i < jws::PrayerCount; ++i) {
        if (m.minutes[d][i] >= 1440 || (i > 0 && m.minutes[d][i] <= m.minutes[d][i - 1])) {
          return false;
        }
      }
    }
    return true;
  }

  static void syncTaskEntry(void* arg) {
    auto* self = static_cast<KemenagService*>(arg);
    self->runSyncTask();
    vTaskDelete(nullptr);
  }

  bool fetchJsonBounded(WiFiClientSecure& client, const String& url, DynamicJsonDocument& doc,
                        char (&err)[96]) {
    HTTPClient http;
    http.setTimeout(15000);
    http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    if (!http.begin(client, url)) {
      snprintf(err, sizeof(err), "Gagal menginisialisasi koneksi HTTPS Kemenag.");
      return false;
    }
    http.addHeader("Accept", "application/json");
    http.addHeader("User-Agent", "JWS-ESP32-Kotamobagu/1.0");
    const int status = http.GET();
    if (status != HTTP_CODE_OK) {
      snprintf(err, sizeof(err), "Respons HTTP Kemenag: %d", status);
      http.end();
      return false;
    }
    const int contentLen = http.getSize();
    if (contentLen > 12288) {
      snprintf(err, sizeof(err), "Respons Kemenag melebihi batas aman (12 KB).");
      http.end();
      return false;
    }
    const String payload = http.getString();
    http.end();
    if (payload.length() == 0 || payload.length() > 12288) {
      snprintf(err, sizeof(err), "Ukuran payload JSON tidak valid.");
      return false;
    }
    const DeserializationError jsonErr = deserializeJson(doc, payload);
    if (jsonErr) {
      snprintf(err, sizeof(err), "Gagal memproses JSON Kemenag.");
      return false;
    }
    return true;
  }

  void runSyncTask() {
    char err[96] = "";
    jws::ScopedNetLock netLock(20000);
    if (!netLock.locked()) {
      finishSync(false, "Jaringan sedang sibuk, sinkronisasi Kemenag ditunda.", nullptr);
      return;
    }
    WiFiClientSecure client;
    client.setCACert(jws::KemenagRootCa);
    client.setTimeout(15);

    DynamicJsonDocument doc(12288);
    String provId;
    String cityId;

    if (!fetchJsonBounded(client, "https://bimasislam.kemenag.go.id/api/sihat/provinsi", doc, err)) {
      finishSync(false, err, nullptr);
      return;
    }
    for (JsonObject item : doc["data"].as<JsonArray>()) {
      if (strcmp(item["name"] | "", "SULAWESI UTARA") == 0) {
        provId = item["id"] | "";
        break;
      }
    }
    if (provId.isEmpty()) {
      finishSync(false, "Provinsi SULAWESI UTARA tidak ditemukan pada respons Kemenag.", nullptr);
      return;
    }

    doc.clear();
    const String kabkoUrl =
        "https://bimasislam.kemenag.go.id/api/sihat/kabko?prov=" + provId;
    if (!fetchJsonBounded(client, kabkoUrl, doc, err)) {
      finishSync(false, err, nullptr);
      return;
    }
    for (JsonObject item : doc["data"].as<JsonArray>()) {
      if (strcmp(item["name"] | "", "KOTA KOTAMOBAGU") == 0) {
        cityId = item["id"] | "";
        break;
      }
    }
    if (cityId.isEmpty()) {
      finishSync(false, "KOTA KOTAMOBAGU tidak ditemukan pada respons Kemenag.", nullptr);
      return;
    }

    doc.clear();
    char scheduleUrl[220];
    snprintf(scheduleUrl, sizeof(scheduleUrl),
             "https://bimasislam.kemenag.go.id/api/sihat/jadwal?prov=%s&kabko=%s&thn=%04d&bln=%02d",
             provId.c_str(), cityId.c_str(), syncYear_, syncMonth_);
    if (!fetchJsonBounded(client, String(scheduleUrl), doc, err)) {
      finishSync(false, err, nullptr);
      return;
    }

    if ((doc["status"] | 0) != 1 ||
        strcmp(doc["prov"] | "", "SULAWESI UTARA") != 0 ||
        strcmp(doc["kabko"] | "", "KOTA KOTAMOBAGU") != 0) {
      finishSync(false, "Metadata wilayah jadwal Kemenag tidak sesuai.", nullptr);
      return;
    }

    CachedMonth candidate{};
    candidate.year = static_cast<int16_t>(syncYear_);
    candidate.month = static_cast<int8_t>(syncMonth_);
    candidate.dayCount = static_cast<int8_t>(daysInMonth(syncYear_, syncMonth_));
    snprintf(candidate.fetchedAt, sizeof(candidate.fetchedAt), "%04d-%02d (live)",
             syncYear_, syncMonth_);

    JsonObject dataObj = doc["data"].as<JsonObject>();
    static const char* const fields[jws::PrayerCount] = {
        "imsak", "subuh", "terbit", "dzuhur", "ashar", "maghrib", "isya"};

    for (int d = 1; d <= candidate.dayCount; ++d) {
      char dateKey[12];
      snprintf(dateKey, sizeof(dateKey), "%04d-%02d-%02d", syncYear_, syncMonth_, d);
      JsonObject row = dataObj[dateKey].as<JsonObject>();
      if (row.isNull()) {
        finishSync(false, "Data tanggal bulanan Kemenag belum lengkap.", nullptr);
        return;
      }
      for (int i = 0; i < jws::PrayerCount; ++i) {
        if (!parseHhMm(row[fields[i]] | "", candidate.minutes[d - 1][i])) {
          finishSync(false, "Format jam pada jadwal Kemenag tidak valid.", nullptr);
          return;
        }
      }
    }

    if (!validCachedMonth(candidate)) {
      finishSync(false, "Urutan waktu jadwal Kemenag tidak valid.", nullptr);
      return;
    }

    char okMsg[96];
    snprintf(okMsg, sizeof(okMsg),
             "Sinkronisasi Kemenag %04d-%02d berhasil (%d hari tervalidasi).",
             syncYear_, syncMonth_, candidate.dayCount);
    finishSync(true, okMsg, &candidate);
  }

  void finishSync(bool ok, const char* message, const CachedMonth* newMonth) {
    if (mutex_ && xSemaphoreTake(mutex_, pdMS_TO_TICKS(50)) == pdTRUE) {
      if (ok && newMonth) {
        cached_ = *newMonth;
        hasCache_ = true;
        pendingCacheCommit_ = true;
        snprintf(syncState_, sizeof(syncState_), "ok");
      } else {
        snprintf(syncState_, sizeof(syncState_), "error");
      }
      snprintf(lastMessage_, sizeof(lastMessage_), "%s", message);
      xSemaphoreGive(mutex_);
    }
    syncRunning_.store(false, std::memory_order_release);
  }

  Preferences prefs_;
  SemaphoreHandle_t mutex_ = nullptr;
  CachedMonth cached_{};
  jws::ClockReading lastClock_{};
  bool nvsReady_ = false;
  bool hasCache_ = false;
  bool pendingCacheCommit_ = false;
  uint32_t lastSyncAttemptMs_ = 0;
  int syncYear_ = 2026;
  int syncMonth_ = 11;
  const char* activeSource_ = "kemenag_builtin";
  char syncState_[24] = "idle";
  char lastMessage_[96] = "Tabel resmi Kemenag Kota Kotamobagu siap";
  mutable std::atomic<bool> lastOfficial_{false};
  std::atomic<uint32_t> revision_{1};
  std::atomic<bool> syncRunning_{false};
};
