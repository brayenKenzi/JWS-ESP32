// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <Arduino.h>
#include <SPI.h>

namespace jws {

enum class TouchGesture {
  None,
  Tap,
  SwipeLeft,
  SwipeRight
};

struct TouchEvent {
  TouchGesture gesture = TouchGesture::None;
  int16_t x = 0;
  int16_t y = 0;
};

class Xpt2046Touch {
 public:
  Xpt2046Touch(int csPin = 3, int irqPin = 1) : csPin_(csPin), irqPin_(irqPin) {}

  void begin() {
    if (csPin_ >= 0) {
      pinMode(csPin_, OUTPUT);
      digitalWrite(csPin_, HIGH);
    }
    if (irqPin_ >= 0) {
      pinMode(irqPin_, INPUT_PULLUP);
    }
  }

  TouchEvent processTouch(uint32_t nowMs) {
    TouchEvent ev;
    if (csPin_ < 0) return ev;

    // Batasi polling SPI touch maksimal tiap 25ms agar tidak membebani bus SPI layar
    if (nowMs - lastPollMs_ < 25) return ev;
    lastPollMs_ = nowMs;

    // Cek apakah IRQ pin aktif LOW (jika tersambung).
    // Tapi jika IRQ pin tidak tersambung/terbaca HIGH, kita tetap coba polling Z-pressure
    // sehingga touchscreen tetap jalan walau user lupa colok pin T_IRQ!
    bool irqLow = false;
    if (irqPin_ >= 0) {
      irqLow = (digitalRead(irqPin_) == LOW);
    }

    int16_t curX = 0, curY = 0;
    bool touched = readCoordinates(curX, curY);

    if (touched) {
      if (!isDown_) {
        // Touch pertama kali ditekan
        isDown_ = true;
        startX_ = curX;
        startY_ = curY;
        downMs_ = nowMs;
        Serial.printf("[TOUCH DOWN] X=%d Y=%d (irq=%d)\n", curX, curY, irqLow ? 0 : 1);
      }
      lastX_ = curX;
      lastY_ = curY;
      lastTouchMs_ = nowMs;
    } else {
      if (isDown_) {
        // Jari dilepas -> hitung gesture
        isDown_ = false;
        const int16_t dx = lastX_ - startX_;
        const int16_t dy = lastY_ - startY_;
        const uint32_t dur = nowMs - downMs_;

        Serial.printf("[TOUCH UP] Start=(%d,%d) End=(%d,%d) dx=%d dy=%d dur=%ums\n",
                      startX_, startY_, lastX_, lastY_, dx, dy, (unsigned)dur);

        if (dur < 1200) {
          // Swipe kiri: geser ke kiri (dx negatif signifikan)
          if (dx < -35 && abs(dx) > abs(dy)) {
            ev.gesture = TouchGesture::SwipeLeft;
            Serial.println("[TOUCH] >>> GESTURE SWIPE LEFT DETECTED");
          }
          // Swipe kanan: geser ke kanan (dx positif signifikan)
          else if (dx > 35 && abs(dx) > abs(dy)) {
            ev.gesture = TouchGesture::SwipeRight;
            Serial.println("[TOUCH] >>> GESTURE SWIPE RIGHT DETECTED");
          }
          // Tap: jika pergeseran kecil
          else if (abs(dx) < 30 && abs(dy) < 30) {
            ev.gesture = TouchGesture::Tap;
            ev.x = startX_;
            ev.y = startY_;
            Serial.printf("[TOUCH] >>> GESTURE TAP DETECTED at (%d, %d)\n", ev.x, ev.y);
          }
        }
      }
    }

    return ev;
  }

  uint32_t lastTouchMs() const { return lastTouchMs_; }

 private:
  int csPin_;
  int irqPin_;
  bool isDown_ = false;
  int16_t startX_ = 0;
  int16_t startY_ = 0;
  int16_t lastX_ = 0;
  int16_t lastY_ = 0;
  uint32_t downMs_ = 0;
  uint32_t lastTouchMs_ = 0;
  uint32_t lastPollMs_ = 0;

  bool readCoordinates(int16_t& outX, int16_t& outY) {
    // XPT2046 SPI Clock maksimal 2.5 MHz, Mode 0
    digitalWrite(csPin_, LOW);
    SPI.beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE0));

    // Protokol baku XPT2046 (Paul Stoffregen algorithm):
    // 1. Baca Z1 & Z2 pressure
    SPI.transfer(0xB1); // Perintah ukur Z1
    int16_t z1 = SPI.transfer16(0xC1) >> 3; // Baca Z1, kirim perintah Z2
    int z = z1 + 4095;
    int16_t z2 = SPI.transfer16(0x91) >> 3; // Baca Z2, kirim perintah X
    z -= z2;

    // Ambang batas tekanan (threshold)
    // Jika tidak ditekan, z bernilai sangat rendah atau negatif
    if (z < 350) {
      // Power down XPT2046 ADC
      SPI.transfer16(0xD0);
      SPI.transfer16(0);
      SPI.endTransaction();
      digitalWrite(csPin_, HIGH);
      return false;
    }

    // 2. Baca beberapa sampel koordinat X dan Y untuk meredam noise
    SPI.transfer16(0x91); // Dummy read untuk stabilisasi ADC
    int16_t rawY1 = SPI.transfer16(0xD1) >> 3;
    int16_t rawX1 = SPI.transfer16(0x91) >> 3;
    int16_t rawY2 = SPI.transfer16(0xD1) >> 3;
    int16_t rawX2 = SPI.transfer16(0x91) >> 3;

    // Power down touch controller di akhir pembacaan
    SPI.transfer16(0xD0);
    SPI.transfer16(0);

    SPI.endTransaction();
    digitalWrite(csPin_, HIGH);

    // Rata-ratakan 2 sampel
    int32_t rawX = (rawX1 + rawX2) / 2;
    int32_t rawY = (rawY1 + rawY2) / 2;

    // Validasi range ADC (0..4095)
    if (rawX < 150 || rawX > 3950 || rawY < 150 || rawY > 3950) {
      return false;
    }

    // Mapping koordinat ke resolusi 320 x 240 (Landscape Rotation 1):
    // Sumbu horizontal (X: 0..320) dari rawY (3750 di kiri -> 350 di kanan)
    // Sumbu vertikal (Y: 0..240) dari rawX (3750 di atas -> 350 di bawah)
    int32_t px = map(rawY, 3750, 350, 0, 320);
    int32_t py = map(rawX, 3750, 350, 0, 240);

    if (px < 0) px = 0;
    if (px > 319) px = 319;
    if (py < 0) py = 0;
    if (py > 239) py = 239;

    outX = static_cast<int16_t>(px);
    outY = static_cast<int16_t>(py);
    return true;
  }
};

}  // namespace jws
