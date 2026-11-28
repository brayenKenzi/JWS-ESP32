// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include <atomic>
#include <stdint.h>

namespace jws {

enum class AudioChime : uint8_t {
  None = 0,
  BootWelcome = 1,
  AdzanAlert = 2,
  IqomahTick = 3,
  IqomahFinal = 4,
  ChronosNotif = 5,
  ChronosRing = 6,
  TouchClick = 7
};

struct AudioStatus {
  bool hardwareReady = false;
  bool enabled = true;
  bool playing = false;
  uint8_t volume = 24; // 0..30
  char lastSpoken[96] = "";
};

class AudioService {
 public:
  static AudioService& instance();

  void begin();
  void setVolume(uint8_t vol0to30);
  uint8_t getVolume() const;
  void setEnabled(bool en);
  bool isEnabled() const;

  void playChime(AudioChime chime);
  void speakText(const char* text);
  void stop();
  bool isMicSuppressed() const;
  AudioStatus status();

 private:
  AudioService() = default;
  static void taskEntry(void* arg);
  void taskLoop();
  void renderChime(AudioChime chime);
  void renderTone(uint16_t freqHz, uint16_t durationMs, uint16_t pauseMs = 20);
  void renderTts(const char* text);

  Preferences prefs_;
  portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
  TaskHandle_t taskHandle_ = nullptr;
  uint8_t* mp3Buf_ = nullptr;
  size_t mp3BufCap_ = 0;

  bool initialized_ = false;
  bool i2sReady_ = false;
  std::atomic<bool> enabled_{true};
  std::atomic<bool> playing_{false};
  std::atomic<bool> stopReq_{false};
  std::atomic<uint8_t> volume_{24};
  std::atomic<uint8_t> pendingChime_{0};
  std::atomic<bool> pendingTts_{false};
  std::atomic<uint32_t> echoGuardUntilMs_{0};
  char ttsQueue_[220] = "";
  char lastSpoken_[96] = "Speaker MAX98357A Siap (GPIO 40, 41, 42)";
};

}  // namespace jws
