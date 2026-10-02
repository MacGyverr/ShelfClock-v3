#pragma once

#include <stddef.h>
#include <time.h>

namespace shelfclock {
struct HomeCommand;
struct HomeCommandResult;

namespace runtime {

struct EnvironmentReading {
  float humidity;
  float temperatureC;
  float temperatureF;
};

class HostServices {
 public:
  virtual ~HostServices() = default;
  virtual bool readLocalTime(struct tm &value) = 0;
  virtual void configureNetworkTime(long gmtOffsetSeconds, int daylightOffsetSeconds,
                                    const char *server) = 0;
  virtual bool networkConnected() const = 0;
  virtual void formatIpAddress(char *destination, size_t destinationSize) const = 0;
  virtual void formatNetworkName(char *destination, size_t destinationSize) const = 0;
  virtual bool beginRtc() = 0;
  virtual bool readRtc(struct tm &value) = 0;
  virtual bool rtcLostPower() = 0;
  virtual void writeRtc(const struct tm &value) = 0;
  virtual void beginEnvironmentSensor() = 0;
  virtual EnvironmentReading readEnvironment() = 0;
  virtual void cooperateDuringSetup() = 0;
  virtual void requestNetworkRecovery() = 0;
};

void setHostServices(HostServices &services);
void setupStandalone();
void setupHosted(HostServices &services);
HomeCommandResult applyHostedHomeCommand(const HomeCommand &command);
void requestNetworkRecovery();
void serviceClock();
void serviceStandalone();
void serviceHosted();

}  // namespace runtime
}  // namespace shelfclock
