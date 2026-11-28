// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <atomic>
#include <driver/gpio.h>
#include <driver/i2s.h>
#include <esp_heap_caps.h>
#include <math.h>
#include <mbedtls/base64.h>
#include <soc/gpio_reg.h>
#include <soc/io_mux_reg.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>
#include <Interfaces.h>
#include "OnlineFeedService.h"
#include "MikrotikService.h"
#include "NetLock.h"
#include "RelayControlService.h"
#include "ChronosService.h"
#include "AudioService.h"

namespace jws {

enum class XiaozhiState : uint8_t {
  Idle = 0,
  Listening = 1,
  Thinking = 2,
  Speaking = 3
};

struct XiaozhiSnapshot {
  XiaozhiState state = XiaozhiState::Idle;
  char agentName[28] = "UPIN AI";
  char agentPrompt[200] =
      "Kamu Upin (gaya ramah 'Betul betul betul!'), asisten pintar JWS & NOC MikroTik di Kotamobagu. Jawab singkat padat max 85 huruf.";
  char userQuery[72] = "Panggil 'Halo Upin' di dekat mic!";
  char replyLine1[52] = "Halo kawan! Aku Upin, asisten pintar JWS.";
  char replyLine2[52] = "Panggil 'Halo Upin' atau tap layar ya!";
  char replyLine3[52] = "";
  char nextPrayerBadge[28] = "JWS SIAP";
  char pinStatus[28] = "SCK:4 WS:5 SD:6";
  char apiBaseUrl[128] = "";
  char apiModel[64] = "";
  uint8_t bars[12] = {6, 10, 14, 18, 22, 16, 12, 18, 24, 16, 10, 6};
  uint8_t micLevel = 0;
  bool hasApiKey = false;
  bool micHardwareReady = false;
};

class XiaozhiService {
 public:
  static XiaozhiService& instance() {
    static XiaozhiService s;
    return s;
  }

  void begin() {
    prefs_.begin("jws_xz", false);
    if (!prefs_.getBool("gemini_off_v1", false)) {
      prefs_.putString("api_key", "");
      prefs_.putBool("gemini_off_v1", true);
    }
    aiTlsClient_.setInsecure();
    String key = prefs_.getString("api_key", "");
    String baseUrl = prefs_.getString("api_base", "");
    String modelId = prefs_.getString("api_model", "");
    String agName = prefs_.getString("agent_name", "");
    String agPrompt = prefs_.getString("agent_prompt", "");

    bool migrateDefaultName = false;
    if (agName == "Xiaozhi Islami AI") {
      agName = "UPIN AI";
      migrateDefaultName = true;
    }

    portENTER_CRITICAL(&xzMux_);
    copySafe(apiKey_, sizeof(apiKey_), key.c_str());
    copySafe(apiBaseUrl_, sizeof(apiBaseUrl_), baseUrl.c_str());
    copySafe(apiModel_, sizeof(apiModel_), modelId.c_str());
    copySafe(snap_.apiBaseUrl, sizeof(snap_.apiBaseUrl), apiBaseUrl_);
    copySafe(snap_.apiModel, sizeof(snap_.apiModel), apiModel_);
    snap_.hasApiKey = (apiKey_[0] != '\0');
    if (agName.length() > 0) {
      copySafe(snap_.agentName, sizeof(snap_.agentName), agName.c_str());
    }
    if (agPrompt.length() > 0) {
      copySafe(snap_.agentPrompt, sizeof(snap_.agentPrompt), agPrompt.c_str());
    }
    portEXIT_CRITICAL(&xzMux_);

    if (migrateDefaultName) {
      prefs_.putString("agent_name", "UPIN AI");
    }

    initMicrophone();

    if (!preRollBuf_) {
      preRollBuf_ = reinterpret_cast<int16_t*>(allocPsram(kPreRollSamples * sizeof(int16_t)));
    }
    if (!wavBuf_) {
      // Pre-allocate persistent audio buffer ONCE (96 KB @ 16kHz in 8MB OPI PSRAM,
      // fallback 48 KB @ 8kHz) so recording NEVER hits OOM or fragments SRAM!
      wavBufRate_ = 16000;
      wavBufMaxSamples_ = (wavBufRate_ * 30UL) / 10UL;
      wavBuf_ = allocPsram(44UL + wavBufMaxSamples_ * 2UL);
      if (!wavBuf_) {
        wavBufRate_ = 8000;
        wavBufMaxSamples_ = (wavBufRate_ * 30UL) / 10UL;
        wavBuf_ = allocPsram(44UL + wavBufMaxSamples_ * 2UL);
      }
      Serial.printf("[XIAOZHI] Audio buffer %s: %lu Hz (%lu B) | PSRAM free=%uKB\n",
                    wavBuf_ ? "READY" : "FAILED",
                    static_cast<unsigned long>(wavBufRate_),
                    static_cast<unsigned long>(wavBuf_ ? (44UL + wavBufMaxSamples_ * 2UL) : 0UL),
                    static_cast<unsigned>(ESP.getFreePsram() / 1024U));
    }

    if (taskHandle_ == nullptr) {
      xTaskCreatePinnedToCore(taskEntry, "xz_bg", 10240, this, 1, &taskHandle_, 0);
    }
  }

  void setApiKey(const char* key) {
    portENTER_CRITICAL(&xzMux_);
    copySafe(apiKey_, sizeof(apiKey_), key ? key : "");
    snap_.hasApiKey = (apiKey_[0] != '\0');
    portEXIT_CRITICAL(&xzMux_);
    prefs_.putString("api_key", apiKey_);
  }

  void setAgentConfig(const char* agentName,
                      const char* agentPrompt,
                      const char* key,
                      bool clearKey = false,
                      const char* baseUrl = nullptr,
                      const char* modelId = nullptr) {
    char savedName[28] = "";
    char savedPrompt[200] = "";
    char savedKey[128] = "";
    char savedBase[128] = "";
    char savedModel[64] = "";
    bool saveName = false;
    bool savePrompt = false;
    bool saveKey = false;
    bool saveBase = false;
    bool saveModel = false;

    portENTER_CRITICAL(&xzMux_);
    if (agentName && agentName[0] != '\0') {
      copySafe(snap_.agentName, sizeof(snap_.agentName), agentName);
      copySafe(savedName, sizeof(savedName), snap_.agentName);
      saveName = true;
    }
    if (agentPrompt && agentPrompt[0] != '\0') {
      copySafe(snap_.agentPrompt, sizeof(snap_.agentPrompt), agentPrompt);
      copySafe(savedPrompt, sizeof(savedPrompt), snap_.agentPrompt);
      savePrompt = true;
    }
    if (clearKey) {
      apiKey_[0] = '\0';
      apiBaseUrl_[0] = '\0';
      apiModel_[0] = '\0';
      snap_.apiBaseUrl[0] = '\0';
      snap_.apiModel[0] = '\0';
      snap_.hasApiKey = false;
      saveKey = true;
      saveBase = true;
      saveModel = true;
    } else {
      if (key && key[0] != '\0') {
        copySafe(apiKey_, sizeof(apiKey_), key);
        copySafe(savedKey, sizeof(savedKey), apiKey_);
        snap_.hasApiKey = true;
        saveKey = true;
      }
      if (baseUrl != nullptr) {
        copySafe(apiBaseUrl_, sizeof(apiBaseUrl_), baseUrl);
        copySafe(snap_.apiBaseUrl, sizeof(snap_.apiBaseUrl), apiBaseUrl_);
        copySafe(savedBase, sizeof(savedBase), apiBaseUrl_);
        saveBase = true;
      }
      if (modelId != nullptr) {
        copySafe(apiModel_, sizeof(apiModel_), modelId);
        copySafe(snap_.apiModel, sizeof(snap_.apiModel), apiModel_);
        copySafe(savedModel, sizeof(savedModel), apiModel_);
        saveModel = true;
      }
    }
    portEXIT_CRITICAL(&xzMux_);

    if (saveName) prefs_.putString("agent_name", savedName);
    if (savePrompt) prefs_.putString("agent_prompt", savedPrompt);
    if (saveKey) prefs_.putString("api_key", savedKey);
    if (saveBase) prefs_.putString("api_base", savedBase);
    if (saveModel) prefs_.putString("api_model", savedModel);
  }

  // Triggered from Web Dashboard or VAD to record 2s WAV audio into PSRAM -> Gemini Audio
  void triggerVoiceCapture() {
    portENTER_CRITICAL(&xzMux_);
    snap_.state = XiaozhiState::Listening;
    copySafe(snap_.userQuery, sizeof(snap_.userQuery), "Merekam suara dari INMP441...");
    copySafe(snap_.replyLine1, sizeof(snap_.replyLine1), "Silakan bicara sekarang (3 detik)...");
    copySafe(snap_.replyLine2, sizeof(snap_.replyLine2), "");
    copySafe(snap_.replyLine3, sizeof(snap_.replyLine3), "");
    stateChangedMs_ = millis();
    portEXIT_CRITICAL(&xzMux_);
    manualVoiceTrigger_.store(true, std::memory_order_release);
    wakeRequested_.store(true, std::memory_order_release);
    pendingVoiceRecord_.store(true, std::memory_order_release);
  }

  bool consumeWakeRequest() {
    return wakeRequested_.exchange(false, std::memory_order_acq_rel);
  }

  int8_t consumeSlideRequest() {
    return pendingSlideRequest_.exchange(-1, std::memory_order_acq_rel);
  }

  uint8_t consumePomodoroCmd() {
    return pendingPomodoroCmd_.exchange(0, std::memory_order_acq_rel);
  }

  // Non-blocking snapshot copy safe for Core 1 UI & WebController
  XiaozhiSnapshot snapshot() {
    portENTER_CRITICAL(&xzMux_);
    XiaozhiSnapshot copy = snap_;
    portEXIT_CRITICAL(&xzMux_);
    return copy;
  }

  // Called from Core 1 inside render() — strictly zero blocking I/O
  void tick(const DisplayFrame& frame, uint32_t nowMs, bool isViewingXiaozhi) {
    viewing_.store(isViewingXiaozhi, std::memory_order_relaxed);
    (void)nowMs;

    char badgeBuf[28] = "WITA";
    updatePrayerBadge(frame, badgeBuf, sizeof(badgeBuf));

    portENTER_CRITICAL(&xzMux_);
    latestFrame_ = frame;
    hasFrame_ = true;
    copySafe(snap_.nextPrayerBadge, sizeof(snap_.nextPrayerBadge), badgeBuf);
    portEXIT_CRITICAL(&xzMux_);
  }

  // Triggered when user taps the Xiaozhi face/waveform on TFT touchscreen
  void triggerNextTopic(const DisplayFrame& frame) {
    portENTER_CRITICAL(&xzMux_);
    latestFrame_ = frame;
    hasFrame_ = true;
    const uint8_t intent = nextIntent_;
    nextIntent_ = (nextIntent_ + 1) % 5;
    portEXIT_CRITICAL(&xzMux_);

    answerLocalIntent(intent, nullptr);
  }

  // Triggered from Web Dashboard POST /api/v1/xiaozhi
  void askQuestion(const char* question) {
    if (!question || question[0] == '\0') return;
    portENTER_CRITICAL(&xzMux_);
    copySafe(snap_.userQuery, sizeof(snap_.userQuery), question);
    copySafe(pendingCloudPrompt_, sizeof(pendingCloudPrompt_), question);
    const bool useCloud = (WiFi.status() == WL_CONNECTED);
    if (useCloud) {
      snap_.state = XiaozhiState::Thinking;
      copySafe(snap_.replyLine1, sizeof(snap_.replyLine1), "Menghubungkan ke AI Internet...");
      copySafe(snap_.replyLine2, sizeof(snap_.replyLine2), "Mohon tunggu sebentar.");
      stateChangedMs_ = millis();
      pendingCloudAsk_.store(true, std::memory_order_release);
    }
    portEXIT_CRITICAL(&xzMux_);
    wakeRequested_.store(true, std::memory_order_release);

    if (!useCloud) {
      const uint8_t intent = matchIntentByKeyword(question);
      answerLocalIntent(intent, question);
    }
  }

 private:
  static constexpr i2s_port_t MicI2sPort = I2S_NUM_0;
  static constexpr int MicSckPin = 4;
  static constexpr int MicWsPin = 5;
  static constexpr int MicSdPin = 6;

  Preferences prefs_;
  TaskHandle_t taskHandle_ = nullptr;
  portMUX_TYPE xzMux_ = portMUX_INITIALIZER_UNLOCKED;
  WiFiClientSecure aiTlsClient_;

  XiaozhiSnapshot snap_{};
  DisplayFrame latestFrame_{};
  bool hasFrame_ = false;
  char apiKey_[128] = "";
  char apiBaseUrl_[128] = "";
  char apiModel_[64] = "";
  char pendingCloudPrompt_[128] = "";

  std::atomic<bool> viewing_{false};
  std::atomic<bool> pendingCloudAsk_{false};
  std::atomic<bool> pendingVoiceRecord_{false};
  std::atomic<bool> wakeRequested_{false};
  std::atomic<int8_t> pendingSlideRequest_{-1};
  std::atomic<uint8_t> pendingPomodoroCmd_{0}; // 1=start 25m, 2=pause, 3=reset, 4=break 5m
  uint8_t* wavBuf_ = nullptr;
  uint32_t wavBufRate_ = 16000;
  uint32_t wavBufMaxSamples_ = 48000;
  uint8_t dzikirIdx_ = 0;
  bool micReady_ = false;
  uint8_t nextIntent_ = 0;
  int sckPin_ = 4;
  int wsPin_ = 5;
  int sdPin_ = 6;
  float noiseFloor_ = -1.0f;
  uint32_t lastDiagLogMs_ = 0;
  uint32_t zeroSignalCount_ = 0;

  static uint8_t* allocPsram(size_t sz) {
    void* ptr = heap_caps_malloc(sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!ptr) ptr = malloc(sz);
    return static_cast<uint8_t*>(ptr);
  }

  static void appendJsonEscaped(char* dst, size_t cap, size_t& pos, const char* src) {
    if (!src || !dst || pos + 2 >= cap) return;
    for (size_t i = 0; src[i] != '\0' && pos + 8 < cap; ++i) {
      const char ch = src[i];
      if (ch == '"' || ch == '\\') {
        dst[pos++] = '\\';
        dst[pos++] = ch;
      } else if (ch == '\n') {
        dst[pos++] = '\\';
        dst[pos++] = 'n';
      } else if (ch == '\r') {
        continue;
      } else if (static_cast<unsigned char>(ch) >= 0x20) {
        dst[pos++] = ch;
      }
    }
    dst[pos] = '\0';
  }

  static void writeWavHeader(uint8_t* hdr, uint32_t sampleRate, uint32_t pcmBytes) {
    const uint32_t riffSize = 36UL + pcmBytes;
    const uint32_t byteRate = sampleRate * 2UL;
    memcpy(hdr + 0, "RIFF", 4);
    memcpy(hdr + 4, &riffSize, 4);
    memcpy(hdr + 8, "WAVE", 4);
    memcpy(hdr + 12, "fmt ", 4);
    const uint32_t fmtSize = 16;
    const uint16_t audioFmt = 1;
    const uint16_t channels = 1;
    const uint16_t blockAlign = 2;
    const uint16_t bitsPerSample = 16;
    memcpy(hdr + 16, &fmtSize, 4);
    memcpy(hdr + 20, &audioFmt, 2);
    memcpy(hdr + 22, &channels, 2);
    memcpy(hdr + 24, &sampleRate, 4);
    memcpy(hdr + 28, &byteRate, 4);
    memcpy(hdr + 32, &blockAlign, 2);
    memcpy(hdr + 34, &bitsPerSample, 2);
    memcpy(hdr + 36, "data", 4);
    memcpy(hdr + 40, &pcmBytes, 4);
  }

  uint32_t stateChangedMs_ = 0;
  uint32_t voiceStartMs_ = 0;
  uint32_t silenceStartMs_ = 0;
  uint32_t phraseStartMs_ = 0;
  uint32_t lastVoiceFrameMs_ = 0;
  uint8_t voiceFrameCount_ = 0;
  bool voicePending_ = false;
  uint32_t bootWarmupUntilMs_ = 0;
  static constexpr size_t kPreRollSamples = 5600; // 0.35s @ 16kHz
  int16_t* preRollBuf_ = nullptr;
  size_t preRollWriteIdx_ = 0;
  size_t preRollFilled_ = 0;
  std::atomic<bool> manualVoiceTrigger_{false};
  uint32_t conversationOpenUntilMs_ = 0;
  bool wasMicSuppressed_ = false;

  static void copySafe(char* dst, size_t cap, const char* src) {
    if (!dst || cap == 0) return;
    if (!src) {
      dst[0] = '\0';
      return;
    }
    strncpy(dst, src, cap - 1);
    dst[cap - 1] = '\0';
  }

  static inline void spinTicks(uint32_t ticks) {
    const uint32_t start = ESP.getCycleCount();
    while (static_cast<uint32_t>(ESP.getCycleCount() - start) < ticks) {
    }
  }

  void runBitBangMicTest(int sck, int ws, int sd) {
#ifdef JWS_DESKTOP_S3
    gpio_reset_pin(static_cast<gpio_num_t>(sck));
    gpio_reset_pin(static_cast<gpio_num_t>(ws));
    gpio_reset_pin(static_cast<gpio_num_t>(sd));
    pinMode(sck, OUTPUT);
    pinMode(ws, OUTPUT);
    pinMode(sd, INPUT_PULLDOWN);
    gpio_set_level(static_cast<gpio_num_t>(sck), 0);
    gpio_set_level(static_cast<gpio_num_t>(ws), 0);

    uint32_t onesCount = 0;
    uint32_t edgeCount = 0;
    int prevLvl = -1;
    uint32_t bbLeft = 0;
    uint32_t bbRight = 0;

    for (int cyc = 0; cyc < 3000; ++cyc) {
      uint32_t lWord = 0;
      uint32_t rWord = 0;
      gpio_set_level(static_cast<gpio_num_t>(ws), 0);
      for (int b = 0; b < 32; ++b) {
        gpio_set_level(static_cast<gpio_num_t>(sck), 0);
        spinTicks(120);
        gpio_set_level(static_cast<gpio_num_t>(sck), 1);
        const int lvl = gpio_get_level(static_cast<gpio_num_t>(sd));
        if (cyc >= 2100) {
          if (lvl) ++onesCount;
          if (prevLvl >= 0 && lvl != prevLvl) ++edgeCount;
          prevLvl = lvl;
          lWord = (lWord << 1) | static_cast<uint32_t>(lvl & 1);
        }
        spinTicks(120);
      }
      gpio_set_level(static_cast<gpio_num_t>(ws), 1);
      for (int b = 0; b < 32; ++b) {
        gpio_set_level(static_cast<gpio_num_t>(sck), 0);
        spinTicks(120);
        gpio_set_level(static_cast<gpio_num_t>(sck), 1);
        const int lvl = gpio_get_level(static_cast<gpio_num_t>(sd));
        if (cyc >= 2100) {
          if (lvl) ++onesCount;
          if (prevLvl >= 0 && lvl != prevLvl) ++edgeCount;
          prevLvl = lvl;
          rWord = (rWord << 1) | static_cast<uint32_t>(lvl & 1);
        }
        spinTicks(120);
      }
      if (cyc == 2999) {
        bbLeft = lWord;
        bbRight = rWord;
      }
      if ((cyc & 0x3FF) == 0x3FF) {
        vTaskDelay(1);
      }
    }
    Serial.printf("[MIC-BITBANG] SCK=%d WS=%d SD=%d -> ones=%u edges=%u L=0x%08lX R=0x%08lX\n",
                  sck, ws, sd,
                  static_cast<unsigned>(onesCount),
                  static_cast<unsigned>(edgeCount),
                  static_cast<unsigned long>(bbLeft),
                  static_cast<unsigned long>(bbRight));
#else
    (void)sck;
    (void)ws;
    (void)sd;
#endif
  }

  bool installI2sWithPins(int sck, int ws, int sd, bool verifyClocks = false) {
#ifdef JWS_DESKTOP_S3
    gpio_reset_pin(static_cast<gpio_num_t>(sck));
    gpio_reset_pin(static_cast<gpio_num_t>(ws));
    gpio_reset_pin(static_cast<gpio_num_t>(sd));

    i2s_config_t cfg = {};
    cfg.mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_RX);
    cfg.sample_rate = 16000;
    cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;
    cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
    cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    cfg.dma_buf_count = 6;
    cfg.dma_buf_len = 128;
    cfg.use_apll = false;
    cfg.tx_desc_auto_clear = false;
    cfg.fixed_mclk = 0;
    cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    cfg.bits_per_chan = I2S_BITS_PER_CHAN_32BIT;
#if SOC_I2S_SUPPORTS_TDM
    cfg.chan_mask = static_cast<i2s_channel_t>(I2S_TDM_ACTIVE_CH0 | I2S_TDM_ACTIVE_CH1);
#endif

    i2s_pin_config_t pins = {};
    pins.mck_io_num = I2S_PIN_NO_CHANGE;
    pins.bck_io_num = sck;
    pins.ws_io_num = ws;
    pins.data_out_num = I2S_PIN_NO_CHANGE;
    pins.data_in_num = sd;

    if (i2s_driver_install(MicI2sPort, &cfg, 0, nullptr) != ESP_OK) {
      return false;
    }
    if (i2s_set_pin(MicI2sPort, &pins) != ESP_OK) {
      i2s_driver_uninstall(MicI2sPort);
      return false;
    }
    gpio_set_pull_mode(static_cast<gpio_num_t>(sd), GPIO_PULLDOWN_ONLY);
    i2s_zero_dma_buffer(MicI2sPort);
    i2s_start(MicI2sPort);

    if (verifyClocks) {
      PIN_INPUT_ENABLE(GPIO_PIN_MUX_REG[sck]);
      PIN_INPUT_ENABLE(GPIO_PIN_MUX_REG[ws]);
      uint32_t prevReg = REG_READ(GPIO_IN_REG);
      uint32_t sckEdges = 0;
      uint32_t wsEdges = 0;
      uint32_t sdEdges = 0;
      for (int i = 0; i < 500; ++i) {
        spinTicks(120);
        const uint32_t cur = REG_READ(GPIO_IN_REG);
        const uint32_t diff = cur ^ prevReg;
        if (diff & (1UL << sck)) ++sckEdges;
        if (diff & (1UL << ws)) ++wsEdges;
        if (diff & (1UL << sd)) ++sdEdges;
        prevReg = cur;
      }
      Serial.printf("[MIC-CLK-VERIFY] SCK(%d)_edges=%u WS(%d)_edges=%u SD(%d)_edges=%u\n",
                    sck, static_cast<unsigned>(sckEdges),
                    ws, static_cast<unsigned>(wsEdges),
                    sd, static_cast<unsigned>(sdEdges));
    }
    return true;
#else
    (void)sck;
    (void)ws;
    (void)sd;
    (void)verifyClocks;
    return false;
#endif
  }

  void probeCurrentI2sSignal(uint64_t& abs0Out, uint64_t& abs1Out, uint32_t& nonZeroOut,
                             int32_t& s0LastOut, int32_t& s1LastOut) {
#ifdef JWS_DESKTOP_S3
    int32_t probeBuf[256];
    vTaskDelay(pdMS_TO_TICKS(150));
    uint64_t abs0Sum = 0;
    uint64_t abs1Sum = 0;
    size_t measuredFrames = 0;
    nonZeroOut = 0;
    s0LastOut = 0;
    s1LastOut = 0;
    for (int chunk = 0; chunk < 4; ++chunk) {
      size_t bytesRead = 0;
      if (i2s_read(MicI2sPort, probeBuf, sizeof(probeBuf), &bytesRead, pdMS_TO_TICKS(40)) == ESP_OK &&
          bytesRead >= 16) {
        const size_t nFrames = (bytesRead / sizeof(int32_t)) / 2;
        if (nFrames >= 4 && chunk >= 2) {
          s0LastOut = probeBuf[0];
          s1LastOut = probeBuf[1];
          int64_t sum0 = 0, sum1 = 0;
          for (size_t f = 0; f < nFrames; ++f) {
            if (probeBuf[f * 2] != 0) ++nonZeroOut;
            if (probeBuf[f * 2 + 1] != 0) ++nonZeroOut;
            sum0 += (probeBuf[f * 2] >> 8);
            sum1 += (probeBuf[f * 2 + 1] >> 8);
          }
          const int32_t dc0 = static_cast<int32_t>(sum0 / static_cast<int64_t>(nFrames));
          const int32_t dc1 = static_cast<int32_t>(sum1 / static_cast<int64_t>(nFrames));
          for (size_t f = 0; f < nFrames; ++f) {
            int32_t v0 = (probeBuf[f * 2] >> 8) - dc0;
            int32_t v1 = (probeBuf[f * 2 + 1] >> 8) - dc1;
            abs0Sum += static_cast<uint64_t>(v0 < 0 ? -v0 : v0);
            abs1Sum += static_cast<uint64_t>(v1 < 0 ? -v1 : v1);
          }
          measuredFrames += nFrames;
        }
      }
    }
    abs0Out = (measuredFrames > 0) ? (abs0Sum / measuredFrames) : 0;
    abs1Out = (measuredFrames > 0) ? (abs1Sum / measuredFrames) : 0;
#else
    abs0Out = 0;
    abs1Out = 0;
    nonZeroOut = 0;
    s0LastOut = 0;
    s1LastOut = 0;
#endif
  }

  void initMicrophone() {
#ifdef JWS_DESKTOP_S3
    static constexpr int kPinCombos[8][3] = {
        {4, 5, 6}, {5, 4, 6}, {5, 6, 4}, {6, 5, 4},
        {4, 6, 5}, {6, 4, 5}, {15, 16, 17}, {16, 15, 17},
    };
    int bestSck = 4;
    int bestWs = 5;
    int bestSd = 6;
    uint64_t bestSignal = 0;
    bool foundSignal = false;

    for (int p = 0; p < 8; ++p) {
      const int cSck = kPinCombos[p][0];
      const int cWs = kPinCombos[p][1];
      const int cSd = kPinCombos[p][2];
      if (!installI2sWithPins(cSck, cWs, cSd, false)) continue;

      uint64_t abs0 = 0, abs1 = 0;
      uint32_t nonZeroCount = 0;
      int32_t s0Last = 0, s1Last = 0;
      probeCurrentI2sSignal(abs0, abs1, nonZeroCount, s0Last, s1Last);
      Serial.printf("[MIC-SCAN] Try SCK=%d WS=%d SD=%d -> abs0=%llu abs1=%llu nz=%u s0=0x%08lX s1=0x%08lX\n",
                    cSck, cWs, cSd,
                    static_cast<unsigned long long>(abs0),
                    static_cast<unsigned long long>(abs1),
                    static_cast<unsigned>(nonZeroCount),
                    static_cast<unsigned long>(static_cast<uint32_t>(s0Last)),
                    static_cast<unsigned long>(static_cast<uint32_t>(s1Last)));

      const uint64_t totalAc = abs0 + abs1;
      const bool nonRailed = (abs0 > 0 && abs0 < 4000000ULL) || (abs1 > 0 && abs1 < 4000000ULL);
      if ((totalAc > 0 || nonZeroCount > 0) && (!foundSignal || (nonRailed && totalAc > bestSignal))) {
        foundSignal = true;
        bestSignal = totalAc;
        bestSck = cSck;
        bestWs = cWs;
        bestSd = cSd;
      }
      if (p == 0 && foundSignal && nonRailed) {
        // Default SCK=4 WS=5 SD=6 active right away; skip scanning fallback combinations
        sckPin_ = bestSck;
        wsPin_ = bestWs;
        sdPin_ = bestSd;
        micReady_ = true;
        bootWarmupUntilMs_ = millis() + 4000UL;
        Serial.printf("[XIAOZHI] I2S Mic ready on default pins: SCK=%d WS=%d SD=%d (signal=%llu)\n",
                      sckPin_, wsPin_, sdPin_, static_cast<unsigned long long>(bestSignal));
        portENTER_CRITICAL(&xzMux_);
        snap_.micHardwareReady = micReady_;
        snprintf(snap_.pinStatus, sizeof(snap_.pinStatus), "SCK:%d WS:%d SD:%d", sckPin_, wsPin_, sdPin_);
        portEXIT_CRITICAL(&xzMux_);
        return;
      }
      i2s_driver_uninstall(MicI2sPort);
    }

    if (installI2sWithPins(bestSck, bestWs, bestSd)) {
      vTaskDelay(pdMS_TO_TICKS(150));
      i2s_zero_dma_buffer(MicI2sPort);
      sckPin_ = bestSck;
      wsPin_ = bestWs;
      sdPin_ = bestSd;
      micReady_ = true;
      Serial.printf("[XIAOZHI] Selected I2S Mic pins: SCK=%d WS=%d SD=%d (signal=%llu)\n",
                    sckPin_, wsPin_, sdPin_, static_cast<unsigned long long>(bestSignal));
    }
#endif
    bootWarmupUntilMs_ = millis() + 4000UL;
    portENTER_CRITICAL(&xzMux_);
    snap_.micHardwareReady = micReady_;
    snprintf(snap_.pinStatus, sizeof(snap_.pinStatus), "SCK:%d WS:%d SD:%d", sckPin_, wsPin_, sdPin_);
    portEXIT_CRITICAL(&xzMux_);
  }

  static void pickStereoChannelAndDc(const int32_t* samples, size_t nFrames, int& chOut, int32_t& dcOut) {
    int64_t sum0 = 0;
    int64_t sum1 = 0;
    for (size_t f = 0; f < nFrames; ++f) {
      sum0 += (samples[f * 2] >> 8);
      sum1 += (samples[f * 2 + 1] >> 8);
    }
    const int32_t dc0 = static_cast<int32_t>(sum0 / static_cast<int64_t>(nFrames));
    const int32_t dc1 = static_cast<int32_t>(sum1 / static_cast<int64_t>(nFrames));
    uint64_t abs0 = 0;
    uint64_t abs1 = 0;
    for (size_t f = 0; f < nFrames; ++f) {
      int32_t v0 = (samples[f * 2] >> 8) - dc0;
      int32_t v1 = (samples[f * 2 + 1] >> 8) - dc1;
      abs0 += static_cast<uint64_t>(v0 < 0 ? -v0 : v0);
      abs1 += static_cast<uint64_t>(v1 < 0 ? -v1 : v1);
    }
    if (abs0 >= abs1) {
      chOut = 0;
      dcOut = dc0;
    } else {
      chOut = 1;
      dcOut = dc1;
    }
  }

  static void formatMin12(int totalMin, char* out, size_t cap) {
    if (totalMin < 0 || totalMin >= 1440) {
      snprintf(out, cap, "--:--");
      return;
    }
    const int h24 = totalMin / 60;
    const int m = totalMin % 60;
    const int h12 = (h24 % 12 == 0) ? 12 : (h24 % 12);
    snprintf(out, cap, "%02d:%02d%s", h12, m, (h24 >= 12) ? "PM" : "AM");
  }

  static void updatePrayerBadge(const DisplayFrame& frame, char* out, size_t cap) {
    char clockStr[10] = "--:--";
    if (frame.clock.valid) {
      formatMin12(frame.clock.hour * 60 + frame.clock.minute, clockStr, sizeof(clockStr));
    }
    if (frame.clock.valid && frame.schedule.valid) {
      NextPrayerInfo nxt = nextPrayerCountdown(frame.schedule, frame.clock.hour, frame.clock.minute, frame.clock.second);
      if (nxt.valid) {
        char pTime[10];
        formatMin12(nxt.targetMinute, pTime, sizeof(pTime));
        snprintf(out, cap, "%s %s | %s", prayerName(nxt.prayer), pTime, clockStr);
        return;
      }
    }
    snprintf(out, cap, "Kotamobagu | %s", clockStr);
  }

  static void wrapReplyTo3Lines(const char* text,
                                char* line1, size_t cap1,
                                char* line2, size_t cap2,
                                char* line3, size_t cap3) {
    if (cap1) line1[0] = '\0';
    if (cap2) line2[0] = '\0';
    if (cap3) line3[0] = '\0';
    if (!text || !text[0]) return;

    auto takeChunk = [](const char* src, char* dst, size_t dstCap, size_t maxChars) -> const char* {
      while (*src == ' ') ++src;
      const size_t rem = strlen(src);
      if (rem == 0) {
        if (dstCap) dst[0] = '\0';
        return src;
      }
      const size_t limit = (maxChars < dstCap - 1) ? maxChars : (dstCap - 1);
      if (rem <= limit) {
        copySafe(dst, dstCap, src);
        return src + rem;
      }
      size_t split = limit;
      while (split > limit / 2 && src[split] != ' ') --split;
      if (split <= limit / 2) split = limit;
      memcpy(dst, src, split);
      dst[split] = '\0';
      const char* next = src + split;
      while (*next == ' ') ++next;
      return next;
    };

    // Line 1 has "Upin: " prefix (6 chars), so max 39 chars fits cleanly inside 284px dialog box
    const char* p = takeChunk(text, line1, cap1, 39);
    p = takeChunk(p, line2, cap2, 45);
    takeChunk(p, line3, cap3, 45);
  }

  static uint8_t matchIntentByKeyword(const char* q) {
    if (!q || q[0] == '\0') return 0;
    if (strcasestr(q, "buka ") || strcasestr(q, "pindah ") || strcasestr(q, "tampilkan ")) {
      if (strcasestr(q, "mikrotik") || strcasestr(q, "cuaca") || strcasestr(q, "saham") ||
          strcasestr(q, "berita") || strcasestr(q, "jadwal") || strcasestr(q, "jws") ||
          strcasestr(q, "task manager") || strcasestr(q, "sistem") || strcasestr(q, "performa") ||
          strcasestr(q, "relay") || strcasestr(q, "saklar") || strcasestr(q, "lampu")) {
        return 8;
      }
    }
    if (strcasestr(q, "relay") || strcasestr(q, "saklar") || strcasestr(q, "lampu") ||
        strcasestr(q, "nyalakan semua") || strcasestr(q, "matikan semua") ||
        strcasestr(q, "hidupkan semua") || strcasestr(q, "padamkan semua")) {
      return 10;
    }
    if (strcasestr(q, "cari hp") || strcasestr(q, "bunyikan hp") || strcasestr(q, "find phone") ||
        strcasestr(q, "stop cari hp") || strcasestr(q, "putar musik") || strcasestr(q, "play musik") ||
        strcasestr(q, "jeda musik") || strcasestr(q, "pause musik") || strcasestr(q, "lagu berikutnya") ||
        strcasestr(q, "next musik") || strcasestr(q, "lagu sebelumnya") || strcasestr(q, "foto kamera") ||
        strcasestr(q, "ambil foto") || strcasestr(q, "notifikasi") || strcasestr(q, "chronos") ||
        strcasestr(q, "bluetooth") || strcasestr(q, "baterai hp")) {
      return 11;
    }
    if (strcasestr(q, "brightness") || strcasestr(q, "kecerahan") ||
        strcasestr(q, "redupkan") || strcasestr(q, "terangkan") ||
        strcasestr(q, "volume") || strcasestr(q, "tes suara") ||
        strcasestr(q, "besarkan suara") || strcasestr(q, "kecilkan suara")) {
      return 9;
    }
    if (strcasestr(q, "cek sistem") || strcasestr(q, "cek memori") || strcasestr(q, "cek ram") ||
        strcasestr(q, "psram") || strcasestr(q, "uptime") || strcasestr(q, "sinyal wifi") ||
        strcasestr(q, "status esp") || strcasestr(q, "task manager")) {
      return 6;
    }
    if (strcasestr(q, "dzikir") || strcasestr(q, "zikir") || strcasestr(q, "doa harian") ||
        strcasestr(q, "tasbih") || strcasestr(q, "motivasi")) {
      return 7;
    }
    if (strcasestr(q, "mikrotik") || strcasestr(q, "router") || strcasestr(q, "pppoe") ||
        strcasestr(q, "hotspot") || strcasestr(q, "jaringan") || strcasestr(q, "ether") ||
        strcasestr(q, "trafik") || strcasestr(q, "cpu")) {
      return 1;
    }
    if (strcasestr(q, "cuaca") || strcasestr(q, "suhu") || strcasestr(q, "hujan") || strcasestr(q, "panas")) {
      return 2;
    }
    if (strcasestr(q, "saham") || strcasestr(q, "ihsg") || strcasestr(q, "bbca") || strcasestr(q, "bursa")) {
      return 3;
    }
    if (strcasestr(q, "berita") || strcasestr(q, "kabar") || strcasestr(q, "hadits") || strcasestr(q, "nasihat")) {
      return 4;
    }
    if (strcasestr(q, "sholat") || strcasestr(q, "salat") || strcasestr(q, "imsak") ||
        strcasestr(q, "subuh") || strcasestr(q, "dzuhur") || strcasestr(q, "zuhur") ||
        strcasestr(q, "ashar") || strcasestr(q, "asar") || strcasestr(q, "maghrib") ||
        strcasestr(q, "magrib") || strcasestr(q, "isya") || strcasestr(q, "jadwal") ||
        strcasestr(q, "adzan") || strcasestr(q, "azan") || strcasestr(q, "iqomah") ||
        strcasestr(q, "jam") || strcasestr(q, "waktu")) {
      return 0;
    }
    return 5;
  }

  void answerLocalIntent(uint8_t intent, const char* customQuery) {
    portENTER_CRITICAL(&xzMux_);
    const DisplayFrame frameCopy = latestFrame_;
    const bool validFrame = hasFrame_;
    portEXIT_CRITICAL(&xzMux_);

    char qBuf[72] = "";
    char ansBuf[160] = "";

    switch (intent) {
      case 6: {
        copySafe(qBuf, sizeof(qBuf), customQuery ? customQuery : "Status Kesehatan Sistem ESP32-S3");
        const uint32_t upSec = millis() / 1000UL;
        const uint32_t upH = upSec / 3600UL;
        const uint32_t upM = (upSec % 3600UL) / 60UL;
        snprintf(ansBuf, sizeof(ansBuf),
                 "RAM %uKB, PSRAM %uKB/%uKB, WiFi %ddBm (%s), Up %luj %lum.",
                 static_cast<unsigned>(ESP.getFreeHeap() / 1024U),
                 static_cast<unsigned>(ESP.getFreePsram() / 1024U),
                 static_cast<unsigned>(ESP.getPsramSize() / 1024U),
                 (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : 0,
                 (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString().c_str() : "AP",
                 static_cast<unsigned long>(upH), static_cast<unsigned long>(upM));
        break;
      }
      case 7: {
        copySafe(qBuf, sizeof(qBuf), customQuery ? customQuery : "Dzikir & Doa Harian");
        static constexpr const char* kDzikirList[5] = {
            "Subhanallah wabihamdihi, Subhanallahil 'azhim (ringan di lisan, berat di timbangan).",
            "Allahumma inni as'aluka 'ilman nafi'an, wa rizqan thayyiban, wa 'amalan mutaqabbalan.",
            "Hasbunallah wa ni'mal wakil, ni'mal maula wa ni'man nashir. Semangat beraktivitas!",
            "Rabbana atina fid-dunya hasanah wa fil-akhirati hasanah wa qina 'adzaban-nar.",
            "Astaghfirullahal 'azhim. Perbanyak istighfar agar hati tenang & rezeki dilapangkan."};
        const uint8_t idx = dzikirIdx_++ % 5;
        copySafe(ansBuf, sizeof(ansBuf), kDzikirList[idx]);
        break;
      }
      case 8: {
        copySafe(qBuf, sizeof(qBuf), customQuery ? customQuery : "Pindah Layar");
        int8_t target = 0;
        const char* targetName = "Jadwal Sholat";
        if (customQuery) {
          if (strcasestr(customQuery, "cuaca")) { target = 1; targetName = "Cuaca Kotamobagu"; }
          else if (strcasestr(customQuery, "saham")) { target = 2; targetName = "Saham IDX"; }
          else if (strcasestr(customQuery, "berita")) { target = 3; targetName = "Berita CNBC"; }
          else if (strcasestr(customQuery, "mikrotik")) { target = 4; targetName = "MikroTik NOC"; }
          else if (strcasestr(customQuery, "task manager") || strcasestr(customQuery, "performa") ||
                   strcasestr(customQuery, "sistem")) {
            target = 6; targetName = "Task Manager S3";
          } else if (strcasestr(customQuery, "relay") || strcasestr(customQuery, "saklar") ||
                     strcasestr(customQuery, "lampu")) {
            target = 7; targetName = "Kontrol Relay 2CH";
          }
        }
        pendingSlideRequest_.store(target, std::memory_order_release);
        snprintf(ansBuf, sizeof(ansBuf), "Siap kawan! Membuka layar %s sekarang. Betul betul betul!", targetName);
        break;
      }
      case 10: {
        copySafe(qBuf, sizeof(qBuf), customQuery ? customQuery : "Kontrol Relay 2CH");
        auto& rc = RelayControlService::instance();
        const bool turnOff = customQuery && (strcasestr(customQuery, "mati") ||
                                             strcasestr(customQuery, "padam") ||
                                             strcasestr(customQuery, "off") ||
                                             strcasestr(customQuery, "tutup"));
        const bool onState = !turnOff;
        const char* actWord = onState ? "dinyalakan" : "dimatikan";
        if (customQuery && strcasestr(customQuery, "semua")) {
          rc.setAll(onState);
          snprintf(ansBuf, sizeof(ansBuf), "Siap! Semua Relay (CH1 & CH2) sudah %s.", actWord);
        } else if (customQuery && (strcasestr(customQuery, "relay 1") || strcasestr(customQuery, "relay satu") ||
                                   strcasestr(customQuery, "lampu 1") || strcasestr(customQuery, "lampu satu"))) {
          rc.setRelay1(onState);
          snprintf(ansBuf, sizeof(ansBuf), "Relay 1 (GPIO 15 Active-HIGH) berhasil %s!", actWord);
        } else if (customQuery && (strcasestr(customQuery, "relay 2") || strcasestr(customQuery, "relay dua") ||
                                   strcasestr(customQuery, "lampu 2") || strcasestr(customQuery, "lampu dua"))) {
          rc.setRelay2(onState);
          snprintf(ansBuf, sizeof(ansBuf), "Relay 2 (GPIO 16 Active-LOW) berhasil %s!", actWord);
        } else {
          rc.setRelay1(onState);
          snprintf(ansBuf, sizeof(ansBuf), "Relay 1 (GPIO 15) berhasil %s! Betul betul betul!", actWord);
        }
        break;
      }
      case 11: {
        copySafe(qBuf, sizeof(qBuf), customQuery ? customQuery : "Chronos BLE Smartphone");
        auto& ch = ChronosService::instance();
        const auto cs = ch.snapshot();
        if (customQuery && (strcasestr(customQuery, "stop cari") || strcasestr(customQuery, "hentikan"))) {
          ch.findPhone(false);
          snprintf(ansBuf, sizeof(ansBuf), "Pencarian HP dihentikan. Alhamdulillah jika sudah ketemu!");
        } else if (customQuery && (strcasestr(customQuery, "cari hp") || strcasestr(customQuery, "bunyikan hp") ||
                                   strcasestr(customQuery, "find phone"))) {
          if (cs.connected) {
            ch.findPhone(true);
            snprintf(ansBuf, sizeof(ansBuf), "Membunyikan HP Anda via Bluetooth Chronos sekarang!");
          } else {
            snprintf(ansBuf, sizeof(ansBuf), "HP belum terhubung ke Bluetooth JWS-UPIN-S3 di aplikasi Chronos.");
          }
        } else if (customQuery && (strcasestr(customQuery, "berikutnya") || strcasestr(customQuery, "next"))) {
          ch.musicNext();
          snprintf(ansBuf, sizeof(ansBuf), "Siap! Memutar lagu berikutnya di HP via Chronos BLE.");
        } else if (customQuery && strcasestr(customQuery, "sebelumnya")) {
          ch.musicPrev();
          snprintf(ansBuf, sizeof(ansBuf), "Siap! Kembali ke lagu sebelumnya di HP via Chronos BLE.");
        } else if (customQuery && (strcasestr(customQuery, "musik") || strcasestr(customQuery, "lagu"))) {
          ch.musicPlayPause();
          snprintf(ansBuf, sizeof(ansBuf), "Perintah Play/Pause musik dikirim ke HP via Bluetooth!");
        } else if (customQuery && (strcasestr(customQuery, "foto") || strcasestr(customQuery, "kamera"))) {
          ch.capturePhoto();
          snprintf(ansBuf, sizeof(ansBuf), "Cekrek! Tombol kamera HP ditekan via Chronos BLE.");
        } else {
          if (cs.connected) {
            if (cs.notifCount > 0 && cs.notifTitle[0]) {
              snprintf(ansBuf, sizeof(ansBuf), "BLE HP Aktif (Bat %u%%). [%s] %s: %s",
                       cs.phoneBattery, cs.notifApp, cs.notifTitle, cs.notifMsg);
            } else {
              snprintf(ansBuf, sizeof(ansBuf), "Chronos BLE Terhubung ke HP (Baterai HP %u%%). Belum ada notifikasi baru.",
                       cs.phoneBattery);
            }
          } else {
            snprintf(ansBuf, sizeof(ansBuf), "Bluetooth Chronos siap (%s). Hubungkan dari aplikasi Chronos di HP.",
                     cs.deviceName);
          }
        }
        break;
      }
      case 9: {
        if (customQuery && (strcasestr(customQuery, "volume") || strcasestr(customQuery, "suara"))) {
          copySafe(qBuf, sizeof(qBuf), customQuery);
          uint8_t curVol = AudioService::instance().getVolume();
          if (strcasestr(customQuery, "kecil") || strcasestr(customQuery, "turun") || strcasestr(customQuery, "pelan")) {
            curVol = (curVol > 6) ? static_cast<uint8_t>(curVol - 6) : 3;
          } else if (strcasestr(customQuery, "besar") || strcasestr(customQuery, "naik") || strcasestr(customQuery, "keras")) {
            curVol = (curVol <= 24) ? static_cast<uint8_t>(curVol + 6) : 30;
          } else {
            for (const char* p = customQuery; *p; ++p) {
              if (*p >= '0' && *p <= '9') {
                int v = atoi(p);
                if (v >= 0 && v <= 30) curVol = static_cast<uint8_t>(v);
                else if (v > 30 && v <= 100) curVol = static_cast<uint8_t>((v * 30) / 100);
                break;
              }
            }
          }
          AudioService::instance().setVolume(curVol);
          snprintf(ansBuf, sizeof(ansBuf), "Halo kawan! Speaker MAX98357A siap, volume diatur ke %u dari 30. Betul betul betul!", curVol);
          break;
        }
        copySafe(qBuf, sizeof(qBuf), customQuery ? customQuery : "Kontrol Kecerahan Layar");
        uint8_t curBri = OnlineFeedService::instance().getTftBrightness();
        if (customQuery && strcasestr(customQuery, "redup")) {
          curBri = (curBri > 25) ? static_cast<uint8_t>(curBri - 20) : 10;
        } else if (customQuery && strcasestr(customQuery, "terang")) {
          curBri = (curBri <= 80) ? static_cast<uint8_t>(curBri + 20) : 100;
        } else if (customQuery) {
          for (const char* p = customQuery; *p; ++p) {
            if (*p >= '0' && *p <= '9') {
              int v = atoi(p);
              if (v >= 5 && v <= 100) curBri = static_cast<uint8_t>(v);
              break;
            }
          }
        }
        OnlineFeedService::instance().setTftBrightness(curBri);
        snprintf(ansBuf, sizeof(ansBuf), "Kecerahan layar TFT diatur ke %u persen.", curBri);
        break;
      }
      case 1: {
        copySafe(qBuf, sizeof(qBuf), customQuery ? customQuery : "Bagaimana status MikroTik NOC sekarang?");
        const auto& mt = MikrotikService::instance().feed();
        snprintf(ansBuf, sizeof(ansBuf),
                 "Router %s (%s) CPU %u%%. eth1 RX:%.1fM, PPPoE:%d HS:%d.",
                 mt.identity, mt.connected ? "LIVE" : "DEMO", mt.cpuLoad,
                 mt.ifaces[0].rxMbps, mt.totalPppoe, mt.totalHotspot);
        break;
      }
      case 2: {
        copySafe(qBuf, sizeof(qBuf), customQuery ? customQuery : "Bagaimana cuaca Kotamobagu hari ini?");
        const auto& w = OnlineFeedService::instance().weather();
        snprintf(ansBuf, sizeof(ansBuf),
                 "Cuaca Kotamobagu %s, suhu %.1f C dengan kelembaban %d%%.",
                 w.statusText, w.temp, w.humidity);
        break;
      }
      case 3: {
        copySafe(qBuf, sizeof(qBuf), customQuery ? customQuery : "Bagaimana pergerakan IHSG & saham?");
        const auto& st = OnlineFeedService::instance().stocks();
        snprintf(ansBuf, sizeof(ansBuf),
                 "IHSG di %.1f (%+.2f%%). %s Rp %.0f (%+.1f%%).",
                 st.ihsg.price, st.ihsg.percent,
                 st.items[0].symbol, st.items[0].price, st.items[0].percent);
        break;
      }
      case 4: {
        copySafe(qBuf, sizeof(qBuf), customQuery ? customQuery : "Apa berita terkini & pesan hari ini?");
        const auto& nw = OnlineFeedService::instance().news();
        if (nw.count > 0 && nw.headlines[0][0]) {
          snprintf(ansBuf, sizeof(ansBuf), "Info CNBC: %.95s", nw.headlines[0]);
        } else {
          snprintf(ansBuf, sizeof(ansBuf), "Awali pekerjaan dengan Bismillah dan jaga sholat.");
        }
        break;
      }
      case 5: {
        copySafe(qBuf, sizeof(qBuf), customQuery ? customQuery : "Pertanyaan Umum");
        if (WiFi.status() == WL_CONNECTED) {
          snprintf(ansBuf, sizeof(ansBuf),
                   "Halo kawan! Server AI sedang padat, coba tanya sekali lagi ke Upin ya!");
        } else {
          snprintf(ansBuf, sizeof(ansBuf),
                   "ESP32 belum terhubung ke internet. Cek koneksi Wi-Fi agar AI bisa menjawab bebas.");
        }
        break;
      }
      case 0:
      default: {
        copySafe(qBuf, sizeof(qBuf), customQuery ? customQuery : "Kapan waktu sholat berikutnya?");
        if (validFrame && frameCopy.clock.valid && frameCopy.schedule.valid) {
          Prayer queryPrayer = PrayerCount;
          if (customQuery) {
            if (strcasestr(customQuery, "imsak")) queryPrayer = Imsak;
            else if (strcasestr(customQuery, "subuh")) queryPrayer = Subuh;
            else if (strcasestr(customQuery, "dzuhur") || strcasestr(customQuery, "zuhur")) queryPrayer = Dzuhur;
            else if (strcasestr(customQuery, "ashar") || strcasestr(customQuery, "asar")) queryPrayer = Ashar;
            else if (strcasestr(customQuery, "maghrib") || strcasestr(customQuery, "magrib")) queryPrayer = Maghrib;
            else if (strcasestr(customQuery, "isya")) queryPrayer = Isya;
          }
          if (queryPrayer != PrayerCount && frameCopy.schedule.minutes[queryPrayer] >= 0) {
            char pTime[10];
            formatMin12(frameCopy.schedule.minutes[queryPrayer], pTime, sizeof(pTime));
            snprintf(ansBuf, sizeof(ansBuf),
                     "Untuk Kota Kotamobagu, waktu %s jatuh pada pukul %s WITA.",
                     prayerName(queryPrayer), pTime);
          } else {
            NextPrayerInfo nxt = nextPrayerCountdown(
                frameCopy.schedule, frameCopy.clock.hour, frameCopy.clock.minute, frameCopy.clock.second);
            if (nxt.valid) {
              char pTime[10];
              formatMin12(nxt.targetMinute, pTime, sizeof(pTime));
              const int remHrs = static_cast<int>(nxt.secondsRemaining / 3600);
              const int remMins = static_cast<int>((nxt.secondsRemaining % 3600) / 60);
              snprintf(ansBuf, sizeof(ansBuf),
                       "Waktu %s pukul %s WITA (sisa %d jam %d menit lagi).",
                       prayerName(nxt.prayer), pTime, remHrs, remMins);
            } else {
              snprintf(ansBuf, sizeof(ansBuf), "Jadwal sholat Kemenag Kotamobagu sedang dimuat.");
            }
          }
        } else {
          snprintf(ansBuf, sizeof(ansBuf), "Menunggu sinkronisasi jam untuk jadwal sholat.");
        }
        break;
      }
    }

    char l1[52] = "", l2[52] = "", l3[52] = "";
    wrapReplyTo3Lines(ansBuf, l1, sizeof(l1), l2, sizeof(l2), l3, sizeof(l3));

    portENTER_CRITICAL(&xzMux_);
    copySafe(snap_.userQuery, sizeof(snap_.userQuery), qBuf);
    copySafe(snap_.replyLine1, sizeof(snap_.replyLine1), l1);
    copySafe(snap_.replyLine2, sizeof(snap_.replyLine2), l2);
    copySafe(snap_.replyLine3, sizeof(snap_.replyLine3), l3);
    snap_.state = XiaozhiState::Speaking;
    stateChangedMs_ = millis();
    portEXIT_CRITICAL(&xzMux_);

    AudioService::instance().speakText(ansBuf);
  }

  static void sanitizeForTft(String& s) {
    s.replace("\xC2\xB0" "C", " C");
    s.replace("\xC2\xB0", " ");
    s.replace("\xE2\x80\x93", "-");
    s.replace("\xE2\x80\x94", "-");
    s.replace("**", "");
    s.replace('\n', ' ');
    s.replace('\r', ' ');
    s.replace('\t', ' ');
    String clean;
    clean.reserve(s.length());
    bool lastSpace = false;
    for (size_t i = 0; i < s.length(); ++i) {
      const unsigned char ch = static_cast<unsigned char>(s.charAt(static_cast<unsigned int>(i)));
      if (ch >= 0x20 && ch < 0x7F) {
        if (ch == ' ') {
          if (!lastSpace) {
            clean += ' ';
            lastSpace = true;
          }
        } else {
          clean += static_cast<char>(ch);
          lastSpace = false;
        }
      }
    }
    clean.trim();
    s = clean;
  }

  void buildAgentContext(char* out, size_t cap, const char* agName, const char* agPrompt,
                         const DisplayFrame& frameCopy, bool validFrame) {
    const auto& mt = MikrotikService::instance().feed();
    const auto& w = OnlineFeedService::instance().weather();
    if (validFrame && frameCopy.clock.valid && frameCopy.schedule.valid) {
      NextPrayerInfo nxt = nextPrayerCountdown(
          frameCopy.schedule, frameCopy.clock.hour, frameCopy.clock.minute, frameCopy.clock.second);
      char pTime[6] = "--:--";
      if (nxt.valid) formatMinute(nxt.targetMinute, pTime);
      snprintf(out, cap,
               "Kamu \"%s\" (%s). Jawab LANGSUNG pertanyaan user dgn cerdas, akurat & singkat "
               "(Bahasa Indonesia max 90 huruf, tanpa markdown/emoji). Jika ditanya pengetahuan "
               "umum/sains/sejarah/Islam, jawab langsung topiknya. Gunakan data lokal Kotamobagu "
               "ini HANYA jika ditanya jam/sholat/cuaca/MikroTik: Jam %02d:%02d WITA, "
               "sholat %s %s, Cuaca %.1fC %s, Router %s CPU %u%% PPPoE:%d HS:%d.",
               agName, agPrompt,
               frameCopy.clock.hour, frameCopy.clock.minute,
               nxt.valid ? prayerName(nxt.prayer) : "Sholat", pTime,
               w.temp, w.statusText, mt.identity, mt.cpuLoad, mt.totalPppoe, mt.totalHotspot);
    } else {
      snprintf(out, cap,
               "Kamu \"%s\" (%s). Jawab LANGSUNG pertanyaan user dgn cerdas, akurat & singkat "
               "(Bahasa Indonesia max 90 huruf, tanpa markdown/emoji). Jika ditanya pengetahuan "
               "umum/sains/sejarah/Islam, jawab langsung topiknya.",
               agName, agPrompt);
    }
  }

  static void parseVoiceTranscriptAndReply(const String& rawText,
                                           char* userOut, size_t userCap,
                                           char* replyOut, size_t replyCap) {
    String clean = rawText;
    clean.replace('\r', '\n');
    clean.replace("**", "");
    int uIdx = clean.indexOf("USER:");
    if (uIdx < 0) uIdx = clean.indexOf("User:");
    int rIdx = clean.indexOf("REPLY:");
    if (rIdx < 0) rIdx = clean.indexOf("Reply:");

    if (uIdx >= 0 && rIdx > uIdx) {
      String uSub = clean.substring(uIdx + 5, rIdx);
      uSub.replace('\n', ' ');
      uSub.trim();
      if (uSub.startsWith("\"") && uSub.endsWith("\"") && uSub.length() >= 2) {
        uSub = uSub.substring(1, uSub.length() - 1);
      }
      copySafe(userOut, userCap, uSub.length() > 0 ? uSub.c_str() : "Suara Anda (Voice AI)");

      String rSub = clean.substring(rIdx + 6);
      rSub.replace('\n', ' ');
      rSub.trim();
      copySafe(replyOut, replyCap, rSub.c_str());
    } else {
      clean.replace('\n', ' ');
      clean.trim();
      copySafe(userOut, userCap, "Suara Anda (Voice AI)");
      copySafe(replyOut, replyCap, clean.c_str());
    }
  }

  static String urlEncode(const char* str) {
    if (!str) return String();
    String out;
    out.reserve(strlen(str) * 2);
    static constexpr char kHex[] = "0123456789ABCDEF";
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(str); *p; ++p) {
      const unsigned char c = *p;
      if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
          c == '-' || c == '_' || c == '.' || c == '~') {
        out += static_cast<char>(c);
      } else if (c == ' ') {
        out += "%20";
      } else {
        out += '%';
        out += kHex[(c >> 4) & 0x0F];
        out += kHex[c & 0x0F];
      }
    }
    return out;
  }

  static bool matchUpinPrefixCandidate(const String& candidate, String& normalizedOut, String& strippedOut) {
    static constexpr const char* kPrefixes[] = {
        "halo upin", "hai upin", "alo upin", "halo ipin", "hai ipin",
        "halo ubin", "halo apin", "halo opin", "halo pin", "halo udin",
        "hello again", "helloween", "hello upin", "hello ipin",
        "upin", "ipin"
    };
    String trimmed = candidate;
    trimmed.trim();
    if (trimmed.length() == 0) return false;
    String lower = trimmed;
    lower.toLowerCase();

    for (size_t i = 0; i < sizeof(kPrefixes) / sizeof(kPrefixes[0]); ++i) {
      const char* pref = kPrefixes[i];
      const size_t pLen = strlen(pref);
      if (lower.startsWith(pref)) {
        const char nextCh = (lower.length() > pLen) ? lower.charAt(static_cast<unsigned int>(pLen)) : '\0';
        const bool boundary = (nextCh == '\0' || nextCh == ' ' || nextCh == ',' || nextCh == '.' ||
                               nextCh == '!' || nextCh == '?' || nextCh == ':' || nextCh == ';');
        if (!boundary) continue;
        size_t pos = pLen;
        while (pos < trimmed.length()) {
          const char c = trimmed.charAt(static_cast<unsigned int>(pos));
          if (c == ' ' || c == ',' || c == '.' || c == '!' || c == '?' || c == ':' || c == '-') {
            ++pos;
          } else {
            break;
          }
        }
        strippedOut = trimmed.substring(static_cast<unsigned int>(pos));
        strippedOut.trim();
        normalizedOut = (strippedOut.length() > 0) ? (String("Halo Upin ") + strippedOut) : String("Halo Upin!");
        return true;
      }
    }
    return false;
  }

  bool transcribePcmGoogleStt(const int16_t* pcm, size_t sampleCount, uint32_t sampleRate,
                              String& outTranscript, bool& outHasUpinPrefix, String& outStrippedQuestion) {
    outTranscript = "";
    outHasUpinPrefix = false;
    outStrippedQuestion = "";
    if (!pcm || sampleCount == 0 || WiFi.status() != WL_CONNECTED) return false;

    WiFiClient client;
    HTTPClient http;
    char url[180];
    snprintf(url, sizeof(url),
             "http://www.google.com/speech-api/v2/recognize?client=chromium&lang=id-ID&pFilter=0&key=%s",
             "AIzaSyBOti4mM-6x9WDnZIjIeyEU21OpBXqWBgw");
    int httpCode = -1;
    String respBody;
    const uint32_t t0 = millis();
    if (http.begin(client, url)) {
      char ctype[40];
      snprintf(ctype, sizeof(ctype), "audio/l16; rate=%lu", static_cast<unsigned long>(sampleRate));
      http.addHeader("Content-Type", ctype);
      http.setTimeout(5000);
      httpCode = http.POST(const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(pcm)), sampleCount * 2U);
      if (httpCode == HTTP_CODE_OK) {
        respBody = http.getString();
      }
      http.end();
    }
    const uint32_t sttMs = millis() - t0;

    if (httpCode != HTTP_CODE_OK || respBody.length() == 0) {
      Serial.printf("[STT] http=%d ms=%lu hasPrefix=0 text=''\n", httpCode, static_cast<unsigned long>(sttMs));
      return false;
    }

    String firstCand;
    int searchFrom = 0;
    while (true) {
      const int keyPos = respBody.indexOf("\"transcript\"", searchFrom);
      if (keyPos < 0) break;
      int colon = respBody.indexOf(':', keyPos + 12);
      if (colon < 0) break;
      int q1 = respBody.indexOf('"', colon + 1);
      if (q1 < 0) break;
      int p = q1 + 1;
      String cand;
      while (p < static_cast<int>(respBody.length())) {
        const char ch = respBody.charAt(p);
        if (ch == '\\' && p + 1 < static_cast<int>(respBody.length())) {
          const char esc = respBody.charAt(p + 1);
          if (esc == 'n' || esc == 'r' || esc == 't') cand += ' ';
          else cand += esc;
          p += 2;
          continue;
        }
        if (ch == '"') break;
        cand += ch;
        ++p;
      }
      cand.trim();
      if (cand.length() > 0) {
        if (firstCand.length() == 0) firstCand = cand;
        String normOut, stripOut;
        if (matchUpinPrefixCandidate(cand, normOut, stripOut)) {
          outTranscript = normOut;
          outHasUpinPrefix = true;
          outStrippedQuestion = stripOut;
          Serial.printf("[STT] http=%d hasPrefix=1 text='%s'\n", httpCode, outTranscript.c_str());
          return true;
        }
      }
      searchFrom = p + 1;
    }

    if (firstCand.length() > 0) {
      outTranscript = firstCand;
      outHasUpinPrefix = false;
      Serial.printf("[STT] http=%d hasPrefix=0 text='%s'\n", httpCode, outTranscript.c_str());
      return true;
    }

    Serial.printf("[STT] http=%d hasPrefix=0 text=''\n", httpCode);
    return false;
  }

  void callGeminiCloud(const char* prompt, bool skipGemini = false, const char* customDisplayQuery = nullptr) {
    // Local-First Fast Path: jawab pertanyaan lokal & perintah suara (JWS/MikroTik/Cuaca/Saham/Berita/Sistem/Dzikir/Navigasi/Pomodoro) instan < 5ms
    const uint8_t localIntent = matchIntentByKeyword(prompt);
    if (localIntent != 5) {
      answerLocalIntent(localIntent,
                        (customDisplayQuery && customDisplayQuery[0]) ? customDisplayQuery : prompt);
      return;
    }

    struct AiBusyGuard {
      AiBusyGuard() { NetLock::instance().setAiBusy(true); }
      ~AiBusyGuard() { NetLock::instance().setAiBusy(false); }
    } aiGuard;
    ScopedNetLock netLock(15000);

    char keyCopy[128] = "";
    char baseCopy[128] = "";
    char modelCopy[64] = "";
    char agName[28] = "";
    char agPrompt[200] = "";
    portENTER_CRITICAL(&xzMux_);
    copySafe(keyCopy, sizeof(keyCopy), apiKey_);
    copySafe(baseCopy, sizeof(baseCopy), apiBaseUrl_);
    copySafe(modelCopy, sizeof(modelCopy), apiModel_);
    copySafe(agName, sizeof(agName), snap_.agentName);
    copySafe(agPrompt, sizeof(agPrompt), snap_.agentPrompt);
    const DisplayFrame frameCopy = latestFrame_;
    const bool validFrame = hasFrame_;
    portEXIT_CRITICAL(&xzMux_);

    if (WiFi.status() != WL_CONNECTED) {
      answerLocalIntent(localIntent,
                        (customDisplayQuery && customDisplayQuery[0]) ? customDisplayQuery : prompt);
      return;
    }

    char sysCtx[640] = "";
    buildAgentContext(sysCtx, sizeof(sysCtx), agName, agPrompt, frameCopy, validFrame);

    String replyText;
    bool gotReply = false;

    // Engine 1: Custom OpenAI-Compatible Provider (Groq, Oktacloud, OpenRouter, dll.)
    if (baseCopy[0] != '\0') {
      String endpoint(baseCopy);
      endpoint.trim();
      while (endpoint.endsWith("/")) {
        endpoint.remove(endpoint.length() - 1);
      }
      if (!endpoint.endsWith("/chat/completions")) {
        endpoint += "/chat/completions";
      }

      const uint32_t t0 = millis();
      HTTPClient http;
      http.setReuse(true);
      if (http.begin(aiTlsClient_, endpoint)) {
        http.addHeader("Content-Type", "application/json");
        http.addHeader("User-Agent", "JWS-ESP32-UpinAI/2.0");
        if (keyCopy[0] != '\0') {
          http.addHeader("Authorization", String("Bearer ") + keyCopy);
        }
        http.setTimeout(6500);

        DynamicJsonDocument reqDoc(1024);
        reqDoc["model"] = (modelCopy[0] != '\0') ? modelCopy : "default";
        reqDoc["max_tokens"] = 65;
        reqDoc["temperature"] = 0.6;
        JsonArray msgs = reqDoc.createNestedArray("messages");
        JsonObject sysMsg = msgs.createNestedObject();
        sysMsg["role"] = "system";
        sysMsg["content"] = String(sysCtx) + " Jawab ramah singkat max 85 huruf tanpa markdown.";
        JsonObject usrMsg = msgs.createNestedObject();
        usrMsg["role"] = "user";
        usrMsg["content"] = String(prompt);

        String reqBody;
        serializeJson(reqDoc, reqBody);
        int code = http.POST(reqBody);
        if (code == HTTP_CODE_OK) {
          StaticJsonDocument<128> filter;
          filter["choices"][0]["message"]["content"] = true;
          DynamicJsonDocument respDoc(2048);
          String rawResp = http.getString();
          if (deserializeJson(respDoc, rawResp, DeserializationOption::Filter(filter)) == DeserializationError::Ok) {
            const char* txt = respDoc["choices"][0]["message"]["content"];
            if (txt && txt[0]) {
              replyText = String(txt);
              sanitizeForTft(replyText);
              gotReply = replyText.length() > 0;
            }
          }
        }
        Serial.printf("[AI-CUSTOM] http=%d model='%s' ms=%lu reply='%s'\n",
                      code, modelCopy[0] ? modelCopy : "default",
                      static_cast<unsigned long>(millis() - t0), replyText.c_str());
        http.end();
      }
    }

    // Engine 2: Gemini API (bila Custom Base URL kosong & API Key berawalan AIza atau AQ.)
    if (!gotReply && !skipGemini && keyCopy[0] != '\0' &&
        (strncmp(keyCopy, "AIza", 4) == 0 || strncmp(keyCopy, "AQ.", 3) == 0)) {
      DynamicJsonDocument reqDoc(1280);
      JsonArray contents = reqDoc.createNestedArray("contents");
      JsonObject msg = contents.createNestedObject();
      JsonArray parts = msg.createNestedArray("parts");
      JsonObject part = parts.createNestedObject();
      part["text"] = String(sysCtx) +
                     " Jawab ramah dlm Bahasa Indonesia max 90 huruf tanpa markdown.\nPertanyaan: " +
                     String(prompt);
      JsonObject genCfg = reqDoc.createNestedObject("generationConfig");
      genCfg["maxOutputTokens"] = 70;
      genCfg["temperature"] = 0.6;
      JsonObject thinkCfg = genCfg.createNestedObject("thinkingConfig");
      thinkCfg["thinkingBudget"] = 0;

      String reqBody;
      serializeJson(reqDoc, reqBody);

      const uint32_t t0 = millis();
      HTTPClient http;
      http.setReuse(true);
      char url[240];
      const char* gemModel = (modelCopy[0] != '\0') ? modelCopy : "gemini-2.5-flash-lite";
      snprintf(url, sizeof(url),
               "https://generativelanguage.googleapis.com/v1beta/models/%s:generateContent",
               gemModel);
      if (http.begin(aiTlsClient_, url)) {
        http.addHeader("Content-Type", "application/json");
        http.addHeader("x-goog-api-key", keyCopy);
        http.setTimeout(6500);
        int code = http.POST(reqBody);
        if (code == HTTP_CODE_OK) {
          StaticJsonDocument<128> filter;
          filter["candidates"][0]["content"]["parts"][0]["text"] = true;
          DynamicJsonDocument respDoc(1536);
          if (deserializeJson(respDoc, http.getStream(), DeserializationOption::Filter(filter)) == DeserializationError::Ok) {
            const char* txt = respDoc["candidates"][0]["content"]["parts"][0]["text"];
            if (txt && txt[0]) {
              replyText = String(txt);
              sanitizeForTft(replyText);
              gotReply = replyText.length() > 0;
            }
          }
        }
        Serial.printf("[AI-GEMINI] http=%d model='%s' ms=%lu reply='%s'\n",
                      code, gemModel, static_cast<unsigned long>(millis() - t0), replyText.c_str());
        http.end();
      }
    }

    // Engine 3: Fallback Free Internet AI (Keyless api.llm7.io)
    if (!gotReply) {
      const uint32_t t0 = millis();
      WiFiClientSecure client;
      client.setInsecure();
      HTTPClient http;
      if (http.begin(client, "https://api.llm7.io/v1/chat/completions")) {
        http.addHeader("Content-Type", "application/json");
        http.addHeader("User-Agent", "JWS-ESP32-UpinAI/2.0");
        http.setTimeout(3500);

        DynamicJsonDocument reqDoc(1024);
        reqDoc["model"] = "default";
        reqDoc["max_tokens"] = 65;
        JsonArray msgs = reqDoc.createNestedArray("messages");
        JsonObject sysMsg = msgs.createNestedObject();
        sysMsg["role"] = "system";
        sysMsg["content"] = sysCtx;
        JsonObject usrMsg = msgs.createNestedObject();
        usrMsg["role"] = "user";
        usrMsg["content"] = String(prompt);

        String reqBody;
        serializeJson(reqDoc, reqBody);
        int code = http.POST(reqBody);
        if (code == HTTP_CODE_OK) {
          StaticJsonDocument<128> filter;
          filter["choices"][0]["message"]["content"] = true;
          DynamicJsonDocument respDoc(2048);
          String rawResp = http.getString();
          if (deserializeJson(respDoc, rawResp, DeserializationOption::Filter(filter)) == DeserializationError::Ok) {
            const char* txt = respDoc["choices"][0]["message"]["content"];
            if (txt && txt[0]) {
              replyText = String(txt);
              sanitizeForTft(replyText);
              gotReply = replyText.length() > 0;
            }
          }
        }
        Serial.printf("[AI-LLM7] http=%d ms=%lu reply='%s'\n",
                      code, static_cast<unsigned long>(millis() - t0), replyText.c_str());
        http.end();
      }
    }

    // Engine 4: Secondary Free Internet Fallback (Wikipedia Indonesia REST API)
    if (!gotReply) {
      WiFiClientSecure client;
      client.setInsecure();
      HTTPClient http;
      String sUrl = "https://id.wikipedia.org/w/api.php?action=query&list=search&srsearch=" +
                    urlEncode(prompt) + "&utf8=&format=json&srlimit=1";
      String pageTitle;
      if (http.begin(client, sUrl)) {
        http.addHeader("User-Agent", "JWS-ESP32-UpinAI/2.0");
        http.setTimeout(4000);
        int code = http.GET();
        if (code == HTTP_CODE_OK) {
          StaticJsonDocument<128> wFilter;
          wFilter["query"]["search"][0]["title"] = true;
          DynamicJsonDocument wDoc(512);
          String wResp = http.getString();
          if (deserializeJson(wDoc, wResp, DeserializationOption::Filter(wFilter)) == DeserializationError::Ok) {
            const char* t = wDoc["query"]["search"][0]["title"];
            if (t && t[0]) pageTitle = String(t);
          }
        }
        http.end();
      }
      if (pageTitle.length() > 0) {
        HTTPClient http2;
        String sumUrl = "https://id.wikipedia.org/api/rest_v1/page/summary/" + urlEncode(pageTitle.c_str());
        if (http2.begin(client, sumUrl)) {
          http2.addHeader("User-Agent", "JWS-ESP32-UpinAI/2.0");
          http2.setTimeout(6000);
          int code2 = http2.GET();
          if (code2 == HTTP_CODE_OK) {
            StaticJsonDocument<96> sFilter;
            sFilter["extract"] = true;
            DynamicJsonDocument sDoc(1536);
            String sResp = http2.getString();
            if (deserializeJson(sDoc, sResp, DeserializationOption::Filter(sFilter)) == DeserializationError::Ok) {
              const char* ext = sDoc["extract"];
              if (ext && ext[0]) {
                replyText = String(ext);
                sanitizeForTft(replyText);
                if (replyText.length() > 90) {
                  replyText = replyText.substring(0, 87) + "...";
                }
                gotReply = replyText.length() > 0;
              }
            }
          }
          http2.end();
        }
      }
    }

    if (!gotReply) {
      answerLocalIntent(matchIntentByKeyword(prompt),
                        (customDisplayQuery && customDisplayQuery[0]) ? customDisplayQuery : prompt);
      return;
    }

    char l1[52] = "", l2[52] = "", l3[52] = "";
    wrapReplyTo3Lines(replyText.c_str(), l1, sizeof(l1), l2, sizeof(l2), l3, sizeof(l3));
    portENTER_CRITICAL(&xzMux_);
    if (customDisplayQuery && customDisplayQuery[0] != '\0') {
      copySafe(snap_.userQuery, sizeof(snap_.userQuery), customDisplayQuery);
    }
    copySafe(snap_.replyLine1, sizeof(snap_.replyLine1), l1);
    copySafe(snap_.replyLine2, sizeof(snap_.replyLine2), l2);
    copySafe(snap_.replyLine3, sizeof(snap_.replyLine3), l3);
    snap_.state = XiaozhiState::Speaking;
    stateChangedMs_ = millis();
    portEXIT_CRITICAL(&xzMux_);

    AudioService::instance().speakText(replyText.c_str());
  }

  void replyWithWakePrompt(const char* voiceQuery, bool skipGemini = false) {
    uint8_t pickedTopic = 0;
    portENTER_CRITICAL(&xzMux_);
    pickedTopic = nextIntent_;
    nextIntent_ = (nextIntent_ + 1) % 5;
    copySafe(snap_.userQuery, sizeof(snap_.userQuery), voiceQuery);
    snap_.state = XiaozhiState::Thinking;
    copySafe(snap_.replyLine1, sizeof(snap_.replyLine1), "Halo kawan! Upin sedang menjawab...");
    copySafe(snap_.replyLine2, sizeof(snap_.replyLine2), "");
    stateChangedMs_ = millis();
    portEXIT_CRITICAL(&xzMux_);

    if (WiFi.status() == WL_CONNECTED) {
      static constexpr const char* kWakePrompts[5] = {
          "User memanggil 'Halo Upin!'. Sapa balik dgn ceria ala Upin ('Betul betul betul!') dan sebutkan jadwal sholat berikutnya serta suhu cuaca Kotamobagu.",
          "User memanggil 'Halo Upin!'. Sapa sebagai Upin dan laporkan status router MikroTik NOC (CPU, PPPoE, Hotspot) saat ini.",
          "User memanggil 'Halo Upin!'. Sapa sebagai Upin dan berikan 1 fakta unik Islami atau pengetahuan sains singkat hari ini.",
          "User memanggil 'Halo Upin!'. Sapa sebagai Upin dan buat 1 pantun singkat 2 baris yang lucu & semangat!",
          "User memanggil 'Halo Upin!'. Sapa sebagai Upin dan beri motivasi singkat menjaga sholat berjamaah & sedekah."};
      callGeminiCloud(kWakePrompts[pickedTopic], skipGemini, voiceQuery);
    } else {
      answerLocalIntent(pickedTopic, voiceQuery);
    }
  }

  void recordAndAskGeminiAudio() {
    if (!micReady_) {
      portENTER_CRITICAL(&xzMux_);
      copySafe(snap_.userQuery, sizeof(snap_.userQuery), "Mic INMP441 belum siap");
      copySafe(snap_.replyLine1, sizeof(snap_.replyLine1), "Cek kabel SCK=4, WS=5, SD=6, L/R=GND");
      copySafe(snap_.replyLine2, sizeof(snap_.replyLine2), "Atau ketik tanya lewat Web Dashboard.");
      snap_.state = XiaozhiState::Speaking;
      stateChangedMs_ = millis();
      portEXIT_CRITICAL(&xzMux_);
      return;
    }

    struct AiBusyGuard {
      AiBusyGuard() { NetLock::instance().setAiBusy(true); }
      ~AiBusyGuard() { NetLock::instance().setAiBusy(false); }
    } aiGuard;

    const bool isManual = manualVoiceTrigger_.exchange(false, std::memory_order_acq_rel);
    static constexpr float kEnvelope[12] = {0.28f, 0.45f, 0.65f, 0.84f, 1.0f, 0.76f,
                                            0.58f, 0.80f, 0.96f, 0.72f, 0.48f, 0.28f};

    if (!wavBuf_) {
      wavBufRate_ = 16000;
      wavBufMaxSamples_ = (wavBufRate_ * 30UL) / 10UL;
      wavBuf_ = allocPsram(44UL + wavBufMaxSamples_ * 2UL);
      if (!wavBuf_) {
        wavBufRate_ = 8000;
        wavBufMaxSamples_ = (wavBufRate_ * 30UL) / 10UL;
        wavBuf_ = allocPsram(44UL + wavBufMaxSamples_ * 2UL);
      }
    }
    if (!wavBuf_) {
      answerLocalIntent(nextIntent_, "Memori penuh untuk rekam suara");
      return;
    }
    uint32_t outRate = wavBufRate_;
    uint32_t maxSamples = wavBufMaxSamples_;
    uint8_t* wavBuf = wavBuf_;
    int16_t* pcmOut = reinterpret_cast<int16_t*>(wavBuf + 44);

#ifdef JWS_DESKTOP_S3
    if (isManual) {
      i2s_zero_dma_buffer(MicI2sPort);
    }
#endif
    portENTER_CRITICAL(&xzMux_);
    snap_.state = XiaozhiState::Listening;
    copySafe(snap_.userQuery, sizeof(snap_.userQuery),
             isManual ? "Merekam suara Anda (Mic INMP441)..." : "Mendengarkan 'Halo Upin'...");
    copySafe(snap_.replyLine1, sizeof(snap_.replyLine1), "Silakan bicara ke mic INMP441...");
    copySafe(snap_.replyLine2, sizeof(snap_.replyLine2), "");
    copySafe(snap_.replyLine3, sizeof(snap_.replyLine3), "");
    stateChangedMs_ = millis();
    portEXIT_CRITICAL(&xzMux_);

    uint32_t collected = 0;
    if (!isManual && preRollBuf_ && preRollFilled_ > 0) {
      const size_t avail = preRollFilled_;
      const size_t oldest = (avail < kPreRollSamples) ? 0 : preRollWriteIdx_;
      if (outRate == 16000) {
        const size_t toCopy = (avail < maxSamples) ? avail : maxSamples;
        const size_t startOff = avail - toCopy;
        for (size_t i = 0; i < toCopy; ++i) {
          pcmOut[collected++] = preRollBuf_[(oldest + startOff + i) % kPreRollSamples];
        }
      } else {
        for (size_t i = 0; i + 1 < avail && collected < maxSamples; i += 2) {
          const int32_t sA = preRollBuf_[(oldest + i) % kPreRollSamples];
          const int32_t sB = preRollBuf_[(oldest + i + 1) % kPreRollSamples];
          pcmOut[collected++] = static_cast<int16_t>((sA + sB) / 2);
        }
      }
    }

    uint32_t chunkIdx = 0;
    uint8_t peakLvl = 0;
    bool speechPeakSeen = (!isManual);
    const uint32_t recStartMs = millis();
    uint32_t lastSpeechChunkMs = recStartMs;
#ifdef JWS_DESKTOP_S3
    int32_t rawBuf[256];
    const size_t frameStep = (outRate == 8000) ? 2 : 1;
    while (collected < maxSamples && static_cast<uint32_t>(millis() - recStartMs) < 3000UL) {
      size_t bytesRead = 0;
      if (i2s_read(MicI2sPort, rawBuf, sizeof(rawBuf), &bytesRead, pdMS_TO_TICKS(40)) == ESP_OK &&
          bytesRead >= 16) {
        const size_t nFrames = (bytesRead / sizeof(int32_t)) / 2;
        if (nFrames < 2) continue;
        int ch = 0;
        int32_t dc = 0;
        pickStereoChannelAndDc(rawBuf, nFrames, ch, dc);

        uint64_t chunkAbs = 0;
        size_t chunkSamples = 0;
        for (size_t f = 0; f + (frameStep - 1) < nFrames && collected < maxSamples; f += frameStep) {
          int32_t s = 0;
          if (frameStep == 2) {
            int32_t vA = ((rawBuf[f * 2 + ch] >> 8) - dc) >> 2;
            int32_t vB = ((rawBuf[(f + 1) * 2 + ch] >> 8) - dc) >> 2;
            s = (vA + vB) / 2;
          } else {
            s = ((rawBuf[f * 2 + ch] >> 8) - dc) >> 2;
          }
          if (s > 32767) s = 32767;
          if (s < -32767) s = -32767;
          pcmOut[collected++] = static_cast<int16_t>(s);
          uint32_t a = static_cast<uint32_t>(s < 0 ? -s : s);
          chunkAbs += a;
          ++chunkSamples;
        }
        if (chunkSamples > 0) {
          uint32_t avgChunk = static_cast<uint32_t>(chunkAbs / chunkSamples);
          if (avgChunk >= 220) {
            speechPeakSeen = true;
            lastSpeechChunkMs = millis();
          }
          if ((++chunkIdx & 1U) == 0) {
            uint32_t lvl = (avgChunk > 180) ? ((avgChunk - 180) / 14U) : 0;
            if (lvl > 100) lvl = 100;
            if (lvl > peakLvl) peakLvl = static_cast<uint8_t>(lvl);
            portENTER_CRITICAL(&xzMux_);
            snap_.micLevel = static_cast<uint8_t>(lvl);
            for (int b = 0; b < 12; ++b) {
              float wave = 0.45f + 0.55f * fabsf(sinf(chunkIdx * 0.35f + b * 0.55f));
              int h = static_cast<int>(8.0f + (static_cast<float>(lvl) / 100.0f) * 46.0f * kEnvelope[b] * wave);
              if (h < 8) h = 8;
              if (h > 54) h = 54;
              snap_.bars[b] = static_cast<uint8_t>(h);
            }
            portEXIT_CRITICAL(&xzMux_);
          }
          if (speechPeakSeen && collected >= (outRate * 85UL) / 100UL &&
              static_cast<uint32_t>(millis() - lastSpeechChunkMs) >= 350UL) {
            break;
          }
        }
      } else {
        vTaskDelay(pdMS_TO_TICKS(5));
      }
    }
#endif
    (void)peakLvl;

    uint32_t peakSample = 0;
    for (uint32_t i = 0; i < collected; ++i) {
      const int32_t v = pcmOut[i];
      const uint32_t a = static_cast<uint32_t>(v < 0 ? -v : v);
      if (a > peakSample) peakSample = a;
    }
    if (collected > 0 && peakSample > 60 && peakSample < 24000) {
      int32_t gain = 24000 / static_cast<int32_t>(peakSample);
      if (gain < 1) gain = 1;
      if (gain > 30) gain = 30;
      if (gain > 1) {
        for (uint32_t i = 0; i < collected; ++i) {
          int32_t scaled = static_cast<int32_t>(pcmOut[i]) * gain;
          if (scaled > 32767) scaled = 32767;
          if (scaled < -32767) scaled = -32767;
          pcmOut[i] = static_cast<int16_t>(scaled);
        }
      }
    }
    writeWavHeader(wavBuf, outRate, collected * 2UL);

    char keyCopy[128] = "";
    portENTER_CRITICAL(&xzMux_);
    copySafe(keyCopy, sizeof(keyCopy), apiKey_);
    snap_.state = XiaozhiState::Thinking;
    copySafe(snap_.userQuery, sizeof(snap_.userQuery), "Menerjemahkan suara ke teks (STT)...");
    copySafe(snap_.replyLine1, sizeof(snap_.replyLine1), "Memproses ucapan dari mic INMP441...");
    copySafe(snap_.replyLine2, sizeof(snap_.replyLine2), "");
    copySafe(snap_.replyLine3, sizeof(snap_.replyLine3), "");
    stateChangedMs_ = millis();
    portEXIT_CRITICAL(&xzMux_);

    if (WiFi.status() != WL_CONNECTED) {
      answerLocalIntent(5, "Suara terekam (Wi-Fi belum terhubung)");
      return;
    }

    String sttText;
    bool hasPrefix = false;
    String strippedQ;
    const bool sttOk = transcribePcmGoogleStt(pcmOut, collected, outRate, sttText, hasPrefix, strippedQ);

    if (sttOk && sttText.length() > 0) {
      if (!isManual && !hasPrefix) {
        AudioStatus as = AudioService::instance().status();
        if (isLikelySelfEcho(sttText, as.lastSpoken)) {
          Serial.printf("[XIAOZHI] Self-echo ignored: '%s'\n", sttText.c_str());
          portENTER_CRITICAL(&xzMux_);
          snap_.state = XiaozhiState::Idle;
          stateChangedMs_ = millis();
          portEXIT_CRITICAL(&xzMux_);
          return;
        }
      }

      portENTER_CRITICAL(&xzMux_);
      copySafe(snap_.userQuery, sizeof(snap_.userQuery), sttText.c_str());
      portEXIT_CRITICAL(&xzMux_);

      const bool convOpen = (static_cast<int32_t>(millis() - conversationOpenUntilMs_) < 0);
      if (hasPrefix || convOpen || isManual) {
        conversationOpenUntilMs_ = millis() + 30000UL;
        if (hasPrefix && strippedQ.length() == 0) {
          answerLocalIntent(0, sttText.c_str());
        } else {
          callGeminiCloud(hasPrefix ? strippedQ.c_str() : sttText.c_str(),
                          keyCopy[0] == '\0',
                          sttText.c_str());
        }
        return;
      }

      portENTER_CRITICAL(&xzMux_);
      copySafe(snap_.replyLine1, sizeof(snap_.replyLine1), "Ucapan terbaca! Gunakan awalan 'Halo Upin'");
      copySafe(snap_.replyLine2, sizeof(snap_.replyLine2), "untuk membuka percakapan dgn Upin ya kawan.");
      snap_.state = XiaozhiState::Speaking;
      stateChangedMs_ = millis();
      portEXIT_CRITICAL(&xzMux_);
      return;
    }

    if (!isManual) {
      portENTER_CRITICAL(&xzMux_);
      snap_.state = XiaozhiState::Idle;
      stateChangedMs_ = millis();
      portEXIT_CRITICAL(&xzMux_);
      return;
    }

    portENTER_CRITICAL(&xzMux_);
    copySafe(snap_.userQuery, sizeof(snap_.userQuery), "(Suara belum terdengar jelas)");
    copySafe(snap_.replyLine1, sizeof(snap_.replyLine1), "Bicara agak dekat ke mic (10-30 cm):");
    copySafe(snap_.replyLine2, sizeof(snap_.replyLine2), "Ucapkan 'Halo Upin <pertanyaan Anda>'");
    snap_.state = XiaozhiState::Speaking;
    stateChangedMs_ = millis();
    portEXIT_CRITICAL(&xzMux_);
  }

  static bool isLikelySelfEcho(const String& sttText, const char* lastSpoken) {
    if (!lastSpoken || lastSpoken[0] == '\0' || sttText.length() == 0) return false;
    String sttLow = sttText;
    sttLow.toLowerCase();
    sttLow.trim();
    String spkLow(lastSpoken);
    spkLow.toLowerCase();
    spkLow.trim();
    if (sttLow.length() >= 4 && spkLow.indexOf(sttLow) >= 0) {
      return true;
    }
    int longWords = 0;
    int matchedWords = 0;
    String tok;
    const size_t n = sttLow.length();
    for (size_t i = 0; i <= n; ++i) {
      const char ch = (i < n) ? sttLow.charAt(static_cast<unsigned int>(i)) : ' ';
      const bool isAlnum = (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9');
      if (isAlnum) {
        tok += ch;
      } else if (tok.length() > 0) {
        if (tok.length() >= 4) {
          ++longWords;
          if (spkLow.indexOf(tok) >= 0) {
            ++matchedWords;
          }
        }
        tok = "";
      }
    }
    if (longWords == 0) return false;
    return (matchedWords >= 2) || (matchedWords * 2 >= longWords);
  }

  static void taskEntry(void* arg) {
    static_cast<XiaozhiService*>(arg)->backgroundLoop();
  }

  void sampleMicAndBars(uint32_t nowMs) {
    static constexpr float kEnvelope[12] = {0.26f, 0.42f, 0.60f, 0.80f, 0.98f, 0.72f,
                                            0.56f, 0.78f, 0.96f, 0.70f, 0.46f, 0.26f};
    uint32_t rawAvg = 0;
    uint32_t zcr = 0;
    uint32_t binRaw[12] = {0};
    bool gotAudio = false;
    int chosenCh = 0;
    int32_t s0Hex = 0;
    int32_t s1Hex = 0;
    int32_t s2Hex = 0;
    int32_t s3Hex = 0;

    const bool spkPlaying = AudioService::instance().isMicSuppressed();
#ifdef JWS_DESKTOP_S3
    XiaozhiState curStateBeforeRead = XiaozhiState::Idle;
    portENTER_CRITICAL(&xzMux_);
    curStateBeforeRead = snap_.state;
    portEXIT_CRITICAL(&xzMux_);
    if (spkPlaying) {
      preRollFilled_ = 0;
      preRollWriteIdx_ = 0;
      wasMicSuppressed_ = true;
    } else if (wasMicSuppressed_) {
      wasMicSuppressed_ = false;
      if (micReady_) {
        i2s_zero_dma_buffer(MicI2sPort);
      }
      preRollFilled_ = 0;
      preRollWriteIdx_ = 0;
    }
    const bool fillPreRoll = (preRollBuf_ != nullptr) && !spkPlaying &&
                             (curStateBeforeRead == XiaozhiState::Idle || curStateBeforeRead == XiaozhiState::Speaking);

    if (micReady_) {
      int32_t samples[256];
      uint64_t totalAbs = 0;
      size_t totalFrames = 0;
      uint64_t binAcc[12] = {0};
      size_t binCnt[12] = {0};
      int8_t zcrSign = 0;

      for (int c = 0; c < 3; ++c) {
        size_t bytesRead = 0;
        const TickType_t waitTicks = (c == 0) ? pdMS_TO_TICKS(12) : 0;
        if (i2s_read(MicI2sPort, samples, sizeof(samples), &bytesRead, waitTicks) != ESP_OK ||
            bytesRead < 64) {
          break;
        }
        const size_t nFrames = (bytesRead / sizeof(int32_t)) / 2;
        if (nFrames < 12) continue;

        s0Hex = samples[0];
        s1Hex = samples[1];
        s2Hex = samples[2];
        s3Hex = samples[3];
        int ch = 0;
        int32_t dc = 0;
        pickStereoChannelAndDc(samples, nFrames, ch, dc);
        chosenCh = ch;

        const size_t perBin = nFrames / 12;
        for (int b = 0; b < 12; ++b) {
          const size_t start = static_cast<size_t>(b) * perBin;
          size_t end = (b == 11) ? nFrames : (start + perBin);
          if (end > nFrames) end = nFrames;
          for (size_t f = start; f < end; ++f) {
            const int32_t diff = (samples[f * 2 + ch] >> 8) - dc;
            if (fillPreRoll) {
              int32_t sPre = diff >> 4;
              if (sPre > 32767) sPre = 32767;
              if (sPre < -32767) sPre = -32767;
              preRollBuf_[preRollWriteIdx_] = static_cast<int16_t>(sPre);
              preRollWriteIdx_ = (preRollWriteIdx_ + 1) % kPreRollSamples;
              if (preRollFilled_ < kPreRollSamples) ++preRollFilled_;
            }
            if (diff > 1200) {
              if (zcrSign < 0) ++zcr;
              zcrSign = 1;
            } else if (diff < -1200) {
              if (zcrSign > 0) ++zcr;
              zcrSign = -1;
            }
            const int32_t v = (diff < 0) ? -diff : diff;
            binAcc[b] += static_cast<uint64_t>(v);
            ++binCnt[b];
            totalAbs += static_cast<uint64_t>(v);
            ++totalFrames;
          }
        }
      }

      if (totalFrames > 0) {
        rawAvg = static_cast<uint32_t>(totalAbs / totalFrames);
        zcr = static_cast<uint32_t>((static_cast<uint64_t>(zcr) * 128ULL) / totalFrames);
        for (int b = 0; b < 12; ++b) {
          binRaw[b] = (binCnt[b] > 0) ? static_cast<uint32_t>(binAcc[b] / binCnt[b]) : 0;
        }
        gotAudio = true;
      }
    }
#endif

    uint8_t curLevel = 0;
    uint8_t sliceEnergy[12] = {0};
    portENTER_CRITICAL(&xzMux_);
    XiaozhiState st = snap_.state;
    if (gotAudio && !spkPlaying) {
      if (rawAvg > 50) {
        if (noiseFloor_ < 0.0f) {
          float initFl = static_cast<float>(rawAvg);
          if (initFl < 800.0f) initFl = 800.0f;
          if (initFl > 8000.0f) initFl = 8000.0f;
          noiseFloor_ = initFl;
        } else if (static_cast<float>(rawAvg) < noiseFloor_) {
          noiseFloor_ = 0.85f * noiseFloor_ + 0.15f * static_cast<float>(rawAvg);
        } else if (static_cast<float>(rawAvg) < noiseFloor_ * 1.30f) {
          noiseFloor_ = 0.98f * noiseFloor_ + 0.02f * static_cast<float>(rawAvg);
        }
        if (noiseFloor_ < 800.0f) noiseFloor_ = 800.0f;
        if (noiseFloor_ > 12000.0f) noiseFloor_ = 12000.0f;
      }

      const float effFloor = (noiseFloor_ > 0.0f) ? noiseFloor_ : 800.0f;
      const float gate = effFloor * 1.45f + 950.0f;
      float net = static_cast<float>(rawAvg) - gate;
      if (net < 0.0f || zcr > 36 || zcr < 3) net = 0.0f;
      uint32_t lvl = static_cast<uint32_t>(net / 140.0f);
      if (lvl > 100) lvl = 100;
      curLevel = static_cast<uint8_t>(lvl);

      for (int b = 0; b < 12; ++b) {
        float netBin = static_cast<float>(binRaw[b]) - gate;
        if (netBin < 0.0f || zcr > 36 || zcr < 3) netBin = 0.0f;
        uint32_t mapped = static_cast<uint32_t>(netBin / 140.0f);
        if (mapped > 100) mapped = 100;
        sliceEnergy[b] = static_cast<uint8_t>(mapped);
      }
    }
    const bool warmupDone = (static_cast<int32_t>(nowMs - bootWarmupUntilMs_) >= 0);
    if (!warmupDone || spkPlaying) {
      curLevel = 0;
      for (int b = 0; b < 12; ++b) sliceEnergy[b] = 0;
      voicePending_ = false;
      voiceFrameCount_ = 0;
    }
    snap_.micLevel = curLevel;

    // Human voice filter + responsive pre-roll wake trigger (suppressed while speaker is playing)
    const bool isHumanVoice = warmupDone && !spkPlaying && (curLevel >= 14) && (zcr >= 4) && (zcr <= 34);
    if (micReady_) {
      if (st == XiaozhiState::Idle ||
          (st == XiaozhiState::Speaking && static_cast<uint32_t>(nowMs - stateChangedMs_) > 6000UL)) {
        if (isHumanVoice) {
          if (voiceFrameCount_ < 250) ++voiceFrameCount_;
          if (voiceFrameCount_ >= 3) {
            voicePending_ = false;
            voiceFrameCount_ = 0;
            manualVoiceTrigger_.store(false, std::memory_order_release);
            wakeRequested_.store(true, std::memory_order_release);
            pendingVoiceRecord_.store(true, std::memory_order_release);
          }
        } else {
          voicePending_ = false;
          voiceFrameCount_ = 0;
        }
      } else if (st == XiaozhiState::Listening) {
        if (curLevel > 12) {
          silenceStartMs_ = nowMs;
        }
        if (static_cast<uint32_t>(nowMs - silenceStartMs_) > 750UL ||
            static_cast<uint32_t>(nowMs - stateChangedMs_) > 4500UL) {
          st = XiaozhiState::Thinking;
          snap_.state = XiaozhiState::Thinking;
          stateChangedMs_ = nowMs;
        }
      } else {
        voicePending_ = false;
        voiceFrameCount_ = 0;
      }
    } else {
      voicePending_ = false;
      voiceFrameCount_ = 0;
    }

    const float t = static_cast<float>(nowMs) / 1000.0f;
    for (int i = 0; i < 12; ++i) {
      float h = 6.0f;
      if (st == XiaozhiState::Listening) {
        const float energy = (sliceEnergy[i] > curLevel) ? static_cast<float>(sliceEnergy[i])
                                                         : static_cast<float>(curLevel);
        h = 6.0f + (energy / 100.0f) * 48.0f * kEnvelope[i];
      } else if (st == XiaozhiState::Speaking) {
        const float speechWave = 0.45f + 0.55f * fabsf(sinf(t * 7.5f + i * 0.65f));
        h = 8.0f + 44.0f * kEnvelope[i] * speechWave;
      } else if (st == XiaozhiState::Thinking) {
        const float scan = 0.35f + 0.65f * fabsf(sinf(t * 4.2f - i * 0.45f));
        h = 6.0f + 26.0f * scan;
      } else {
        // Gentle idle breathing + subtle mic reaction even if below VAD threshold
        const float breath = 0.22f + 0.14f * sinf(t * 2.2f + i * 0.4f);
        const float micHint = micReady_ ? (static_cast<float>(curLevel) * 0.25f) : 0.0f;
        h = 6.0f + 34.0f * kEnvelope[i] * breath + micHint;
      }
      int hi = static_cast<int>(h);
      if (hi < 6) hi = 6;
      if (hi > 54) hi = 54;
      snap_.bars[i] = static_cast<uint8_t>(hi);
    }
    const float floorSnapshot = noiseFloor_;
    const uint8_t vFramesSnap = voiceFrameCount_;
    portEXIT_CRITICAL(&xzMux_);

    if (static_cast<uint32_t>(nowMs - lastDiagLogMs_) >= 1000UL) {
      lastDiagLogMs_ = nowMs;
      Serial.printf("[MIC-DIAG] SCK=%d WS=%d SD=%d ch=%d s0=0x%08lX s1=0x%08lX s2=0x%08lX s3=0x%08lX rawAvg=%lu floor=%.1f lvl=%u%% zcr=%u vFrames=%u state=%d\n",
                    sckPin_, wsPin_, sdPin_, chosenCh,
                    static_cast<unsigned long>(static_cast<uint32_t>(s0Hex)),
                    static_cast<unsigned long>(static_cast<uint32_t>(s1Hex)),
                    static_cast<unsigned long>(static_cast<uint32_t>(s2Hex)),
                    static_cast<unsigned long>(static_cast<uint32_t>(s3Hex)),
                    static_cast<unsigned long>(rawAvg), floorSnapshot,
                    static_cast<unsigned>(curLevel),
                    static_cast<unsigned>(zcr),
                    static_cast<unsigned>(vFramesSnap),
                    static_cast<int>(st));
    }

    if (gotAudio && rawAvg == 0 && viewing_.load(std::memory_order_relaxed)) {
      if (++zeroSignalCount_ >= 120) {
        zeroSignalCount_ = 0;
#ifdef JWS_DESKTOP_S3
        const int nextSck = (sckPin_ == 4) ? 5 : 4;
        const int nextWs = (sckPin_ == 4) ? 4 : 5;
        const int nextSd = 6;
        i2s_driver_uninstall(MicI2sPort);
        if (installI2sWithPins(nextSck, nextWs, nextSd)) {
          sckPin_ = nextSck;
          wsPin_ = nextWs;
          sdPin_ = nextSd;
          char pinMsg[28];
          snprintf(pinMsg, sizeof(pinMsg), "SCK:%d WS:%d SD:%d", sckPin_, wsPin_, sdPin_);
          portENTER_CRITICAL(&xzMux_);
          copySafe(snap_.pinStatus, sizeof(snap_.pinStatus), pinMsg);
          portEXIT_CRITICAL(&xzMux_);
          Serial.printf("[XIAOZHI] Live pin toggle retry: %s\n", pinMsg);
        }
#endif
      }
    } else if (rawAvg > 0) {
      zeroSignalCount_ = 0;
    }
  }

  void backgroundLoop() {
    for (;;) {
      const uint32_t nowMs = millis();

      // Handle pending voice recording request (Web Dashboard or VAD)
      if (pendingVoiceRecord_.exchange(false, std::memory_order_acq_rel)) {
        const bool isMan = manualVoiceTrigger_.load(std::memory_order_acquire);
        if (isMan || !AudioService::instance().isMicSuppressed()) {
          recordAndAskGeminiAudio();
        }
        vTaskDelay(pdMS_TO_TICKS(35));
        continue;
      }

      // Handle pending cloud AI prompt from Web Dashboard
      if (pendingCloudAsk_.exchange(false, std::memory_order_acq_rel)) {
        char promptCopy[128] = "";
        portENTER_CRITICAL(&xzMux_);
        copySafe(promptCopy, sizeof(promptCopy), pendingCloudPrompt_);
        portEXIT_CRITICAL(&xzMux_);
        callGeminiCloud(promptCopy);
      }

      // Check state transitions (Thinking -> Speaking -> Idle)
      const bool audioBusy = AudioService::instance().isMicSuppressed();
      bool triggerIntentFromVad = false;
      uint8_t pickedIntent = 0;
      portENTER_CRITICAL(&xzMux_);
      if (!pendingCloudAsk_.load(std::memory_order_relaxed) &&
          !pendingVoiceRecord_.load(std::memory_order_relaxed) &&
          snap_.state == XiaozhiState::Thinking &&
          static_cast<uint32_t>(nowMs - stateChangedMs_) >= 600UL) {
        triggerIntentFromVad = true;
        pickedIntent = nextIntent_;
        nextIntent_ = (nextIntent_ + 1) % 5;
      } else if (snap_.state == XiaozhiState::Speaking) {
        if (audioBusy) {
          stateChangedMs_ = nowMs;
        } else if (static_cast<uint32_t>(nowMs - stateChangedMs_) >= 3500UL) {
          snap_.state = XiaozhiState::Idle;
          stateChangedMs_ = nowMs;
        }
      }
      portEXIT_CRITICAL(&xzMux_);

      if (triggerIntentFromVad) {
        answerLocalIntent(pickedIntent, nullptr);
      }

      sampleMicAndBars(nowMs);
      vTaskDelay(pdMS_TO_TICKS(25));
    }
  }
};

}  // namespace jws
