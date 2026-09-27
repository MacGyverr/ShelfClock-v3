#include "StandaloneHost.h"
#include "ShelfClockConfig.h"

#include <WiFi.h>
#include <AutoConnectCredential.h>

#include <math.h>
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
  configTime(gmtOffsetSeconds, daylightOffsetSeconds, server);
}

bool StandaloneHostServices::networkConnected() const {
  return WiFi.status() == WL_CONNECTED;
}

void StandaloneHostServices::formatIpAddress(char *destination, size_t destinationSize) const {
  if (destinationSize == 0) {
    return;
  }
  snprintf(destination, destinationSize, "%s", WiFi.localIP().toString().c_str());
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
}

bool beginNetwork(AutoConnect &portal, const char *hostname, NetworkStatus &status) {
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
}

void serviceNetwork(WebServer &server, AutoConnect &portal, NetworkStatus &status) {
  server.handleClient();
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
}

}  // namespace standalone
}  // namespace shelfclock
