#pragma once

#include <Arduino.h>
#include <AutoConnect.h>
#include <WebServer.h>

#include "ShelfClockRuntime.h"

namespace shelfclock {
namespace standalone {

static const int kWifiMaxRetries = 100;
static const unsigned long kWifiMaxRetryDuration = 600000;
static const unsigned long kWifiRetryIntervalMs = 6000;

struct NetworkStatus {
  unsigned long startTime = 0;
  unsigned long elapsedTime = 0;
  unsigned long lastRetryAttempt = 0;
  int retryCount = 0;
  int totalReconnections = 0;
};

class StandaloneHostServices final : public runtime::HostServices {
 public:
  bool readLocalTime(struct tm &value) override;
  void configureNetworkTime(long gmtOffsetSeconds, int daylightOffsetSeconds,
                            const char *server) override;
  bool networkConnected() const override;
  void formatIpAddress(char *destination, size_t destinationSize) const override;
  void formatNetworkName(char *destination, size_t destinationSize) const override;
  bool beginRtc() override;
  bool readRtc(struct tm &value) override;
  bool rtcLostPower() override;
  void writeRtc(const struct tm &value) override;
  void beginEnvironmentSensor() override;
  runtime::EnvironmentReading readEnvironment() override;
  void cooperateDuringSetup() override;
  void requestNetworkRecovery() override;
};

extern StandaloneHostServices hostServices;

bool beginNetwork(AutoConnect &portal, const char *hostname, NetworkStatus &status);
void serviceNetwork(WebServer &server, AutoConnect &portal, NetworkStatus &status);

}  // namespace standalone
}  // namespace shelfclock
