// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "PrayerCalculator.h"
#include <string.h>

namespace jws {
struct Configuration {
  CalculationSettings calculation;
  uint32_t revision = 1;
  char mosqueName[65] = "Nama Masjid";
  char runningText[257] = "Selamat datang. Mari menjaga ketenangan dan kebersihan masjid.";
  bool runningTextEnabled = true;
  uint16_t scrollIntervalMs = 80;
  int16_t corrections[PrayerCount] = {0,0,0,0,0,0,0};
  uint8_t panelCount = 2;
  uint8_t panelRows = 1;
  bool calligraphyEnabled = true;
  uint8_t brightness = 60;
  int8_t hijriOffsetDays = 0;
  uint8_t adzanDurationMinutes = 4;
  uint8_t iqomahMinutes[5] = {12, 10, 10, 7, 10};
  char khatibName[33] = "Ust. H. Ahmad Fauzi";
  char imamName[33] = "Ust. Abdurrahman";
  char muadzinName[33] = "Sdr. Rizky Pratama";
  char bilalName[33] = "Sdr. Hidayatullah";
  bool khotbahEnabled = true;
  bool qrEnabled = true;
  char qrText[129] = "https://masjid-kotamobagu.id/infaq";
  bool autoSleepEnabled = false;
  uint8_t sleepHour = 23;
  uint8_t wakeHour = 3;
  uint8_t audioVolume = 20;
  bool buzzerEnabled = true;
  int8_t tarhimOffsetMinutes = -6;
};
inline bool printableText(const char* text, size_t capacity, bool allowEmpty) {
  if (!text || capacity == 0) return false;
  size_t length = 0;
  for (; length < capacity && text[length]; ++length)
    if (static_cast<unsigned char>(text[length]) < 32 || static_cast<unsigned char>(text[length]) > 126) return false;
  return length < capacity && (allowEmpty || length > 0);
}
inline bool validConfiguration(const Configuration& c) {
  if (!validSettings(c.calculation) || c.revision == 0 ||
      !printableText(c.mosqueName, sizeof c.mosqueName, false) ||
      !printableText(c.runningText, sizeof c.runningText, true) ||
      !printableText(c.khatibName, sizeof c.khatibName, true) ||
      !printableText(c.imamName, sizeof c.imamName, true) ||
      !printableText(c.muadzinName, sizeof c.muadzinName, true) ||
      !printableText(c.bilalName, sizeof c.bilalName, true) ||
      !printableText(c.qrText, sizeof c.qrText, true) ||
      c.scrollIntervalMs < 40 || c.scrollIntervalMs > 250 ||
      c.panelCount < 1 || c.panelCount > 4 ||
      c.panelRows < 1 || c.panelRows > 2 ||
      c.brightness < 5 || c.brightness > 100 ||
      c.hijriOffsetDays < -2 || c.hijriOffsetDays > 2 ||
      c.adzanDurationMinutes < 1 || c.adzanDurationMinutes > 10 ||
      c.sleepHour > 23 || c.wakeHour > 23 ||
      c.audioVolume > 30 ||
      c.tarhimOffsetMinutes < -15 || c.tarhimOffsetMinutes > 0) return false;
  for (int i = 0; i < PrayerCount; ++i) if (c.corrections[i] < -30 || c.corrections[i] > 30) return false;
  for (int i = 0; i < 5; ++i) if (c.iqomahMinutes[i] < 1 || c.iqomahMinutes[i] > 30) return false;
  return true;
}
inline const char* prayerKey(int i) {
  static const char* const keys[] = {"imsak","subuh","syuruq","dzuhur","ashar","maghrib","isya"};
  return i >= 0 && i < PrayerCount ? keys[i] : "unknown";
}
inline const char* iqomahKey(int i) {
  static const char* const keys[] = {"subuh","dzuhur","ashar","maghrib","isya"};
  return i >= 0 && i < 5 ? keys[i] : "unknown";
}
inline Schedule correctedSchedule(const Schedule& original, const Configuration& c) {
  Schedule result = original;
  if (!original.valid || !validConfiguration(c)) { result.valid = false; return result; }
  for (int i = 0; i < PrayerCount; ++i) {
    result.minutes[i] += c.corrections[i];
    if (result.minutes[i] < 0 || result.minutes[i] >= 1440 ||
        (i > 0 && result.minutes[i] <= result.minutes[i-1])) result.valid = false;
  }
  return result;
}
}  // namespace jws
