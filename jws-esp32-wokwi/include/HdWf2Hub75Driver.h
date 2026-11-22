// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <Arduino.h>
#include <Interfaces.h>
#include "CalligraphyArt.h"
#include <stdio.h>
#include <string.h>

#ifdef JWS_HD_WF2_HUB75
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#endif

// Pinout Resmi Huidu HD-WF2 (ESP32-S3) & ESP32 Classic (mengikuti standar Twinkle HUB75)
struct HdWf2Pinmap {
#if defined(CONFIG_IDF_TARGET_ESP32S3)
  static constexpr int8_t PinR1 = 2;
  static constexpr int8_t PinG1 = 6;
  static constexpr int8_t PinB1 = 10;
  static constexpr int8_t PinR2 = 3;
  static constexpr int8_t PinG2 = 7;
  static constexpr int8_t PinB2 = 11;
  static constexpr int8_t PinA = 39;
  static constexpr int8_t PinB = 38;
  static constexpr int8_t PinC = 37;
  static constexpr int8_t PinD = 36;
  static constexpr int8_t PinE = 21;
  static constexpr int8_t PinCLK = 34;
  static constexpr int8_t PinLAT = 33;
  static constexpr int8_t PinOE = 35;
  static constexpr int8_t PinBuzzer = 4;
  static constexpr int8_t PinDfRx = 5;
  static constexpr int8_t PinDfTx = 8;
  static constexpr int8_t PinRtcScl = 42;
  static constexpr int8_t PinRtcSda = 41;
#else
  static constexpr int8_t PinR1 = 19;
  static constexpr int8_t PinG1 = 13;
  static constexpr int8_t PinB1 = 18;
  static constexpr int8_t PinR2 = 5;
  static constexpr int8_t PinG2 = 12;
  static constexpr int8_t PinB2 = 17;
  static constexpr int8_t PinA = 16;
  static constexpr int8_t PinB = 14;
  static constexpr int8_t PinC = 4;
  static constexpr int8_t PinD = 27;
  static constexpr int8_t PinE = 25;
  static constexpr int8_t PinCLK = 2;
  static constexpr int8_t PinLAT = 23;
  static constexpr int8_t PinOE = 15;
  static constexpr int8_t PinBuzzer = 32;
  static constexpr int8_t PinDfRx = -1;
  static constexpr int8_t PinDfTx = -1;
  static constexpr int8_t PinRtcScl = 22;
  static constexpr int8_t PinRtcSda = 21;
#endif
};

class HdWf2Hub75Driver final : public jws::DisplayDriver {
 public:
  static constexpr uint16_t kPanelWidth = 64;
  static constexpr uint16_t kPanelHeight = 32;
  static constexpr uint8_t kMaxPanels = 4;

  bool begin() override {
#ifdef JWS_HD_WF2_HUB75
    return initMatrix(activePanels_, activeRows_);
#else
    return true;
#endif
  }

  void render(const jws::DisplayFrame& frame, uint32_t nowMs) override {
    const uint8_t requestedPanels =
        (frame.configuration.panelCount >= 1 && frame.configuration.panelCount <= kMaxPanels)
            ? frame.configuration.panelCount
            : 2;
    const uint8_t requestedRows =
        (frame.configuration.panelRows >= 1 && frame.configuration.panelRows <= 2)
            ? frame.configuration.panelRows
            : 1;

#ifdef JWS_HD_WF2_HUB75
    if (!dmaDisplay_ || requestedPanels != activePanels_ || requestedRows != activeRows_) {
      if (!initMatrix(requestedPanels, requestedRows)) return;
    }
    const uint8_t bri = frame.configuration.brightness;
    dmaDisplay_->setBrightness8(static_cast<uint8_t>((bri * 255U) / 100U));
    renderToCanvas(*dmaDisplay_, frame, nowMs, activePanels_, activeRows_);
#else
    activePanels_ = requestedPanels;
    activeRows_ = requestedRows;
    (void)nowMs;
#endif
  }

  static bool isSleeping(const jws::Configuration& cfg, const jws::ClockReading& clock) {
    if (!cfg.autoSleepEnabled || !clock.valid) return false;
    if (cfg.sleepHour == cfg.wakeHour) return false;
    if (cfg.sleepHour > cfg.wakeHour) {
      return clock.hour >= cfg.sleepHour || clock.hour < cfg.wakeHour;
    }
    return clock.hour >= cfg.sleepHour && clock.hour < cfg.wakeHour;
  }

  template <typename CanvasT>
  void renderToCanvas(CanvasT& canvas, const jws::DisplayFrame& frame, uint32_t nowMs,
                      uint8_t panelCount, uint8_t panelRows) {
    const uint16_t totalWidth = kPanelWidth * panelCount;
    const uint16_t totalHeight = kPanelHeight * panelRows;
    canvas.fillScreen(kBgBlack);
    if (isSleeping(frame.configuration, frame.clock)) {
      // Auto-Sleep mode: only draw a tiny dim heartbeat dot
      canvas.drawPixel(totalWidth - 2, totalHeight - 2, kBorderBlue);
      return;
    }

    canvas.setTextWrap(false);
    canvas.setTextSize(1);

    const bool showCalligraphy = frame.configuration.calligraphyEnabled;
    const auto nextInfo = frame.clock.valid
        ? jws::nextPrayerCountdown(frame.schedule, frame.clock.hour, frame.clock.minute, frame.clock.second)
        : jws::NextPrayerInfo{};
    char cdBuf[10];
    jws::formatCountdown(nextInfo.valid ? nextInfo.secondsRemaining : -1, cdBuf);

    if (panelRows == 2) {
      renderTwoRowTwinkleLayout(canvas, frame, nextInfo, cdBuf, showCalligraphy, totalWidth);
    } else if (panelCount == 1) {
      renderSinglePanel(canvas, frame, nextInfo, cdBuf, showCalligraphy, nowMs);
    } else if (panelCount == 2) {
      renderDualPanel(canvas, frame, nextInfo, cdBuf, showCalligraphy, nowMs);
    } else {
      renderWideMultiPanel(canvas, frame, nextInfo, cdBuf, showCalligraphy, totalWidth);
    }

    renderBottomTicker(canvas, frame.configuration, nowMs, totalWidth, totalHeight);
  }

 private:
  static constexpr uint16_t kBgBlack = 0x0000;
  static constexpr uint16_t kGold = 0xFEA0;
  static constexpr uint16_t kEmerald = 0x07E8;
  static constexpr uint16_t kCyan = 0x07FF;
  static constexpr uint16_t kWhite = 0xFFFF;
  static constexpr uint16_t kAmber = 0xFD20;
  static constexpr uint16_t kBorderBlue = 0x194A;

  uint8_t activePanels_ = 2;
  uint8_t activeRows_ = 1;
  int16_t tickerX_ = 128;
  uint32_t lastScrollStepMs_ = 0;
  char lastTickerText_[257] = "";

#ifdef JWS_HD_WF2_HUB75
  MatrixPanel_I2S_DMA* dmaDisplay_ = nullptr;

  bool initMatrix(uint8_t cols, uint8_t rows) {
    if (dmaDisplay_) {
      delete dmaDisplay_;
      dmaDisplay_ = nullptr;
    }
    activePanels_ = (cols >= 1 && cols <= kMaxPanels) ? cols : 2;
    activeRows_ = (rows >= 1 && rows <= 2) ? rows : 1;
    HUB75_I2S_CFG::i2s_pins pins = {
      HdWf2Pinmap::PinR1, HdWf2Pinmap::PinG1, HdWf2Pinmap::PinB1,
      HdWf2Pinmap::PinR2, HdWf2Pinmap::PinG2, HdWf2Pinmap::PinB2,
      HdWf2Pinmap::PinA,  HdWf2Pinmap::PinB,  HdWf2Pinmap::PinC,
      HdWf2Pinmap::PinD,  HdWf2Pinmap::PinE,
      HdWf2Pinmap::PinLAT, HdWf2Pinmap::PinOE, HdWf2Pinmap::PinCLK
    };
    HUB75_I2S_CFG mxconfig(kPanelWidth, kPanelHeight * activeRows_, activePanels_, pins);
    mxconfig.clkphase = false;
    mxconfig.driver = HUB75_I2S_CFG::FM6126A;
    dmaDisplay_ = new MatrixPanel_I2S_DMA(mxconfig);
    if (!dmaDisplay_->begin()) return false;
    dmaDisplay_->clearScreen();
    tickerX_ = kPanelWidth * activePanels_;
    return true;
  }
#endif

  // 2-Row (64px tall) Twinkle Layout: 32x32 Twinkle Calligraphy + Hari + Hijri + 6 Waktu Sholat (Tanpa Syuruq)
  template <typename CanvasT>
  void renderTwoRowTwinkleLayout(CanvasT& canvas, const jws::DisplayFrame& frame,
                                 const jws::NextPrayerInfo& nextInfo, const char* cdBuf,
                                 bool showCalligraphy, uint16_t totalWidth) {
    int16_t leftX = 2;
    int16_t rightX = totalWidth - 2;
    if (showCalligraphy) {
      // Twinkle 32x32 Medallion: Muhammad (محمد) on Left, Allah (الله) on Right — Distinct Gold/Cyan/Emerald Palette
      jws::drawTwinkleMedallion32x32(canvas, 0, 0, false, kGold, kCyan, kEmerald, kBgBlack);
      jws::drawTwinkleMedallion32x32(canvas, totalWidth - 32, 0, true, kGold, kCyan, kEmerald, kBgBlack);
      leftX = 34;
      rightX = totalWidth - 34;
    }

    // Nama Hari & Pasaran (Kiri Atas)
    if (frame.clock.valid) {
      canvas.setTextColor(kGold);
      canvas.setCursor(leftX, 2);
      canvas.print(jws::weekdayName(frame.clock.date));
      canvas.print(" ");
      canvas.setTextColor(kCyan);
      canvas.print(jws::pasaranName(frame.clock.date));

      const auto hijri = jws::gregorianToHijri(frame.clock.date, frame.configuration.hijriOffsetDays);
      canvas.setTextColor(kEmerald);
      canvas.setCursor(leftX, 11);
      canvas.printf("%02d-%02d-%04d | %d %s %dH",
                    frame.clock.date.day, frame.clock.date.month, frame.clock.date.year,
                    hijri.day, jws::hijriMonthName(hijri.month), hijri.year);
    }

    // Jam Utama Besar (Tengah/Kanan Atas)
    char timeStr[9];
    if (frame.clock.valid) {
      snprintf(timeStr, sizeof(timeStr), "%02d:%02d:%02d",
               frame.clock.hour, frame.clock.minute, frame.clock.second);
    } else {
      snprintf(timeStr, sizeof(timeStr), "--:--:--");
    }
    canvas.setTextSize(2);
    canvas.setTextColor(kWhite);
    canvas.setCursor(rightX - 96, 4);
    canvas.print(timeStr);
    canvas.setTextSize(1);

    canvas.setTextColor(kCyan);
    canvas.setCursor(leftX, 21);
    canvas.print(">");
    canvas.print(nextInfo.valid ? jws::prayerName(nextInfo.prayer) : "--");
    canvas.print(" ");
    canvas.print(cdBuf);

    // Row 2 (y=33..54): 6 Waktu Sholat (Imsak, Subuh, Dzuhur, Ashar, Maghrib, Isya — Tanpa Syuruq)
    const int16_t colWidth = totalWidth / jws::DisplayedPrayerCount;
    for (int i = 0; i < jws::DisplayedPrayerCount; ++i) {
      const jws::Prayer p = jws::kDisplayedPrayers[i];
      char tBuf[6];
      jws::formatMinute(frame.schedule.valid ? frame.schedule.minutes[p] : -1, tBuf);
      const int16_t cx = 2 + i * colWidth;
      const bool isNext = nextInfo.valid && nextInfo.prayer == p;
      canvas.setTextColor(isNext ? kCyan : kGold);
      canvas.setCursor(cx, 35);
      canvas.print(jws::prayerName(p));
      canvas.setTextColor(isNext ? kEmerald : kWhite);
      canvas.setCursor(cx, 45);
      canvas.print(tBuf);
    }
  }

  template <typename CanvasT>
  void renderSinglePanel(CanvasT& canvas, const jws::DisplayFrame& frame,
                         const jws::NextPrayerInfo& nextInfo, const char* cdBuf,
                         bool showCalligraphy, uint32_t nowMs) {
    if (showCalligraphy) {
      jws::drawCalligraphyMedallion(canvas, 0, 0, 12, false, kGold, kCyan, kBgBlack);
      jws::drawCalligraphyMedallion(canvas, 52, 0, 12, true, kGold, kCyan, kBgBlack);
    }
    char timeStr[9];
    if (frame.clock.valid) {
      snprintf(timeStr, sizeof(timeStr), "%02d:%02d", frame.clock.hour, frame.clock.minute);
    } else {
      snprintf(timeStr, sizeof(timeStr), "--:--");
    }
    canvas.setTextColor(kEmerald);
    canvas.setCursor(17, 2);
    canvas.print(timeStr);

    const uint8_t page = (nowMs / 3000U) % 7U;
    canvas.setCursor(2, 14);
    if (page < 6) {
      const jws::Prayer p = jws::kDisplayedPrayers[page];
      char pTime[6];
      jws::formatMinute(frame.schedule.valid ? frame.schedule.minutes[p] : -1, pTime);
      canvas.setTextColor(kGold);
      canvas.print(jws::prayerName(p));
      canvas.setTextColor(kWhite);
      canvas.print(" ");
      canvas.print(pTime);
    } else {
      canvas.setTextColor(kCyan);
      if (frame.clock.valid) {
        canvas.print(jws::weekdayName(frame.clock.date));
        canvas.print(" ");
      }
      canvas.print(cdBuf + 1);
    }
  }

  template <typename CanvasT>
  void renderDualPanel(CanvasT& canvas, const jws::DisplayFrame& frame,
                       const jws::NextPrayerInfo& nextInfo, const char* cdBuf,
                       bool showCalligraphy, uint32_t nowMs) {
    int16_t leftX = 2;
    int16_t rightLimit = 126;
    if (showCalligraphy) {
      jws::drawCalligraphyMedallion(canvas, 1, 1, 12, false, kGold, kCyan, kBgBlack);
      jws::drawCalligraphyMedallion(canvas, 115, 1, 12, true, kGold, kCyan, kBgBlack);
      leftX = 15;
      rightLimit = 113;
    }

    char timeStr[9];
    if (frame.clock.valid) {
      snprintf(timeStr, sizeof(timeStr), "%02d:%02d:%02d",
               frame.clock.hour, frame.clock.minute, frame.clock.second);
    } else {
      snprintf(timeStr, sizeof(timeStr), "--:--:--");
    }
    canvas.setTextColor(kEmerald);
    canvas.setCursor(leftX, 3);
    canvas.print(timeStr);

    canvas.setTextColor(kGold);
    canvas.setCursor(leftX + 51, 3);
    if (frame.clock.valid) {
      canvas.print(jws::weekdayName(frame.clock.date));
    }

    // 6 Waktu Sholat dibagi 2 grup x 3 waktu agar nama & jam tampil lega dan jelas
    const uint8_t group = (nowMs / 3500U) % 2U;
    const int startIdx = group * 3;
    for (int i = 0; i < 3; ++i) {
      const jws::Prayer p = jws::kDisplayedPrayers[startIdx + i];
      char tBuf[6];
      jws::formatMinute(frame.schedule.valid ? frame.schedule.minutes[p] : -1, tBuf);
      const int16_t cellX = 2 + i * 42;
      canvas.setTextColor(kGold);
      canvas.setCursor(cellX, 14);
      canvas.print(jws::prayerName(p));
      canvas.setTextColor(kWhite);
      canvas.print(":");
      canvas.print(tBuf);
    }
    (void)rightLimit;
    (void)nextInfo;
    (void)cdBuf;
  }

  template <typename CanvasT>
  void renderWideMultiPanel(CanvasT& canvas, const jws::DisplayFrame& frame,
                            const jws::NextPrayerInfo& nextInfo, const char* cdBuf,
                            bool showCalligraphy, uint16_t totalWidth) {
    int16_t headerLeft = 2;
    int16_t headerRight = totalWidth - 2;
    if (showCalligraphy) {
      jws::drawCalligraphyMedallion(canvas, 1, 0, 16, false, kGold, kCyan, kBgBlack);
      jws::drawCalligraphyMedallion(canvas, totalWidth - 17, 0, 16, true, kGold, kCyan, kBgBlack);
      headerLeft = 20;
      headerRight = totalWidth - 20;
    }

    char timeStr[9];
    if (frame.clock.valid) {
      snprintf(timeStr, sizeof(timeStr), "%02d:%02d:%02d",
               frame.clock.hour, frame.clock.minute, frame.clock.second);
    } else {
      snprintf(timeStr, sizeof(timeStr), "--:--:--");
    }
    canvas.setTextColor(kEmerald);
    canvas.setCursor(headerLeft, 1);
    canvas.print(timeStr);

    if (frame.clock.valid) {
      canvas.setTextColor(kGold);
      canvas.setCursor(headerLeft + 52, 1);
      canvas.print(jws::weekdayName(frame.clock.date));
      canvas.print(" | ");
    }
    canvas.setTextColor(kWhite);
    char cleanMosque[16];
    strncpy(cleanMosque, frame.configuration.mosqueName, sizeof(cleanMosque) - 1);
    cleanMosque[sizeof(cleanMosque) - 1] = '\0';
    canvas.print(cleanMosque);

    canvas.setTextColor(kGold);
    canvas.setCursor(headerRight - 78, 1);
    canvas.print(">");
    canvas.print(nextInfo.valid ? jws::prayerName(nextInfo.prayer) : "--");
    canvas.setTextColor(kCyan);
    canvas.print(" ");
    canvas.print(cdBuf);

    // 6 Waktu Sholat (Tanpa Syuruq) dengan kolom lebih lebar & jelas
    const int16_t availWidth = headerRight - headerLeft;
    const int16_t colWidth = availWidth / jws::DisplayedPrayerCount;
    for (int i = 0; i < jws::DisplayedPrayerCount; ++i) {
      const jws::Prayer p = jws::kDisplayedPrayers[i];
      char tBuf[6];
      jws::formatMinute(frame.schedule.valid ? frame.schedule.minutes[p] : -1, tBuf);
      const int16_t cx = headerLeft + i * colWidth;
      const bool isNext = nextInfo.valid && nextInfo.prayer == p;
      canvas.setTextColor(isNext ? kCyan : kGold);
      canvas.setCursor(cx, 10);
      canvas.print(jws::prayerName(p));
      canvas.setTextColor(isNext ? kWhite : kEmerald);
      canvas.setCursor(cx, 17);
      canvas.print(tBuf);
    }
  }

  static size_t panelCountWidthChars(uint16_t totalWidth) {
    return totalWidth >= 256 ? 5 : 3;
  }

  template <typename CanvasT>
  void renderBottomTicker(CanvasT& canvas, const jws::Configuration& cfg,
                          uint32_t nowMs, uint16_t totalWidth, uint16_t totalHeight) {
    const int16_t lineY = totalHeight - 9;
    canvas.drawFastHLine(0, lineY, totalWidth, kBorderBlue);
    if (!cfg.runningTextEnabled || !cfg.runningText[0]) return;

    if (strcmp(lastTickerText_, cfg.runningText) != 0) {
      strncpy(lastTickerText_, cfg.runningText, sizeof(lastTickerText_) - 1);
      lastTickerText_[sizeof(lastTickerText_) - 1] = '\0';
      tickerX_ = totalWidth;
      lastScrollStepMs_ = nowMs;
    }

    const uint32_t interval = cfg.scrollIntervalMs ? cfg.scrollIntervalMs : 80;
    if (nowMs - lastScrollStepMs_ >= interval) {
      const uint32_t steps = (nowMs - lastScrollStepMs_) / interval;
      lastScrollStepMs_ += steps * interval;
      tickerX_ -= static_cast<int16_t>(steps * 2);
      const int16_t textPixelWidth = static_cast<int16_t>(strlen(cfg.runningText) * 6);
      if (tickerX_ < -textPixelWidth) tickerX_ = totalWidth;
    }

    canvas.setTextColor(kGold);
    canvas.setCursor(tickerX_, lineY + 2);
    canvas.print(cfg.runningText);
  }
};
