// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "PrayerCalculator.h"
#include <stddef.h>

namespace jws {
inline int dayOfYear(Date d) {
  if (!validDate(d)) return -1;
  static const int lengths[] = {31,28,31,30,31,30,31,31,30,31,30,31};
  const bool leap = d.year % 4 == 0 && (d.year % 100 != 0 || d.year % 400 == 0);
  int result = d.day - 1;
  for (int m = 1; m < d.month; ++m) result += lengths[m-1] + (m == 2 && leap ? 1 : 0);
  return result;
}
inline Schedule lookupAnnual(Date d, int year, const uint16_t (*rows)[PrayerCount], size_t rowCount) {
  Schedule result;
  result.date = d;
  const int index = dayOfYear(d);
  if (!rows || year != d.year || index < 0 || static_cast<size_t>(index) >= rowCount) return result;
  result.valid = true;
  for (int i = 0; i < PrayerCount; ++i) {
    result.minutes[i] = rows[index][i];
    if (result.minutes[i] >= 1440 || (i > 0 && result.minutes[i] <= result.minutes[i-1])) result.valid = false;
  }
  return result;
}
}  // namespace jws
