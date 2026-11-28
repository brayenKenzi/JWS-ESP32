// SPDX-License-Identifier: GPL-3.0-only
#include "AudioService.h"
#include "NetLock.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <driver/i2s.h>
#include <esp_heap_caps.h>
#include <math.h>
#include <string.h>

#ifdef JWS_DESKTOP_S3
#define MINIMP3_IMPLEMENTATION
#define MINIMP3_NO_SIMD
#include "minimp3.h"
#endif

namespace jws {

namespace {
constexpr i2s_port_t SpkI2sPort = I2S_NUM_1;
constexpr int SpkBclkPin = 40;
constexpr int SpkLrcPin  = 41;
constexpr int SpkDinPin  = 42;
constexpr uint32_t DefaultSampleRate = 24000;

void copyStrSafe(char* dst, size_t cap, const char* src) {
  if (!dst || cap == 0) return;
  if (!src) {
    dst[0] = '\0';
    return;
  }
  strncpy(dst, src, cap - 1);
  dst[cap - 1] = '\0';
}

#ifdef JWS_DESKTOP_S3
mp3dec_t gMp3Dec;
int16_t gPcmFrame[MINIMP3_MAX_SAMPLES_PER_FRAME];
#endif

String urlEncodeUtf8(const char* s) {
  String out;
  out.reserve(strlen(s) * 2);
  for (size_t i = 0; s[i] != '\0'; ++i) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
        (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.') {
      out += static_cast<char>(c);
    } else if (c == ' ') {
      out += "%20";
    } else {
      char hex[4];
      snprintf(hex, sizeof(hex), "%%%02X", c);
      out += hex;
    }
  }
  return out;
}
}  // namespace

AudioService& AudioService::instance() {
  static AudioService svc;
  return svc;
}

void AudioService::begin() {
  if (initialized_) return;
  initialized_ = true;

  prefs_.begin("jws_audio", false);
  uint8_t savedVol = prefs_.getUChar("vol", 24);
  if (savedVol > 30) savedVol = 24;
  volume_.store(savedVol, std::memory_order_relaxed);
  enabled_.store(prefs_.getBool("en", true), std::memory_order_relaxed);

#ifdef JWS_DESKTOP_S3
  i2s_config_t cfg = {};
  cfg.mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_TX);
  cfg.sample_rate = DefaultSampleRate;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  cfg.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
  cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  cfg.dma_buf_count = 8;
  cfg.dma_buf_len = 256;
  cfg.use_apll = false;
  cfg.tx_desc_auto_clear = true;

  i2s_pin_config_t pins = {};
  pins.bck_io_num = SpkBclkPin;
  pins.ws_io_num = SpkLrcPin;
  pins.data_out_num = SpkDinPin;
  pins.data_in_num = I2S_PIN_NO_CHANGE;

  if (i2s_driver_install(SpkI2sPort, &cfg, 0, nullptr) == ESP_OK) {
    if (i2s_set_pin(SpkI2sPort, &pins) == ESP_OK) {
      i2s_zero_dma_buffer(SpkI2sPort);
      i2sReady_ = true;
    }
  }

  mp3BufCap_ = 98304U; // 96 KB in 8MB PSRAM for TTS MP3 stream
  mp3Buf_ = static_cast<uint8_t*>(heap_caps_malloc(mp3BufCap_, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!mp3Buf_) {
    mp3BufCap_ = 24576U;
    mp3Buf_ = static_cast<uint8_t*>(malloc(mp3BufCap_));
  }

  if (i2sReady_) {
    xTaskCreatePinnedToCore(taskEntry, "audio_bg", 10240, this, 1, &taskHandle_, 0);
    playChime(AudioChime::BootWelcome);
    Serial.printf("[AUDIO] MAX98357A I2S1 READY: BCLK=GPIO%d LRC=GPIO%d DIN=GPIO%d | Vol=%u/30 |Buf=%uKB\n",
                  SpkBclkPin, SpkLrcPin, SpkDinPin, savedVol, static_cast<unsigned>(mp3BufCap_ / 1024U));
  } else {
    Serial.println("[AUDIO] ERROR: Failed to init I2S1 for MAX98357A");
  }
#endif
}

void AudioService::setVolume(uint8_t vol0to30) {
  if (vol0to30 > 30) vol0to30 = 30;
  volume_.store(vol0to30, std::memory_order_release);
  if (initialized_) {
    prefs_.putUChar("vol", vol0to30);
  }
}

uint8_t AudioService::getVolume() const {
  return volume_.load(std::memory_order_acquire);
}

void AudioService::setEnabled(bool en) {
  enabled_.store(en, std::memory_order_release);
  if (initialized_) {
    prefs_.putBool("en", en);
  }
  if (!en) {
    stop();
  }
}

bool AudioService::isEnabled() const {
  return enabled_.load(std::memory_order_acquire);
}

void AudioService::playChime(AudioChime chime) {
  if (!enabled_.load(std::memory_order_acquire) || volume_.load(std::memory_order_acquire) == 0) return;
  pendingChime_.store(static_cast<uint8_t>(chime), std::memory_order_release);
}

void AudioService::speakText(const char* text) {
  if (!text || text[0] == '\0') return;
  if (!enabled_.load(std::memory_order_acquire) || volume_.load(std::memory_order_acquire) == 0) return;

  portENTER_CRITICAL(&mux_);
  copyStrSafe(ttsQueue_, sizeof(ttsQueue_), text);
  copyStrSafe(lastSpoken_, sizeof(lastSpoken_), text);
  portEXIT_CRITICAL(&mux_);

  stopReq_.store(true, std::memory_order_release);
  pendingTts_.store(true, std::memory_order_release);
}

void AudioService::stop() {
  stopReq_.store(true, std::memory_order_release);
  pendingChime_.store(0, std::memory_order_release);
  pendingTts_.store(false, std::memory_order_release);
}

AudioStatus AudioService::status() {
  AudioStatus st;
  st.hardwareReady = i2sReady_;
  st.enabled = enabled_.load(std::memory_order_acquire);
  st.playing = playing_.load(std::memory_order_acquire);
  st.volume = volume_.load(std::memory_order_acquire);
  portENTER_CRITICAL(&mux_);
  copyStrSafe(st.lastSpoken, sizeof(st.lastSpoken), lastSpoken_);
  portEXIT_CRITICAL(&mux_);
  return st;
}

void AudioService::taskEntry(void* arg) {
  static_cast<AudioService*>(arg)->taskLoop();
}

void AudioService::taskLoop() {
  for (;;) {
    const uint8_t chimeVal = pendingChime_.exchange(0, std::memory_order_acq_rel);
    if (chimeVal != 0) {
      stopReq_.store(false, std::memory_order_release);
      playing_.store(true, std::memory_order_release);
      renderChime(static_cast<AudioChime>(chimeVal));
#ifdef JWS_DESKTOP_S3
      i2s_zero_dma_buffer(SpkI2sPort);
#endif
      playing_.store(false, std::memory_order_release);
    }

    if (pendingTts_.exchange(false, std::memory_order_acq_rel)) {
      char localText[220];
      portENTER_CRITICAL(&mux_);
      copyStrSafe(localText, sizeof(localText), ttsQueue_);
      portEXIT_CRITICAL(&mux_);

      stopReq_.store(false, std::memory_order_release);
      playing_.store(true, std::memory_order_release);
      renderTts(localText);
#ifdef JWS_DESKTOP_S3
      i2s_zero_dma_buffer(SpkI2sPort);
#endif
      playing_.store(false, std::memory_order_release);
    }

    vTaskDelay(pdMS_TO_TICKS(30));
  }
}

void AudioService::renderTone(uint16_t freqHz, uint16_t durationMs, uint16_t pauseMs) {
#ifdef JWS_DESKTOP_S3
  if (!i2sReady_ || stopReq_.load(std::memory_order_acquire)) return;
  const uint8_t vol = volume_.load(std::memory_order_acquire);
  if (vol == 0 || freqHz == 0) {
    if (durationMs > 0) vTaskDelay(pdMS_TO_TICKS(durationMs));
    return;
  }

  i2s_set_clk(SpkI2sPort, DefaultSampleRate, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_MONO);

  const uint32_t totalSamples = (DefaultSampleRate * durationMs) / 1000U;
  const uint32_t fadeSamples = (totalSamples > 480U) ? 240U : (totalSamples / 4U);
  const float maxAmp = (static_cast<float>(vol) / 30.0f) * 14000.0f;
  const float omega = (2.0f * 3.14159265f * static_cast<float>(freqHz)) / static_cast<float>(DefaultSampleRate);

  int16_t chunk[256];
  uint32_t pos = 0;
  while (pos < totalSamples && !stopReq_.load(std::memory_order_acquire)) {
    const uint32_t n = ((totalSamples - pos) > 256U) ? 256U : (totalSamples - pos);
    for (uint32_t i = 0; i < n; ++i) {
      const uint32_t sIdx = pos + i;
      float env = 1.0f;
      if (fadeSamples > 0) {
        if (sIdx < fadeSamples) {
          env = static_cast<float>(sIdx) / static_cast<float>(fadeSamples);
        } else if (sIdx + fadeSamples > totalSamples) {
          env = static_cast<float>(totalSamples - sIdx) / static_cast<float>(fadeSamples);
        }
      }
      // Rich warm sine + subtle 2nd harmonic
      const float phase = omega * static_cast<float>(sIdx);
      const float wave = 0.82f * sinf(phase) + 0.18f * sinf(phase * 2.0f);
      chunk[i] = static_cast<int16_t>(wave * maxAmp * env);
    }
    size_t written = 0;
    i2s_write(SpkI2sPort, chunk, n * sizeof(int16_t), &written, pdMS_TO_TICKS(100));
    pos += n;
  }
  if (pauseMs > 0 && !stopReq_.load(std::memory_order_acquire)) {
    i2s_zero_dma_buffer(SpkI2sPort);
    vTaskDelay(pdMS_TO_TICKS(pauseMs));
  }
#else
  (void)freqHz;
  (void)durationMs;
  (void)pauseMs;
#endif
}

void AudioService::renderChime(AudioChime chime) {
  switch (chime) {
    case AudioChime::BootWelcome:
      // Warm Islamic Desk Gadget startup arpeggio (C5 - E5 - G5 - C6)
      renderTone(523, 110, 20);
      renderTone(659, 110, 20);
      renderTone(784, 110, 20);
      renderTone(1047, 240, 30);
      break;
    case AudioChime::AdzanAlert:
      // Majestic Masjid Chime before Adzan speech
      renderTone(392, 180, 30);
      renderTone(523, 220, 30);
      renderTone(659, 220, 30);
      renderTone(784, 380, 80);
      renderTone(659, 200, 30);
      renderTone(784, 520, 50);
      break;
    case AudioChime::IqomahTick:
      renderTone(880, 100, 10);
      break;
    case AudioChime::IqomahFinal:
      renderTone(1047, 680, 30);
      break;
    case AudioChime::ChronosNotif:
      renderTone(659, 110, 25);
      renderTone(880, 180, 20);
      break;
    case AudioChime::ChronosRing:
      renderTone(784, 140, 25);
      renderTone(988, 140, 25);
      renderTone(1175, 220, 40);
      break;
    case AudioChime::TouchClick:
      renderTone(1200, 25, 5);
      break;
    default:
      break;
  }
}

void AudioService::renderTts(const char* rawText) {
#ifdef JWS_DESKTOP_S3
  if (!i2sReady_ || !mp3Buf_ || !rawText || rawText[0] == '\0') return;
  if (WiFi.status() != WL_CONNECTED) {
    renderChime(AudioChime::ChronosNotif);
    return;
  }

  // Clean and bound text to <= 175 chars so Google Translate TTS responds in 1 fast MP3 stream
  char clean[180];
  size_t cPos = 0;
  for (size_t i = 0; rawText[i] != '\0' && cPos + 1 < sizeof(clean); ++i) {
    char ch = rawText[i];
    if (ch == '\n' || ch == '\r' || ch == '\t' || ch == '*' || ch == '"') ch = ' ';
    if (static_cast<unsigned char>(ch) >= 32 && static_cast<unsigned char>(ch) < 127) {
      clean[cPos++] = ch;
    }
  }
  clean[cPos] = '\0';
  if (cPos == 0) return;

  String encoded = urlEncodeUtf8(clean);
  String url = String("http://translate.google.com/translate_tts?ie=UTF-8&tl=id&client=tw-ob&q=") + encoded;

  WiFiClient client;
  HTTPClient http;
  size_t totalBytes = 0;
  http.setUserAgent("Mozilla/5.0 (Windows NT 10.0; Win64; x64)");
  http.setTimeout(6000);
  if (http.begin(client, url)) {
    const int code = http.GET();
    if (code == HTTP_CODE_OK) {
      WiFiClient* stream = http.getStreamPtr();
      uint32_t startWait = millis();
      while ((http.connected() || stream->available()) &&
             totalBytes < mp3BufCap_ &&
             (millis() - startWait) < 6500UL &&
             !stopReq_.load(std::memory_order_acquire)) {
        const size_t avail = stream->available();
        if (avail > 0) {
          const size_t toRead = ((totalBytes + avail) <= mp3BufCap_) ? avail : (mp3BufCap_ - totalBytes);
          const int r = stream->readBytes(mp3Buf_ + totalBytes, toRead);
          if (r > 0) {
            totalBytes += static_cast<size_t>(r);
            startWait = millis();
          }
        } else {
          vTaskDelay(pdMS_TO_TICKS(10));
        }
      }
    } else {
      Serial.printf("[AUDIO-TTS] HTTP error=%d\n", code);
    }
    http.end();
  }

  if (totalBytes < 128 || stopReq_.load(std::memory_order_acquire)) {
    return;
  }

  mp3dec_init(&gMp3Dec);
  uint32_t currentRate = 0;
  size_t offset = 0;
  const float gain = (static_cast<float>(volume_.load(std::memory_order_acquire)) / 30.0f) * 1.45f;

  while (offset + 4 < totalBytes && !stopReq_.load(std::memory_order_acquire)) {
    mp3dec_frame_info_t info = {};
    const int samples = mp3dec_decode_frame(&gMp3Dec, mp3Buf_ + offset,
                                            static_cast<int>(totalBytes - offset),
                                            gPcmFrame, &info);
    if (info.frame_bytes <= 0) break;
    offset += static_cast<size_t>(info.frame_bytes);

    if (samples > 0 && info.hz > 0) {
      if (static_cast<uint32_t>(info.hz) != currentRate) {
        currentRate = static_cast<uint32_t>(info.hz);
        i2s_set_clk(SpkI2sPort, currentRate, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_MONO);
      }
      // Convert to mono if 2 channels & apply digital volume scaling with soft clipping
      const int ch = (info.channels == 2) ? 2 : 1;
      for (int i = 0; i < samples; ++i) {
        int32_t v = gPcmFrame[i * ch];
        if (ch == 2) {
          v = (v + gPcmFrame[i * 2 + 1]) / 2;
        }
        v = static_cast<int32_t>(static_cast<float>(v) * gain);
        if (v > 32700) v = 32700;
        if (v < -32700) v = -32700;
        gPcmFrame[i] = static_cast<int16_t>(v);
      }
      size_t written = 0;
      i2s_write(SpkI2sPort, gPcmFrame, static_cast<size_t>(samples) * sizeof(int16_t),
                &written, pdMS_TO_TICKS(200));
    }
  }
  Serial.printf("[AUDIO-TTS] Played %u bytes MP3 (%u Hz) -> '%s'\n",
                static_cast<unsigned>(totalBytes), static_cast<unsigned>(currentRate), clean);
#else
  (void)rawText;
#endif
}

}  // namespace jws
