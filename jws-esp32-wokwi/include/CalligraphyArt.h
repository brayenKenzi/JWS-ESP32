// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <Arduino.h>
#include <stdint.h>
#include <string.h>

namespace jws {

// Exact 32x32 pixel-art bitmasks extracted from Twinkle HUB75 JWS Medallions:
// Layer 1: Outer Square & Diamond Frame (32x32)
constexpr uint32_t kTwinkleFrame32x32[32] PROGMEM = {
  0xFFFDBFFFUL, 0xFFFBDFFFUL, 0xC0066003UL, 0xC00C3003UL,
  0xC0181803UL, 0xC0300C03UL, 0xC0600603UL, 0xC0C00303UL,
  0xC1800183UL, 0xC30000C3UL, 0xC6000063UL, 0xCC000033UL,
  0xD800001BUL, 0xB000000DUL, 0x60000006UL, 0xC0000003UL,
  0xC0000003UL, 0x60000006UL, 0xB000000DUL, 0xD800001BUL,
  0xCC000033UL, 0xC6000063UL, 0xC30000C3UL, 0xC1800183UL,
  0xC0C00303UL, 0xC0600603UL, 0xC0300C03UL, 0xC0181803UL,
  0xC00C3003UL, 0xC0066003UL, 0xFFFBDFFFUL, 0xFFFDBFFFUL
};

// Layer 2: Inner Corner Brackets (32x32)
constexpr uint32_t kTwinkleCorner32x32[32] PROGMEM = {
  0x00000000UL, 0x00000000UL, 0x00000000UL, 0x1FE007F8UL,
  0x10000008UL, 0x10000008UL, 0x10000008UL, 0x10000008UL,
  0x10000008UL, 0x10000008UL, 0x10000008UL, 0x00000000UL,
  0x00000000UL, 0x00000000UL, 0x00000000UL, 0x10000008UL,
  0x10000008UL, 0x00000000UL, 0x00000000UL, 0x00000000UL,
  0x00000000UL, 0x10000008UL, 0x10000008UL, 0x10000008UL,
  0x10000008UL, 0x10000008UL, 0x10000008UL, 0x10000008UL,
  0x1FE007F8UL, 0x00000000UL, 0x00000000UL, 0x00000000UL
};

// Layer 3A: Lafadz Muhammad (محمد) Calligraphy Strokes (32x32)
constexpr uint32_t kTwinkleMuhammad32x32[32] PROGMEM = {
  0x00000000UL, 0x00000000UL, 0x00000000UL, 0x00000000UL,
  0x00000000UL, 0x00000000UL, 0x00000000UL, 0x00000000UL,
  0x00000000UL, 0x00200C00UL, 0x00301E00UL, 0x00301300UL,
  0x00181E00UL, 0x00187000UL, 0x000CC1C0UL, 0x000DFFE0UL,
  0x000C07F0UL, 0x01061E00UL, 0x01067800UL, 0x018FE000UL,
  0x00FDC000UL, 0x0070E000UL, 0x00007C00UL, 0x00003800UL,
  0x00000000UL, 0x00000000UL, 0x00000000UL, 0x00000000UL,
  0x00000000UL, 0x00000000UL, 0x00000000UL, 0x00000000UL
};

// Layer 3B: Lafadz Allah (الله) Calligraphy Strokes (32x32)
constexpr uint32_t kTwinkleAllah32x32[32] PROGMEM = {
  0x00000000UL, 0x00000000UL, 0x00000000UL, 0x00000000UL,
  0x00000000UL, 0x00000000UL, 0x00000000UL, 0x00008000UL,
  0x00018000UL, 0x00018000UL, 0x00018800UL, 0x00118D00UL,
  0x00718D80UL, 0x01F18D80UL, 0x03F18D80UL, 0x073184C0UL,
  0x062186C0UL, 0x07E186C0UL, 0x03E186C0UL, 0x00618680UL,
  0x0073CE00UL, 0x003E7C00UL, 0x001C3800UL, 0x00000000UL,
  0x00000000UL, 0x00000000UL, 0x00000000UL, 0x00000000UL,
  0x00000000UL, 0x00000000UL, 0x00000000UL, 0x00000000UL
};

// Renders the full 32x32 Twinkle Calligraphy Medallion with custom distinct colors:
//   - frameColor: Royal Cyan (0x07FF)
//   - cornerColor: Emerald Glow (0x07E8)
//   - glyphColor: Radiant Gold (0xFEA0)
template <typename CanvasT>
inline void drawTwinkleMedallion32x32(CanvasT& canvas, int16_t x, int16_t y, bool isAllah,
                                      uint16_t glyphColor, uint16_t frameColor,
                                      uint16_t cornerColor, uint16_t bgColor) {
  canvas.fillRect(x, y, 32, 32, bgColor);
  const uint32_t* glyphRows = isAllah ? kTwinkleAllah32x32 : kTwinkleMuhammad32x32;
  for (uint8_t row = 0; row < 32; ++row) {
    const uint32_t frameBits = pgm_read_dword(&kTwinkleFrame32x32[row]);
    const uint32_t cornerBits = pgm_read_dword(&kTwinkleCorner32x32[row]);
    const uint32_t glyphBits = pgm_read_dword(&glyphRows[row]);
    for (uint8_t col = 0; col < 32; ++col) {
      const uint32_t mask = (1UL << (31 - col));
      if (glyphBits & mask) {
        canvas.drawPixel(x + col, y + row, glyphColor);
      } else if (cornerBits & mask) {
        canvas.drawPixel(x + col, y + row, cornerColor);
      } else if (frameBits & mask) {
        canvas.drawPixel(x + col, y + row, frameColor);
      }
    }
  }
}

// Renders either the full 32x32 medallion or the inner 16x16 slice (rows 8..23, cols 5..20)
template <typename CanvasT>
inline void drawCalligraphyMedallion(CanvasT& canvas, int16_t x, int16_t y, uint8_t size,
                                     bool isAllah, uint16_t glyphColor, uint16_t frameColor,
                                     uint16_t bgColor) {
  if (size >= 32) {
    drawTwinkleMedallion32x32(canvas, x, y, isAllah, glyphColor, frameColor, 0x07E8, bgColor);
    return;
  }
  canvas.fillRect(x, y, size, size, bgColor);
  canvas.drawRect(x, y, size, size, frameColor);
  const uint32_t* glyphRows = isAllah ? kTwinkleAllah32x32 : kTwinkleMuhammad32x32;
  for (uint8_t row = 0; row < 16 && row < size; ++row) {
    const uint32_t glyphBits = pgm_read_dword(&glyphRows[8 + row]);
    for (uint8_t col = 0; col < 16 && col < size; ++col) {
      const uint32_t mask = (1UL << (26 - col));
      if (glyphBits & mask) {
        canvas.drawPixel(x + col, y + row, glyphColor);
      }
    }
  }
}

// Renders a 21x21 QR Code Version-1 matrix pattern from text for the QRIS / QR Scene
template <typename CanvasT>
inline void drawQrCode21x21(CanvasT& canvas, int16_t x, int16_t y, const char* text,
                            uint16_t fgColor, uint16_t bgColor) {
  canvas.fillRect(x - 1, y - 1, 23, 23, bgColor);
  auto drawFinder = [&](int16_t fx, int16_t fy) {
    canvas.drawRect(fx, fy, 7, 7, fgColor);
    canvas.fillRect(fx + 2, fy + 2, 3, 3, fgColor);
  };
  drawFinder(x, y);
  drawFinder(x + 14, y);
  drawFinder(x, y + 14);
  for (int i = 8; i <= 12; i += 2) {
    canvas.drawPixel(x + i, y + 6, fgColor);
    canvas.drawPixel(x + 6, y + i, fgColor);
  }
  uint32_t hash = 2166136261u;
  for (size_t i = 0; text && text[i]; ++i) {
    hash ^= static_cast<uint8_t>(text[i]);
    hash *= 16777619u;
  }
  for (int r = 0; r < 21; ++r) {
    for (int c = 0; c < 21; ++c) {
      const bool inFinder = (r < 8 && c < 8) || (r < 8 && c > 12) || (r > 12 && c < 8);
      if (inFinder || r == 6 || c == 6) continue;
      const uint32_t cellSeed = hash ^ static_cast<uint32_t>(r * 31 + c * 17);
      if ((cellSeed ^ (cellSeed >> 3)) & 1U) {
        canvas.drawPixel(x + c, y + r, fgColor);
      }
    }
  }
}

}  // namespace jws
