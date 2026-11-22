// SPDX-License-Identifier: GPL-3.0-only
// Independently implemented from the solar equations documented at:
// https://praytimes.org/docs/calculation (Hamid Zarrabi-Zadeh / PrayTimes.org).
#include "PrayerCalculator.h"
#include <math.h>
#include <stdio.h>

namespace jws {
namespace {
constexpr double Pi = 3.14159265358979323846;
double sine(double x) { return sin(x * Pi / 180.0); }
double cosine(double x) { return cos(x * Pi / 180.0); }
double tangent(double x) { return tan(x * Pi / 180.0); }
double degrees(double x) { return x * 180.0 / Pi; }
double normalize(double value, double period) { return value - period * floor(value / period); }
double julian(Date d) {
  if (d.month <= 2) { --d.year; d.month += 12; }
  const double century = floor(d.year / 100.0);
  return floor(365.25 * (d.year + 4716)) + floor(30.6001 * (d.month + 1)) +
         d.day + 2 - century + floor(century / 4) - 1524.5;
}
struct Sun { double declination; double equation; };
Sun sunPosition(double jd) {
  const double days = jd - 2451545.0;
  const double anomaly = normalize(357.529 + 0.98560028 * days, 360);
  const double meanLongitude = normalize(280.459 + 0.98564736 * days, 360);
  const double longitude = normalize(meanLongitude + 1.915 * sine(anomaly) +
                                     0.020 * sine(2 * anomaly), 360);
  const double obliquity = 23.439 - 0.00000036 * days;
  const double rightAscension = normalize(degrees(atan2(cosine(obliquity) * sine(longitude),
                                                       cosine(longitude))) / 15, 24);
  return {degrees(asin(sine(obliquity) * sine(longitude))),
          meanLongitude / 15 - rightAscension};
}
double noon(double jd, double guess) {
  return normalize(12 - sunPosition(jd + guess / 24).equation, 24);
}
// Altitude in degrees above the horizon; NaN explicitly represents a missing event.
double event(double jd, double latitude, double altitude, double guess, bool morning) {
  const Sun sun = sunPosition(jd + guess / 24);
  const double ratio = (sine(altitude) - sine(latitude) * sine(sun.declination)) /
                       (cosine(latitude) * cosine(sun.declination));
  if (!isfinite(ratio) || ratio < -1 || ratio > 1) return NAN;
  const double offset = degrees(acos(ratio)) / 15;
  return noon(jd, guess) + (morning ? -offset : offset);
}
int roundMinute(double hours, int correction = 0) {
  if (!isfinite(hours) || hours < -24.0 || hours > 48.0) return -1;
  const int value = static_cast<int>(floor(hours * 60 + 0.5)) + correction;
  return value >= 0 && value < 1440 ? value : -1;
}
}  // namespace
const char* prayerName(Prayer p) {
  static const char* const names[] = {"Imsak", "Subuh", "Syuruq", "Dzuhur", "Ashar", "Maghrib", "Isya"};
  return p < PrayerCount ? names[p] : "?";
}
bool validDate(Date d) {
  if (d.year < 2000 || d.year > 2099 || d.month < 1 || d.month > 12 || d.day < 1) return false;
  static const int days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
  const bool leap = d.year % 4 == 0 && (d.year % 100 != 0 || d.year % 400 == 0);
  return d.day <= days[d.month - 1] + (d.month == 2 && leap ? 1 : 0);
}
bool validSettings(const CalculationSettings& s) {
  return isfinite(s.latitude) && fabs(s.latitude) < 90 &&
         isfinite(s.longitude) && fabs(s.longitude) <= 180 &&
         s.utcOffsetMinutes >= -720 && s.utcOffsetMinutes <= 840 &&
         isfinite(s.fajrAngle) && s.fajrAngle > 0 && s.fajrAngle < 30 &&
         isfinite(s.ishaAngle) && s.ishaAngle > 0 && s.ishaAngle < 30 &&
         s.imsakMinutes >= 0 && s.imsakMinutes <= 60 &&
         s.ihtiyatMinutes >= 0 && s.ihtiyatMinutes <= 30 &&
         (s.asrShadowFactor == 1 || s.asrShadowFactor == 2);
}
Schedule calculate(Date date, const CalculationSettings& s) {
  Schedule result;
  result.date = date;
  if (!validDate(date) || !validSettings(s)) return result;
  const double jd = julian(date) - s.longitude / 360.0;
  const double zone = s.utcOffsetMinutes / 60.0 - s.longitude / 15.0;
  const double asrDeclination = sunPosition(jd + 13.0 / 24).declination;
  const double asrAltitude = degrees(atan(1.0 / (s.asrShadowFactor + tangent(fabs(s.latitude - asrDeclination)))));
  result.minutes[Subuh] = roundMinute(event(jd, s.latitude, -s.fajrAngle, 5, true) + zone, s.ihtiyatMinutes);
  result.minutes[Syuruq] = roundMinute(event(jd, s.latitude, -0.833, 6, true) + zone);
  result.minutes[Dzuhur] = roundMinute(noon(jd, 12) + zone, s.ihtiyatMinutes);
  result.minutes[Ashar] = roundMinute(event(jd, s.latitude, asrAltitude, 13, false) + zone, s.ihtiyatMinutes);
  result.minutes[Maghrib] = roundMinute(event(jd, s.latitude, -0.833, 18, false) + zone, s.ihtiyatMinutes);
  result.minutes[Isya] = roundMinute(event(jd, s.latitude, -s.ishaAngle, 18, false) + zone, s.ihtiyatMinutes);
  // Imsak is relative to the displayed, corrected Subuh time.
  if (result.minutes[Subuh] >= s.imsakMinutes) result.minutes[Imsak] = result.minutes[Subuh] - s.imsakMinutes;
  result.valid = true;
  for (int i = 0; i < PrayerCount; ++i) {
    if (result.minutes[i] < 0 || (i > 0 && result.minutes[i] < result.minutes[i - 1]) ||
        (i > 1 && result.minutes[i] == result.minutes[i - 1])) result.valid = false;
  }
  return result;
}
void formatMinute(int minute, char (&output)[6]) {
  if (minute < 0 || minute >= 1440) { snprintf(output, sizeof output, "--:--"); return; }
  snprintf(output, sizeof output, "%02d:%02d", minute / 60, minute % 60);
}
NextPrayerInfo nextPrayerCountdown(const Schedule& schedule, int hour, int minute, int second) {
  NextPrayerInfo info;
  if (!schedule.valid || hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 59) {
    return info;
  }
  static constexpr Prayer ObligatoryPrayers[] = {Subuh, Dzuhur, Ashar, Maghrib, Isya};
  const int32_t nowSeconds = static_cast<int32_t>(hour) * 3600 + static_cast<int32_t>(minute) * 60 + second;
  for (Prayer p : ObligatoryPrayers) {
    const int targetMin = schedule.minutes[p];
    if (targetMin < 0 || targetMin >= 1440) return NextPrayerInfo{};
    const int32_t targetSec = static_cast<int32_t>(targetMin) * 60;
    if (targetSec > nowSeconds) {
      info.prayer = p;
      info.targetMinute = targetMin;
      info.secondsRemaining = targetSec - nowSeconds;
      info.valid = true;
      return info;
    }
  }
  const int subuhMin = schedule.minutes[Subuh];
  if (subuhMin < 0 || subuhMin >= 1440) return NextPrayerInfo{};
  info.prayer = Subuh;
  info.targetMinute = subuhMin;
  info.secondsRemaining = (86400 - nowSeconds) + static_cast<int32_t>(subuhMin) * 60;
  info.valid = true;
  return info;
}
void formatCountdown(int32_t secondsRemaining, char (&output)[10]) {
  if (secondsRemaining < 0 || secondsRemaining >= 172800) {
    snprintf(output, sizeof output, "--:--:--");
    return;
  }
  const int hh = static_cast<int>(secondsRemaining / 3600);
  const int mm = static_cast<int>((secondsRemaining % 3600) / 60);
  const int ss = static_cast<int>(secondsRemaining % 60);
  if (hh > 99) {
    snprintf(output, sizeof output, "--:--:--");
    return;
  }
  snprintf(output, sizeof output, "-%02d:%02d:%02d", hh, mm, ss);
}
HijriDate gregorianToHijri(Date date, int8_t offsetDays) {
  HijriDate h;
  if (!validDate(date) || offsetDays < -5 || offsetDays > 5) return h;
  const int32_t jd = static_cast<int32_t>(floor(julian(date) + 0.5)) + offsetDays;
  int32_t l = jd - 1948440 + 10632;
  const int32_t n = (l - 1) / 10631;
  l = l - 10631 * n + 354;
  const int32_t j = ((10985 - l) / 5316) * ((50 * l) / 17719) + (l / 5670) * ((43 * l) / 15238);
  l = l - ((30 - j) / 15) * ((17719 * j) / 50) - (j / 16) * ((15238 * j) / 43) + 29;
  h.month = static_cast<int>((24 * l) / 709);
  h.day = static_cast<int>(l - (709 * h.month) / 24);
  h.year = static_cast<int>(30 * n + j - 30);
  h.valid = h.year > 1400 && h.month >= 1 && h.month <= 12 && h.day >= 1 && h.day <= 30;
  return h;
}
const char* hijriMonthName(int month) {
  static const char* const months[] = {
    "Muharram", "Safar", "Rabiul Awal", "Rabiul Akhir",
    "Jumadil Awal", "Jumadil Akhir", "Rajab", "Syaban",
    "Ramadhan", "Syawal", "Dzulqaidah", "Dzulhijjah"
  };
  return (month >= 1 && month <= 12) ? months[month - 1] : "?";
}
const char* weekdayName(Date date) {
  if (!validDate(date)) return "?";
  static const char* const days[] = {
    "Senin", "Selasa", "Rabu", "Kamis", "Jumat", "Sabtu", "Minggu"
  };
  const int32_t jd = static_cast<int32_t>(floor(julian(date) + 0.5));
  const int idx = static_cast<int>((jd % 7 + 7) % 7);
  return days[idx];
}
const char* pasaranName(Date date) {
  if (!validDate(date)) return "?";
  static const char* const pasaran[] = {"Legi", "Pahing", "Pon", "Wage", "Kliwon"};
  // Reference: 2026-11-07 (JD 2461352) is Sabtu Pahing -> index 1
  const int32_t jd = static_cast<int32_t>(floor(julian(date) + 0.5));
  const int idx = static_cast<int>(((jd - 2461351) % 5 + 5) % 5);
  return pasaran[idx];
}
const char* moonPhaseName(Date date) {
  if (!validDate(date)) return "?";
  static const char* const phases[] = {
    "NEW MOON", "WAXING CRESC", "FIRST QUARTER", "WAXING GIBB",
    "FULL MOON", "WANING GIBB", "LAST QUARTER", "WANING CRESC"
  };
  const double synodic = 29.530588853;
  const double age = normalize(julian(date) - 2451550.1, synodic);
  const int index = static_cast<int>(floor((age / synodic) * 8.0 + 0.5)) % 8;
  return phases[index];
}
int dhuhaMinute(const Schedule& schedule, int offsetAfterSyuruq) {
  if (!schedule.valid || offsetAfterSyuruq < 10 || offsetAfterSyuruq > 45) return -1;
  const int syuruq = schedule.minutes[Syuruq];
  if (syuruq < 0) return -1;
  const int dhuha = syuruq + offsetAfterSyuruq;
  return (dhuha > syuruq && dhuha < schedule.minutes[Dzuhur]) ? dhuha : -1;
}
}  // namespace jws
