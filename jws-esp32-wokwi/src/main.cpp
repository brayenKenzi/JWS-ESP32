// SPDX-License-Identifier: GPL-3.0-only
#include <Arduino.h>
#include <WiFi.h>
#include <JwsApp.h>
#include <Simulation.h>
#include "AppConfig.h"
#include "SimulationDrivers.h"
#include "HdWf2Hub75Driver.h"
#include "ConfigurationStorage.h"
#include "KemenagService.h"
#include "NetLock.h"
#include "SinricProService.h"
#include "ChronosService.h"
#include "WebController.h"

#if !defined(JWS_SIMULATION) && !defined(JWS_HD_WF2_HUB75)
#error "Specify either JWS_SIMULATION or JWS_HD_WF2_HUB75 target."
#endif

namespace {
PersistentConfigurationStore store(defaultConfiguration());
KemenagService timetable;
#ifdef JWS_HD_WF2_HUB75
HdWf2Hub75Driver display;
#else
Ili9341DisplayDriver display;
#endif
SerialMockAudio audio;
#ifdef JWS_MOCK_CLOCK
jws::MockClockProvider clockProvider;
#else
NtpClockProvider clockProvider;
#endif
jws::JwsApp app(clockProvider, display, store, &timetable);
WebController web(app, timetable);
bool ready = false;
uint32_t lastLogMs = 0;
uint32_t lastHealthLogMs = 0;
uint32_t tableRevision = 0;
uint32_t lastWifiRetryMs = 0;
bool hasSavedWifi = false;
bool networkAnnounced = false;
}
void setup() {
  Serial.begin(115200);
  Serial.println("[BOOT] JWS Kotamobagu | Kemenag | HD-WF2 & Simulator");
  Serial.printf("[MEM] Reset=%s | InternalHeap=%uKB | PSRAM=%uKB/%uKB\n",
                jws::resetReasonText(esp_reset_reason()),
                static_cast<unsigned>(ESP.getFreeHeap() / 1024U),
                static_cast<unsigned>(ESP.getFreePsram() / 1024U),
                static_cast<unsigned>(ESP.getPsramSize() / 1024U));
  if (!store.begin()) Serial.println("[STORE] NVS unavailable; dashboard saves may fail");
  if (!timetable.begin()) Serial.println("[KEMENAG] Online sync unavailable; using valid built-in tables when available");

  // Aktifkan Wi-Fi Mode AP + STA agar selalu dapat diakses langsung via HP/Laptop
  // (Modem sleep wajib aktif agar Wi-Fi & Bluetooth BLE Chronos dapat berjalan bersamaan di antena 2.4 GHz)
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP("JWS-KOTAMOBAGU", "12345678");
  Serial.printf("[WIFI] Access Point Aktif: SSID='JWS-KOTAMOBAGU' Key='12345678' IP=%s\n",
                WiFi.softAPIP().toString().c_str());

#ifdef JWS_MOCK_CLOCK
  // Mock time is offline, but the simulated Wi-Fi still serves the dashboard.
  WiFi.begin("Wokwi-GUEST", "", 6);
#else
  web.setTimeHandler([](time_t epoch) {
    clockProvider.setEpochTime(epoch);
  });

  // Sambungkan ke WiFi rumah jika sudah pernah disimpan
  Preferences netPrefs;
  netPrefs.begin("jws_net", true);
  String savedSsid = netPrefs.getString("ssid", "");
  String savedPass = netPrefs.getString("pass", "");
  netPrefs.end();

  if (savedSsid.length() > 0) {
    hasSavedWifi = true;
    Serial.printf("[WIFI] Menyambung ke WiFi tersimpan: '%s'...\n", savedSsid.c_str());
    WiFi.begin(savedSsid.c_str(), savedPass.c_str());
  }

  web.setWifiHandler([](const String& ssid, const String& pass) {
    Preferences p;
    p.begin("jws_net", false);
    p.putString("ssid", ssid);
    p.putString("pass", pass);
    p.end();
    hasSavedWifi = (ssid.length() > 0);
    Serial.printf("[WIFI] Konfigurasi baru disimpan: SSID='%s'. Menyambungkan...\n", ssid.c_str());
    WiFi.begin(ssid.c_str(), pass.c_str());
  });
#endif
  ready = app.begin();
  if (!ready) Serial.println("[ERROR] Configuration or display initialization failed");
  if (ready && !web.begin()) Serial.println("[ERROR] Dashboard initialization failed");
  jws::SinricProService::instance().begin();
#ifndef JWS_MOCK_CLOCK
  jws::ChronosService::instance().setTimeSyncHandler([](time_t epoch) {
    clockProvider.setEpochTime(epoch);
  });
#endif
  jws::ChronosService::instance().begin();
  Serial.printf("[WEB] Dashboard siap pada http://%s (AP Langsung) atau http://localhost:8180 (Wokwi)\n",
                WiFi.softAPIP().toString().c_str());
}
void loop() {
  const uint32_t nowMs = millis();
  bool changed = ready && app.tick(nowMs);
  if (ready) {
    timetable.tick(app.frame().clock, nowMs);
    if (timetable.revision() != tableRevision) {
      tableRevision = timetable.revision();
      app.invalidateSchedule();
      changed = app.tick(nowMs) || changed;
    }
    web.tick();
  }
  jws::SinricProService::instance().tick();
  jws::ChronosService::instance().tick();
  if (changed) {
    const auto& frame = app.frame();
    Serial.printf("[SCHEDULE] KEMENAG KOTA KOTAMOBAGU %04d-%02d-%02d valid=%d config=%lu\n",
                  frame.schedule.date.year, frame.schedule.date.month, frame.schedule.date.day,
                  frame.schedule.valid, static_cast<unsigned long>(app.configuration().revision));
    for (int i = 0; i < jws::PrayerCount; ++i) {
      char base[6], finalTime[6];
      jws::formatMinute(frame.baseSchedule.minutes[i], base);
      jws::formatMinute(frame.schedule.valid ? frame.schedule.minutes[i] : -1, finalTime);
      Serial.printf("  %s base=%s correction=%d final=%s\n", jws::prayerName(static_cast<jws::Prayer>(i)),
                    base, app.configuration().corrections[i], finalTime);
    }
  }
  if (WiFi.status() == WL_CONNECTED && !networkAnnounced) {
    networkAnnounced = true;
    Serial.printf("[WEB] WiFi connected; device IP=%s\n", WiFi.localIP().toString().c_str());
  } else if (WiFi.status() != WL_CONNECTED) networkAnnounced = false;
#ifdef JWS_MOCK_CLOCK
  if (WiFi.status() != WL_CONNECTED && static_cast<uint32_t>(nowMs - lastWifiRetryMs) >= 15000) {
    lastWifiRetryMs = nowMs;
    WiFi.reconnect();
  }
#else
  if (hasSavedWifi && WiFi.status() != WL_CONNECTED && static_cast<uint32_t>(nowMs - lastWifiRetryMs) >= 30000) {
    lastWifiRetryMs = nowMs;
    WiFi.reconnect();
  }
#endif
  audio.tick(nowMs);
  if (ready && static_cast<uint32_t>(nowMs - lastLogMs) >= 1000) {
    lastLogMs = nowMs;
    const auto& reading = app.frame().clock;
    Serial.printf("[TIME] %02d:%02d:%02d valid=%d source=%d\n", reading.hour, reading.minute,
                  reading.second, reading.valid, static_cast<int>(reading.source));
  }
  if (static_cast<uint32_t>(nowMs - lastHealthLogMs) >= 30000) {
    lastHealthLogMs = nowMs;
    Serial.printf("[HEALTH] heap=%uKB minHeap=%uKB psram=%uKB rssi=%ddBm up=%lus\n",
                  static_cast<unsigned>(ESP.getFreeHeap() / 1024U),
                  static_cast<unsigned>(ESP.getMinFreeHeap() / 1024U),
                  static_cast<unsigned>(ESP.getFreePsram() / 1024U),
                  (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : 0,
                  static_cast<unsigned long>(nowMs / 1000UL));
  }
  yield();
}
