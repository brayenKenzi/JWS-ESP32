// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <Arduino.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <atomic>

namespace jws {

// Global TLS / Network serialization guard so Kemenag, OnlineFeed, and UPIN AI
// never run concurrent mbedTLS handshakes (~45 KB internal SRAM each).
class NetLock {
 public:
  static NetLock& instance() {
    static NetLock lock;
    return lock;
  }

  bool acquire(uint32_t timeoutMs = 15000UL) {
    ensureInit();
    if (!mutex_) return true;
    return xSemaphoreTake(mutex_, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
  }

  void release() {
    if (mutex_) {
      xSemaphoreGive(mutex_);
    }
  }

  void setAiBusy(bool busy) {
    aiBusy_.store(busy, std::memory_order_release);
  }

  bool isAiBusy() const {
    return aiBusy_.load(std::memory_order_acquire);
  }

 private:
  NetLock() = default;

  void ensureInit() {
    if (!mutex_) {
      portENTER_CRITICAL(&initMux_);
      if (!mutex_) {
        mutex_ = xSemaphoreCreateMutex();
      }
      portEXIT_CRITICAL(&initMux_);
    }
  }

  SemaphoreHandle_t mutex_ = nullptr;
  portMUX_TYPE initMux_ = portMUX_INITIALIZER_UNLOCKED;
  std::atomic<bool> aiBusy_{false};
};

class ScopedNetLock {
 public:
  explicit ScopedNetLock(uint32_t timeoutMs = 15000UL)
      : locked_(NetLock::instance().acquire(timeoutMs)) {}
  ~ScopedNetLock() {
    if (locked_) {
      NetLock::instance().release();
    }
  }
  ScopedNetLock(const ScopedNetLock&) = delete;
  ScopedNetLock& operator=(const ScopedNetLock&) = delete;
  bool locked() const { return locked_; }

 private:
  bool locked_ = false;
};

inline const char* resetReasonText(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON:   return "POWERON";
    case ESP_RST_EXT:       return "EXT_PIN";
    case ESP_RST_SW:        return "SW_RESET";
    case ESP_RST_PANIC:     return "PANIC";
    case ESP_RST_INT_WDT:   return "INT_WDT";
    case ESP_RST_TASK_WDT:  return "TASK_WDT";
    case ESP_RST_WDT:       return "OTHER_WDT";
    case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
    case ESP_RST_BROWNOUT:  return "BROWNOUT";
    case ESP_RST_SDIO:      return "SDIO";
    default:                return "UNKNOWN";
  }
}

}  // namespace jws
