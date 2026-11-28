// SPDX-License-Identifier: GPL-3.0-only
#include "SimulationDrivers.h"
#include "CalligraphyArt.h"
#include "NetLock.h"
#include <WiFi.h>
#include <SPI.h>
#include <esp_sntp.h>
#include <atomic>
#include <time.h>
#include <string.h>

namespace {
std::atomic<bool> syncEvent{false};
void onTimeSync(struct timeval*) { syncEvent.store(true, std::memory_order_release); }

// Palet Warna Kaya & Colorful untuk Layar TFT 320x240
constexpr uint16_t ColorBg       = 0x0000;  // Deep Pure Black
constexpr uint16_t ColorHeader   = 0x10A4;  // Slate Glass Header
constexpr uint16_t ColorCard     = 0x0842;  // Dark Blue Slate Card
constexpr uint16_t ColorBorder   = 0x2168;  // Card Border
constexpr uint16_t ColorWhite    = 0xFFFF;
constexpr uint16_t ColorGold     = 0xFEA0;  // Islamic Gold
constexpr uint16_t ColorAmber    = 0xFD20;  // Vibrant Amber
constexpr uint16_t ColorEmerald  = 0x07E0;  // Bright Emerald Green
constexpr uint16_t ColorCyan     = 0x07FF;  // Tech Cyan
constexpr uint16_t ColorMuted    = 0x9CF3;  // Slate Muted
constexpr uint16_t ColorCoral    = 0xF9C7;  // Warm Coral
constexpr uint16_t ColorPurple   = 0xC9DF;  // Royal Violet
constexpr uint16_t ColorActiveBg = 0x0344;  // Active Prayer Background

bool sameDate(jws::Date a, jws::Date b) {
  return a.year == b.year && a.month == b.month && a.day == b.day;
}

const uint16_t kPrayerHeaderColors[jws::DisplayedPrayerCount] = {
  ColorMuted,    // Imsak: Soft Slate
  ColorCyan,     // Subuh: Tech Cyan
  0xFFE0,        // Dzuhur: Bright Yellow/Gold
  ColorAmber,    // Ashar: Vibrant Amber
  ColorCoral,    // Maghrib: Coral Red
  ColorPurple    // Isya: Royal Purple
};

struct ThemeColors {
  uint16_t bg;
  uint16_t header;
  uint16_t card;
  uint16_t border;
  uint16_t accent1;
  uint16_t accent2;
  const char* name;
};

const ThemeColors kThemes[4] = {
  {0x0000, 0x10A4, 0x0842, 0x2168, 0xFEA0, 0x07E0, "Midnight Gold"},
  {0x01E4, 0x0A86, 0x0283, 0x1B65, 0x27F5, 0xFEA0, "Emerald Mosque"},
  {0x0155, 0x0279, 0x0A13, 0x1B98, 0x07FF, 0xF9C7, "Royal Sapphire"},
  {0x18C3, 0x2945, 0x2102, 0x39E7, 0xFD20, 0xFEA0, "Cyber Sunset"}
};
}

bool Ili9341DisplayDriver::begin() {
#ifdef JWS_DESKTOP_S3
  SPI.begin(12, 9, 11, 10);
  touch_.begin();
  ledcSetup(0, 5000, 8);
  ledcAttachPin(21, 0);
#else
  SPI.begin(18, 19, 23, 27);
#endif
  jws::OnlineFeedService::instance().begin();
  jws::MikrotikService::instance().begin();
  jws::AudioService::instance().begin();
  jws::XiaozhiService::instance().begin();
  jws::RelayControlService::instance().begin();
  colorTheme_ = jws::OnlineFeedService::instance().getTheme();
  const uint8_t initBri = jws::OnlineFeedService::instance().getTftBrightness();
  lastAppliedBri_ = initBri;
  tft_.setBrightnessPct(initBri);
#ifdef JWS_DESKTOP_S3
  ledcWrite(0, static_cast<uint32_t>((initBri * 255U) / 100U));
#endif
  tft_.begin(40000000);
  tft_.setRotation(1);  // 320 x 240 landscape
  tft_.setTextWrap(false);
  haveFrame_ = false;
  drawChrome();
#ifdef JWS_DESKTOP_S3
  Serial.printf("[DISPLAY] ILI9341 S3 Ready 320x240 (40MHz); Touch CS=3 IRQ=1; Software+PWM Brightness=%u%%\n",
                lastAppliedBri_);
#else
  Serial.println("[DISPLAY] ILI9341 Initialized 320x240; SPI SCK=18 MOSI=23 MISO=19 CS=27 DC=26");
#endif
  return true;
}

void Ili9341DisplayDriver::setForcedScene(uint8_t scene, uint32_t durationMs) {
  forcedScene_ = static_cast<int>(scene);
  forcedSceneUntilMs_ = millis() + durationMs;
  sceneInitDone_ = false;
  haveFrame_ = false;
  Serial.printf("[DISPLAY] Live Scene Switched to %u for %u ms\n", scene, durationMs);
}

void Ili9341DisplayDriver::setActiveSlide(uint8_t slide) {
  activeSlide_ = slide % 8;
  lastSlideTouchMs_ = millis();
  sceneInitDone_ = false;
  haveFrame_ = false;
  Serial.printf("[DISPLAY] Active Slide Switched to %u\n", activeSlide_);
}

void Ili9341DisplayDriver::setColorTheme(uint8_t theme) {
  colorTheme_ = theme % 4;
  jws::OnlineFeedService::instance().setTheme(colorTheme_);
  sceneInitDone_ = false;
  haveFrame_ = false;
  Serial.printf("[DISPLAY] Theme Switched to %u (%s)\n", colorTheme_, kThemes[colorTheme_].name);
}

void Ili9341DisplayDriver::drawChrome() {
  const auto& theme = kThemes[colorTheme_ % 4];
  tft_.fillScreen(theme.bg);
  tft_.fillRect(0, 0, 320, 26, theme.header);
  tft_.drawFastHLine(0, 26, 320, theme.accent1);

  // Digital Clock Card Outer
  tft_.fillRoundRect(6, 28, 198, 74, 5, theme.card);
  tft_.drawRoundRect(6, 28, 198, 74, 5, theme.border);

  // Next Prayer Card Outer
  tft_.fillRoundRect(208, 28, 106, 74, 5, theme.card);
  tft_.drawRoundRect(208, 28, 106, 74, 5, theme.accent1);

  // 6 Prayer Cards Outers
  for (int i = 0; i < jws::DisplayedPrayerCount; ++i) {
    const int col = i % 3;
    const int row = i / 3;
    const int x = 6 + col * 104;
    const int y = 106 + row * 57;
    tft_.fillRoundRect(x, y, 100, 53, 4, theme.card);
    tft_.drawRoundRect(x, y, 100, 53, 4, theme.border);
  }

  // Running Text Bar Outer
  tft_.fillRect(0, 222, 320, 18, theme.header);
  tft_.drawFastHLine(0, 222, 320, theme.border);
}

void Ili9341DisplayDriver::drawClock(const jws::ClockReading& clock) {
  const bool hmChanged = !haveFrame_ ||
                         (clock.valid != previous_.clock.valid) ||
                         (clock.hour != previous_.clock.hour) ||
                         (clock.minute != previous_.clock.minute);
  if (clock.valid) {
    if (hmChanged) {
      const int h12 = (clock.hour % 12 == 0) ? 12 : (clock.hour % 12);
      const char* ampm = (clock.hour >= 12) ? "PM" : "AM";
      tft_.setTextColor(ColorWhite, ColorCard);
      tft_.setTextSize(4);
      tft_.setCursor(14, 34);
      tft_.printf("%02d:%02d", h12, clock.minute);

      tft_.setTextSize(1);
      tft_.setTextColor(ColorCyan, ColorCard);
      tft_.setCursor(144, 35);
      tft_.print(ampm);
    }
    tft_.setTextSize(2);
    tft_.setTextColor(ColorGold, ColorCard);
    tft_.setCursor(140, 48);
    tft_.printf(":%02d", clock.second);
  } else if (hmChanged) {
    tft_.setTextColor(ColorWhite, ColorCard);
    tft_.setTextSize(4);
    tft_.setCursor(14, 34);
    tft_.print("--:--");
    tft_.setTextSize(1);
    tft_.setTextColor(ColorCyan, ColorCard);
    tft_.setCursor(144, 35);
    tft_.print("  ");
    tft_.setTextSize(2);
    tft_.setTextColor(ColorGold, ColorCard);
    tft_.setCursor(140, 48);
    tft_.print(":--");
  }
}

void Ili9341DisplayDriver::drawDate(const jws::DisplayFrame& frame) {
  const auto& clock = frame.clock;
  tft_.fillRect(12, 72, 186, 28, ColorCard);
  tft_.setTextSize(1);
  tft_.setTextColor(ColorCyan, ColorCard);
  tft_.setCursor(14, 73);
  if (clock.valid) {
    tft_.printf("%s, %02d-%02d-%04d", jws::weekdayName(clock.date),
                clock.date.day, clock.date.month, clock.date.year);
    tft_.setTextColor(ColorGold, ColorCard);
    tft_.setCursor(14, 87);
    const auto hijri = jws::gregorianToHijri(clock.date, frame.configuration.hijriOffsetDays);
    tft_.printf("%d %s %dH", hijri.day, jws::hijriMonthName(hijri.month), hijri.year);
  } else {
    tft_.print("Menghubungkan ke Wi-Fi / Jam HP...");
  }
}

void Ili9341DisplayDriver::drawSchedule(const jws::DisplayFrame& frame) {
  const int curMin = frame.clock.valid ? frame.clock.hour * 60 + frame.clock.minute : -1;

  for (int i = 0; i < jws::DisplayedPrayerCount; ++i) {
    const int col = i % 3;
    const int row = i / 3;
    const int x = 6 + col * 104;
    const int y = 106 + row * 57;
    const jws::Prayer p = jws::kDisplayedPrayers[i];

    char time[8] = "--:--";
    const char* ampm = "  ";
    const int pMin = frame.clock.valid && frame.schedule.valid ? frame.schedule.minutes[p] : -1;
    if (pMin >= 0 && pMin < 1440) {
      const int ph24 = pMin / 60;
      const int pm = pMin % 60;
      const int ph12 = (ph24 % 12 == 0) ? 12 : (ph24 % 12);
      snprintf(time, sizeof(time), "%02d:%02d", ph12, pm);
      ampm = (ph24 >= 12) ? "PM" : "AM";
    }

    // Deteksi apakah sholat ini sedang aktif
    int nextPMin = 1440;
    if (i < jws::DisplayedPrayerCount - 1) {
      nextPMin = frame.schedule.minutes[jws::kDisplayedPrayers[i + 1]];
    }
    const bool isCur = frame.clock.valid && pMin >= 0 && curMin >= pMin && curMin < nextPMin;

    const uint16_t cardBg = isCur ? ColorActiveBg : ColorCard;
    const uint16_t cardBorder = isCur ? ColorEmerald : ColorBorder;
    tft_.fillRoundRect(x, y, 100, 53, 4, cardBg);
    tft_.drawRoundRect(x, y, 100, 53, 4, cardBorder);
    if (isCur) {
      tft_.drawRoundRect(x + 1, y + 1, 98, 51, 4, ColorEmerald);
    }

    // Nama Waktu Sholat
    tft_.setTextSize(1);
    tft_.setTextColor(kPrayerHeaderColors[i], cardBg);
    tft_.setCursor(x + 8, y + 7);
    tft_.print(jws::prayerName(p));

    if (isCur) {
      tft_.setTextColor(ColorGold, cardBg);
      tft_.setCursor(x + 64, y + 7);
      tft_.print("NOW");
    }

    // Jam Sholat 12-Jam (Besar & Jelas + AM/PM)
    tft_.setTextSize(2);
    tft_.setTextColor(isCur ? ColorWhite : kPrayerHeaderColors[i], cardBg);
    tft_.setCursor(x + 8, y + 25);
    tft_.print(time);

    tft_.setTextSize(1);
    tft_.setTextColor(isCur ? ColorGold : ColorMuted, cardBg);
    tft_.setCursor(x + 74, y + 32);
    tft_.print(ampm);
  }
}

void Ili9341DisplayDriver::drawCountdown(const jws::DisplayFrame& frame) {
  const auto info = frame.clock.valid
      ? jws::nextPrayerCountdown(frame.schedule, frame.clock.hour, frame.clock.minute, frame.clock.second)
      : jws::NextPrayerInfo{};

  if (!haveFrame_) {
    tft_.fillRect(210, 32, 102, 68, 0x1062);
    tft_.setTextSize(1);
    tft_.setTextColor(ColorMuted, 0x1062);
    tft_.setCursor(214, 34);
    tft_.print("MENUJU");
  }

  tft_.setTextSize(2);
  tft_.setTextColor(ColorEmerald, 0x1062);
  tft_.setCursor(214, 46);
  if (info.valid) {
    tft_.printf(">%-6s", jws::prayerName(info.prayer));
  } else {
    tft_.print(">--    ");
  }

  char cd[10];
  jws::formatCountdown(info.valid ? info.secondsRemaining : -1, cd);
  tft_.setTextSize(1);
  tft_.setTextColor(ColorAmber, 0x1062);
  tft_.setCursor(214, 66);
  tft_.printf("%-8s", cd);

  tft_.setTextSize(1);
  tft_.setTextColor(ColorGold, 0x1062);
  tft_.setCursor(214, 82);
  tft_.printf("%-8s", frame.clock.valid ? jws::pasaranName(frame.clock.date) : "WITA");
}

void Ili9341DisplayDriver::drawTitle(const jws::Configuration& config) {
  tft_.fillRect(0, 0, 320, 26, ColorHeader);
  tft_.drawFastHLine(0, 26, 320, ColorEmerald);

  if (config.calligraphyEnabled) {
    jws::drawCalligraphyMedallion(tft_, 6, 5, 16, false, ColorGold, ColorCyan, ColorHeader);
    jws::drawCalligraphyMedallion(tft_, 298, 5, 16, true, ColorGold, ColorCyan, ColorHeader);
  }

  tft_.setTextSize(1);
  tft_.setTextColor(ColorWhite, ColorHeader);
  tft_.setCursor(config.calligraphyEnabled ? 28 : 12, 6);
  const size_t maxChars = config.calligraphyEnabled ? 38 : 46;
  if (strlen(config.mosqueName) <= maxChars) {
    tft_.print(config.mosqueName);
  } else {
    char title[49];
    snprintf(title, sizeof title, "%.*s...", static_cast<int>(maxChars - 3), config.mosqueName);
    tft_.print(title);
  }

  tft_.setTextColor(ColorEmerald, ColorHeader);
  tft_.setCursor(config.calligraphyEnabled ? 28 : 12, 16);
  char ipLine[42];
  if (WiFi.status() == WL_CONNECTED && static_cast<uint32_t>(WiFi.localIP()) != 0U) {
    snprintf(ipLine, sizeof(ipLine), "IP: %s", WiFi.localIP().toString().c_str());
  } else {
    snprintf(ipLine, sizeof(ipLine), "IP AP: %s (Menunggu WiFi)", WiFi.softAPIP().toString().c_str());
  }
  tft_.print(ipLine);
}

void Ili9341DisplayDriver::drawTicker(const jws::Configuration& config, uint32_t nowMs, bool reset) {
  if (reset) { scrollX_ = 0; lastScrollMs_ = nowMs; }
  const bool enabled = config.runningTextEnabled && config.runningText[0] != '\0';
  if (!enabled || config.scrollIntervalMs == 0) {
    if (reset) { tft_.fillRect(0, 223, 320, 9, ColorHeader); }
    return;
  }

  char combinedTicker[380];
  const auto& nw = jws::OnlineFeedService::instance().news();
  if (nw.count > 0 && nw.headlines[0][0]) {
    snprintf(combinedTicker, sizeof(combinedTicker), "%s   *** BERITA TERKINI: %s ***   ",
             config.runningText, nw.headlines[0]);
  } else {
    snprintf(combinedTicker, sizeof(combinedTicker), "%s     ", config.runningText);
  }

  const int len = static_cast<int>(strlen(combinedTicker));
  if (len <= 0) return;

  const uint32_t charIntervalMs = (config.scrollIntervalMs < 40U) ? 110U : (config.scrollIntervalMs * 2U);
  const uint32_t steps = static_cast<uint32_t>(nowMs - lastScrollMs_) / charIntervalMs;
  if (!reset && steps == 0) return;
  if (steps > 0) {
    lastScrollMs_ += steps * charIntervalMs;
    scrollX_ = static_cast<int16_t>((static_cast<int>(scrollX_) + static_cast<int>(steps)) % len);
  }

  char windowBuf[53];
  for (int i = 0; i < 52; ++i) {
    windowBuf[i] = combinedTicker[(static_cast<int>(scrollX_) + i) % len];
  }
  windowBuf[52] = '\0';

  tft_.setTextSize(1);
  tft_.setTextColor(ColorAmber, ColorHeader);
  tft_.setCursor(4, 224);
  tft_.print(windowBuf);
}

void Ili9341DisplayDriver::drawSceneAdzan(const jws::DisplayFrame& frame, const char* pName, int remSec) {
  (void)frame;
  if (!sceneInitDone_) {
    sceneInitDone_ = true;
    lastRemSec_ = -1;
    tft_.fillScreen(0x0821);
    tft_.drawRect(4, 4, 312, 232, ColorGold);
    tft_.drawRect(6, 6, 308, 228, ColorEmerald);

    jws::drawCalligraphyMedallion(tft_, 16, 16, 20, false, ColorGold, ColorCyan, 0x0821);
    jws::drawCalligraphyMedallion(tft_, 280, 16, 20, true, ColorGold, ColorCyan, 0x0821);

    tft_.setTextSize(2);
    tft_.setTextColor(ColorAmber, 0x0821);
    tft_.setCursor(55, 20);
    tft_.print("MASUK WAKTU SHOLAT");

    tft_.setTextSize(4);
    tft_.setTextColor(ColorWhite, 0x0821);
    const int pLen = static_cast<int>(strlen(pName)) * 24;
    tft_.setCursor((320 - pLen) / 2, 65);
    tft_.print(pName);

    tft_.setTextSize(2);
    tft_.setTextColor(ColorEmerald, 0x0821);
    tft_.setCursor(24, 120);
    tft_.print("SAATNYA SHOLAT BERJAMAAH");

    tft_.setTextSize(1);
    tft_.setTextColor(ColorWhite, 0x0821);
    tft_.setCursor(48, 205);
    tft_.print("Mari Menuju Kemenangan & Memakmurkan Masjid");

    jws::AudioService::instance().playChime(jws::AudioChime::AdzanAlert);
  }

  if (remSec != lastRemSec_) {
    lastRemSec_ = remSec;
    char remBuf[28];
    snprintf(remBuf, sizeof(remBuf), "Waktu Adzan: %02d:%02d", remSec / 60, remSec % 60);
    tft_.setTextSize(2);
    tft_.setTextColor(ColorGold, 0x0821);
    tft_.setCursor(45, 160);
    tft_.print(remBuf);
  }
}

void Ili9341DisplayDriver::drawSceneIqomah(const jws::DisplayFrame& frame, const char* pName, int remSec, int totSec) {
  (void)frame;
  if (!sceneInitDone_) {
    sceneInitDone_ = true;
    lastRemSec_ = -1;
    tft_.fillScreen(0x0842);
    tft_.drawRect(4, 4, 312, 232, ColorAmber);

    tft_.setTextSize(2);
    tft_.setTextColor(ColorGold, 0x0842);
    tft_.setCursor(42, 14);
    tft_.print("HITUNG MUNDUR IQOMAH");

    char pBuf[32];
    snprintf(pBuf, sizeof(pBuf), "SHOLAT %s", pName);
    tft_.setTextSize(2);
    tft_.setTextColor(ColorCyan, 0x0842);
    tft_.setCursor((320 - static_cast<int>(strlen(pBuf)) * 12) / 2, 38);
    tft_.print(pBuf);

    // Giant Countdown Box
    tft_.fillRoundRect(24, 62, 272, 78, 8, ColorHeader);
    tft_.drawRoundRect(24, 62, 272, 78, 8, ColorAmber);

    // Progress Bar Outer
    const int barW = 260;
    tft_.drawRoundRect(30, 154, barW, 14, 4, ColorBorder);

    // Footer Message
    tft_.setTextSize(1);
    tft_.setTextColor(ColorAmber, 0x0842);
    tft_.setCursor(38, 184);
    tft_.print("LURUSKAN DAN RAPATKAN SHAF SHOLAT");
    tft_.setTextColor(ColorMuted, 0x0842);
    tft_.setCursor(44, 206);
    tft_.print("Harap Nonaktifkan / Silent Nada Dering HP");
  }

  if (remSec != lastRemSec_) {
    lastRemSec_ = remSec;
    if (remSec > 1 && remSec <= 10) {
      jws::AudioService::instance().playChime(jws::AudioChime::IqomahTick);
    } else if (remSec == 1) {
      jws::AudioService::instance().playChime(jws::AudioChime::IqomahFinal);
    }
    char timeBuf[16];
    snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", remSec / 60, remSec % 60);
    tft_.setTextSize(6);
    tft_.setTextColor(ColorWhite, ColorHeader);
    tft_.setCursor(72, 78);
    tft_.print(timeBuf);

    // Progress Bar
    const int barW = 260;
    tft_.fillRect(32, 156, barW - 4, 10, ColorHeader);
    if (totSec > 0 && remSec >= 0) {
      int fillW = (remSec * (barW - 4)) / totSec;
      if (fillW > barW - 4) fillW = barW - 4;
      if (fillW < 0) fillW = 0;
      if (fillW > 0) {
        tft_.fillRoundRect(32, 156, fillW, 10, 3, (remSec <= 10) ? ColorCoral : ColorEmerald);
      }
    }
  }
}

void Ili9341DisplayDriver::drawSceneKhotbah(const jws::DisplayFrame& frame) {
  if (sceneInitDone_) return;
  sceneInitDone_ = true;

  tft_.fillScreen(0x0821);
  tft_.drawRect(4, 4, 312, 232, ColorEmerald);

  jws::drawCalligraphyMedallion(tft_, 14, 8, 16, false, ColorGold, ColorCyan, 0x0821);
  jws::drawCalligraphyMedallion(tft_, 282, 8, 16, true, ColorGold, ColorCyan, 0x0821);

  tft_.setTextSize(2);
  tft_.setTextColor(ColorGold, 0x0821);
  tft_.setCursor(42, 12);
  tft_.print("PETUGAS SHOLAT JUMAT");

  const auto& cfg = frame.configuration;
  // Card 1: Khatib
  tft_.fillRoundRect(12, 38, 144, 78, 5, ColorHeader);
  tft_.drawRoundRect(12, 38, 144, 78, 5, ColorGold);
  tft_.setTextSize(1);
  tft_.setTextColor(ColorGold, ColorHeader);
  tft_.setCursor(20, 46);
  tft_.print("KHATIB JUMAT:");
  tft_.setTextSize(1);
  tft_.setTextColor(ColorWhite, ColorHeader);
  tft_.setCursor(20, 68);
  tft_.print(cfg.khatibName[0] ? cfg.khatibName : "Ust. H. Ahmad Fauzi");

  // Card 2: Imam
  tft_.fillRoundRect(164, 38, 144, 78, 5, ColorHeader);
  tft_.drawRoundRect(164, 38, 144, 78, 5, ColorEmerald);
  tft_.setTextSize(1);
  tft_.setTextColor(ColorEmerald, ColorHeader);
  tft_.setCursor(172, 46);
  tft_.print("IMAM SHOLAT:");
  tft_.setTextSize(1);
  tft_.setTextColor(ColorWhite, ColorHeader);
  tft_.setCursor(172, 68);
  tft_.print(cfg.imamName[0] ? cfg.imamName : "Ust. Abdurrahman");

  // Card 3: Muadzin
  tft_.fillRoundRect(12, 124, 144, 78, 5, ColorHeader);
  tft_.drawRoundRect(12, 124, 144, 78, 5, ColorCyan);
  tft_.setTextSize(1);
  tft_.setTextColor(ColorCyan, ColorHeader);
  tft_.setCursor(20, 132);
  tft_.print("MUADZIN:");
  tft_.setTextSize(1);
  tft_.setTextColor(ColorWhite, ColorHeader);
  tft_.setCursor(20, 154);
  tft_.print(cfg.muadzinName[0] ? cfg.muadzinName : "Sdr. Rizky Pratama");

  // Card 4: Bilal
  tft_.fillRoundRect(164, 124, 144, 78, 5, ColorHeader);
  tft_.drawRoundRect(164, 124, 144, 78, 5, ColorAmber);
  tft_.setTextSize(1);
  tft_.setTextColor(ColorAmber, ColorHeader);
  tft_.setCursor(172, 132);
  tft_.print("BILAL:");
  tft_.setTextSize(1);
  tft_.setTextColor(ColorWhite, ColorHeader);
  tft_.setCursor(172, 154);
  tft_.print(cfg.bilalName[0] ? cfg.bilalName : "Sdr. Hidayatullah");

  tft_.setTextSize(1);
  tft_.setTextColor(ColorMuted, 0x0821);
  tft_.setCursor(44, 216);
  tft_.print("Jumat Berkah - Mari Makmurkan Masjid");
}

void Ili9341DisplayDriver::drawSceneQr(const jws::DisplayFrame& frame) {
  if (sceneInitDone_) return;
  sceneInitDone_ = true;

  tft_.fillScreen(0x0842);
  tft_.drawRect(4, 4, 312, 232, ColorCyan);

  tft_.setTextSize(2);
  tft_.setTextColor(ColorGold, 0x0842);
  tft_.setCursor(38, 12);
  tft_.print("INFAQ & SHODAQOH");

  // QR Graphic Frame
  tft_.fillRect(16, 38, 114, 114, ColorWhite);
  tft_.drawRect(16, 38, 114, 114, ColorCyan);

  // Finder corners
  tft_.fillRect(24, 46, 26, 26, 0x0000);
  tft_.fillRect(28, 50, 18, 18, ColorWhite);
  tft_.fillRect(32, 54, 10, 10, 0x0000);

  tft_.fillRect(96, 46, 26, 26, 0x0000);
  tft_.fillRect(100, 50, 18, 18, ColorWhite);
  tft_.fillRect(104, 54, 10, 10, 0x0000);

  tft_.fillRect(24, 118, 26, 26, 0x0000);
  tft_.fillRect(28, 122, 18, 18, ColorWhite);
  tft_.fillRect(32, 126, 10, 10, 0x0000);

  for (int qy = 50; qy < 140; qy += 12) {
    for (int qx = 60; qx < 90; qx += 12) {
      if ((qx + qy) % 24 == 0) tft_.fillRect(qx, qy, 6, 6, 0x0000);
    }
  }

  // Right Side Text
  tft_.setTextSize(2);
  tft_.setTextColor(ColorWhite, 0x0842);
  tft_.setCursor(140, 44);
  tft_.print("SCAN QRIS");

  tft_.setTextSize(1);
  tft_.setTextColor(ColorGold, 0x0842);
  tft_.setCursor(140, 70);
  tft_.print(frame.configuration.mosqueName);

  tft_.setTextSize(1);
  tft_.setTextColor(ColorCyan, 0x0842);
  tft_.setCursor(140, 96);
  tft_.print("Rekening / Tautan Infaq:");

  tft_.setTextSize(1);
  tft_.setTextColor(ColorWhite, 0x0842);
  tft_.setCursor(140, 114);
  if (frame.configuration.qrText[0]) {
    char urlSub[24];
    snprintf(urlSub, sizeof(urlSub), "%.22s", frame.configuration.qrText);
    tft_.print(urlSub);
  } else {
    tft_.print("Infaq & Shodaqoh Masjid");
  }

  tft_.setTextSize(1);
  tft_.setTextColor(ColorEmerald, 0x0842);
  tft_.setCursor(24, 172);
  tft_.print("Infaq Membawa Berkah Dunia & Akhirat");
  tft_.setTextColor(ColorMuted, 0x0842);
  tft_.setCursor(16, 202);
  tft_.print("Salurkan infaq terbaik Anda untuk kemakmuran masjid");
}

void Ili9341DisplayDriver::drawSceneSholat(const jws::DisplayFrame& frame) {
  (void)frame;
  if (sceneInitDone_) return;
  sceneInitDone_ = true;

  tft_.fillScreen(0x0000);
  tft_.drawRect(8, 8, 304, 224, 0x03E6);

  tft_.setTextSize(2);
  tft_.setTextColor(ColorEmerald, 0x0000);
  tft_.setCursor(22, 45);
  tft_.print("LURUSKAN & RAPATKAN SHAF");

  tft_.setTextSize(2);
  tft_.setTextColor(ColorWhite, 0x0000);
  tft_.setCursor(30, 95);
  tft_.print("SHOLAT SEDANG BERLANGSUNG");

  tft_.setTextSize(1);
  tft_.setTextColor(ColorAmber, 0x0000);
  tft_.setCursor(40, 155);
  tft_.print("Mohon Menjaga Kekhusyukan Jamaah");
  tft_.setTextColor(ColorMuted, 0x0000);
  tft_.setCursor(44, 185);
  tft_.print("Pastikan HP Dalam Mode Senyap / Hening");
}

void Ili9341DisplayDriver::drawSlideDots(uint8_t active) {
  const int dotY = 236;
  const auto& theme = kThemes[colorTheme_ % 4];
  for (int i = 0; i < 8; ++i) {
    const int dotX = 90 + i * 20;
    if (i == active) {
      tft_.fillCircle(dotX, dotY, 4, theme.accent1);
    } else {
      tft_.fillCircle(dotX, dotY, 3, theme.header);
      tft_.drawCircle(dotX, dotY, 3, ColorMuted);
    }
  }
}

void Ili9341DisplayDriver::handleTouch(const jws::DisplayFrame& frame, uint32_t nowMs) {
  jws::TouchEvent ev = touch_.processTouch(nowMs);
  if (ev.gesture == jws::TouchGesture::None) {
    // Jangan auto-kembali saat UPIN AI sedang mendengarkan/berpikir di Slide 5
    const auto xzState = jws::XiaozhiService::instance().snapshot().state;
    const bool keepSlide5 = (activeSlide_ == 5 &&
                             (xzState == jws::XiaozhiState::Listening || xzState == jws::XiaozhiState::Thinking));
    const uint32_t timeoutMs = (activeSlide_ >= 4) ? 60000UL : 45000UL;
    if (activeSlide_ != 0 && !keepSlide5 && (nowMs - lastSlideTouchMs_) > timeoutMs) {
      activeSlide_ = 0;
      sceneInitDone_ = false;
      haveFrame_ = false;
    }
    return;
  }

  lastSlideTouchMs_ = nowMs;

  if (chronosAlertActive_) {
    jws::ChronosService::instance().dismissAlert();
    chronosAlertActive_ = false;
    sceneInitDone_ = false;
    haveFrame_ = false;
    Serial.println("[TOUCH] Dismissed Chronos Notification / Call Overlay");
    return;
  }

  // 1. Gesture SWIPE
  if (ev.gesture == jws::TouchGesture::SwipeLeft) {
    if (activeSlide_ == 10) {
      activeSlide_ = 2; // Dari detail saham kembali ke daftar saham
    } else if (activeSlide_ == 11) {
      activeSlide_ = 3; // Dari detail berita kembali ke daftar berita
    } else if (activeSlide_ == 12) {
      activeSlide_ = 4; // Dari grafik port kembali ke NOC overview
    } else if (activeSlide_ == 13 || activeSlide_ == 14) {
      const auto& mt = jws::MikrotikService::instance().feed();
      const int cnt = (activeSlide_ == 13) ? mt.pppoeCount : mt.hotspotCount;
      const int totalPages = (cnt > 0) ? ((cnt + 4) / 5) : 1;
      if (totalPages > 1) {
        nocListPage_ = (nocListPage_ + 1) % totalPages;
        lastNocRefreshMs_ = 0;
        Serial.printf("[TOUCH] NOC List Page -> %d/%d\n", nocListPage_ + 1, totalPages);
        return;
      }
      activeSlide_ = 4;
      nocListPage_ = 0;
    } else if (activeSlide_ <= 7) {
      activeSlide_ = (activeSlide_ + 1) % 8;
    }
    sceneInitDone_ = false;
    haveFrame_ = false;
    Serial.printf("[TOUCH] Swipe Left -> Slide %u\n", activeSlide_);
  } else if (ev.gesture == jws::TouchGesture::SwipeRight) {
    if (activeSlide_ == 10) {
      activeSlide_ = 2;
    } else if (activeSlide_ == 11) {
      activeSlide_ = 3;
    } else if (activeSlide_ >= 12 && activeSlide_ <= 14) {
      activeSlide_ = 4;
      nocListPage_ = 0;
    } else if (activeSlide_ <= 7) {
      activeSlide_ = (activeSlide_ + 7) % 8;
    }
    sceneInitDone_ = false;
    haveFrame_ = false;
    Serial.printf("[TOUCH] Swipe Right -> Slide %u\n", activeSlide_);
  }
  // 2. Gesture TAP (Sentuhan tombol/kartu)
  else if (ev.gesture == jws::TouchGesture::Tap) {
    // A. Di Sub-Menu Detail (10: Saham, 11: Berita, 12: Port, 13: PPPoE, 14: Hotspot)
    if (activeSlide_ == 10 || activeSlide_ == 11) {
      activeSlide_ = (activeSlide_ == 10) ? 2 : 3;
      sceneInitDone_ = false;
      haveFrame_ = false;
      Serial.printf("[TOUCH] Back pressed -> Slide %u\n", activeSlide_);
      return;
    }
    if (activeSlide_ == 12) {
      activeSlide_ = 4;
      sceneInitDone_ = false;
      haveFrame_ = false;
      Serial.println("[TOUCH] Back to MikroTik NOC Overview");
      return;
    }
    if (activeSlide_ == 13 || activeSlide_ == 14) {
      if (ev.x < 110 && ev.y < 36) {
        activeSlide_ = 4;
        nocListPage_ = 0;
        sceneInitDone_ = false;
        haveFrame_ = false;
        Serial.println("[TOUCH] Back to MikroTik NOC Overview");
        return;
      }
      const auto& mt = jws::MikrotikService::instance().feed();
      const int cnt = (activeSlide_ == 13) ? mt.pppoeCount : mt.hotspotCount;
      const int totalPages = (cnt > 0) ? ((cnt + 4) / 5) : 1;
      if (ev.y >= 36 && totalPages > 1) {
        nocListPage_ = (nocListPage_ + 1) % totalPages;
        lastNocRefreshMs_ = 0;
        Serial.printf("[TOUCH] Next NOC Page -> %d/%d\n", nocListPage_ + 1, totalPages);
        return;
      }
      activeSlide_ = 4;
      nocListPage_ = 0;
      sceneInitDone_ = false;
      haveFrame_ = false;
      return;
    }

    // B. Di Slide 2 (Saham IDX & Watchlist)
    if (activeSlide_ == 2) {
      if (ev.y >= 30 && ev.y <= 82 && ev.x >= 10 && ev.x <= 310) {
        selectedStockIndex_ = -1; // IHSG
        activeSlide_ = 10;
        sceneInitDone_ = false;
        haveFrame_ = false;
        Serial.println("[TOUCH] Opened IHSG Detail");
        return;
      } else if (ev.y >= 86 && ev.y <= 144) {
        if (ev.x >= 10 && ev.x <= 156) {
          selectedStockIndex_ = 0;
          activeSlide_ = 10;
          sceneInitDone_ = false;
          haveFrame_ = false;
          return;
        } else if (ev.x >= 164 && ev.x <= 310) {
          selectedStockIndex_ = 1;
          activeSlide_ = 10;
          sceneInitDone_ = false;
          haveFrame_ = false;
          return;
        }
      } else if (ev.y >= 150 && ev.y <= 208) {
        if (ev.x >= 10 && ev.x <= 156) {
          selectedStockIndex_ = 2;
          activeSlide_ = 10;
          sceneInitDone_ = false;
          haveFrame_ = false;
          return;
        } else if (ev.x >= 164 && ev.x <= 310) {
          selectedStockIndex_ = 3;
          activeSlide_ = 10;
          sceneInitDone_ = false;
          haveFrame_ = false;
          return;
        }
      }
    }
    // C. Di Slide 3 (Berita Nasional)
    else if (activeSlide_ == 3) {
      if (ev.x >= 10 && ev.x <= 310) {
        if (ev.y >= 34 && ev.y <= 88) {
          selectedNewsIndex_ = 0;
          activeSlide_ = 11;
          sceneInitDone_ = false;
          haveFrame_ = false;
          return;
        } else if (ev.y >= 92 && ev.y <= 146) {
          selectedNewsIndex_ = 1;
          activeSlide_ = 11;
          sceneInitDone_ = false;
          haveFrame_ = false;
          return;
        } else if (ev.y >= 150 && ev.y <= 204) {
          selectedNewsIndex_ = 2;
          activeSlide_ = 11;
          sceneInitDone_ = false;
          haveFrame_ = false;
          return;
        }
      }
    }
    // D. Di Slide 4 (MikroTik NOC Overview)
    else if (activeSlide_ == 4) {
      // 4 Interface Cards
      if (ev.y >= 32 && ev.y <= 90) {
        if (ev.x >= 8 && ev.x <= 156) {
          selectedPortIndex_ = 0;
          activeSlide_ = 12;
          sceneInitDone_ = false;
          haveFrame_ = false;
          Serial.println("[TOUCH] Opened MikroTik Port 0 Graph");
          return;
        } else if (ev.x >= 164 && ev.x <= 312) {
          selectedPortIndex_ = 1;
          activeSlide_ = 12;
          sceneInitDone_ = false;
          haveFrame_ = false;
          Serial.println("[TOUCH] Opened MikroTik Port 1 Graph");
          return;
        }
      } else if (ev.y >= 94 && ev.y <= 152) {
        if (ev.x >= 8 && ev.x <= 156) {
          selectedPortIndex_ = 2;
          activeSlide_ = 12;
          sceneInitDone_ = false;
          haveFrame_ = false;
          Serial.println("[TOUCH] Opened MikroTik Port 2 Graph");
          return;
        } else if (ev.x >= 164 && ev.x <= 312) {
          selectedPortIndex_ = 3;
          activeSlide_ = 12;
          sceneInitDone_ = false;
          haveFrame_ = false;
          Serial.println("[TOUCH] Opened MikroTik Port 3 Graph");
          return;
        }
      }
      // 2 Bottom Action Cards: PPPoE Active & Hotspot User
      else if (ev.y >= 156 && ev.y <= 212) {
        if (ev.x >= 8 && ev.x <= 156) {
          activeSlide_ = 13;
          nocListPage_ = 0;
          sceneInitDone_ = false;
          haveFrame_ = false;
          Serial.println("[TOUCH] Opened PPPoE Active List");
          return;
        } else if (ev.x >= 164 && ev.x <= 312) {
          activeSlide_ = 14;
          nocListPage_ = 0;
          sceneInitDone_ = false;
          haveFrame_ = false;
          Serial.println("[TOUCH] Opened Hotspot Active List");
          return;
        }
      }
    }

    // E. Di Slide 5 (Xiaozhi Islami AI)
    else if (activeSlide_ == 5) {
      if (ev.y >= 32 && ev.y <= 214) {
        if (jws::XiaozhiService::instance().snapshot().micHardwareReady) {
          jws::XiaozhiService::instance().triggerVoiceCapture();
          Serial.println("[TOUCH] Xiaozhi Face/Dialog Tapped -> Voice STT Capture");
        } else {
          jws::XiaozhiService::instance().triggerNextTopic(frame);
          Serial.println("[TOUCH] Xiaozhi Face/Dialog Tapped -> Next Topic");
        }
        lastXiaozhiAnimMs_ = 0;
        return;
      }
    }

    // F. Di Slide 6 (Task Manager: CPU, RAM, PSRAM & Brightness Control)
    else if (activeSlide_ == 6) {
      if (ev.y >= 176 && ev.y <= 218) {
        uint8_t curBri = jws::OnlineFeedService::instance().getTftBrightness();
        if (ev.x < 100) {
          curBri = (curBri > 25) ? static_cast<uint8_t>(curBri - 20) : 10;
        } else if (ev.x > 220) {
          curBri = (curBri <= 80) ? static_cast<uint8_t>(curBri + 20) : 100;
        } else {
          curBri = (curBri >= 100) ? 20 : static_cast<uint8_t>(curBri + 20);
        }
        jws::OnlineFeedService::instance().setTftBrightness(curBri);
        lastFocusRefreshMs_ = 0;
        Serial.printf("[TOUCH] TFT Brightness -> %u%%\n", curBri);
        return;
      }
    }

    // G. Di Slide 7 (Kontrol Relay 2CH)
    else if (activeSlide_ == 7) {
      auto& rc = jws::RelayControlService::instance();
      if (ev.y >= 34 && ev.y <= 176) {
        if (ev.x >= 8 && ev.x <= 156) {
          rc.toggleRelay1();
          lastRelayRefreshMs_ = 0;
          return;
        } else if (ev.x >= 164 && ev.x <= 312) {
          rc.toggleRelay2();
          lastRelayRefreshMs_ = 0;
          return;
        }
      } else if (ev.y >= 184 && ev.y <= 218) {
        if (ev.x >= 8 && ev.x <= 156) {
          rc.setAll(true);
          lastRelayRefreshMs_ = 0;
          return;
        } else if (ev.x >= 164 && ev.x <= 312) {
          rc.setAll(false);
          lastRelayRefreshMs_ = 0;
          return;
        }
      }
    }

    // H. Navigasi Global (hanya di Slide 0..7)
    if (activeSlide_ <= 7) {
      if (ev.x > 260 && ev.y < 35) {
        setColorTheme((colorTheme_ + 1) % 4);
      } else if (ev.y > 215) {
        int tapSlide = (ev.x - 80) / 20;
        if (tapSlide >= 0 && tapSlide <= 7) {
          activeSlide_ = tapSlide;
          sceneInitDone_ = false;
          haveFrame_ = false;
        }
      } else if (ev.x < 40) {
        activeSlide_ = (activeSlide_ + 7) % 8;
        sceneInitDone_ = false;
        haveFrame_ = false;
      } else if (ev.x > 280) {
        activeSlide_ = (activeSlide_ + 1) % 8;
        sceneInitDone_ = false;
        haveFrame_ = false;
      }
    }
  }
}

void Ili9341DisplayDriver::drawSlideWeather(const jws::DisplayFrame& frame) {
  if (sceneInitDone_) return;
  sceneInitDone_ = true;

  const auto& theme = kThemes[colorTheme_ % 4];
  tft_.fillScreen(theme.bg);
  tft_.fillRect(0, 0, 320, 26, theme.header);
  tft_.drawFastHLine(0, 26, 320, theme.accent1);

  tft_.setTextSize(2);
  tft_.setTextColor(theme.accent1, theme.header);
  tft_.setCursor(10, 6);
  tft_.print("INFO CUACA & IKLIM");

  tft_.setTextSize(1);
  tft_.setTextColor(ColorWhite, theme.header);
  tft_.setCursor(245, 10);
  tft_.print(theme.name);

  const auto& w = jws::OnlineFeedService::instance().weather();

  // Big Temperature Card
  tft_.fillRoundRect(10, 34, 180, 110, 6, theme.card);
  tft_.drawRoundRect(10, 34, 180, 110, 6, theme.accent1);

  tft_.setTextSize(1);
  tft_.setTextColor(theme.accent2, theme.card);
  tft_.setCursor(20, 44);
  tft_.print("SUHU UDARA SAAT INI");

  char tempBuf[16];
  snprintf(tempBuf, sizeof(tempBuf), "%.1f C", w.temp);
  tft_.setTextSize(4);
  tft_.setTextColor(ColorWhite, theme.card);
  tft_.setCursor(22, 64);
  tft_.print(tempBuf);

  tft_.setTextSize(1);
  tft_.setTextColor(ColorMuted, theme.card);
  tft_.setCursor(22, 115);
  char locBuf[32];
  snprintf(locBuf, sizeof(locBuf), "Wilayah: %.20s", frame.configuration.mosqueName[0] ? frame.configuration.mosqueName : "Kotamobagu");
  tft_.print(locBuf);

  // Humidity & Condition Card
  tft_.fillRoundRect(198, 34, 112, 110, 6, theme.card);
  tft_.drawRoundRect(198, 34, 112, 110, 6, theme.border);

  tft_.setTextSize(1);
  tft_.setTextColor(ColorCyan, theme.card);
  tft_.setCursor(208, 44);
  tft_.print("KELEMBABAN:");
  char humBuf[16];
  snprintf(humBuf, sizeof(humBuf), "%d %%", w.humidity);
  tft_.setTextSize(3);
  tft_.setTextColor(ColorWhite, theme.card);
  tft_.setCursor(208, 62);
  tft_.print(humBuf);

  tft_.setTextSize(1);
  tft_.setTextColor(theme.accent1, theme.card);
  tft_.setCursor(208, 98);
  tft_.print("STATUS:");
  tft_.setTextColor(ColorEmerald, theme.card);
  tft_.setCursor(208, 115);
  tft_.print(w.statusText);

  // Forecast & Advice Banner
  tft_.fillRoundRect(10, 150, 300, 66, 6, theme.card);
  tft_.drawRoundRect(10, 150, 300, 66, 6, theme.border);

  tft_.setTextSize(1);
  tft_.setTextColor(theme.accent1, theme.card);
  tft_.setCursor(20, 162);
  tft_.print("PANDUAN SHOLAT & CUACA HARI INI:");
  tft_.setTextColor(ColorWhite, theme.card);
  tft_.setCursor(20, 180);
  if (w.code >= 51) {
    tft_.print("Kemungkinan hujan/gerimis. Bawa payung ke masjid.");
  } else {
    tft_.print("Cuaca kondusif untuk sholat berjamaah di masjid.");
  }
  tft_.setTextColor(ColorMuted, theme.card);
  tft_.setCursor(20, 196);
  tft_.print("Sumber: Open-Meteo Satellite API (Real-time)");

  drawSlideDots(1);
}

void Ili9341DisplayDriver::drawSlideStocks(const jws::DisplayFrame& frame) {
  (void)frame;
  if (sceneInitDone_) return;
  sceneInitDone_ = true;

  const auto& theme = kThemes[colorTheme_ % 4];
  tft_.fillScreen(theme.bg);
  tft_.fillRect(0, 0, 320, 26, theme.header);
  tft_.drawFastHLine(0, 26, 320, ColorGold);

  tft_.setTextSize(2);
  tft_.setTextColor(ColorGold, theme.header);
  tft_.setCursor(10, 6);
  tft_.print("BURSA EFEK (IDX) & SAHAM");

  const auto& st = jws::OnlineFeedService::instance().stocks();

  // Top Card: IHSG Composite
  const bool ihsgUp = st.ihsg.change >= 0.0f;
  const uint16_t ihsgCol = ihsgUp ? ColorEmerald : ColorCoral;
  tft_.fillRoundRect(10, 32, 300, 48, 5, theme.card);
  tft_.drawRoundRect(10, 32, 300, 48, 5, ihsgCol);

  tft_.setTextSize(1);
  tft_.setTextColor(ColorGold, theme.card);
  tft_.setCursor(20, 40);
  tft_.print("IHSG (COMPOSITE INDEX):");

  char ihsgPriceBuf[24];
  snprintf(ihsgPriceBuf, sizeof(ihsgPriceBuf), "%.2f", st.ihsg.price);
  tft_.setTextSize(2);
  tft_.setTextColor(ColorWhite, theme.card);
  tft_.setCursor(20, 56);
  tft_.print(ihsgPriceBuf);

  char ihsgChgBuf[32];
  snprintf(ihsgChgBuf, sizeof(ihsgChgBuf), "%+.2f (%+.2f%%)", st.ihsg.change, st.ihsg.percent);
  tft_.setTextSize(1);
  tft_.setTextColor(ihsgCol, theme.card);
  tft_.setCursor(170, 58);
  tft_.print(ihsgChgBuf);

  // 4 Watchlist Items in 2x2 Grid
  for (int i = 0; i < 4; ++i) {
    const int col = i % 2;
    const int row = i / 2;
    const int x = 10 + col * 154;
    const int y = 86 + row * 64;
    
    tft_.fillRoundRect(x, y, 146, 58, 4, theme.card);
    tft_.drawRoundRect(x, y, 146, 58, 4, theme.border);

    if (i < st.itemCount && st.items[i].symbol[0]) {
      const auto& it = st.items[i];
      const bool isUp = it.change >= 0.0f;
      const uint16_t itCol = isUp ? ColorEmerald : ColorCoral;

      // Symbol
      tft_.setTextSize(2);
      tft_.setTextColor(ColorWhite, theme.card);
      tft_.setCursor(x + 8, y + 8);
      tft_.print(it.symbol);

      // Change Pill
      char chgPill[16];
      snprintf(chgPill, sizeof(chgPill), "%+.1f%%", it.percent);
      tft_.fillRoundRect(x + 82, y + 8, 56, 16, 3, itCol);
      tft_.setTextSize(1);
      tft_.setTextColor(0x0000, itCol);
      tft_.setCursor(x + 88, y + 12);
      tft_.print(chgPill);

      // Price
      char prBuf[20];
      snprintf(prBuf, sizeof(prBuf), "Rp %.0f", it.price);
      tft_.setTextSize(1);
      tft_.setTextColor(ColorCyan, theme.card);
      tft_.setCursor(x + 8, y + 36);
      tft_.print(prBuf);
    } else {
      tft_.setTextSize(1);
      tft_.setTextColor(ColorMuted, theme.card);
      tft_.setCursor(x + 16, y + 24);
      tft_.print("Atur di Web HP");
    }
  }

  // Footer
  tft_.setTextSize(1);
  tft_.setTextColor(ColorGold, theme.bg);
  tft_.setCursor(14, 214);
  tft_.print("Tap kartu utk grafik & bid-offer | Geser utk menu");

  drawSlideDots(2);
}

void Ili9341DisplayDriver::drawSlideNews(const jws::DisplayFrame& frame) {
  (void)frame;
  if (sceneInitDone_) return;
  sceneInitDone_ = true;

  const auto& theme = kThemes[colorTheme_ % 4];
  tft_.fillScreen(theme.bg);
  tft_.fillRect(0, 0, 320, 26, theme.header);
  tft_.drawFastHLine(0, 26, 320, ColorCoral);

  tft_.setTextSize(2);
  tft_.setTextColor(ColorCoral, theme.header);
  tft_.setCursor(10, 6);
  tft_.print("BERITA INDONESIA");

  const auto& nw = jws::OnlineFeedService::instance().news();
  tft_.setTextSize(1);
  tft_.setTextColor(ColorWhite, theme.header);
  tft_.setCursor(210, 10);
  tft_.print(nw.sourceName);

  // 3 Headline Cards
  for (int i = 0; i < 3; ++i) {
    const int y = 34 + i * 58;
    tft_.fillRoundRect(10, y, 300, 52, 4, theme.card);
    tft_.drawRoundRect(10, y, 300, 52, 4, theme.border);

    // Number Bullet
    tft_.fillCircle(24, y + 16, 8, ColorCoral);
    tft_.setTextSize(1);
    tft_.setTextColor(ColorWhite, ColorCoral);
    tft_.setCursor(21, y + 12);
    tft_.print(i + 1);

    // Headline Text
    if (i < nw.count && nw.headlines[i][0]) {
      char line1[42] = "";
      char line2[42] = "";
      const int hLen = strlen(nw.headlines[i]);
      if (hLen <= 38) {
        strncpy(line1, nw.headlines[i], sizeof(line1) - 1);
      } else {
        int split = 36;
        while (split > 20 && nw.headlines[i][split] != ' ') split--;
        if (split <= 20) split = 36;
        strncpy(line1, nw.headlines[i], split);
        line1[split] = '\0';
        strncpy(line2, nw.headlines[i] + split + 1, sizeof(line2) - 1);
        line2[sizeof(line2) - 1] = '\0';
      }

      tft_.setTextSize(1);
      tft_.setTextColor(ColorWhite, theme.card);
      tft_.setCursor(40, y + 10);
      tft_.print(line1);

      if (line2[0]) {
        tft_.setTextColor(ColorCyan, theme.card);
        tft_.setCursor(40, y + 26);
        tft_.print(line2);
      }
    } else {
      tft_.setTextSize(1);
      tft_.setTextColor(ColorMuted, theme.card);
      tft_.setCursor(40, y + 18);
      tft_.print("Menghubungkan ke server berita nasional...");
    }
  }

  tft_.setTextSize(1);
  tft_.setTextColor(ColorGold, theme.bg);
  tft_.setCursor(14, 214);
  tft_.print("Tap berita utk baca narasi penuh | Geser utk menu");

  drawSlideDots(3);
}

void Ili9341DisplayDriver::drawSlideStockDetail(const jws::DisplayFrame& frame) {
  (void)frame;
  if (sceneInitDone_) return;
  sceneInitDone_ = true;

  const auto& theme = kThemes[colorTheme_ % 4];
  tft_.fillScreen(theme.bg);

  const auto& st = jws::OnlineFeedService::instance().stocks();
  jws::StockItem it;
  if (selectedStockIndex_ < 0) {
    it = st.ihsg;
  } else if (selectedStockIndex_ < st.itemCount) {
    it = st.items[selectedStockIndex_];
  } else {
    it = st.items[0];
  }

  const bool isUp = it.change >= 0.0f;
  const uint16_t itCol = isUp ? ColorEmerald : ColorCoral;

  // 1. Header Bar: Tombol Kembali + Simbol
  tft_.fillRect(0, 0, 320, 28, theme.header);
  tft_.drawFastHLine(0, 28, 320, theme.border);

  // Tombol [ < KEMBALI ]
  tft_.fillRoundRect(6, 4, 76, 20, 3, ColorHeader);
  tft_.drawRoundRect(6, 4, 76, 20, 3, ColorCyan);
  tft_.setTextSize(1);
  tft_.setTextColor(ColorCyan, ColorHeader);
  tft_.setCursor(12, 10);
  tft_.print("< KEMBALI");

  // Simbol & Nama Emiten
  tft_.setTextSize(2);
  tft_.setTextColor(ColorWhite, theme.header);
  tft_.setCursor(96, 6);
  tft_.print(it.symbol);

  tft_.setTextSize(1);
  tft_.setTextColor(ColorGold, theme.header);
  tft_.setCursor(170, 10);
  tft_.print(it.name[0] ? it.name : "IDX Emiten");

  // 2. Kartu Harga & Perubahan (Y: 32..66)
  tft_.fillRoundRect(10, 32, 300, 34, 4, theme.card);
  tft_.drawRoundRect(10, 32, 300, 34, 4, theme.border);

  char prBuf[24];
  if (selectedStockIndex_ < 0) {
    snprintf(prBuf, sizeof(prBuf), "%.2f", it.price);
  } else {
    snprintf(prBuf, sizeof(prBuf), "Rp %.0f", it.price);
  }
  tft_.setTextSize(2);
  tft_.setTextColor(ColorWhite, theme.card);
  tft_.setCursor(18, 40);
  tft_.print(prBuf);

  char chgBuf[32];
  snprintf(chgBuf, sizeof(chgBuf), "%+.1f (%+.2f%%)", it.change, it.percent);
  tft_.setTextSize(1);
  tft_.setTextColor(itCol, theme.card);
  tft_.setCursor(170, 44);
  tft_.print(chgBuf);

  // 3. Mini Grafik Line / Area Chart (Y: 70..136, H: 66)
  tft_.fillRoundRect(10, 70, 300, 68, 4, 0x0821);
  tft_.drawRoundRect(10, 70, 300, 68, 4, theme.border);

  tft_.setTextSize(1);
  tft_.setTextColor(ColorMuted, 0x0821);
  tft_.setCursor(16, 74);
  tft_.print("INTRADAY CHART & TREN:");

  // Grid garis halus
  for (int gy = 86; gy < 132; gy += 15) {
    tft_.drawFastHLine(14, gy, 292, 0x1084);
  }

  // Gambar Kurva Tren Harga (Line Chart)
  const float base = it.price - it.change;
  const float pRange = (it.dayHigh > it.dayLow && it.dayHigh > 0) ? (it.dayHigh - it.dayLow) : (it.price * 0.04f);
  int prevX = 20, prevY = 110;
  for (int p = 0; p < 10; ++p) {
    float simPrice = base + (it.change * (p / 9.0f)) + (sin(p * 1.3f) * (pRange * 0.15f));
    int curX = 20 + p * 30;
    int curY = 126 - (int)(((simPrice - it.dayLow) / (pRange > 0 ? pRange : 1.0f)) * 40.0f);
    if (curY < 82) curY = 82;
    if (curY > 128) curY = 128;
    if (p > 0) {
      tft_.drawLine(prevX, prevY, curX, curY, itCol);
      tft_.drawLine(prevX, prevY + 1, curX, curY + 1, itCol);
    }
    prevX = curX;
    prevY = curY;
  }

  char hlBuf[36];
  snprintf(hlBuf, sizeof(hlBuf), "H: %.0f  L: %.0f", it.dayHigh, it.dayLow);
  tft_.setTextColor(ColorAmber, 0x0821);
  tft_.setCursor(200, 74);
  tft_.print(hlBuf);

  // 4. Tabel Order Book (BID & OFFER) (Y: 142..214, H: 72)
  tft_.fillRoundRect(10, 142, 300, 70, 4, theme.card);
  tft_.drawRoundRect(10, 142, 300, 70, 4, theme.border);

  tft_.fillRect(12, 144, 146, 16, 0x0A24);
  tft_.fillRect(162, 144, 146, 16, 0x2084);
  tft_.setTextSize(1);
  tft_.setTextColor(ColorEmerald, 0x0A24);
  tft_.setCursor(44, 148);
  tft_.print("BID (BELI)");

  tft_.setTextColor(ColorCoral, 0x2084);
  tft_.setCursor(196, 148);
  tft_.print("OFFER (JUAL)");

  float tick = (it.price > 5000) ? 25.0f : (it.price > 2000 ? 10.0f : 5.0f);
  if (selectedStockIndex_ < 0) tick = 1.0f;

  for (int r = 0; r < 3; ++r) {
    const int ry = 164 + r * 15;
    float bidPr = it.price - (r * tick);
    float ofrPr = it.price + ((r + 1) * tick);
    int bidLot = (int)(2540 - r * 620 + ((int)(it.price) % 300));
    int ofrLot = (int)(3120 + r * 450 + ((int)(it.price) % 400));

    char bBuf[24];
    snprintf(bBuf, sizeof(bBuf), "%.0f (%dK)", bidPr, bidLot / 100);
    tft_.setTextColor(ColorEmerald, theme.card);
    tft_.setCursor(20, ry);
    tft_.print(bBuf);

    tft_.drawFastVLine(159, 144, 66, theme.border);

    char oBuf[24];
    snprintf(oBuf, sizeof(oBuf), "%.0f (%dK)", ofrPr, ofrLot / 100);
    tft_.setTextColor(ColorCoral, theme.card);
    tft_.setCursor(174, ry);
    tft_.print(oBuf);
  }

  tft_.setTextSize(1);
  tft_.setTextColor(ColorMuted, theme.bg);
  tft_.setCursor(24, 218);
  tft_.print("Tekan [< KEMBALI] atau swipe kanan utk watchlist");
}

void Ili9341DisplayDriver::drawSlideNewsDetail(const jws::DisplayFrame& frame) {
  (void)frame;
  if (sceneInitDone_) return;
  sceneInitDone_ = true;

  const auto& theme = kThemes[colorTheme_ % 4];
  tft_.fillScreen(theme.bg);

  const auto& nw = jws::OnlineFeedService::instance().news();
  int idx = selectedNewsIndex_;
  if (idx < 0 || idx >= 3) idx = 0;

  // 1. Header Bar: Tombol Kembali + Kategori
  tft_.fillRect(0, 0, 320, 28, theme.header);
  tft_.drawFastHLine(0, 28, 320, theme.border);

  // Tombol [ < KEMBALI ]
  tft_.fillRoundRect(6, 4, 76, 20, 3, ColorHeader);
  tft_.drawRoundRect(6, 4, 76, 20, 3, ColorCyan);
  tft_.setTextSize(1);
  tft_.setTextColor(ColorCyan, ColorHeader);
  tft_.setCursor(12, 10);
  tft_.print("< KEMBALI");

  tft_.setTextSize(1);
  tft_.setTextColor(ColorCoral, theme.header);
  tft_.setCursor(94, 6);
  tft_.print("BERITA NASIONAL");
  tft_.setTextColor(ColorMuted, theme.header);
  tft_.setCursor(94, 16);
  tft_.print(nw.sourceName);

  // 2. Kartu Isi Berita (Y: 34..212, H: 178)
  tft_.fillRoundRect(10, 34, 300, 178, 4, theme.card);
  tft_.drawRoundRect(10, 34, 300, 178, 4, theme.border);

  tft_.setTextSize(1);
  tft_.setTextColor(ColorGold, theme.card);
  tft_.setCursor(20, 42);
  tft_.print(nw.pubDates[idx][0] ? nw.pubDates[idx] : "Update Terkini Live");

  tft_.drawFastHLine(20, 52, 280, ColorGold);

  // Judul Lengkap (Word Wrap otomatis)
  tft_.setTextSize(1);
  tft_.setTextColor(ColorWhite, theme.card);
  const char* hText = nw.headlines[idx];
  int curY = 58;
  int hLen = strlen(hText);
  int offset = 0;
  for (int line = 0; line < 3 && offset < hLen; ++line) {
    char lBuf[46] = "";
    int chunk = 42;
    if (offset + chunk >= hLen) {
      strncpy(lBuf, hText + offset, sizeof(lBuf) - 1);
      offset = hLen;
    } else {
      while (chunk > 20 && hText[offset + chunk] != ' ') chunk--;
      if (chunk <= 20) chunk = 42;
      strncpy(lBuf, hText + offset, chunk);
      lBuf[chunk] = '\0';
      offset += chunk;
      if (offset < hLen && hText[offset] == ' ') offset++;
    }
    tft_.setCursor(20, curY);
    tft_.print(lBuf);
    curY += 12;
  }

  tft_.drawFastHLine(20, curY + 2, 280, theme.border);
  curY += 8;

  // Narasi Deskripsi Berita Lengkap (Word Wrap)
  tft_.setTextSize(1);
  tft_.setTextColor(ColorCyan, theme.card);
  const char* dText = nw.descriptions[idx][0] ? nw.descriptions[idx] : "Menghubungkan ke CNBC RSS Feed...";
  int dLen = strlen(dText);
  offset = 0;
  for (int line = 0; line < 7 && offset < dLen; ++line) {
    char lBuf[46] = "";
    int chunk = 44;
    if (offset + chunk >= dLen) {
      strncpy(lBuf, dText + offset, sizeof(lBuf) - 1);
      offset = dLen;
    } else {
      while (chunk > 25 && dText[offset + chunk] != ' ') chunk--;
      if (chunk <= 25) chunk = 44;
      strncpy(lBuf, dText + offset, chunk);
      lBuf[chunk] = '\0';
      offset += chunk;
      if (offset < dLen && dText[offset] == ' ') offset++;
    }
    tft_.setCursor(20, curY);
    tft_.print(lBuf);
    curY += 11;
    if (curY > 198) break;
  }

  tft_.setTextSize(1);
  tft_.setTextColor(ColorMuted, theme.bg);
  tft_.setCursor(24, 218);
  tft_.print("Tekan [< KEMBALI] atau swipe kanan utk berita lain");
}

namespace {
void formatBitrate(float mbps, char* buf, size_t cap) {
  if (mbps >= 1000.0f) {
    snprintf(buf, cap, "%.2fG", mbps / 1000.0f);
  } else if (mbps >= 100.0f) {
    snprintf(buf, cap, "%.0fM", mbps);
  } else if (mbps >= 1.0f) {
    snprintf(buf, cap, "%.1fM", mbps);
  } else {
    snprintf(buf, cap, "%.0fK", mbps * 1000.0f);
  }
}

void drawClientTableScreen(Adafruit_ILI9341& tft, const ThemeColors& theme,
                           const char* title, uint16_t accentCol,
                           int totalOnline, int count, const jws::MikrotikClientUser* users,
                           int& page, bool& sceneInitDone, uint32_t& lastRefreshMs, uint32_t nowMs) {
  if (!sceneInitDone) {
    sceneInitDone = true;
    lastRefreshMs = 0;
    tft.fillScreen(theme.bg);

    // Header Bar
    tft.fillRect(0, 0, 320, 28, theme.header);
    tft.drawFastHLine(0, 28, 320, accentCol);

    tft.fillRoundRect(6, 4, 72, 20, 3, ColorHeader);
    tft.drawRoundRect(6, 4, 72, 20, 3, ColorCyan);
    tft.setTextSize(1);
    tft.setTextColor(ColorCyan, ColorHeader);
    tft.setCursor(11, 10);
    tft.print("< KEMBALI");

    tft.setTextSize(1);
    tft.setTextColor(accentCol, theme.header);
    tft.setCursor(86, 10);
    tft.print(title);

    // Table Header Bar
    tft.fillRoundRect(8, 32, 304, 18, 3, theme.header);
    tft.setTextColor(ColorGold, theme.header);
    tft.setCursor(14, 37);
    tft.print("USERNAME");
    tft.setCursor(108, 37);
    tft.print("IP ADDRESS");
    tft.setCursor(192, 37);
    tft.print("UPTIME");
    tft.setCursor(252, 37);
    tft.print("RX / TX");

    // Table Frame
    tft.fillRoundRect(8, 52, 304, 160, 4, theme.card);
    tft.drawRoundRect(8, 52, 304, 160, 4, theme.border);
  }

  if (lastRefreshMs != 0 && static_cast<uint32_t>(nowMs - lastRefreshMs) < 1000UL) {
    return;
  }
  lastRefreshMs = nowMs;

  const int totalPages = (count > 0) ? ((count + 4) / 5) : 1;
  if (page < 0 || page >= totalPages) page = 0;
  const int startIdx = page * 5;

  // Update Online Count Badge in Header
  tft.fillRect(236, 5, 78, 18, theme.header);
  tft.fillRoundRect(240, 6, 72, 16, 3, accentCol);
  char badge[18];
  snprintf(badge, sizeof(badge), "%d Online", totalOnline);
  tft.setTextSize(1);
  tft.setTextColor(0x0000, accentCol);
  tft.setCursor(246, 10);
  tft.print(badge);

  // Redraw Table Rows Cleanly
  tft.fillRect(10, 54, 300, 156, theme.card);
  for (int r = 0; r < 5; ++r) {
    const int idx = startIdx + r;
    const int ry = 58 + r * 30;
    if (r > 0) {
      tft.drawFastHLine(14, ry - 4, 292, theme.border);
    }
    if (idx >= count || users[idx].username[0] == '\0') {
      if (r == 0 && count == 0) {
        tft.setTextColor(ColorMuted, theme.card);
        tft.setCursor(62, 120);
        tft.print("Belum ada sesi aktif di router");
      }
      continue;
    }
    const auto& u = users[idx];

    // Username (max 14 chars)
    char uName[16];
    snprintf(uName, sizeof(uName), "%.14s", u.username);
    tft.setTextSize(1);
    tft.setTextColor(ColorWhite, theme.card);
    tft.setCursor(14, ry + 5);
    tft.print(uName);

    // IP Address
    tft.setTextColor(ColorCyan, theme.card);
    tft.setCursor(104, ry + 5);
    tft.print(u.address);

    // Uptime (compact max 9 chars)
    char upBuf[11];
    snprintf(upBuf, sizeof(upBuf), "%.9s", u.uptime);
    tft.setTextColor(ColorMuted, theme.card);
    tft.setCursor(188, ry + 5);
    tft.print(upBuf);

    // Live RX & TX stacked neatly
    char rxBuf[12], txBuf[12];
    formatBitrate(u.rxMbps, rxBuf, sizeof(rxBuf));
    formatBitrate(u.txMbps, txBuf, sizeof(txBuf));
    tft.setTextColor(ColorEmerald, theme.card);
    tft.setCursor(252, ry);
    tft.print("D:");
    tft.print(rxBuf);
    tft.setTextColor(ColorAmber, theme.card);
    tft.setCursor(252, ry + 11);
    tft.print("U:");
    tft.print(txBuf);
  }

  // Update Footer Hint + Page Indicator
  tft.fillRect(0, 214, 320, 26, theme.bg);
  tft.setTextSize(1);
  if (totalPages > 1) {
    tft.setTextColor(ColorMuted, theme.bg);
    tft.setCursor(10, 219);
    tft.print("Tap tabel utk hal. berikutnya | < Kembali");

    tft.fillRoundRect(242, 215, 70, 18, 3, theme.header);
    tft.drawRoundRect(242, 215, 70, 18, 3, accentCol);
    char pgBuf[24];
    snprintf(pgBuf, sizeof(pgBuf), "Hal %d/%d >", page + 1, totalPages);
    tft.setTextColor(accentCol, theme.header);
    tft.setCursor(247, 220);
    tft.print(pgBuf);
  } else {
    tft.setTextColor(ColorMuted, theme.bg);
    tft.setCursor(26, 218);
    tft.print("Tap [< KEMBALI] atau swipe kanan ke NOC");
  }
}
}  // namespace

void Ili9341DisplayDriver::drawSlideMikrotik(const jws::DisplayFrame& frame, uint32_t nowMs) {
  (void)frame;
  const auto& theme = kThemes[colorTheme_ % 4];
  const auto& mt = jws::MikrotikService::instance().feed();

  if (!sceneInitDone_) {
    sceneInitDone_ = true;
    lastNocRefreshMs_ = 0;
    tft_.fillScreen(theme.bg);

    // Header Bar
    tft_.fillRect(0, 0, 320, 26, theme.header);
    tft_.drawFastHLine(0, 26, 320, ColorCyan);

    // 4 Interface Card Shells (2x2 Grid)
    for (int i = 0; i < 4; ++i) {
      const int col = i % 2;
      const int row = i / 2;
      const int cx = 8 + col * 156;
      const int cy = 32 + row * 62;
      tft_.fillRoundRect(cx, cy, 148, 58, 4, theme.card);
      tft_.drawRoundRect(cx, cy, 148, 58, 4, theme.border);
    }

    // 2 Interactive Bottom Sub-Menu Buttons: PPPoE & Hotspot
    tft_.fillRoundRect(8, 158, 148, 54, 4, theme.card);
    tft_.drawRoundRect(8, 158, 148, 54, 4, ColorCyan);
    tft_.setTextSize(1);
    tft_.setTextColor(ColorCyan, theme.card);
    tft_.setCursor(16, 165);
    tft_.print("PPPoE ACTIVE USER");
    tft_.setTextColor(ColorMuted, theme.card);
    tft_.setCursor(16, 199);
    tft_.print("Tap utk list & trafik >");

    tft_.fillRoundRect(164, 158, 148, 54, 4, theme.card);
    tft_.drawRoundRect(164, 158, 148, 54, 4, ColorGold);
    tft_.setTextColor(ColorGold, theme.card);
    tft_.setCursor(172, 165);
    tft_.print("HOTSPOT USER");
    tft_.setTextColor(ColorMuted, theme.card);
    tft_.setCursor(172, 199);
    tft_.print("Tap utk list & trafik >");

    drawSlideDots(4);
  }

  if (lastNocRefreshMs_ != 0 && static_cast<uint32_t>(nowMs - lastNocRefreshMs_) < 1000UL) {
    return;
  }
  lastNocRefreshMs_ = nowMs;

  // 1. Update Header Telemetry (Identity, CPU Alert, Uptime/LinkMode — Zero-Flicker in-place)
  tft_.setTextSize(1);
  tft_.setTextColor(ColorCyan, theme.header);
  tft_.setCursor(8, 5);
  tft_.printf("%-24.24s", mt.identity);

  tft_.setTextColor(ColorMuted, theme.header);
  tft_.setCursor(8, 15);
  tft_.printf("UP: %-20.20s", mt.uptime);

  // Status badge LIVE vs OFFLN vs DEMO
  const bool hasCreds = (mt.pass[0] != '\0');
  const uint16_t modeCol = mt.connected ? ColorEmerald : (hasCreds ? ColorCoral : ColorAmber);
  const char* modeLbl = mt.connected ? "LIVE " : (hasCreds ? "OFFLN" : "DEMO ");
  tft_.fillRoundRect(158, 5, 44, 15, 3, modeCol);
  tft_.setTextColor(0x0000, modeCol);
  tft_.setCursor(164, 9);
  tft_.print(modeLbl);

  // CPU Load Pill (Green <60, Amber 60..84, Red Alert >=85)
  const uint16_t cpuCol = (mt.cpuLoad >= 85) ? 0xF800 : ((mt.cpuLoad >= 60) ? ColorAmber : ColorEmerald);
  tft_.fillRoundRect(208, 5, 56, 15, 3, cpuCol);
  char cpuBuf[12];
  snprintf(cpuBuf, sizeof(cpuBuf), "CPU %2u%%", mt.cpuLoad);
  tft_.setTextColor(0x0000, cpuCol);
  tft_.setCursor(213, 9);
  tft_.print(cpuBuf);

  // RAM / Temp Pill
  char memBuf[10];
  snprintf(memBuf, sizeof(memBuf), "M:%2u%% ", mt.memPercent);
  tft_.setTextColor(ColorWhite, theme.header);
  tft_.setCursor(270, 9);
  tft_.print(memBuf);

  // 2. Update 4 Interface Cards (Zero-Flicker in-place without card fillRect)
  for (int i = 0; i < 4; ++i) {
    const int col = i % 2;
    const int row = i / 2;
    const int cx = 8 + col * 156;
    const int cy = 32 + row * 62;
    const auto& iface = mt.ifaces[i];
    const char* dispName = iface.label[0] ? iface.label : iface.name;

    // Alert border on Link Down
    const uint16_t borderCol = iface.running ? theme.border : 0xF800;
    tft_.drawRoundRect(cx, cy, 148, 58, 4, borderCol);

    // Link status circle + R/D badge
    const uint16_t stCol = iface.running ? ColorEmerald : 0xF800;
    tft_.fillCircle(cx + 12, cy + 12, 5, stCol);
    tft_.setTextSize(1);
    tft_.setTextColor(0x0000, stCol);
    tft_.setCursor(cx + 10, cy + 9);
    tft_.print(iface.running ? "R" : "D");

    // Interface Name (up to 16 chars padded)
    tft_.setTextColor(ColorWhite, theme.card);
    tft_.setCursor(cx + 22, cy + 8);
    tft_.printf("%-16.16s", dispName);

    // RX & TX Live Throughput
    char rxBuf[12], txBuf[12];
    formatBitrate(iface.rxMbps, rxBuf, sizeof(rxBuf));
    formatBitrate(iface.txMbps, txBuf, sizeof(txBuf));

    tft_.setTextColor(ColorEmerald, theme.card);
    tft_.setCursor(cx + 8, cy + 24);
    tft_.printf("RX:%-8.8s", rxBuf);

    tft_.setTextColor(ColorAmber, theme.card);
    tft_.setCursor(cx + 80, cy + 24);
    tft_.printf("TX:%-8.8s", txBuf);

    // Mini Bandwidth Progress Bar (scaled to 500 Mbps visual bar)
    const int barW = 132;
    const int barX = cx + 8;
    const int barY = cy + 40;
    if (iface.running) {
      int fillW = static_cast<int>(((iface.rxMbps + iface.txMbps) / 500.0f) * barW);
      if (fillW > barW) fillW = barW;
      if (fillW < 4) fillW = 4;
      tft_.fillRect(barX, barY, fillW, 6, (iface.rxMbps > 350.0f) ? ColorCoral : ColorCyan);
      if (fillW < barW) {
        tft_.fillRect(barX + fillW, barY, barW - fillW, 6, 0x0000);
      }
    } else {
      tft_.setTextColor(ColorCoral, theme.card);
      tft_.setCursor(barX, barY - 1);
      tft_.print("LINK DOWN ALERT!      ");
    }
  }

  // 3. Update PPPoE & Hotspot Action Cards (Zero-Flicker count update)
  tft_.setTextSize(2);
  tft_.setTextColor(ColorWhite, theme.card);
  tft_.setCursor(16, 179);
  tft_.printf("%-10s", (String(mt.totalPppoe) + " Online").c_str());

  tft_.setCursor(172, 179);
  tft_.printf("%-10s", (String(mt.totalHotspot) + " Online").c_str());
}

void Ili9341DisplayDriver::drawSlideMikrotikPortDetail(const jws::DisplayFrame& frame, uint32_t nowMs) {
  (void)frame;
  const auto& theme = kThemes[colorTheme_ % 4];
  const auto& mt = jws::MikrotikService::instance().feed();
  const int idx = (selectedPortIndex_ >= 0 && selectedPortIndex_ < 4) ? selectedPortIndex_ : 0;
  const auto& iface = mt.ifaces[idx];
  const char* dispName = iface.label[0] ? iface.label : iface.name;

  if (!sceneInitDone_) {
    sceneInitDone_ = true;
    lastNocRefreshMs_ = 0;
    tft_.fillScreen(theme.bg);

    // Header Bar
    tft_.fillRect(0, 0, 320, 28, theme.header);
    tft_.drawFastHLine(0, 28, 320, ColorCyan);

    tft_.fillRoundRect(6, 4, 72, 20, 3, ColorHeader);
    tft_.drawRoundRect(6, 4, 72, 20, 3, ColorCyan);
    tft_.setTextSize(1);
    tft_.setTextColor(ColorCyan, ColorHeader);
    tft_.setCursor(11, 10);
    tft_.print("< KEMBALI");

    tft_.setTextSize(1);
    tft_.setTextColor(ColorWhite, theme.header);
    tft_.setCursor(86, 10);
    tft_.print("CCR: ");
    tft_.setTextColor(ColorGold, theme.header);
    char hdrName[18];
    snprintf(hdrName, sizeof(hdrName), "%.16s", dispName);
    tft_.print(hdrName);

    // Graph Box Shell (Y: 32..144)
    tft_.fillRoundRect(8, 32, 304, 112, 4, 0x0821);
    tft_.drawRoundRect(8, 32, 304, 112, 4, theme.border);

    // Telemetry Box Shell (Y: 148..214)
    tft_.fillRoundRect(8, 148, 304, 66, 4, theme.card);
    tft_.drawRoundRect(8, 148, 304, 66, 4, theme.border);

    tft_.setTextSize(1);
    tft_.setTextColor(ColorMuted, theme.bg);
    tft_.setCursor(26, 218);
    tft_.print("Tap layar / swipe kanan utk kembali ke NOC");
  }

  if (lastNocRefreshMs_ != 0 && static_cast<uint32_t>(nowMs - lastNocRefreshMs_) < 1000UL) {
    return;
  }
  lastNocRefreshMs_ = nowMs;

  // Header status badge
  tft_.fillRect(236, 5, 78, 18, theme.header);
  const uint16_t stCol = iface.running ? ColorEmerald : 0xF800;
  tft_.fillRoundRect(238, 6, 74, 16, 3, stCol);
  tft_.setTextSize(1);
  tft_.setTextColor(0x0000, stCol);
  tft_.setCursor(245, 10);
  tft_.print(iface.running ? "RUNNING" : "LINK DOWN");

  // Redraw Inner Graph Box (anti-flicker)
  const int gx = 12, gy = 36, gw = 296, gh = 104;
  tft_.fillRect(gx, gy, gw, gh, 0x0821);

  for (int y = gy + 22; y < gy + gh; y += 22) {
    tft_.drawFastHLine(gx, y, gw, 0x10A4);
  }
  for (int x = gx + 48; x < gx + gw; x += 48) {
    tft_.drawFastVLine(x, gy + 14, gh - 16, 0x10A4);
  }

  // Dynamic Y scale (minimum 0.5 Mbps so small links still draw cleanly)
  float maxVal = 0.5f;
  for (int i = 0; i < 12; ++i) {
    if (iface.rxHistory[i] > maxVal) maxVal = iface.rxHistory[i];
    if (iface.txHistory[i] > maxVal) maxVal = iface.txHistory[i];
  }
  maxVal *= 1.2f;

  // Legend & Scale header inside graph
  char scaleBuf[20];
  formatBitrate(maxVal, scaleBuf, sizeof(scaleBuf));
  tft_.setTextSize(1);
  tft_.setTextColor(ColorEmerald, 0x0821);
  tft_.setCursor(gx + 4, gy + 2);
  tft_.print("-- RX");
  tft_.setTextColor(ColorAmber, 0x0821);
  tft_.setCursor(gx + 54, gy + 2);
  tft_.print("-- TX");
  tft_.setTextColor(ColorMuted, 0x0821);
  tft_.setCursor(gx + 218, gy + 2);
  tft_.print("Peak:");
  tft_.print(scaleBuf);

  // Plot 12-point dual waveform
  const int chartTop = gy + 16;
  const int chartH = gh - 20;
  int prevX = gx + 4;
  int prevRxY = chartTop + chartH;
  int prevTxY = chartTop + chartH;
  for (int i = 0; i < 12; ++i) {
    const int px = gx + 6 + (i * (gw - 14)) / 11;
    int rxY = chartTop + chartH - static_cast<int>((iface.rxHistory[i] / maxVal) * chartH);
    int txY = chartTop + chartH - static_cast<int>((iface.txHistory[i] / maxVal) * chartH);
    if (rxY < chartTop) rxY = chartTop;
    if (rxY > chartTop + chartH) rxY = chartTop + chartH;
    if (txY < chartTop) txY = chartTop;
    if (txY > chartTop + chartH) txY = chartTop + chartH;

    if (i > 0) {
      tft_.drawLine(prevX, prevRxY, px, rxY, ColorEmerald);
      tft_.drawLine(prevX, prevRxY + 1, px, rxY + 1, ColorEmerald);
      tft_.drawLine(prevX, prevTxY, px, txY, ColorAmber);
      tft_.drawLine(prevX, prevTxY + 1, px, txY + 1, ColorAmber);
    }
    tft_.fillCircle(px, rxY, 2, ColorCyan);
    tft_.fillCircle(px, txY, 2, ColorGold);
    prevX = px;
    prevRxY = rxY;
    prevTxY = txY;
  }

  // Redraw Telemetry Stats Box
  tft_.fillRect(10, 150, 300, 62, theme.card);
  char rxStr[12], txStr[12], pkRx[12], pkTx[12];
  formatBitrate(iface.rxMbps, rxStr, sizeof(rxStr));
  formatBitrate(iface.txMbps, txStr, sizeof(txStr));
  formatBitrate(iface.peakRxMbps, pkRx, sizeof(pkRx));
  formatBitrate(iface.peakTxMbps, pkTx, sizeof(pkTx));

  tft_.setTextSize(1);
  tft_.setTextColor(ColorEmerald, theme.card);
  tft_.setCursor(16, 156);
  tft_.printf("RX: %-6s (%lu pps)", rxStr, static_cast<unsigned long>(iface.rxPps));
  tft_.setTextColor(ColorCyan, theme.card);
  tft_.setCursor(208, 156);
  tft_.printf("Max: %s", pkRx);

  tft_.setTextColor(ColorAmber, theme.card);
  tft_.setCursor(16, 174);
  tft_.printf("TX: %-6s (%lu pps)", txStr, static_cast<unsigned long>(iface.txPps));
  tft_.setTextColor(ColorGold, theme.card);
  tft_.setCursor(208, 174);
  tft_.printf("Max: %s", pkTx);

  tft_.drawFastHLine(16, 190, 288, theme.border);
  tft_.setTextColor(ColorMuted, theme.card);
  tft_.setCursor(16, 196);
  tft_.printf("%s | CPU %u%% | Up %s", mt.boardName, mt.cpuLoad, mt.uptime);
}

void Ili9341DisplayDriver::drawSlideMikrotikPppoe(const jws::DisplayFrame& frame, uint32_t nowMs) {
  (void)frame;
  const auto& theme = kThemes[colorTheme_ % 4];
  const auto& mt = jws::MikrotikService::instance().feed();
  drawClientTableScreen(tft_, theme, "PPPoE ACTIVE TRAFFIC", ColorCyan,
                        mt.totalPppoe, mt.pppoeCount, mt.pppoeUsers,
                        nocListPage_, sceneInitDone_, lastNocRefreshMs_, nowMs);
}

void Ili9341DisplayDriver::drawSlideMikrotikHotspot(const jws::DisplayFrame& frame, uint32_t nowMs) {
  (void)frame;
  const auto& theme = kThemes[colorTheme_ % 4];
  const auto& mt = jws::MikrotikService::instance().feed();
  drawClientTableScreen(tft_, theme, "HOTSPOT ACTIVE USER", ColorGold,
                        mt.totalHotspot, mt.hotspotCount, mt.hotspotUsers,
                        nocListPage_, sceneInitDone_, lastNocRefreshMs_, nowMs);
}

void Ili9341DisplayDriver::drawSlideXiaozhi(const jws::DisplayFrame& frame, uint32_t nowMs) {
  (void)frame;
  static constexpr uint16_t ColorMint = 0x07F3;
  static constexpr uint16_t ColorWave = 0x05FF;
  static uint8_t prevBars[12] = {0};
  static int8_t prevEyePose = -99;
  static jws::XiaozhiState prevFooterState = static_cast<jws::XiaozhiState>(255);
  static uint8_t prevMicBucket = 255;
  static char prevBadge[28] = "";
  static char prevAgentName[28] = "";
  static char prevQuery[72] = "";
  static char prevReply1[52] = "";
  static char prevReply2[52] = "";
  static char prevReply3[52] = "";

  const jws::XiaozhiSnapshot snap = jws::XiaozhiService::instance().snapshot();

  if (!sceneInitDone_) {
    sceneInitDone_ = true;
    lastXiaozhiAnimMs_ = 0;
    prevEyePose = -99;
    prevFooterState = static_cast<jws::XiaozhiState>(255);
    prevMicBucket = 255;
    prevBadge[0] = '\0';
    prevAgentName[0] = '\0';
    prevQuery[0] = '\0';
    prevReply1[0] = '\0';
    prevReply2[0] = '\0';
    prevReply3[0] = '\0';
    memset(prevBars, 0, sizeof(prevBars));

    tft_.fillScreen(0x0000);
    // Top Header Bar
    tft_.fillRect(0, 0, 320, 25, 0x0842);
    tft_.drawFastHLine(0, 25, 320, ColorCyan);

    // Bottom Dialog Card Frame
    tft_.fillRoundRect(14, 143, 292, 67, 8, 0x0842);
    tft_.drawRoundRect(14, 143, 292, 67, 8, ColorCyan);

    drawSlideDots(5);
  }

  if (lastXiaozhiAnimMs_ != 0 && static_cast<uint32_t>(nowMs - lastXiaozhiAnimMs_) < 110UL) {
    return;
  }
  lastXiaozhiAnimMs_ = nowMs;

  // 0. Update Agent Name Header when changed
  if (strcmp(prevAgentName, snap.agentName) != 0) {
    strncpy(prevAgentName, snap.agentName, sizeof(prevAgentName) - 1);
    prevAgentName[sizeof(prevAgentName) - 1] = '\0';
    tft_.fillRect(8, 2, 146, 20, 0x0842);
    tft_.setTextSize(1);
    tft_.setTextColor(ColorCyan, 0x0842);
    tft_.setCursor(10, 9);
    char hdrAgent[22];
    snprintf(hdrAgent, sizeof(hdrAgent), "%.20s", snap.agentName);
    tft_.print(hdrAgent);
  }

  // 1. Update Prayer & Clock Badge when changed
  if (strcmp(prevBadge, snap.nextPrayerBadge) != 0) {
    strncpy(prevBadge, snap.nextPrayerBadge, sizeof(prevBadge) - 1);
    prevBadge[sizeof(prevBadge) - 1] = '\0';
    tft_.fillRect(156, 2, 160, 20, 0x0842);
    tft_.setTextSize(1);
    tft_.setTextColor(ColorWhite, 0x0842);
    const int bx = 312 - static_cast<int>(strlen(prevBadge)) * 6;
    tft_.setCursor((bx > 158) ? bx : 158, 9);
    tft_.print(prevBadge);
  }

  // 2. Animated Mint/Cyan Robot Eyes + Curved Smile
  const uint32_t blinkCycle = nowMs % 4200UL;
  const bool blinking = (blinkCycle > 4020UL);
  int8_t lookDir = 0; // -1 left, 0 center, +1 right, 2 thinking-up
  if (snap.state == jws::XiaozhiState::Thinking) {
    lookDir = 2;
  } else {
    const uint32_t lookSlot = (nowMs / 1600UL) % 4;
    if (lookSlot == 1) lookDir = -1;
    else if (lookSlot == 3) lookDir = 1;
  }
  const int8_t curEyePose = blinking ? -10 : (static_cast<int8_t>(snap.state) * 10 + lookDir);

  if (curEyePose != prevEyePose) {
    prevEyePose = curEyePose;
    tft_.fillRect(84, 30, 152, 52, 0x0000);

    if (blinking) {
      // Closed happy/blink eyes
      tft_.fillRoundRect(90, 48, 52, 7, 3, ColorMint);
      tft_.fillRoundRect(178, 48, 52, 7, 3, ColorMint);
    } else {
      tft_.fillRoundRect(90, 31, 52, 38, 15, ColorMint);
      tft_.fillRoundRect(178, 31, 52, 38, 15, ColorMint);

      int pxOff = 0;
      int pyOff = 0;
      if (lookDir == -1) pxOff = -7;
      else if (lookDir == 1) pxOff = 7;
      else if (lookDir == 2) pyOff = -6;

      tft_.fillCircle(116 + pxOff, 50 + pyOff, 7, ColorWhite);
      tft_.fillCircle(204 + pxOff, 50 + pyOff, 7, ColorWhite);
    }

    // Smile Arc between eyes (Y: 73..79)
    for (int dx = -20; dx <= 20; ++dx) {
      const int sy = 79 - (dx * dx) / 80;
      tft_.drawPixel(160 + dx, sy, ColorMint);
      tft_.drawPixel(160 + dx, sy + 1, ColorMint);
    }
  }

  // 3. 12-Bar Audio Spectrum Waveform (Y: 85..139, zero-flicker delta erase)
  const uint16_t barCol = (snap.state == jws::XiaozhiState::Listening) ? ColorMint
                          : ((snap.state == jws::XiaozhiState::Thinking) ? ColorGold : ColorWave);
  for (int i = 0; i < 12; ++i) {
    const uint8_t nh = snap.bars[i];
    if (nh != prevBars[i] || snap.state == jws::XiaozhiState::Listening) {
      prevBars[i] = nh;
      const int bx = 68 + i * 16;
      tft_.fillRect(bx, 85, 7, 55, 0x0000);
      const int by = 112 - static_cast<int>(nh) / 2;
      tft_.fillRoundRect(bx, by, 7, nh, 3, barCol);
    }
  }

  // 4. Dialog Box Text (only redrawn when query/reply text changes — 3 full clean lines!)
  if (strcmp(prevQuery, snap.userQuery) != 0 ||
      strcmp(prevReply1, snap.replyLine1) != 0 ||
      strcmp(prevReply2, snap.replyLine2) != 0 ||
      strcmp(prevReply3, snap.replyLine3) != 0) {
    strncpy(prevQuery, snap.userQuery, sizeof(prevQuery) - 1);
    prevQuery[sizeof(prevQuery) - 1] = '\0';
    strncpy(prevReply1, snap.replyLine1, sizeof(prevReply1) - 1);
    prevReply1[sizeof(prevReply1) - 1] = '\0';
    strncpy(prevReply2, snap.replyLine2, sizeof(prevReply2) - 1);
    prevReply2[sizeof(prevReply2) - 1] = '\0';
    strncpy(prevReply3, snap.replyLine3, sizeof(prevReply3) - 1);
    prevReply3[sizeof(prevReply3) - 1] = '\0';

    tft_.fillRect(18, 146, 284, 61, 0x0842);
    tft_.setTextSize(1);

    char qLine[48];
    snprintf(qLine, sizeof(qLine), "User: \"%.38s\"", prevQuery);
    tft_.setTextColor(ColorGold, 0x0842);
    tft_.setCursor(22, 149);
    tft_.print(qLine);

    char xLine[56];
    snprintf(xLine, sizeof(xLine), "Upin: %.39s", prevReply1);
    tft_.setTextColor(ColorWhite, 0x0842);
    tft_.setCursor(22, 163);
    tft_.print(xLine);

    if (prevReply2[0] != '\0') {
      char r2Line[50];
      snprintf(r2Line, sizeof(r2Line), "%.46s", prevReply2);
      tft_.setTextColor(ColorMint, 0x0842);
      tft_.setCursor(22, 177);
      tft_.print(r2Line);
    }

    if (prevReply3[0] != '\0') {
      char r3Line[50];
      snprintf(r3Line, sizeof(r3Line), "%.46s", prevReply3);
      tft_.setTextColor(ColorMint, 0x0842);
      tft_.setCursor(22, 191);
      tft_.print(r3Line);
    }
  }

  // 5. Footer Status Hint
  const uint8_t micBucket = snap.micLevel / 3;
  if (snap.state != prevFooterState ||
      (snap.state == jws::XiaozhiState::Idle && micBucket != prevMicBucket)) {
    prevFooterState = snap.state;
    prevMicBucket = micBucket;
    tft_.fillRect(0, 214, 320, 14, 0x0000);
    tft_.setTextSize(1);
    char hintBuf[54];
    const char* hint = hintBuf;
    uint16_t hCol = ColorMuted;
    if (snap.state == jws::XiaozhiState::Listening) {
      hint = "[Upin Mendengarkan Suara Anda (Mic INMP441)]";
      hCol = ColorMint;
    } else if (snap.state == jws::XiaozhiState::Thinking) {
      hint = "[Upin Sedang Menghubungi AI Internet...]";
      hCol = ColorGold;
    } else if (snap.state == jws::XiaozhiState::Speaking) {
      hint = "[Upin Menjawab | Panggil/Tap Lagi u/ Tanya]";
      hCol = ColorCyan;
    } else {
      snprintf(hintBuf, sizeof(hintBuf), "[Mic:%u%% | Panggil 'Halo Upin' / Tap]", snap.micLevel);
    }
    tft_.setTextColor(hCol, 0x0000);
    const int hx = 160 - static_cast<int>(strlen(hint)) * 3;
    tft_.setCursor((hx > 4) ? hx : 4, 217);
    tft_.print(hint);
  }
}

void Ili9341DisplayDriver::drawSlideFocusSystem(const jws::DisplayFrame& frame, uint32_t nowMs) {
  (void)frame;
  const auto& theme = kThemes[colorTheme_ % 4];
  static bool prevInit = false;
  static uint8_t prevBri = 255;

  if (!sceneInitDone_) {
    sceneInitDone_ = true;
    lastFocusRefreshMs_ = 0;
    prevInit = false;
    prevBri = 255;

    tft_.fillScreen(theme.bg);
    tft_.fillRect(0, 0, 320, 26, theme.header);
    tft_.drawFastHLine(0, 26, 320, theme.accent1);

    tft_.setTextSize(2);
    tft_.setTextColor(theme.accent1, theme.header);
    tft_.setCursor(8, 6);
    tft_.print("TASK MANAGER");

    // Top-Left Card: CPU Usage Graph (x=6, y=30, w=150, h=76)
    tft_.fillRoundRect(6, 30, 150, 76, 4, theme.card);
    tft_.drawRoundRect(6, 30, 150, 76, 4, ColorCyan);

    // Top-Right Card: Internal RAM Graph (x=164, y=30, w=150, h=76)
    tft_.fillRoundRect(164, 30, 150, 76, 4, theme.card);
    tft_.drawRoundRect(164, 30, 150, 76, 4, ColorEmerald);

    // Middle Full-Width Card: 8MB Octal PSRAM Graph & Telemetry (x=6, y=110, w=308, h=66)
    tft_.fillRoundRect(6, 110, 308, 66, 4, theme.card);
    tft_.drawRoundRect(6, 110, 308, 66, 4, ColorGold);

    // Bottom Interactive Brightness Control Bar (x=6, y=180, w=308, h=34)
    tft_.fillRoundRect(6, 180, 308, 34, 4, theme.card);
    tft_.drawRoundRect(6, 180, 308, 34, 4, theme.border);

    // [-] Button
    tft_.fillRoundRect(12, 185, 36, 24, 3, theme.header);
    tft_.drawRoundRect(12, 185, 36, 24, 3, ColorCyan);
    tft_.setTextSize(2);
    tft_.setTextColor(ColorCyan, theme.header);
    tft_.setCursor(25, 190);
    tft_.print("-");

    // [+] Button
    tft_.fillRoundRect(272, 185, 36, 24, 3, theme.header);
    tft_.drawRoundRect(272, 185, 36, 24, 3, ColorCyan);
    tft_.setTextSize(2);
    tft_.setTextColor(ColorCyan, theme.header);
    tft_.setCursor(285, 190);
    tft_.print("+");

    drawSlideDots(6);
  }

  if (lastFocusRefreshMs_ != 0 && static_cast<uint32_t>(nowMs - lastFocusRefreshMs_) < 1000UL) {
    return;
  }
  lastFocusRefreshMs_ = nowMs;

  // Header Uptime & Clock Freq
  const uint32_t upSec = nowMs / 1000UL;
  tft_.setTextSize(1);
  tft_.setTextColor(ColorCyan, theme.header);
  tft_.setCursor(168, 9);
  tft_.printf("240MHz | Up %02lu:%02lu:%02lu",
              static_cast<unsigned long>(upSec / 3600UL),
              static_cast<unsigned long>((upSec % 3600UL) / 60UL),
              static_cast<unsigned long>(upSec % 60UL));

  // Helper lambda to plot a 20-point scrolling Task Manager waveform inside a box
  auto drawWaveformBox = [&](int gx, int gy, int gw, int gh,
                             const uint8_t* hist, uint8_t maxScale,
                             uint16_t lineCol, uint16_t dotCol) {
    tft_.fillRect(gx, gy, gw, gh, 0x0821);
    for (int y = gy + 14; y < gy + gh; y += 14) {
      tft_.drawFastHLine(gx, y, gw, 0x10A4);
    }
    for (int x = gx + 28; x < gx + gw; x += 28) {
      tft_.drawFastVLine(x, gy, gh, 0x10A4);
    }
    if (maxScale < 10) maxScale = 10;
    const int plotTop = gy + 3;
    const int plotH = gh - 6;
    int prevX = gx + 3;
    int prevY = plotTop + plotH;
    for (int i = 0; i < 20; ++i) {
      const int px = gx + 3 + (i * (gw - 7)) / 19;
      uint8_t val = hist[i];
      if (val > maxScale) val = maxScale;
      int py = plotTop + plotH - static_cast<int>((static_cast<uint32_t>(val) * plotH) / maxScale);
      if (py < plotTop) py = plotTop;
      if (py > plotTop + plotH) py = plotTop + plotH;
      if (i > 0) {
        tft_.drawLine(prevX, prevY, px, py, lineCol);
        if (py + 1 <= plotTop + plotH && prevY + 1 <= plotTop + plotH) {
          tft_.drawLine(prevX, prevY + 1, px, py + 1, lineCol);
        }
      }
      if (i == 19) {
        tft_.fillCircle(px, py, 2, dotCol);
      }
      prevX = px;
      prevY = py;
    }
  };

  // 1. CPU USAGE Graph Card
  const uint8_t curCpu = cpuHist_[19];
  tft_.setTextSize(1);
  tft_.setTextColor(ColorCyan, theme.card);
  tft_.setCursor(12, 35);
  tft_.printf("CPU: %2u%% (DualCore) ", curCpu);
  drawWaveformBox(12, 46, 138, 54, cpuHist_, 100, ColorCyan, ColorWhite);

  // 2. INTERNAL RAM (SRAM) Graph Card
  const uint32_t totHeapKB = ESP.getHeapSize() / 1024U;
  const uint32_t freeHeapKB = ESP.getFreeHeap() / 1024U;
  const uint32_t usedHeapKB = (totHeapKB > freeHeapKB) ? (totHeapKB - freeHeapKB) : 0U;
  const uint8_t curRam = ramHist_[19];
  tft_.setTextColor(ColorEmerald, theme.card);
  tft_.setCursor(170, 35);
  tft_.printf("RAM:%2u%% (%u/%uKB) ", curRam,
              static_cast<unsigned>(usedHeapKB), static_cast<unsigned>(totHeapKB));
  drawWaveformBox(170, 46, 138, 54, ramHist_, 100, ColorEmerald, ColorGold);

  // 3. 8MB OCTAL PSRAM Graph & Telemetry Card
  const uint32_t totPsKB = ESP.getPsramSize() / 1024U;
  const uint32_t freePsKB = ESP.getFreePsram() / 1024U;
  const uint32_t usedPsKB = (totPsKB > freePsKB) ? (totPsKB - freePsKB) : 0U;
  const uint8_t curPs = psramHist_[19];
  const auto cs = jws::ChronosService::instance().snapshot();
  const bool wifiOk = (WiFi.status() == WL_CONNECTED);

  tft_.setTextColor(ColorGold, theme.card);
  tft_.setCursor(12, 115);
  tft_.printf("PSRAM 8MB: %2u%%     ", curPs);

  tft_.setTextColor(ColorWhite, theme.card);
  tft_.setCursor(12, 129);
  tft_.printf("Use:%u/%uKB  ", static_cast<unsigned>(usedPsKB), static_cast<unsigned>(totPsKB));

  tft_.setTextColor(ColorCyan, theme.card);
  tft_.setCursor(12, 143);
  tft_.printf("Free:%uK Min:%uK ",
              static_cast<unsigned>(freePsKB),
              static_cast<unsigned>(ESP.getMinFreeHeap() / 1024U));

  tft_.setTextColor(ColorMuted, theme.card);
  tft_.setCursor(12, 157);
  tft_.printf("WiFi:%ddB BLE:%-4s",
              wifiOk ? WiFi.RSSI() : 0,
              cs.connected ? "ON" : "Wait");

  // Auto-scale PSRAM waveform (minimum 20% full scale so 1-15% PSRAM usage plots clearly)
  uint8_t psMax = 20;
  for (int i = 0; i < 20; ++i) {
    if (psramHist_[i] + 5 > psMax) psMax = (psramHist_[i] + 10 > 100) ? 100 : (psramHist_[i] + 10);
  }
  drawWaveformBox(166, 116, 142, 54, psramHist_, psMax, ColorGold, ColorCyan);

  // 4. Bottom Brightness Bar (only redrawn on init or when brightness changes)
  const uint8_t curBri = jws::OnlineFeedService::instance().getTftBrightness();
  if (!prevInit || curBri != prevBri) {
    prevBri = curBri;
    tft_.setTextSize(1);
    tft_.setTextColor(ColorWhite, theme.card);
    tft_.setCursor(58, 185);
    tft_.printf("BRIGHTNESS LAYAR: %3u%% (Tap - / +)", curBri);

    const int barX = 58, barY = 197, barW = 204, barH = 10;
    tft_.drawRect(barX, barY, barW, barH, theme.border);
    int fillW = static_cast<int>(((barW - 2) * curBri) / 100U);
    if (fillW < 2) fillW = 2;
    if (fillW > barW - 2) fillW = barW - 2;
    tft_.fillRect(barX + 1, barY + 1, fillW, barH - 2, ColorAmber);
    if (fillW < barW - 2) {
      tft_.fillRect(barX + 1 + fillW, barY + 1, (barW - 2) - fillW, barH - 2, 0x0821);
    }
  }

  prevInit = true;
}

void Ili9341DisplayDriver::drawSlideRelayControl(const jws::DisplayFrame& frame, uint32_t nowMs) {
  (void)frame;
  const auto& theme = kThemes[colorTheme_ % 4];
  static bool prevInit = false;
  static bool prevR1 = false;
  static bool prevR2 = false;
  static bool prevSinricConn = false;
  static bool prevSinricCfg = false;

  if (!sceneInitDone_) {
    sceneInitDone_ = true;
    lastRelayRefreshMs_ = 0;
    prevInit = false;

    tft_.fillScreen(theme.bg);
    tft_.fillRect(0, 0, 320, 26, theme.header);
    tft_.drawFastHLine(0, 26, 320, theme.accent1);

    tft_.setTextSize(2);
    tft_.setTextColor(theme.accent1, theme.header);
    tft_.setCursor(8, 6);
    tft_.print("KONTROL RELAY 2CH");

    // Bottom Quick All-ON / All-OFF bar (drawn once per slide entry)
    tft_.fillRoundRect(8, 184, 148, 28, 4, 0x0344);
    tft_.drawRoundRect(8, 184, 148, 28, 4, ColorEmerald);
    tft_.setTextSize(1);
    tft_.setTextColor(ColorEmerald, 0x0344);
    tft_.setCursor(38, 194);
    tft_.print("NYALAKAN SEMUA");

    tft_.fillRoundRect(164, 184, 148, 28, 4, 0x2084);
    tft_.drawRoundRect(164, 184, 148, 28, 4, ColorCoral);
    tft_.setTextColor(ColorCoral, 0x2084);
    tft_.setCursor(198, 194);
    tft_.print("MATIKAN SEMUA");

    drawSlideDots(7);
  }

  if (lastRelayRefreshMs_ != 0 && static_cast<uint32_t>(nowMs - lastRelayRefreshMs_) < 250UL) {
    return;
  }
  lastRelayRefreshMs_ = nowMs;

  const auto st = jws::RelayControlService::instance().snapshot();

  // Only redraw when relay or Sinric Pro state actually changes (zero flicker)
  if (prevInit && st.relay1 == prevR1 && st.relay2 == prevR2 &&
      st.sinricConnected == prevSinricConn && st.sinricConfigured == prevSinricCfg) {
    return;
  }

  if (!prevInit || st.sinricConnected != prevSinricConn || st.sinricConfigured != prevSinricCfg) {
    tft_.fillRect(216, 4, 100, 18, theme.header);
    tft_.setTextSize(1);
    const uint16_t badgeCol = st.sinricConnected ? ColorEmerald : (st.sinricConfigured ? ColorAmber : ColorCyan);
    const char* badgeTxt = st.sinricConnected ? "GOOGLE: OK" : (st.sinricConfigured ? "SYNCING..." : "TAP ON/OFF");
    tft_.setTextColor(badgeCol, theme.header);
    tft_.setCursor(226, 9);
    tft_.print(badgeTxt);
  }

  auto drawRelayCard = [&](int x, int y, const char* title, const char* pinInfo, const char* linkTxt, bool on) {
    const uint16_t bg = on ? 0x0344 : theme.card;
    const uint16_t bd = on ? ColorEmerald : theme.border;
    tft_.fillRoundRect(x, y, 148, 142, 6, bg);
    tft_.drawRoundRect(x, y, 148, 142, 6, bd);

    tft_.setTextSize(2);
    tft_.setTextColor(ColorWhite, bg);
    tft_.setCursor(x + 14, y + 12);
    tft_.print(title);

    tft_.setTextSize(1);
    tft_.setTextColor(ColorMuted, bg);
    tft_.setCursor(x + 14, y + 36);
    tft_.print(pinInfo);

    tft_.setTextColor(st.sinricConnected ? ColorEmerald : ColorCyan, bg);
    tft_.setCursor(x + 14, y + 52);
    tft_.print(linkTxt);

    const uint16_t pillBg = on ? ColorEmerald : 0x3186;
    tft_.fillRoundRect(x + 14, y + 76, 120, 48, 6, pillBg);
    tft_.setTextSize(3);
    tft_.setTextColor(on ? 0x0000 : ColorWhite, pillBg);
    tft_.setCursor(x + (on ? 56 : 47), y + 89);
    tft_.print(on ? "ON" : "OFF");
  };

  const bool sinricChanged = (st.sinricConnected != prevSinricConn) || (st.sinricConfigured != prevSinricCfg);
  if (!prevInit || st.relay1 != prevR1 || sinricChanged) {
    drawRelayCard(8, 36, "RELAY CH 1", "GPIO 15 (Act-HIGH)",
                  st.hasDev1 ? (st.sinricConnected ? "Google Home: Siap" : "Sinric Pro: Wait") : "Mode: Lokal/Suara",
                  st.relay1);
  }
  if (!prevInit || st.relay2 != prevR2 || sinricChanged) {
    drawRelayCard(164, 36, "RELAY CH 2", "GPIO 16 (Act-LOW)",
                  st.hasDev2 ? (st.sinricConnected ? "Google Home: Siap" : "Sinric Pro: Wait") : "Mode: Lokal/Suara",
                  st.relay2);
  }

  prevInit = true;
  prevR1 = st.relay1;
  prevR2 = st.relay2;
  prevSinricConn = st.sinricConnected;
  prevSinricCfg = st.sinricConfigured;
}

void Ili9341DisplayDriver::updateStatusRgbLed(ScreenScene scene, uint32_t nowMs) {
#ifdef JWS_DESKTOP_S3
  if (lastRgbLedMs_ != 0 && static_cast<uint32_t>(nowMs - lastRgbLedMs_) < 160UL) {
    return;
  }
  lastRgbLedMs_ = nowMs;

  uint8_t r = 0, g = 0, b = 0;
  const auto xzState = jws::XiaozhiService::instance().snapshot().state;
  const auto cs = jws::ChronosService::instance().snapshot();
  const auto& mt = jws::MikrotikService::instance().feed();
  const bool pulseOn = ((nowMs / 450UL) % 2UL) == 0;

  if (cs.ringing) {
    r = pulseOn ? 28 : 4;
    g = 0;
    b = pulseOn ? 24 : 4;
  } else if (scene == ScreenScene::Adzan || scene == ScreenScene::Iqomah) {
    r = pulseOn ? 24 : 8;
    g = pulseOn ? 18 : 6;
    b = 0;
  } else if (xzState == jws::XiaozhiState::Listening) {
    r = 0; g = 28; b = 8;
  } else if (xzState == jws::XiaozhiState::Thinking) {
    r = 24; g = 14; b = 0;
  } else if (xzState == jws::XiaozhiState::Speaking) {
    r = 0; g = 18; b = 24;
  } else if (WiFi.status() != WL_CONNECTED || (mt.connected && mt.cpuLoad >= 80)) {
    r = pulseOn ? 20 : 6; g = 2; b = 2;
  } else {
    // Soft ambient emerald heartbeat
    g = pulseOn ? 8 : 3;
    b = 2;
  }
  neopixelWrite(48, r, g, b);
#else
  (void)scene;
  (void)nowMs;
#endif
}

void Ili9341DisplayDriver::updateBacklight(const jws::DisplayFrame& frame, uint32_t nowMs) {
  uint8_t targetBri = jws::OnlineFeedService::instance().getTftBrightness();
  if (frame.configuration.autoSleepEnabled && frame.clock.valid) {
    const uint8_t sh = frame.configuration.sleepHour;
    const uint8_t wh = frame.configuration.wakeHour;
    const int curH = frame.clock.hour;
    const bool inSleepWindow = (sh < wh)
        ? (curH >= sh && curH < wh)
        : (curH >= sh || curH < wh);
    if (inSleepWindow && static_cast<uint32_t>(nowMs - lastSlideTouchMs_) > 60000UL) {
      targetBri = 10;
    }
  }
  if (targetBri != lastAppliedBri_) {
    lastAppliedBri_ = targetBri;
    tft_.setBrightnessPct(targetBri);
#ifdef JWS_DESKTOP_S3
    ledcWrite(0, static_cast<uint32_t>((targetBri * 255U) / 100U));
#endif
    // Trigger immediate redraw of the active slide/scene at the new software brightness!
    sceneInitDone_ = false;
    haveFrame_ = false;
    lastFocusRefreshMs_ = 0;
    lastRelayRefreshMs_ = 0;
    lastNocRefreshMs_ = 0;
  }
}


bool Ili9341DisplayDriver::drawChronosAlertOverlay(uint32_t nowMs) {
  static uint32_t prevNotifCount = 0;
  static bool prevRinging = false;

  const auto cs = jws::ChronosService::instance().snapshot();
  const bool notifPopup = (cs.lastNotifMs != 0 && static_cast<uint32_t>(nowMs - cs.lastNotifMs) < 6000UL);
  const bool active = cs.ringing || notifPopup;

  if (!active) {
    if (chronosAlertActive_) {
      chronosAlertActive_ = false;
      sceneInitDone_ = false;
      haveFrame_ = false;
    }
    return false;
  }

  if (!chronosAlertActive_ || cs.ringing != prevRinging || cs.notifCount != prevNotifCount) {
    chronosAlertActive_ = true;
    prevRinging = cs.ringing;
    prevNotifCount = cs.notifCount;

    const uint16_t boxBg = cs.ringing ? 0x2004 : 0x0842;
    const uint16_t boxBd = cs.ringing ? ColorCoral : ColorCyan;

    tft_.fillRoundRect(16, 44, 288, 140, 8, boxBg);
    tft_.drawRoundRect(16, 44, 288, 140, 8, boxBd);
    tft_.drawRoundRect(17, 45, 286, 138, 8, boxBd);

    // Header Bar inside popup
    tft_.fillRoundRect(20, 48, 280, 24, 5, boxBd);
    tft_.setTextSize(1);
    tft_.setTextColor(0x0000, boxBd);
    tft_.setCursor(28, 56);
    if (cs.ringing) {
      tft_.print("PANGGILAN TELEPON MASUK (CHRONOS BLE)");
    } else {
      char hdr[46];
      snprintf(hdr, sizeof(hdr), "NOTIFIKASI HP: %.26s (%s)", cs.notifApp, cs.notifTime);
      tft_.print(hdr);
    }

    // Caller / Title
    tft_.setTextSize(2);
    tft_.setTextColor(ColorGold, boxBg);
    tft_.setCursor(26, 82);
    char titleBuf[24];
    snprintf(titleBuf, sizeof(titleBuf), "%.21s", cs.ringing ? cs.caller : cs.notifTitle);
    tft_.print(titleBuf);

    // Message Body (2 lines)
    tft_.setTextSize(1);
    tft_.setTextColor(ColorWhite, boxBg);
    if (cs.ringing) {
      tft_.setCursor(26, 114);
      tft_.print("Smartphone Anda sedang berdering!");
      tft_.setTextColor(ColorEmerald, boxBg);
      tft_.setCursor(26, 130);
      tft_.print("Silakan angkat panggilan di HP Anda.");
    } else {
      char line1[45] = "";
      char line2[45] = "";
      strncpy(line1, cs.notifMsg, 42);
      line1[42] = '\0';
      if (strlen(cs.notifMsg) > 42) {
        strncpy(line2, cs.notifMsg + 42, 42);
        line2[42] = '\0';
      }
      tft_.setCursor(26, 114);
      tft_.print(line1);
      if (line2[0] != '\0') {
        tft_.setTextColor(ColorCyan, boxBg);
        tft_.setCursor(26, 128);
        tft_.print(line2);
      }
    }

    // Footer Dismiss Hint
    tft_.setTextColor(ColorMuted, boxBg);
    tft_.setCursor(58, 166);
    tft_.print("[Tap Layar untuk Menutup Pop-up]");
  }
  return true;
}

void Ili9341DisplayDriver::drawScenePrayer(const jws::DisplayFrame& frame, uint32_t nowMs) {
  const bool validityChanged = frame.clock.valid != previous_.clock.valid;
  const bool dateChanged = !sameDate(frame.clock.date, previous_.clock.date);
  const bool configChanged = frame.configuration.revision != previous_.configuration.revision;
  const bool titleChanged = strcmp(frame.configuration.mosqueName, previous_.configuration.mosqueName) != 0 ||
      frame.configuration.calligraphyEnabled != previous_.configuration.calligraphyEnabled;
  const bool tickerChanged = strcmp(frame.configuration.runningText, previous_.configuration.runningText) != 0 ||
      frame.configuration.runningTextEnabled != previous_.configuration.runningTextEnabled ||
      frame.configuration.scrollIntervalMs != previous_.configuration.scrollIntervalMs;
  const bool secondChanged = frame.clock.hour != previous_.clock.hour ||
      frame.clock.minute != previous_.clock.minute || frame.clock.second != previous_.clock.second;
  bool scheduleChanged = frame.schedule.valid != previous_.schedule.valid ||
                         !sameDate(frame.schedule.date, previous_.schedule.date);
  for (int i = 0; i < jws::PrayerCount; ++i)
    scheduleChanged = scheduleChanged || frame.schedule.minutes[i] != previous_.schedule.minutes[i];

  if (!haveFrame_) {
    drawChrome();
  }

  const uint32_t curIp = (WiFi.status() == WL_CONNECTED) ? static_cast<uint32_t>(WiFi.localIP()) : 0U;
  const bool ipChanged = (curIp != lastLocalIp_);
  if (!haveFrame_ || titleChanged || ipChanged) {
    lastLocalIp_ = curIp;
    drawTitle(frame.configuration);
  }
  if (!haveFrame_ || validityChanged || secondChanged) drawClock(frame.clock);
  if (!haveFrame_ || validityChanged || dateChanged) drawDate(frame);
  if (!haveFrame_ || validityChanged || scheduleChanged || configChanged) drawSchedule(frame);
  if (!haveFrame_ || validityChanged || secondChanged || scheduleChanged || configChanged) drawCountdown(frame);
  drawTicker(frame.configuration, nowMs, !haveFrame_ || tickerChanged);
  if (!haveFrame_) {
    drawSlideDots(0);
  }
}

void Ili9341DisplayDriver::render(const jws::DisplayFrame& frame, uint32_t nowMs) {
  // 0. Touch handling, Backlight PWM, Task Manager Sampling & Background Ticks
  handleTouch(frame, nowMs);
  updateBacklight(frame, nowMs);
  jws::OnlineFeedService::instance().tick(nowMs);
  jws::MikrotikService::instance().tick(nowMs, activeSlide_ == 4 || activeSlide_ >= 12);
  jws::XiaozhiService::instance().tick(frame, nowMs, activeSlide_ == 5);
  if (jws::XiaozhiService::instance().consumeWakeRequest()) {
    if (activeSlide_ != 5) {
      activeSlide_ = 5;
      sceneInitDone_ = false;
    }
    lastSlideTouchMs_ = nowMs;
  }
  const int8_t voiceSlide = jws::XiaozhiService::instance().consumeSlideRequest();
  if (voiceSlide >= 0 && voiceSlide <= 7) {
    activeSlide_ = static_cast<uint8_t>(voiceSlide);
    sceneInitDone_ = false;
    haveFrame_ = false;
    lastSlideTouchMs_ = nowMs;
  }

  // Sample CPU, RAM, and PSRAM history every 1000 ms for the Task Manager slide
  ++frameTickCount_;
  if (lastTaskSampleMs_ == 0 || static_cast<uint32_t>(nowMs - lastTaskSampleMs_) >= 1000UL) {
    lastTaskSampleMs_ = nowMs;
    const auto xzSnap = jws::XiaozhiService::instance().snapshot();
    const auto csSnap = jws::ChronosService::instance().snapshot();
    uint32_t cpuEst = 14U;
    if (WiFi.status() == WL_CONNECTED) cpuEst += 6U;
    if (csSnap.connected) cpuEst += 4U;
    if (jws::NetLock::instance().isAiBusy()) cpuEst += 36U;
    if (xzSnap.state == jws::XiaozhiState::Listening) cpuEst += 26U;
    else if (xzSnap.state == jws::XiaozhiState::Thinking) cpuEst += 32U;
    cpuEst += (xzSnap.micLevel / 4U) + (esp_random() % 5U);
    if (cpuEst > 98U) cpuEst = 98U;

    const uint32_t totH = ESP.getHeapSize();
    const uint32_t freeH = ESP.getFreeHeap();
    const uint8_t ramEst = (totH > 0)
        ? static_cast<uint8_t>(((totH - freeH) * 100ULL) / totH)
        : 30U;

    const uint32_t totP = ESP.getPsramSize();
    const uint32_t freeP = ESP.getFreePsram();
    uint8_t psEst = (totP > 0)
        ? static_cast<uint8_t>(((totP - freeP) * 100ULL) / totP)
        : 2U;
    if (psEst < 2U && totP > 0) psEst = 2U;

    for (int i = 0; i < 19; ++i) {
      cpuHist_[i] = cpuHist_[i + 1];
      ramHist_[i] = ramHist_[i + 1];
      psramHist_[i] = psramHist_[i + 1];
    }
    cpuHist_[19] = static_cast<uint8_t>(cpuEst);
    ramHist_[19] = ramEst;
    psramHist_[19] = psEst;
    frameTickCount_ = 0;
  }

  // 1. Tentukan Scene yang Aktif
  ScreenScene targetScene = ScreenScene::Prayer;
  const char* activePrayerName = "";
  int remSec = 0;
  int totIqSec = 0;

  // A. Pemicu Paksa (Forced Scene dari tombol Web Dashboard)
  if (forcedScene_ >= 0) {
    if (nowMs < forcedSceneUntilMs_) {
      switch (forcedScene_) {
        case 1: targetScene = ScreenScene::Adzan; activePrayerName = "MAGHRIB"; remSec = 180; break;
        case 2: targetScene = ScreenScene::Iqomah; activePrayerName = "MAGHRIB"; remSec = 420; totIqSec = 420; break;
        case 3: targetScene = ScreenScene::Khotbah; break;
        case 4: targetScene = ScreenScene::Qr; break;
        case 5: targetScene = ScreenScene::Sholat; break;
        default: targetScene = ScreenScene::Prayer; break;
      }
    } else {
      forcedScene_ = -1;
      haveFrame_ = false;
    }
  }

  // B. Pemicu Otomatis Waktu Sholat (Real-Time State Machine)
  if (forcedScene_ < 0 && frame.clock.valid && frame.schedule.valid) {
    const int curSec = (frame.clock.hour * 60 + frame.clock.minute) * 60 + frame.clock.second;
    const jws::Prayer prayersToCheck[5] = {jws::Subuh, jws::Dzuhur, jws::Ashar, jws::Maghrib, jws::Isya};
    const int adzanSec = frame.configuration.adzanDurationMinutes * 60;

    for (int i = 0; i < 5; ++i) {
      const jws::Prayer p = prayersToCheck[i];
      const int pMin = frame.schedule.minutes[p];
      if (pMin < 0) continue;
      const int pStartSec = pMin * 60;
      const int iqSec = frame.configuration.iqomahMinutes[i] * 60;
      const int adzanEndSec = pStartSec + adzanSec;
      const int iqEndSec = adzanEndSec + iqSec;
      const int sholatEndSec = iqEndSec + 12 * 60; // 12 menit hening sholat

      if (curSec >= pStartSec && curSec < adzanEndSec) {
        targetScene = ScreenScene::Adzan;
        activePrayerName = jws::prayerName(p);
        remSec = adzanEndSec - curSec;
        break;
      } else if (curSec >= adzanEndSec && curSec < iqEndSec) {
        targetScene = ScreenScene::Iqomah;
        activePrayerName = jws::prayerName(p);
        remSec = iqEndSec - curSec;
        totIqSec = iqSec;
        break;
      } else if (curSec >= iqEndSec && curSec < sholatEndSec) {
        targetScene = ScreenScene::Sholat;
        break;
      }
    }

    // C. Jika di luar waktu sholat, tampilkan QR Infaq sesekali bila diaktifkan (tanpa slide petugas sholat)
    if (targetScene == ScreenScene::Prayer) {
      const uint32_t cycle = (nowMs / 1000) % 60;
      if (cycle >= 55 && cycle < 60 && frame.configuration.qrEnabled) {
        targetScene = ScreenScene::Qr;
      } else {
        targetScene = ScreenScene::Prayer;
      }
    }
  }

  // 2. Jika Scene berubah, reset inisialisasi scene baru
  if (targetScene != lastScene_) {
    lastScene_ = targetScene;
    sceneInitDone_ = false;
    lastRemSec_ = -1;
    haveFrame_ = false;
  }
  currentScene_ = targetScene;

  // 2b. Update Onboard Smart RGB Status LED (GPIO 48)
  updateStatusRgbLed(currentScene_, nowMs);

  // 2b-bis. Chronos BLE Live Incoming Call / Notification Pop-up Overlay
  if (drawChronosAlertOverlay(nowMs)) {
    return;
  }

  // 2c. Jika tidak sedang ada event sholat/forced dan user di slide lain -> render slide
  if (targetScene == ScreenScene::Prayer && activeSlide_ > 0) {
    if (activeSlide_ != lastRenderedSlide_) {
      lastRenderedSlide_ = activeSlide_;
      sceneInitDone_ = false;
      haveFrame_ = false;
    }
    switch (activeSlide_) {
      case 1:  drawSlideWeather(frame);                 break;
      case 2:  drawSlideStocks(frame);                  break;
      case 3:  drawSlideNews(frame);                    break;
      case 4:  drawSlideMikrotik(frame, nowMs);         break;
      case 5:  drawSlideXiaozhi(frame, nowMs);          break;
      case 6:  drawSlideFocusSystem(frame, nowMs);      break;
      case 7:  drawSlideRelayControl(frame, nowMs);     break;
      case 10: drawSlideStockDetail(frame);             break;
      case 11: drawSlideNewsDetail(frame);              break;
      case 12: drawSlideMikrotikPortDetail(frame, nowMs); break;
      case 13: drawSlideMikrotikPppoe(frame, nowMs);    break;
      case 14: drawSlideMikrotikHotspot(frame, nowMs);  break;
      default: break;
    }
    previous_ = frame;
    haveFrame_ = true;
    return;
  }
  if (lastRenderedSlide_ != 0) {
    lastRenderedSlide_ = 0;
    sceneInitDone_ = false;
    haveFrame_ = false;
  }

  // 3. Render Scene yang dipilih
  switch (currentScene_) {
    case ScreenScene::Adzan:
      drawSceneAdzan(frame, activePrayerName, remSec);
      break;
    case ScreenScene::Iqomah:
      drawSceneIqomah(frame, activePrayerName, remSec, totIqSec);
      break;
    case ScreenScene::Khotbah:
      drawSceneKhotbah(frame);
      break;
    case ScreenScene::Qr:
      drawSceneQr(frame);
      break;
    case ScreenScene::Sholat:
      drawSceneSholat(frame);
      break;
    case ScreenScene::Prayer:
    default:
      drawScenePrayer(frame, nowMs);
      break;
  }

  previous_ = frame;
  haveFrame_ = true;
}

void NtpClockProvider::begin(const jws::Configuration& config) {
  offsetMinutes_.store(config.calculation.utcOffsetMinutes, std::memory_order_relaxed);
  syncEvent.store(false, std::memory_order_relaxed);
  WiFi.persistent(false);
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP("JWS-KOTAMOBAGU", "12345678");
  WiFi.setAutoReconnect(true);
#if defined(JWS_SIMULATION) && !defined(JWS_DESKTOP_S3)
  WiFi.begin("Wokwi-GUEST", "", 6);
#endif
  sntp_set_time_sync_notification_cb(onTimeSync);
  configTime(0, 0, "pool.ntp.org", "time.google.com");
  Serial.printf("[CLOCK] waiting for NTP; UTC offset=%d minutes; AP IP=%s\n",
                offsetMinutes_.load(std::memory_order_relaxed), WiFi.softAPIP().toString().c_str());
}

void NtpClockProvider::tick(uint32_t nowMs) {
  if (syncEvent.exchange(false, std::memory_order_acq_rel) ||
      (!synchronized_.load(std::memory_order_acquire) && sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED)) {
    synchronized_.store(true, std::memory_order_release);
    lastSyncMs_.store(nowMs, std::memory_order_release);
    Serial.println("[CLOCK] NTP synchronized");
  }
  if (WiFi.status() != WL_CONNECTED && static_cast<uint32_t>(nowMs - lastRetryMs_) >= 15000) {
    lastRetryMs_ = nowMs;
#if defined(JWS_SIMULATION) && !defined(JWS_DESKTOP_S3)
    WiFi.reconnect();
#endif
  }
}

void NtpClockProvider::setEpochTime(time_t epochSeconds) {
  // epochSeconds dari JS (Date.now()/1000) adalah Unix epoch UTC murni
  struct timeval tv = { epochSeconds, 0 };
  settimeofday(&tv, nullptr);
  synchronized_.store(true, std::memory_order_release);
  lastSyncMs_.store(millis(), std::memory_order_release);
  Serial.printf("[CLOCK] Waktu berhasil disinkronkan manual/HP: epoch=%ld\n", static_cast<long>(epochSeconds));
}

jws::ClockReading NtpClockProvider::now() const {
  jws::ClockReading result;
  if (!synchronized_.load(std::memory_order_acquire)) return result;
  const time_t epoch = time(nullptr) + offsetMinutes_.load(std::memory_order_relaxed) * 60;
  struct tm local{};
  gmtime_r(&epoch, &local);
  result.date = {local.tm_year + 1900, local.tm_mon + 1, local.tm_mday};
  result.hour = local.tm_hour;
  result.minute = local.tm_min;
  result.second = local.tm_sec;
  const bool fresh = static_cast<uint32_t>(millis() - lastSyncMs_.load(std::memory_order_acquire)) < 7200000;
  result.source = WiFi.status() == WL_CONNECTED && fresh ? jws::ClockSource::Ntp : jws::ClockSource::Holdover;
  result.valid = jws::validDate(result.date);
  return result;
}
