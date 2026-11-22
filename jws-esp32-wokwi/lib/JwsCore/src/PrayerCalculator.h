// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <stdint.h>

namespace jws {
struct Date { int year; int month; int day; };
enum Prayer : uint8_t { Imsak, Subuh, Syuruq, Dzuhur, Ashar, Maghrib, Isya, PrayerCount };
constexpr int DisplayedPrayerCount = 6;
constexpr Prayer kDisplayedPrayers[DisplayedPrayerCount] = {
  Imsak, Subuh, Dzuhur, Ashar, Maghrib, Isya
};
const char* prayerName(Prayer prayer);
struct CalculationSettings {
  double latitude = -5.1477;
  double longitude = 119.4327;
  int utcOffsetMinutes = 480;
  double fajrAngle = 20.0;
  double ishaAngle = 18.0;
  int imsakMinutes = 10;
  int ihtiyatMinutes = 2;
  int asrShadowFactor = 1;
};
struct Schedule {
  Date date{};
  // Minutes from local midnight; -1 means unavailable. Never wrap invalid values.
  int minutes[PrayerCount] = {-1, -1, -1, -1, -1, -1, -1};
  bool valid = false;
};
struct NextPrayerInfo {
  Prayer prayer = Subuh;
  int targetMinute = -1;
  int32_t secondsRemaining = -1;
  bool valid = false;
};
struct HijriDate {
  int year = 0;
  int month = 0;
  int day = 0;
  bool valid = false;
};
bool validDate(Date date);
bool validSettings(const CalculationSettings& settings);
Schedule calculate(Date date, const CalculationSettings& settings);
void formatMinute(int minute, char (&output)[6]);
NextPrayerInfo nextPrayerCountdown(const Schedule& schedule, int hour, int minute, int second);
void formatCountdown(int32_t secondsRemaining, char (&output)[10]);
HijriDate gregorianToHijri(Date date, int8_t offsetDays = 0);
const char* hijriMonthName(int month);
const char* weekdayName(Date date);
const char* pasaranName(Date date);
const char* moonPhaseName(Date date);
int dhuhaMinute(const Schedule& schedule, int offsetAfterSyuruq = 20);
}  // namespace jws
