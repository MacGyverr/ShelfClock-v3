#pragma once

#include "esphome/components/sensor/sensor.h"
#include "esphome/components/time/real_time_clock.h"
#include "esphome/core/component.h"

#include "ShelfClockRuntime.h"
#include "shelfclock/HomeCommand.h"

namespace esphome {
namespace shelfclock_component {

class ShelfClockComponent final : public Component, public shelfclock::runtime::HostServices {
 public:
  void set_time_source(time::RealTimeClock *time_source) { this->time_source_ = time_source; }
  void set_temperature_sensor(sensor::Sensor *sensor) { this->temperature_sensor_ = sensor; }
  void set_humidity_sensor(sensor::Sensor *sensor) { this->humidity_sensor_ = sensor; }

  void setup() override;
  void loop() override;
  float get_setup_priority() const override;

  const char *mode_name() const;
  const char *lightshow_name() const;
  const char *spectrum_name() const;
  int scoreboard_left() const;
  int scoreboard_right() const;

  void select_mode(const std::string &name);
  void select_lightshow(const std::string &name);
  void select_spectrum(const std::string &name);
  void set_scoreboard_left(int value);
  void set_scoreboard_right(int value);
  void start_countdown(int seconds);
  void start_stopwatch(int seconds);
  void save_preset(int preset);
  void load_preset(int preset);
  void start_network_recovery();

  bool readLocalTime(struct tm &value) override;
  void configureNetworkTime(long gmt_offset_seconds, int daylight_offset_seconds,
                            const char *server) override;
  bool networkConnected() const override;
  void formatIpAddress(char *destination, size_t destination_size) const override;
  void formatNetworkName(char *destination, size_t destination_size) const override;
  bool beginRtc() override;
  bool readRtc(struct tm &value) override;
  bool rtcLostPower() override;
  void writeRtc(const struct tm &value) override;
  void beginEnvironmentSensor() override;
  shelfclock::runtime::EnvironmentReading readEnvironment() override;
  void cooperateDuringSetup() override;
  void requestNetworkRecovery() override;

 private:
  bool apply_(shelfclock::HomeCommandType type, int primary = 0, int secondary = 0);

  time::RealTimeClock *time_source_{nullptr};
  sensor::Sensor *temperature_sensor_{nullptr};
  sensor::Sensor *humidity_sensor_{nullptr};
};

}  // namespace shelfclock_component
}  // namespace esphome
