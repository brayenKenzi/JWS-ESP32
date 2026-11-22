// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "Interfaces.h"

namespace jws {
class JwsApp {
 public:
  JwsApp(ClockProvider& clock, DisplayDriver& display, ConfigurationStore& store,
         ScheduleProvider* provider = nullptr)
      : clock_(clock), display_(display), store_(store), provider_(provider) {}
  bool begin() {
    if (!store_.load(configuration_) || !validConfiguration(configuration_)) return false;
    frame_.configuration = configuration_;
    clock_.begin(configuration_);
    return display_.begin();
  }
  bool tick(uint32_t nowMs) {
    clock_.tick(nowMs);
    frame_.clock = clock_.now();
    bool changed = false;
    const Date date = frame_.clock.date;
    if (frame_.clock.valid && (!haveSchedule_ || date.year != frame_.schedule.date.year ||
        date.month != frame_.schedule.date.month || date.day != frame_.schedule.date.day)) {
      frame_.baseSchedule = provider_ ? provider_->getSchedule(date, configuration_) : calculate(date, configuration_.calculation);
      frame_.schedule = correctedSchedule(frame_.baseSchedule, configuration_);
      frame_.officialSchedule = provider_ && provider_->isOfficial();
      haveSchedule_ = true;
      changed = true;
    }
    const bool tickerActive = configuration_.runningTextEnabled && configuration_.runningText[0] != '\0';
    const uint32_t interval = tickerActive ? configuration_.scrollIntervalMs : 250;
    if (!haveRendered_ || static_cast<uint32_t>(nowMs - lastRenderMs_) >= interval) {
      lastRenderMs_ = nowMs;
      haveRendered_ = true;
      display_.render(frame_, nowMs);
    }
    return changed;
  }
  bool applyConfiguration(const Configuration& c) {
    if (!validConfiguration(c)) return false;
    // Reject corrections that would reorder the currently available daily table or reference schedule.
    const Schedule reference = frame_.baseSchedule.valid
        ? frame_.baseSchedule
        : (provider_ ? provider_->getSchedule({2026, 11, 7}, c) : calculate({2026, 11, 7}, c.calculation));
    if (reference.valid && !correctedSchedule(reference, c).valid) return false;
    if (!store_.save(c)) return false;
    configuration_ = c;
    frame_.configuration = c;
    clock_.configure(c);
    invalidateSchedule();
    haveRendered_ = false;
    return true;
  }
  void invalidateSchedule() { haveSchedule_ = false; }
  void setScene(uint8_t scene, uint32_t durationMs = 20000) { display_.setForcedScene(scene, durationMs); }
  void setSlide(uint8_t slide) { display_.setActiveSlide(slide); }
  uint8_t getSlide() const { return display_.getActiveSlide(); }
  void setTheme(uint8_t theme) { display_.setColorTheme(theme); }
  uint8_t getTheme() const { return display_.getColorTheme(); }
  const Configuration& configuration() const { return configuration_; }
  const DisplayFrame& frame() const { return frame_; }
 private:
  ClockProvider& clock_;
  DisplayDriver& display_;
  ConfigurationStore& store_;
  ScheduleProvider* provider_;
  Configuration configuration_;
  DisplayFrame frame_{};
  bool haveSchedule_ = false;
  bool haveRendered_ = false;
  uint32_t lastRenderMs_ = 0;
};
}  // namespace jws
