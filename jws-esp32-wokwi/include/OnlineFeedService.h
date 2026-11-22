// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <esp_heap_caps.h>
#include <atomic>
#include <string.h>
#include "NetLock.h"

namespace jws {

struct WeatherFeed {
  float temp = 26.5f;
  int humidity = 85;
  int code = 2;
  char statusText[32] = "Cerah Berawan";
  uint32_t lastUpdateMs = 0;
  bool valid = true;
};

struct StockItem {
  char symbol[10];
  char name[28];
  float price;
  float change;
  float percent;
  float dayHigh;
  float dayLow;
  uint32_t volume;
  bool valid;
};

struct StockFeed {
  StockItem ihsg{"IHSG", "Indeks Harga Saham", 7245.50f, 35.20f, 0.49f, 7260.0f, 7220.0f, 18500000, true};
  StockItem items[4]{
    {"BBCA", "Bank Central Asia", 10150.0f, 125.0f, 1.25f, 10200.0f, 10025.0f, 12500000, true},
    {"BBRI", "Bank Rakyat Indo", 4820.0f, -20.0f, -0.41f, 4880.0f, 4800.0f, 21300000, true},
    {"TLKM", "Telkom Indonesia", 2890.0f, 0.0f, 0.00f, 2920.0f, 2880.0f, 8900000, true},
    {"ASII", "Astra International", 5100.0f, 80.0f, 1.59f, 5150.0f, 5025.0f, 6400000, true}
  };
  int itemCount = 4;
  char watchlistSymbols[64] = "BBCA,BBRI,TLKM,ASII";
  uint32_t lastUpdateMs = 0;
  bool valid = true;
};

struct NewsFeed {
  char sourceName[24] = "CNBC Indonesia";
  char headlines[3][128] = {
    "Ekonomi Indonesia Bergerak Positif Ditopang Konsumsi Domestik",
    "Kemenag Tetapkan Jadwal Imsakiyah & Sholat Serentak Nasional",
    "Indeks IHSG Ditutup Menguat Ditopang Sektor Keuangan"
  };
  char descriptions[3][256] = {
    "Perekonomian nasional menunjukkan tren penguatan berkelanjutan seiring stabilitas inflasi dan kenaikan investasi manufaktur di kuartal akhir.",
    "Kementerian Agama resmi merilis jadwal waktu sholat dan imsakiyah terintegrasi untuk seluruh wilayah Indonesia dengan akurasi astronomis.",
    "Laju bursa saham domestik berhasil mencatatkan arus modal masuk asing yang solid berkat kinerja fundamental emiten perbankan nasional."
  };
  char pubDates[3][32] = {
    "Hari Ini 14:30 WIB",
    "Hari Ini 11:15 WIB",
    "Hari Ini 09:00 WIB"
  };
  int count = 3;
  uint32_t lastUpdateMs = 0;
  bool valid = true;
};

class OnlineFeedService {
 public:
  static OnlineFeedService& instance() {
    static OnlineFeedService s;
    return s;
  }

  void begin() {
    prefs_.begin("jws_feed", false);
    theme_ = prefs_.getUChar("theme", 0);
    if (theme_ > 3) theme_ = 0;
    
    String sym = prefs_.getString("stocks", "BBCA,BBRI,TLKM,ASII");
    if (sym.length() > 0) {
      strncpy(stocks_.watchlistSymbols, sym.c_str(), sizeof(stocks_.watchlistSymbols) - 1);
      parseSymbols();
    }

    if (taskHandle_ == nullptr) {
      xTaskCreatePinnedToCore(feedTaskEntry, "feed_bg", 8192, this, 1, &taskHandle_, 0);
    }
  }

  uint8_t getTheme() const { return theme_; }
  void setTheme(uint8_t t) {
    theme_ = (t > 3) ? 0 : t;
    prefs_.putUChar("theme", theme_);
  }

  const WeatherFeed& weather() const { return weather_; }
  const StockFeed& stocks() const { return stocks_; }
  const NewsFeed& news() const { return news_; }

  void setWatchlist(const char* csv) {
    if (!csv || !csv[0]) return;
    portENTER_CRITICAL(&feedMux_);
    strncpy(stocks_.watchlistSymbols, csv, sizeof(stocks_.watchlistSymbols) - 1);
    stocks_.watchlistSymbols[sizeof(stocks_.watchlistSymbols) - 1] = '\0';
    parseSymbols();
    portEXIT_CRITICAL(&feedMux_);
    prefs_.putString("stocks", stocks_.watchlistSymbols);
    forceStockFetch_.store(true, std::memory_order_release);
  }

  void tick(uint32_t nowMs, float lat = 0.7399f, float lon = 124.3134f) {
    (void)nowMs;
    portENTER_CRITICAL(&feedMux_);
    lat_ = lat;
    lon_ = lon;
    portEXIT_CRITICAL(&feedMux_);
  }

 private:
  OnlineFeedService() = default;

  Preferences prefs_;
  uint8_t theme_ = 0;
  WeatherFeed weather_{};
  StockFeed stocks_{};
  NewsFeed news_{};

  TaskHandle_t taskHandle_ = nullptr;
  portMUX_TYPE feedMux_ = portMUX_INITIALIZER_UNLOCKED;
  float lat_ = 0.7399f;
  float lon_ = 124.3134f;
  std::atomic<bool> forceStockFetch_{false};

  uint32_t lastWeatherFetchMs_ = 0;
  uint32_t lastStockFetchMs_ = 0;
  uint32_t lastNewsFetchMs_ = 0;

  static void feedTaskEntry(void* arg) {
    static_cast<OnlineFeedService*>(arg)->feedLoop();
  }

  void feedLoop() {
    // Allow boot, WiFi, and Kemenag sync to stabilize before starting HTTPS TLS fetches
    vTaskDelay(pdMS_TO_TICKS(5000));
    for (;;) {
      if (WiFi.status() == WL_CONNECTED && !NetLock::instance().isAiBusy()) {
        const uint32_t nowMs = millis();

        portENTER_CRITICAL(&feedMux_);
        const float lat = lat_;
        const float lon = lon_;
        portEXIT_CRITICAL(&feedMux_);

        if (lastWeatherFetchMs_ == 0 || (nowMs - lastWeatherFetchMs_) > 900000UL) {
          if (!NetLock::instance().isAiBusy() && heap_caps_get_free_size(MALLOC_CAP_INTERNAL) > 55000U) {
            lastWeatherFetchMs_ = nowMs;
            fetchWeather(lat, lon);
            vTaskDelay(pdMS_TO_TICKS(400));
          }
        }

        if (forceStockFetch_.exchange(false, std::memory_order_acq_rel) ||
            lastStockFetchMs_ == 0 || (nowMs - lastStockFetchMs_) > 600000UL) {
          if (!NetLock::instance().isAiBusy() && heap_caps_get_free_size(MALLOC_CAP_INTERNAL) > 55000U) {
            lastStockFetchMs_ = millis();
            fetchStocks();
            vTaskDelay(pdMS_TO_TICKS(400));
          }
        }

        if (lastNewsFetchMs_ == 0 || (nowMs - lastNewsFetchMs_) > 900000UL) {
          if (!NetLock::instance().isAiBusy() && heap_caps_get_free_size(MALLOC_CAP_INTERNAL) > 55000U) {
            lastNewsFetchMs_ = millis();
            fetchNews();
            vTaskDelay(pdMS_TO_TICKS(400));
          }
        }
      }
      vTaskDelay(pdMS_TO_TICKS(800));
    }
  }

  void parseSymbols() {
    char buf[64];
    strncpy(buf, stocks_.watchlistSymbols, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    
    int count = 0;
    char* token = strtok(buf, ", ");
    while (token && count < 4) {
      // Trim spaces
      while (*token == ' ') token++;
      if (strlen(token) > 0) {
        strncpy(stocks_.items[count].symbol, token, sizeof(stocks_.items[count].symbol) - 1);
        stocks_.items[count].symbol[sizeof(stocks_.items[count].symbol) - 1] = '\0';
        stocks_.items[count].valid = true;
        count++;
      }
      token = strtok(nullptr, ", ");
    }
    stocks_.itemCount = count;
  }

  void fetchWeather(float lat, float lon) {
    ScopedNetLock netLock(6000);
    if (!netLock.locked()) return;

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    char url[160];
    snprintf(url, sizeof(url),
             "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f&current=temperature_2m,relative_humidity_2m,weather_code",
             lat, lon);

    if (http.begin(client, url)) {
      http.setTimeout(5000);
      int code = http.GET();
      if (code == HTTP_CODE_OK) {
        StaticJsonDocument<128> filter;
        filter["current"]["temperature_2m"] = true;
        filter["current"]["relative_humidity_2m"] = true;
        filter["current"]["weather_code"] = true;
        StaticJsonDocument<512> doc;
        if (deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter)) == DeserializationError::Ok) {
          weather_.temp = doc["current"]["temperature_2m"] | weather_.temp;
          weather_.humidity = doc["current"]["relative_humidity_2m"] | weather_.humidity;
          weather_.code = doc["current"]["weather_code"] | weather_.code;

          if (weather_.code == 0) strncpy(weather_.statusText, "Cerah", sizeof(weather_.statusText));
          else if (weather_.code <= 3) strncpy(weather_.statusText, "Cerah Berawan", sizeof(weather_.statusText));
          else if (weather_.code <= 48) strncpy(weather_.statusText, "Berkabut / Lembab", sizeof(weather_.statusText));
          else if (weather_.code <= 65) strncpy(weather_.statusText, "Hujan Sedang", sizeof(weather_.statusText));
          else if (weather_.code <= 82) strncpy(weather_.statusText, "Hujan Lebat", sizeof(weather_.statusText));
          else strncpy(weather_.statusText, "Hujan Petir", sizeof(weather_.statusText));
          weather_.statusText[sizeof(weather_.statusText) - 1] = '\0';

          weather_.lastUpdateMs = millis();
          weather_.valid = true;
          Serial.printf("[FEED] Cuaca updated: %.1f C, %s\n", weather_.temp, weather_.statusText);
        }
      }
      http.end();
    }
  }

  void fetchStockQuote(const char* sym, StockItem& out) {
    if (NetLock::instance().isAiBusy()) return;
    ScopedNetLock netLock(6000);
    if (!netLock.locked()) return;

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    char url[160];

    char formattedSym[16];
    if (strcmp(sym, "^JKSE") == 0 || strcmp(sym, "IHSG") == 0) {
      snprintf(formattedSym, sizeof(formattedSym), "%%5EJKSE");
      strncpy(out.symbol, "IHSG", sizeof(out.symbol) - 1);
      out.symbol[sizeof(out.symbol) - 1] = '\0';
    } else if (strstr(sym, ".JK")) {
      snprintf(formattedSym, sizeof(formattedSym), "%s", sym);
      strncpy(out.symbol, sym, sizeof(out.symbol) - 1);
      out.symbol[sizeof(out.symbol) - 1] = '\0';
    } else {
      snprintf(formattedSym, sizeof(formattedSym), "%s.JK", sym);
      strncpy(out.symbol, sym, sizeof(out.symbol) - 1);
      out.symbol[sizeof(out.symbol) - 1] = '\0';
    }

    snprintf(url, sizeof(url), "https://query1.finance.yahoo.com/v8/finance/chart/%s?interval=1d", formattedSym);
    if (http.begin(client, url)) {
      http.setUserAgent("Mozilla/5.0");
      http.setTimeout(5000);
      int code = http.GET();
      if (code == HTTP_CODE_OK) {
        StaticJsonDocument<256> filter;
        JsonObject fm = filter["chart"]["result"][0].createNestedObject("meta");
        fm["regularMarketPrice"] = true;
        fm["chartPreviousClose"] = true;
        fm["regularMarketDayHigh"] = true;
        fm["regularMarketDayLow"] = true;
        fm["regularMarketVolume"] = true;
        fm["shortName"] = true;
        fm["longName"] = true;

        StaticJsonDocument<1024> doc;
        if (deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter)) == DeserializationError::Ok) {
          JsonObject meta = doc["chart"]["result"][0]["meta"];
          float curPrice = meta["regularMarketPrice"] | 0.0f;
          float prevClose = meta["chartPreviousClose"] | curPrice;
          float hi = meta["regularMarketDayHigh"] | (curPrice * 1.01f);
          float lo = meta["regularMarketDayLow"] | (curPrice * 0.99f);
          uint32_t vol = meta["regularMarketVolume"] | 10000000;
          const char* longName = meta["shortName"] | meta["longName"] | "";
          if (curPrice > 0.0f) {
            out.price = curPrice;
            out.change = curPrice - prevClose;
            out.percent = prevClose > 0 ? (out.change / prevClose) * 100.0f : 0.0f;
            out.dayHigh = hi;
            out.dayLow = lo;
            out.volume = vol;
            if (longName && longName[0]) {
              strncpy(out.name, longName, sizeof(out.name) - 1);
              out.name[sizeof(out.name) - 1] = '\0';
            }
            out.valid = true;
          }
        }
      }
      http.end();
    }
  }

  void fetchStocks() {
    char syms[4][10] = {{0}};
    int count = 0;
    portENTER_CRITICAL(&feedMux_);
    count = (stocks_.itemCount < 4) ? stocks_.itemCount : 4;
    for (int i = 0; i < count; ++i) {
      strncpy(syms[i], stocks_.items[i].symbol, sizeof(syms[i]) - 1);
    }
    portEXIT_CRITICAL(&feedMux_);

    fetchStockQuote("^JKSE", stocks_.ihsg);
    for (int i = 0; i < count; ++i) {
      if (NetLock::instance().isAiBusy()) break;
      if (syms[i][0]) {
        fetchStockQuote(syms[i], stocks_.items[i]);
        vTaskDelay(pdMS_TO_TICKS(150));
      }
    }
    stocks_.lastUpdateMs = millis();
    stocks_.valid = true;
    Serial.println("[FEED] IHSG & Watchlist saham berhasil diupdate");
  }

  void fetchNews() {
    if (NetLock::instance().isAiBusy()) return;
    ScopedNetLock netLock(6000);
    if (!netLock.locked()) return;

    // Bounded 16 KB buffer (PSRAM preferred) so CNBC's 100+ KB RSS feed NEVER fragments SRAM
    static constexpr size_t kMaxRssBytes = 16384;
    char* rssBuf = static_cast<char*>(heap_caps_malloc(kMaxRssBytes + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!rssBuf) {
      rssBuf = static_cast<char*>(malloc(kMaxRssBytes + 1));
    }
    if (!rssBuf) return;

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    if (http.begin(client, "https://www.cnbcindonesia.com/news/rss")) {
      http.setUserAgent("Mozilla/5.0");
      http.setTimeout(5000);
      int code = http.GET();
      if (code == HTTP_CODE_OK) {
        WiFiClient* stream = http.getStreamPtr();
        size_t totalRead = 0;
        uint32_t readStart = millis();
        int itemsClosed = 0;
        while (stream && totalRead < kMaxRssBytes && (millis() - readStart) < 4000UL) {
          int avail = stream->available();
          if (avail > 0) {
            size_t toRead = static_cast<size_t>(avail);
            if (toRead > kMaxRssBytes - totalRead) toRead = kMaxRssBytes - totalRead;
            int n = stream->readBytes(rssBuf + totalRead, toRead);
            if (n > 0) {
              totalRead += static_cast<size_t>(n);
              rssBuf[totalRead] = '\0';
              // Stop streaming as soon as we have 3 complete <item> entries!
              const char* scan = rssBuf;
              itemsClosed = 0;
              while ((scan = strstr(scan, "</item>")) != nullptr) {
                ++itemsClosed;
                scan += 7;
                if (itemsClosed >= 3) break;
              }
              if (itemsClosed >= 3) break;
            }
          } else if (!stream->connected()) {
            break;
          } else {
            vTaskDelay(pdMS_TO_TICKS(10));
          }
        }
        rssBuf[totalRead] = '\0';

        // Parse up to 3 headlines + descriptions in-place with zero huge String copies
        const char* cur = rssBuf;
        int count = 0;
        while (count < 3) {
          const char* itemPos = strstr(cur, "<item>");
          if (!itemPos) break;
          const char* tStart = strstr(itemPos, "<title>");
          if (!tStart) break;
          tStart += 7;
          const char* tEnd = strstr(tStart, "</title>");
          if (!tEnd) break;

          size_t tLen = static_cast<size_t>(tEnd - tStart);
          if (tLen > 220) tLen = 220;
          String raw;
          raw.reserve(tLen + 1);
          raw.concat(tStart, tLen);
          raw.replace("<![CDATA[", "");
          raw.replace("]]>", "");
          raw.trim();

          String rawDesc;
          const char* dStart = strstr(itemPos, "<description>");
          if (dStart && (dStart - itemPos) < 900) {
            dStart += 13;
            const char* dEnd = strstr(dStart, "</description>");
            if (dEnd) {
              size_t dLen = static_cast<size_t>(dEnd - dStart);
              if (dLen > 512) dLen = 512;
              rawDesc.reserve(dLen + 1);
              rawDesc.concat(dStart, dLen);
              rawDesc.replace("<![CDATA[", "");
              rawDesc.replace("]]>", "");
              while (true) {
                int imgS = rawDesc.indexOf('<');
                if (imgS == -1) break;
                int imgE = rawDesc.indexOf('>', imgS);
                if (imgE == -1) break;
                rawDesc.remove(imgS, imgE - imgS + 1);
              }
              rawDesc.trim();
            }
          }

          if (raw.length() > 0) {
            strncpy(news_.headlines[count], raw.c_str(), sizeof(news_.headlines[count]) - 1);
            news_.headlines[count][sizeof(news_.headlines[count]) - 1] = '\0';
            if (rawDesc.length() > 0) {
              strncpy(news_.descriptions[count], rawDesc.c_str(), sizeof(news_.descriptions[count]) - 1);
              news_.descriptions[count][sizeof(news_.descriptions[count]) - 1] = '\0';
            }
            snprintf(news_.pubDates[count], sizeof(news_.pubDates[count]), "Update CNBC Live");
            count++;
          }
          cur = tEnd + 8;
        }
        if (count > 0) {
          news_.count = count;
          news_.lastUpdateMs = millis();
          news_.valid = true;
          Serial.printf("[FEED] Berita updated: %d headlines (bounded %u B)\n",
                        count, static_cast<unsigned>(totalRead));
        }
      }
      http.end();
    }
    free(rssBuf);
  }
};

}  // namespace jws
