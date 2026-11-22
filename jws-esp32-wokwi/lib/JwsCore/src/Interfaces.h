// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "Configuration.h"

namespace jws {

enum class ClockSource { Waiting, Ntp, Holdover, Mock };
struct ClockReading {
  Date date{};
  int hour = 0;
  int minute = 0;
  int second = 0;
  ClockSource source = ClockSource::Waiting;
  bool valid = false;
};
struct DisplayFrame { ClockReading clock; Schedule schedule; Schedule baseSchedule; Configuration configuration; bool officialSchedule = false; };
class ScheduleProvider {
 public:
  ScheduleProvider() = default;
  virtual ~ScheduleProvider() = default;
  ScheduleProvider(const ScheduleProvider&) = delete;
  ScheduleProvider& operator=(const ScheduleProvider&) = delete;
  ScheduleProvider(ScheduleProvider&&) = delete;
  ScheduleProvider& operator=(ScheduleProvider&&) = delete;
  virtual Schedule getSchedule(Date date, const Configuration& configuration) = 0;
  virtual bool isOfficial() const { return false; }
};
class DisplayDriver {
 public:
  DisplayDriver() = default;
  virtual ~DisplayDriver() = default;
  DisplayDriver(const DisplayDriver&) = delete;
  DisplayDriver& operator=(const DisplayDriver&) = delete;
  DisplayDriver(DisplayDriver&&) = delete;
  DisplayDriver& operator=(DisplayDriver&&) = delete;
  virtual bool begin() = 0;
  virtual void render(const DisplayFrame& frame, uint32_t nowMs) = 0;
  virtual void setForcedScene(uint8_t scene, uint32_t durationMs = 20000) { (void)scene; (void)durationMs; }
  virtual void setActiveSlide(uint8_t slide) { (void)slide; }
  virtual uint8_t getActiveSlide() const { return 0; }
  virtual void setColorTheme(uint8_t theme) { (void)theme; }
  virtual uint8_t getColorTheme() const { return 0; }
};
class AudioDriver {
 public:
  AudioDriver() = default;
  virtual ~AudioDriver() = default;
  AudioDriver(const AudioDriver&) = delete;
  AudioDriver& operator=(const AudioDriver&) = delete;
  AudioDriver(AudioDriver&&) = delete;
  AudioDriver& operator=(AudioDriver&&) = delete;
  virtual void play(uint16_t track) = 0;
  virtual void stop() = 0;
  virtual void tick(uint32_t nowMs) = 0;
};
class ClockProvider {
 public:
  ClockProvider() = default;
  virtual ~ClockProvider() = default;
  ClockProvider(const ClockProvider&) = delete;
  ClockProvider& operator=(const ClockProvider&) = delete;
  ClockProvider(ClockProvider&&) = delete;
  ClockProvider& operator=(ClockProvider&&) = delete;
  virtual void begin(const Configuration& configuration) = 0;
  virtual void tick(uint32_t nowMs) = 0;
  virtual ClockReading now() const = 0;
  virtual void configure(const Configuration&) {}
};
class ConfigurationStore {
 public:
  ConfigurationStore() = default;
  virtual ~ConfigurationStore() = default;
  ConfigurationStore(const ConfigurationStore&) = delete;
  ConfigurationStore& operator=(const ConfigurationStore&) = delete;
  ConfigurationStore(ConfigurationStore&&) = delete;
  ConfigurationStore& operator=(ConfigurationStore&&) = delete;
  virtual bool load(Configuration& configuration) = 0;
  virtual bool save(const Configuration& configuration) = 0;
};
}  // namespace jws
