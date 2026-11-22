// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <Arduino.h>
#include <Adafruit_ILI9341.h>
#include <Interfaces.h>
#include <atomic>
#include <stdint.h>
#include "Xpt2046Touch.h"
#include "OnlineFeedService.h"
#include "MikrotikService.h"
#include "XiaozhiService.h"
#include "RelayControlService.h"
#include "ChronosService.h"

enum class ScreenScene : uint8_t {
  Prayer = 0,
  Adzan = 1,
  Iqomah = 2,
  Khotbah = 3,
  Qr = 4,
  Sholat = 5
};

class Ili9341DisplayDriver final : public jws::DisplayDriver {
 public:
  bool begin() override;
  void render(const jws::DisplayFrame& frame, uint32_t nowMs) override;
  void setForcedScene(uint8_t scene, uint32_t durationMs = 20000) override;
  void setActiveSlide(uint8_t slide) override;
  uint8_t getActiveSlide() const override { return activeSlide_; }
  void setColorTheme(uint8_t theme) override;
  uint8_t getColorTheme() const override { return colorTheme_; }
  ScreenScene activeScene() const { return currentScene_; }
 private:
#ifdef JWS_DESKTOP_S3
  // ESP32-S3 SPI: SCK=12, MISO=9, MOSI=11, CS=10, DC=13, RST=14
  Adafruit_ILI9341 tft_{&SPI, 13, 10, 14};
  jws::Xpt2046Touch touch_{3, 1}; // CS=3, IRQ=1
#else
  // ESP32 VSPI: SCK=18, MISO=19, MOSI=23, CS=27, DC=26.
  Adafruit_ILI9341 tft_{&SPI, 26, 27, -1};
  jws::Xpt2046Touch touch_{-1, -1};
#endif

  jws::DisplayFrame previous_{};
  bool haveFrame_ = false;
  ScreenScene currentScene_ = ScreenScene::Prayer;
  ScreenScene lastScene_ = ScreenScene::Prayer;
  bool sceneInitDone_ = false;
  int lastRemSec_ = -1;
  int forcedScene_ = -1;
  uint32_t forcedSceneUntilMs_ = 0;
  uint32_t lastSceneRotateMs_ = 0;
  uint8_t rotateStep_ = 0;
  int16_t scrollX_ = 320;
  uint32_t lastScrollMs_ = 0;

  // Multi-Slide & Theme State
  uint8_t activeSlide_ = 0; // 0=JWS, 1=Weather, 2=Stocks, 3=News, 4=Mosque, 5=MikrotikNoc, 6=XiaozhiAI, 7=Focus&System
                            // Sub-menus: 10=StockDetail, 11=NewsDetail, 12=PortDetail, 13=PPPoE, 14=Hotspot
  uint8_t lastRenderedSlide_ = 0;
  uint8_t colorTheme_ = 0;  // 0=Midnight Gold, 1=Emerald Mosque, 2=Royal Sapphire, 3=Cyber Sunset
  uint32_t lastSlideTouchMs_ = 0;
  int selectedStockIndex_ = 0;
  int selectedNewsIndex_ = 0;
  int selectedPortIndex_ = 0;
  int nocListPage_ = 0;
  uint32_t lastNocRefreshMs_ = 0;
  uint32_t lastXiaozhiAnimMs_ = 0;
  uint32_t lastLocalIp_ = 0xFFFFFFFFU;

  // Slide 7: Daily Productivity (Pomodoro Focus Timer + Digital Tasbih + System Health)
  bool pomoRunning_ = false;
  bool pomoBreakMode_ = false;
  uint32_t pomoRemainSec_ = 25UL * 60UL;
  uint32_t pomoLastTickMs_ = 0;
  uint32_t tasbihCount_ = 0;
  uint16_t tasbihTarget_ = 33;
  uint32_t lastFocusRefreshMs_ = 0;
  uint32_t lastRelayRefreshMs_ = 0;
  uint32_t lastRgbLedMs_ = 0;
  bool chronosAlertActive_ = false;

  void handleTouch(const jws::DisplayFrame& frame, uint32_t nowMs);
  void updateStatusRgbLed(ScreenScene scene, uint32_t nowMs);
  bool drawChronosAlertOverlay(uint32_t nowMs);
  void drawSlideWeather(const jws::DisplayFrame& frame);
  void drawSlideStocks(const jws::DisplayFrame& frame);
  void drawSlideNews(const jws::DisplayFrame& frame);
  void drawSlideMosque(const jws::DisplayFrame& frame);
  void drawSlideStockDetail(const jws::DisplayFrame& frame);
  void drawSlideNewsDetail(const jws::DisplayFrame& frame);
  void drawSlideMikrotik(const jws::DisplayFrame& frame, uint32_t nowMs);
  void drawSlideMikrotikPortDetail(const jws::DisplayFrame& frame, uint32_t nowMs);
  void drawSlideMikrotikPppoe(const jws::DisplayFrame& frame, uint32_t nowMs);
  void drawSlideMikrotikHotspot(const jws::DisplayFrame& frame, uint32_t nowMs);
  void drawSlideXiaozhi(const jws::DisplayFrame& frame, uint32_t nowMs);
  void drawSlideFocusSystem(const jws::DisplayFrame& frame, uint32_t nowMs);
  void drawSlideRelayControl(const jws::DisplayFrame& frame, uint32_t nowMs);
  void drawSlideDots(uint8_t activeSlide);

  void drawScenePrayer(const jws::DisplayFrame& frame, uint32_t nowMs);
  void drawSceneAdzan(const jws::DisplayFrame& frame, const char* pName, int remSec);
  void drawSceneIqomah(const jws::DisplayFrame& frame, const char* pName, int remSec, int totSec);
  void drawSceneKhotbah(const jws::DisplayFrame& frame);
  void drawSceneQr(const jws::DisplayFrame& frame);
  void drawSceneSholat(const jws::DisplayFrame& frame);

  void drawChrome();
  void drawClock(const jws::ClockReading& clock);
  void drawDate(const jws::DisplayFrame& frame);
  void drawSchedule(const jws::DisplayFrame& frame);
  void drawCountdown(const jws::DisplayFrame& frame);
  void drawTitle(const jws::Configuration& config);
  void drawTicker(const jws::Configuration& config, uint32_t nowMs, bool reset);
};
class SerialMockAudio final : public jws::AudioDriver {
 public:
  void play(uint16_t track) override { Serial.printf("[AUDIO MOCK] play track=%u\n", track); }
  void stop() override { Serial.println("[AUDIO MOCK] stop"); }
  void tick(uint32_t) override {}
};
class NtpClockProvider final : public jws::ClockProvider {
 public:
  void begin(const jws::Configuration& configuration) override;
  void tick(uint32_t nowMs) override;
  jws::ClockReading now() const override;
  void configure(const jws::Configuration& config) override {
    offsetMinutes_.store(config.calculation.utcOffsetMinutes, std::memory_order_relaxed);
  }
  void setEpochTime(time_t epochSeconds);
  bool isSynchronized() const { return synchronized_.load(std::memory_order_acquire); }
 private:
  std::atomic<int> offsetMinutes_{0};
  uint32_t lastRetryMs_ = 0;
  std::atomic<uint32_t> lastSyncMs_{0};
  std::atomic<bool> synchronized_{false};
};
