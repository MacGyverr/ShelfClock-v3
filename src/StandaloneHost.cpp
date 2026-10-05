#include "StandaloneHost.h"
#include "ShelfClockConfig.h"

#include <WiFi.h>
#include <AutoConnectCredential.h>

#include <math.h>
#include <stdlib.h>
#include <time.h>

#if HAS_RTC
#include <RTClib.h>
#endif

#if HAS_DHT
#include <DHT.h>
#endif

namespace shelfclock {
namespace standalone {

StandaloneHostServices hostServices;

#if SHELFCLOCK_ISOLATION
static char isolationSsid[33] = "ShelfClock";
static const char isolationPassword[] = "shelfclock";
#endif

#if HAS_RTC
static RTC_DS3231 rtc;
#endif

#if HAS_DHT
static DHT dht(DHT_PIN, DHTTYPE);
#endif

bool StandaloneHostServices::readLocalTime(struct tm &value) {
  return getLocalTime(&value);
}

void StandaloneHostServices::configureNetworkTime(long gmtOffsetSeconds,
                                                  int daylightOffsetSeconds,
                                                  const char *server) {
#if SHELFCLOCK_ISOLATION
  (void)gmtOffsetSeconds;
  (void)daylightOffsetSeconds;
  (void)server;
  // The offline ESP32 clock stores the same local wall time as the DS3231.
  setenv("TZ", "UTC0", 1);
  tzset();
#else
  configTime(gmtOffsetSeconds, daylightOffsetSeconds, server);
#endif
}

bool StandaloneHostServices::networkConnected() const {
#if SHELFCLOCK_ISOLATION
  const wifi_mode_t mode = WiFi.getMode();
  return mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA;
#else
  return WiFi.status() == WL_CONNECTED;
#endif
}

void StandaloneHostServices::formatIpAddress(char *destination, size_t destinationSize) const {
  if (destinationSize == 0) {
    return;
  }
#if SHELFCLOCK_ISOLATION
  snprintf(destination, destinationSize, "%s", WiFi.softAPIP().toString().c_str());
#else
  snprintf(destination, destinationSize, "%s", WiFi.localIP().toString().c_str());
#endif
}

void StandaloneHostServices::formatNetworkName(char *destination, size_t destinationSize) const {
  if (destinationSize == 0) {
    return;
  }
#if SHELFCLOCK_ISOLATION
  snprintf(destination, destinationSize, "%s", isolationSsid);
#else
  snprintf(destination, destinationSize, "%s", WiFi.SSID().c_str());
#endif
}

bool StandaloneHostServices::beginRtc() {
#if HAS_RTC
  return rtc.begin();
#else
  return false;
#endif
}

bool StandaloneHostServices::readRtc(struct tm &value) {
#if HAS_RTC
  const DateTime now = rtc.now();
  value = {};
  value.tm_year = now.year() - 1900;
  value.tm_mon = now.month() - 1;
  value.tm_mday = now.day();
  value.tm_hour = now.hour();
  value.tm_min = now.minute();
  value.tm_sec = now.second();
  value.tm_wday = now.dayOfTheWeek();
  value.tm_isdst = -1;
  return true;
#else
  (void)value;
  return false;
#endif
}

bool StandaloneHostServices::rtcLostPower() {
#if HAS_RTC
  return rtc.lostPower();
#else
  return true;
#endif
}

void StandaloneHostServices::writeRtc(const struct tm &value) {
#if HAS_RTC
  rtc.adjust(DateTime(value.tm_year + 1900, value.tm_mon + 1, value.tm_mday,
                      value.tm_hour, value.tm_min, value.tm_sec));
#else
  (void)value;
#endif
}

void StandaloneHostServices::beginEnvironmentSensor() {
#if HAS_DHT
  dht.begin();
#endif
}

runtime::EnvironmentReading StandaloneHostServices::readEnvironment() {
#if HAS_DHT
  return {dht.readHumidity(), dht.readTemperature(), dht.readTemperature(true)};
#else
  return {NAN, NAN, NAN};
#endif
}

void StandaloneHostServices::cooperateDuringSetup() {}

void StandaloneHostServices::requestNetworkRecovery() {
#if SHELFCLOCK_ISOLATION
  ESP.restart();
#else
  AutoConnectCredential credentials;
  station_config_t saved = {};
  while (credentials.entries() > 0 && credentials.load(static_cast<int8_t>(0), &saved)) {
    if (!credentials.del(reinterpret_cast<const char *>(saved.ssid))) {
      Serial.println("Unable to remove a saved Wi-Fi credential");
      break;
    }
    saved = {};
  }
  WiFi.disconnect(true, true);
  delay(100);
  ESP.restart();
#endif
}

bool beginNetwork(AutoConnect &portal, const char *hostname, NetworkStatus &status) {
#if SHELFCLOCK_ISOLATION
  (void) portal;
  (void) hostname;
  const uint32_t suffix = static_cast<uint32_t>(ESP.getEfuseMac() & 0xFFFFFFULL);
  snprintf(isolationSsid, sizeof(isolationSsid), "ShelfClock-%06X", suffix);
  WiFi.mode(WIFI_AP);
  const IPAddress isolationAddress(10, 10, 10, 10);
  const IPAddress isolationSubnet(255, 255, 255, 0);
  if (!WiFi.softAPConfig(isolationAddress, isolationAddress, isolationSubnet)) {
    Serial.println("Could not configure the isolation access point address");
    return false;
  }
  if (!WiFi.softAP(isolationSsid, isolationPassword)) {
    return false;
  }
  status.startTime = millis();
  status.lastRetryAttempt = millis();
  status.retryCount = 0;
  Serial.printf("Isolation access point: %s\n", isolationSsid);
  Serial.printf("Isolation address: %s\n", WiFi.softAPIP().toString().c_str());
  return true;
#else
  AutoConnectConfig config;
  config.autoReconnect = true;
  config.portalTimeout = 20000;
  config.retainPortal = true;
  config.autoRise = true;
  config.reconnectInterval = 6;
  portal.config(config);

  WiFi.hostname(hostname);
  if (!portal.begin()) {
    return false;
  }

  status.startTime = millis();
  status.lastRetryAttempt = millis();
  status.retryCount = 0;
  return true;
#endif
}

void serviceNetwork(WebServer &server, AutoConnect &portal, NetworkStatus &status) {
  server.handleClient();
#if SHELFCLOCK_ISOLATION
  (void) portal;
  (void) status;
  return;
#else
  portal.handleRequest();

  if (WiFi.status() != WL_CONNECTED) {
    status.elapsedTime = millis() - status.startTime;
    if (status.elapsedTime >= kWifiMaxRetryDuration) {
      Serial.println("maximum retry duration exceeded ");
      ESP.restart();
      return;
    }

    if (status.retryCount >= kWifiMaxRetries) {
      Serial.println("maximum retry attemps exceeded ");
      ESP.restart();
      return;
    }

    if ((millis() - status.lastRetryAttempt) >= kWifiRetryIntervalMs) {
      const wl_status_t wifiState = WiFi.status();
      if (wifiState == WL_IDLE_STATUS || wifiState == WL_DISCONNECTED ||
          wifiState == WL_CONNECTION_LOST || wifiState == WL_NO_SSID_AVAIL ||
          wifiState == WL_CONNECT_FAILED) {
        WiFi.disconnect();
        WiFi.begin();
        status.retryCount++;
        status.totalReconnections++;
        status.lastRetryAttempt = millis();
        Serial.print("Retry ");
        Serial.print(status.retryCount);
        Serial.println(" - Reconnecting...");
      }
    }
    return;
  }

  status.retryCount = 0;
  status.startTime = millis();
  status.lastRetryAttempt = millis();
#endif
}

}  // namespace standalone
}  // namespace shelfclock
