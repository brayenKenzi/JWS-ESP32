// SPDX-License-Identifier: GPL-3.0-only
#include <PrayerCalculator.h>
#include <Simulation.h>
#include <JwsApp.h>
#include <Timetable.h>
#include "KemenagData.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fixtures.h"

namespace {
int passed = 0;
int failed = 0;
void check(bool ok, const char* label) {
  printf("%s %s\n", ok ? "PASS" : "FAIL", label);
  ok ? ++passed : ++failed;
}
struct FakeDisplay final : jws::DisplayDriver {
  int renders = 0;
  jws::DisplayFrame frame{};
  bool begin() override { return true; }
  void render(const jws::DisplayFrame& f, uint32_t) override { frame = f; ++renders; }
};
struct WaitingClock final : jws::ClockProvider {
  void begin(const jws::Configuration&) override {}
  void tick(uint32_t) override {}
  jws::ClockReading now() const override { return {}; }
};
}
int main() {
  using namespace jws;
  for (const Fixture& f : fixtures) {
    CalculationSettings s;
    s.latitude = f.latitude;
    s.longitude = f.longitude;
    s.utcOffsetMinutes = f.offset;
    const Schedule result = calculate(f.date, s);
    bool matches = result.valid;
    for (int i = 0; i < PrayerCount; ++i) {
      if (abs(result.minutes[i] - f.minutes[i]) > 1) {
        printf("  %s got=%d expected=%d\n", prayerName(static_cast<Prayer>(i)), result.minutes[i], f.minutes[i]);
        matches = false;
      }
    }
    check(matches, f.name);
  }
  CalculationSettings settings;
  const Date date{2026,11,3};
  const Schedule base = calculate(date, settings);
  check(base.valid && base.minutes[Subuh] - base.minutes[Imsak] == 10, "Imsak = corrected Subuh minus 10 minutes");
  auto shiftedSettings = settings;
  shiftedSettings.utcOffsetMinutes += 60;
  const auto shifted = calculate(date, shiftedSettings);
  bool shiftOk = shifted.valid;
  for (int i = 0; i < PrayerCount; ++i) shiftOk &= shifted.minutes[i] == base.minutes[i] + 60;
  check(shiftOk, "UTC offset shifts every event by exactly 60 minutes");
  shiftedSettings = settings;
  shiftedSettings.ihtiyatMinutes = 0;
  const auto uncorrected = calculate(date, shiftedSettings);
  bool correctionOk = uncorrected.valid;
  for (int i = 0; i < PrayerCount; ++i)
    correctionOk &= base.minutes[i] - uncorrected.minutes[i] == (i == Syuruq ? 0 : 2);
  check(correctionOk, "Ihtiyat affects prayers and imsak, leaves Syuruq unchanged");
  shiftedSettings = settings;
  shiftedSettings.asrShadowFactor = 2;
  check(calculate(date, shiftedSettings).minutes[Ashar] > base.minutes[Ashar], "Hanafi Ashar is later than standard");
  check(validDate({2024,2,29}) && !validDate({2026,2,29}) && !validDate({2026,4,31}) &&
        !validDate({2026,0,1}) && !validDate({2100,1,1}), "Calendar validation and supported year boundaries");
  check(!calculate({2026,2,29}, settings).valid, "Invalid date produces invalid schedule");
  shiftedSettings = settings;
  shiftedSettings.latitude = 69.6492;
  shiftedSettings.longitude = 18.9553;
  shiftedSettings.utcOffsetMinutes = 120;
  const auto polar = calculate({2026,6,21}, shiftedSettings);
  check(!polar.valid && polar.minutes[Syuruq] == -1, "Polar day explicitly unavailable; no invented high-latitude times");
  shiftedSettings.latitude = NAN;
  check(!validSettings(shiftedSettings) && !calculate(date, shiftedSettings).valid, "Reject NaN latitude");
  shiftedSettings = settings;
  shiftedSettings.utcOffsetMinutes = 841;
  check(!validSettings(shiftedSettings), "Reject invalid timezone");
  shiftedSettings = settings;
  shiftedSettings.latitude = 0;
  shiftedSettings.longitude = 0;
  shiftedSettings.utcOffsetMinutes = 0;
  check(calculate(date, shiftedSettings).valid, "Zero latitude/longitude/timezone remain valid configuration");
  shiftedSettings = settings;
  shiftedSettings.imsakMinutes = 0;
  check(calculate(date, shiftedSettings).valid, "Zero-minute imsak accepted");
  char output[6];
  formatMinute(-1, output);
  check(strcmp(output, "--:--") == 0, "Invalid minute formatted explicitly");
  formatMinute(1439, output);
  check(strcmp(output, "23:59") == 0, "Last minute formatted safely");
  MockClockProvider clock;
  FakeDisplay display;
  MemoryConfigurationStore store;
  JwsApp app(clock, display, store);
  check(app.begin() && app.tick(0) && app.frame().clock.date.day == 3 &&
        app.frame().clock.hour == 23 && app.frame().clock.second == 50, "Mock starts deterministically and computes first schedule");
  const int renders = display.renders;
  check(!app.tick(25) && display.renders == renders, "App neither recalculates nor renders inside frame interval");
  check(!app.tick(9999) && app.frame().clock.second == 59, "Sub-second accumulation before midnight");
  check(app.tick(10000) && app.frame().clock.date.day == 4 && app.frame().clock.hour == 0 &&
        app.frame().schedule.date.day == 4, "Local midnight advances clock and recomputes schedule");
  check(!app.tick(11000), "No duplicate daily recalculation");
  clock.begin({});
  clock.tick(UINT32_MAX - 500);
  const auto before = clock.now();
  clock.tick(499);  // exactly 1000 ms including rollover
  const auto after = clock.now();
  check((after.second + 60 - before.second) % 60 == 1, "Mock handles millis rollover");
  Configuration config;
  config.calculation.utcOffsetMinutes = 10000;
  check(!store.save(config), "Store rejects invalid settings");
  WaitingClock waiting;
  FakeDisplay waitingDisplay;
  JwsApp waitingApp(waiting, waitingDisplay, store);
  check(waitingApp.begin() && !waitingApp.tick(0) && !waitingApp.frame().clock.valid &&
        !waitingApp.frame().schedule.valid, "Unsynchronized clock does not publish prayer schedule");
  const auto official = lookupAnnual({2026,11,5}, KemenagBuiltInYear,
      KemenagBuiltInMinutes, sizeof KemenagBuiltInMinutes / sizeof KemenagBuiltInMinutes[0]);
  const int expectedOfficial[] = {236,246,320,690,890,1052,1123};
  bool officialMatches = official.valid;
  for (int i = 0; i < PrayerCount; ++i) officialMatches &= official.minutes[i] == expectedOfficial[i];
  check(officialMatches, "Official Kemenag Kotamobagu 2026-11-05 fixture (not astronomical calculation)");
  check(dayOfYear({2024,3,1}) == 60 && dayOfYear({2026,3,1}) == 59 &&
        dayOfYear({2026,2,29}) == -1, "Official table indexing validates leap days");
  check(!lookupAnnual({2027,1,1}, KemenagBuiltInYear, KemenagBuiltInMinutes, 365).valid,
        "Official table never relabels a different year's data");
  bool annualOrdered = true;
  for (const auto& row : KemenagBuiltInMinutes) for (int i = 0; i < PrayerCount; ++i)
    annualOrdered &= row[i] < 1440 && (i == 0 || row[i] > row[i-1]);
  check(annualOrdered, "All imported official daily events are ordered and inside the day");
  Configuration officialConfig;
  const auto untouched = correctedSchedule(official, officialConfig);
  bool unchanged = untouched.valid;
  for (int i = 0; i < PrayerCount; ++i) unchanged &= untouched.minutes[i] == official.minutes[i];
  check(unchanged, "Zero corrections preserve official values with no implicit ihtiyat");
  officialConfig.corrections[Subuh] = 3;
  officialConfig.corrections[Maghrib] = -2;
  const auto adjusted = correctedSchedule(official, officialConfig);
  check(adjusted.valid && adjusted.minutes[Subuh] == 249 && adjusted.minutes[Maghrib] == 1050 &&
        adjusted.minutes[Dzuhur] == official.minutes[Dzuhur] && official.minutes[Subuh] == 246,
        "Per-prayer positive/negative corrections do not mutate official base");
  officialConfig.corrections[Imsak] = 30;
  check(!correctedSchedule(official, officialConfig).valid, "Corrections cannot reorder Imsak and Subuh");
  officialConfig = {};
  officialConfig.corrections[Isya] = 31;
  check(!validConfiguration(officialConfig), "Correction bounds enforced at 30 minutes");
  officialConfig = {};
  officialConfig.mosqueName[0] = '\0';
  check(!validConfiguration(officialConfig), "Empty mosque name rejected");
  officialConfig = {};
  memset(officialConfig.runningText, 'A', sizeof officialConfig.runningText);
  check(!validConfiguration(officialConfig), "Unterminated running text rejected safely");
  officialConfig = {};
  officialConfig.scrollIntervalMs = 0;
  check(!validConfiguration(officialConfig), "Zero running-text interval rejected");
  struct OfficialProvider final : ScheduleProvider {
    Schedule getSchedule(Date d, const Configuration&) override {
      return lookupAnnual(d, KemenagBuiltInYear, KemenagBuiltInMinutes, 365);
    }
    bool isOfficial() const override { return true; }
  } officialProvider;
  MockClockProvider editableClock;
  MemoryConfigurationStore editableStore;
  FakeDisplay editableDisplay;
  JwsApp editableApp(editableClock, editableDisplay, editableStore, &officialProvider);
  check(editableApp.begin() && editableApp.tick(0) && editableApp.frame().officialSchedule &&
        editableApp.frame().baseSchedule.valid, "App obtains official schedule through injected provider");
  auto edit = editableApp.configuration();
  edit.revision++;
  edit.corrections[Dzuhur] = 5;
  strcpy(edit.mosqueName, "Masjid Uji");
  check(editableApp.applyConfiguration(edit) && editableApp.tick(1000) &&
        editableApp.frame().schedule.minutes[Dzuhur] == editableApp.frame().baseSchedule.minutes[Dzuhur] + 5 &&
        strcmp(editableApp.frame().configuration.mosqueName, "Masjid Uji") == 0 &&
        editableApp.frame().clock.second == 51, "Saved edits recalculate same day without resetting mock clock");
  const auto acceptedRevision = editableApp.configuration().revision;
  edit.corrections[Imsak] = 30;
  check(!editableApp.applyConfiguration(edit) && editableApp.configuration().revision == acceptedRevision,
        "Invalid correction update is atomic and preserves current config");
  struct FailingStore final : ConfigurationStore {
    bool load(Configuration& c) override { c = {}; return true; }
    bool save(const Configuration&) override { return false; }
  } failingStore;
  MockClockProvider failureClock;
  FakeDisplay failureDisplay;
  JwsApp failingApp(failureClock, failureDisplay, failingStore);
  failingApp.begin();
  auto unsaved = failingApp.configuration();
  strcpy(unsaved.mosqueName, "Belum Tersimpan");
  check(!failingApp.applyConfiguration(unsaved) &&
        strcmp(failingApp.configuration().mosqueName, "Nama Masjid") == 0,
        "Persistence failure never reports an applied configuration");
  check(!printableText(nullptr, 16, true) && !printableText("OK", 0, true),
        "Null or zero-capacity text rejected safely");
  check(!lookupAnnual({2026, 11, 7}, KemenagBuiltInYear, nullptr, 365).valid,
        "Null annual table pointer rejected safely");
  WaitingClock waitingForConfig;
  FakeDisplay waitingConfigDisplay;
  MemoryConfigurationStore waitingStore;
  JwsApp waitingConfigApp(waitingForConfig, waitingConfigDisplay, waitingStore, &officialProvider);
  waitingConfigApp.begin();
  auto badWaitingEdit = waitingConfigApp.configuration();
  badWaitingEdit.revision++;
  badWaitingEdit.corrections[Imsak] = 30;
  badWaitingEdit.corrections[Subuh] = -30;
  check(!waitingConfigApp.applyConfiguration(badWaitingEdit),
        "Invalid correction order rejected even before clock synchronization");
  const auto nextAtNoon = nextPrayerCountdown(official, 12, 11, 23);
  char cdText[10];
  formatCountdown(nextAtNoon.secondsRemaining, cdText);
  check(nextAtNoon.valid && nextAtNoon.prayer == Ashar &&
        nextAtNoon.secondsRemaining == (890 * 60 - (12 * 3600 + 11 * 60 + 23)) &&
        strcmp(cdText, "-02:38:37") == 0,
        "Countdown at 12:11:23 targets Ashar (14:50) with -02:38:37");
  const auto nextAfterIsya = nextPrayerCountdown(official, 19, 0, 0);
  formatCountdown(nextAfterIsya.secondsRemaining, cdText);
  check(nextAfterIsya.valid && nextAfterIsya.prayer == Subuh &&
        strcmp(cdText, "-09:06:00") == 0,
        "Countdown after Isya wraps to next day Subuh");
  Configuration panelCfg{};
  panelCfg.panelCount = 0;
  check(!validConfiguration(panelCfg), "Zero panelCount rejected");
  panelCfg.panelCount = 5;
  check(!validConfiguration(panelCfg), "Panel count above 4 rejected");
  panelCfg.panelCount = 4;
  panelCfg.calligraphyEnabled = false;
  check(validConfiguration(panelCfg), "Multi-panel count 1..4 and calligraphy toggle accepted");
  const auto hijri = gregorianToHijri({2026, 11, 7}, 0);
  check(hijri.valid && hijri.year == 1448 && hijri.month >= 1 && hijri.month <= 12 &&
        strcmp(hijriMonthName(hijri.month), "?") != 0,
        "Hijri calendar conversion produces valid 1448H date");
  check(strcmp(pasaranName({2026, 11, 7}), "Pahing") == 0 &&
        strcmp(moonPhaseName({2026, 11, 7}), "?") != 0,
        "Pasaran and moon phase computed deterministically");
  check(dhuhaMinute(official, 20) == official.minutes[Syuruq] + 20,
        "Dhuha time equals Syuruq plus 20 minutes");
  Configuration twinkleCfg{};
  twinkleCfg.panelRows = 3;
  check(!validConfiguration(twinkleCfg), "Panel rows above 2 rejected");
  twinkleCfg.panelRows = 2;
  twinkleCfg.brightness = 80;
  twinkleCfg.iqomahMinutes[0] = 15;
  check(validConfiguration(twinkleCfg), "Twinkle 2-row layout, brightness, and iqomah settings accepted");
  bool syuruqExcluded = (DisplayedPrayerCount == 6);
  for (int i = 0; i < DisplayedPrayerCount; ++i) {
    if (kDisplayedPrayers[i] == Syuruq) syuruqExcluded = false;
  }
  check(syuruqExcluded && strcmp(weekdayName({2026, 11, 7}), "Sabtu") == 0,
        "Displayed prayers are 6 (excluding Syuruq) and weekdayName returns Sabtu for 2026-11-07");
  printf("RESULT: %d passed, %d failed\n", passed, failed);
  return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
