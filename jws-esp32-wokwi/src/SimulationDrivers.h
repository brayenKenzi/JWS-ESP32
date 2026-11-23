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

// Subclass of Adafruit_ILI9341 that scales all RGB565 colors in software (5%..100%)
// so TFT brightness dimming works directly on the screen without needing a GPIO backlight wire!
class DimmableILI9341 final : public Adafruit_ILI9341 {
 public:
  using Adafruit_ILI9341::Adafruit_ILI9341;

  void setBrightnessPct(uint8_t pct) {
    if (pct < 5) pct = 5;
    if (pct > 100) pct = 100;
    briPct_ = pct;
  }
  uint8_t brightnessPct() const { return briPct_; }

  uint16_t dimColor(uint16_t col) const {
    if (briPct_ >= 100 || col == 0) return col;
    const uint32_t r = (((col >> 11) & 0x1FU) * briPct_) / 100U;
    const uint32_t g = (((col >> 5)  & 0x3FU) * briPct_) / 100U;
    const uint32_t b = ((col         & 0x1FU) * briPct_) / 100U;
    return static_cast<uint16_t>((r << 11) | (g << 5) | b);
  }

  void drawPixel(int16_t x, int16_t y, uint16_t color) override {
    Adafruit_ILI9341::drawPixel(x, y, dimColor(color));
  }
  void writePixel(int16_t x, int16_t y, uint16_t color) override {
    Adafruit_ILI9341::writePixel(x, y, dimColor(color));
  }
  void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) override {
    Adafruit_ILI9341::fillRect(x, y, w, h, dimColor(color));
  }
  void writeFillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) override {
    Adafruit_ILI9341::writeFillRect(x, y, w, h, dimColor(color));
  }
  void drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) override {
    Adafruit_ILI9341::drawFastHLine(x, y, w, dimColor(color));
  }
  void writeFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) override {
    Adafruit_ILI9341::writeFastHLine(x, y, w, dimColor(color));
  }
  void drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color) override {
    Adafruit_ILI9341::drawFastVLine(x, y, h, dimColor(color));
  }
  void writeFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color) override {
    Adafruit_ILI9341::writeFastVLine(x, y, h, dimColor(color));
  }

 private:
  uint8_t briPct_ = 80;
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
  DimmableILI9341 tft_{&SPI, 13, 10, 14};
  jws::Xpt2046Touch touch_{3, 1}; // CS=3, IRQ=1
#else
  // ESP32 VSPI: SCK=18, MISO=19, MOSI=23, CS=27, DC=26.
  DimmableILI9341 tft_{&SPI, 26, 27, -1};
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

  // Multi-Slide & Theme State (8 Main Slides: 0..7)
  // 0=JWS, 1=Weather, 2=Stocks, 3=News, 4=MikrotikNoc, 5=XiaozhiAI, 6=TaskManager, 7=RelayControl
  // Sub-menus: 10=StockDetail, 11=NewsDetail, 12=PortDetail, 13=PPPoE, 14=Hotspot
  uint8_t activeSlide_ = 0;
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

  // Slide 6: Task Manager (CPU, Internal RAM, Octal PSRAM Live Graphs + Brightness)
  uint8_t cpuHist_[20] = {0};
  uint8_t ramHist_[20] = {0};
  uint8_t psramHist_[20] = {0};
  uint32_t lastTaskSampleMs_ = 0;
  uint32_t frameTickCount_ = 0;
  uint8_t lastAppliedBri_ = 0;
  uint32_t lastFocusRefreshMs_ = 0;
  uint32_t lastRelayRefreshMs_ = 0;
  uint32_t lastRgbLedMs_ = 0;
  bool chronosAlertActive_ = false;

  void handleTouch(const jws::DisplayFrame& frame, uint32_t nowMs);
  void updateStatusRgbLed(ScreenScene scene, uint32_t nowMs);
  void updateBacklight(const jws::DisplayFrame& frame, uint32_t nowMs);
  bool drawChronosAlertOverlay(uint32_t nowMs);
  void drawSlideWeather(const jws::DisplayFrame& frame);
  void drawSlideStocks(const jws::DisplayFrame& frame);
  void drawSlideNews(const jws::DisplayFrame& frame);
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
