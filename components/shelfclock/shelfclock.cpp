#include "shelfclock.h"

#include "esphome/components/network/util.h"
#include "esphome/components/wifi/wifi_component.h"
#include "esphome/core/application.h"
#include "esphome/core/log.h"

#include <math.h>
#include <stdio.h>

namespace esphome {
namespace shelfclock_component {

static const char *const TAG = "shelfclock";
static const char *const MODE_NAMES[] = {
    "Clock", "Countdown", "Temperature", "Scoreboard", "Stopwatch", "Lightshow",
    nullptr, "Date", "Humidity", "Spectrum", "Display Off", "Scrolling"};
static const char *const LIGHTSHOW_NAMES[] = {
    "Chase", "Twinkles", "Rainbow", "Matrix", "Rain", "Fire", "Snake", "Cylon",
    "Orbit", "Figure-eight"};
static const char *const SPECTRUM_NAMES[] = {
    "BM Up",       "CM Out",       "BL to TR",   "TL to BR",    "Vertical B",
    "TM Down",     "CS In",        "BR to TL",   "TR to BL",    "Horizontal L",
    "Vertical T",  "Horizontal R", "Rainbow",    "Outrun Peak", "Purple",
    "Center",      "Changing",     "Fireplace",  "Waterfall",   "Auto cycle"};

template<size_t N> static int option_index(const std::string &name, const char *const (&options)[N]) {
  for (size_t index = 0; index < N; index++) {
    if (name == options[index]) {
      return static_cast<int>(index);
    }
  }
  return -1;
}

void ShelfClockComponent::setup() {
  ESP_LOGI(TAG, "Starting shared ShelfClock runtime");
  shelfclock::runtime::setupHosted(*this);
}

void ShelfClockComponent::loop() {
  shelfclock::runtime::serviceHosted();
}

float ShelfClockComponent::get_setup_priority() const {
  return setup_priority::LATE;
}

bool ShelfClockComponent::apply_(shelfclock::HomeCommandType type, int primary, int secondary) {
  return shelfclock::runtime::applyHostedHomeCommand(
             shelfclock::HomeCommand(type, primary, secondary))
      .accepted;
}

const char *ShelfClockComponent::mode_name() const {
  const shelfclock::HomeState state = shelfclock::getHomeState();
  if (state.clockMode < (sizeof(MODE_NAMES) / sizeof(MODE_NAMES[0])) &&
      MODE_NAMES[state.clockMode] != nullptr) {
    return MODE_NAMES[state.clockMode];
  }
  return "Clock";
}

const char *ShelfClockComponent::lightshow_name() const {
  const int mode = shelfclock::getHomeState().lightshowMode;
  if (mode >= 0 && mode < static_cast<int>(sizeof(LIGHTSHOW_NAMES) / sizeof(LIGHTSHOW_NAMES[0]))) {
    return LIGHTSHOW_NAMES[mode];
  }
  return LIGHTSHOW_NAMES[0];
}

const char *ShelfClockComponent::spectrum_name() const {
  const int mode = shelfclock::getHomeState().spectrumMode;
  if (mode >= 0 && mode < static_cast<int>(sizeof(SPECTRUM_NAMES) / sizeof(SPECTRUM_NAMES[0]))) {
    return SPECTRUM_NAMES[mode];
  }
  return SPECTRUM_NAMES[0];
}

int ShelfClockComponent::scoreboard_left() const {
  return shelfclock::getHomeState().scoreboardLeft;
}

int ShelfClockComponent::scoreboard_right() const {
  return shelfclock::getHomeState().scoreboardRight;
}

void ShelfClockComponent::select_mode(const std::string &name) {
  static const shelfclock::HomeCommandType commands[] = {
      shelfclock::HomeCommandType::Clock,       shelfclock::HomeCommandType::Countdown,
      shelfclock::HomeCommandType::Temperature, shelfclock::HomeCommandType::Scoreboard,
      shelfclock::HomeCommandType::Stopwatch,   shelfclock::HomeCommandType::Lightshow,
      shelfclock::HomeCommandType::Clock,       shelfclock::HomeCommandType::Date,
      shelfclock::HomeCommandType::Humidity,    shelfclock::HomeCommandType::Spectrum,
      shelfclock::HomeCommandType::DisplayOff,  shelfclock::HomeCommandType::Scrolling};
  for (size_t index = 0; index < sizeof(MODE_NAMES) / sizeof(MODE_NAMES[0]); index++) {
    if (MODE_NAMES[index] != nullptr && name == MODE_NAMES[index]) {
      const int default_timer_ms =
          (commands[index] == shelfclock::HomeCommandType::Countdown ||
           commands[index] == shelfclock::HomeCommandType::Stopwatch)
              ? 60000
              : 0;
      this->apply_(commands[index], default_timer_ms);
      return;
    }
  }
}

void ShelfClockComponent::select_lightshow(const std::string &name) {
  const int mode = option_index(name, LIGHTSHOW_NAMES);
  if (mode >= 0) {
    this->apply_(shelfclock::HomeCommandType::Lightshow, mode);
  }
}

void ShelfClockComponent::select_spectrum(const std::string &name) {
  const int mode = option_index(name, SPECTRUM_NAMES);
  if (mode >= 0) {
    this->apply_(shelfclock::HomeCommandType::Spectrum, mode);
  }
}

void ShelfClockComponent::set_scoreboard_left(int value) {
  this->apply_(shelfclock::HomeCommandType::Scoreboard, value, this->scoreboard_right());
}

void ShelfClockComponent::set_scoreboard_right(int value) {
  this->apply_(shelfclock::HomeCommandType::Scoreboard, this->scoreboard_left(), value);
}

void ShelfClockComponent::start_countdown(int seconds) {
  this->apply_(shelfclock::HomeCommandType::Countdown, seconds * 1000);
}

void ShelfClockComponent::start_stopwatch(int seconds) {
  this->apply_(shelfclock::HomeCommandType::Stopwatch, seconds * 1000);
}

void ShelfClockComponent::save_preset(int preset) {
  this->apply_(shelfclock::HomeCommandType::SavePreset, preset);
}

void ShelfClockComponent::load_preset(int preset) {
  this->apply_(shelfclock::HomeCommandType::LoadPreset, preset);
}

void ShelfClockComponent::start_network_recovery() {
  shelfclock::runtime::requestNetworkRecovery();
}

bool ShelfClockComponent::readLocalTime(struct tm &value) {
  if (this->time_source_ == nullptr) {
    return false;
  }
  ESPTime now = this->time_source_->now();
  if (!now.is_valid()) {
    return false;
  }
  value = now.to_c_tm();
  return true;
}

void ShelfClockComponent::configureNetworkTime(long gmt_offset_seconds,
                                               int daylight_offset_seconds,
                                               const char *server) {
  (void) gmt_offset_seconds;
  (void) daylight_offset_seconds;
  (void) server;
}

bool ShelfClockComponent::networkConnected() const {
  return network::is_connected();
}

void ShelfClockComponent::formatIpAddress(char *destination, size_t destination_size) const {
  if (destination_size == 0) {
    return;
  }
  destination[0] = '\0';
  for (const network::IPAddress &address : network::get_ip_addresses()) {
    if (address.is_set() && address.is_ip4()) {
      char buffer[network::IP_ADDRESS_BUFFER_SIZE] = {};
      address.str_to(buffer);
      snprintf(destination, destination_size, "%s", buffer);
      return;
    }
  }
}

bool ShelfClockComponent::beginRtc() {
  return this->time_source_ != nullptr;
}

bool ShelfClockComponent::readRtc(struct tm &value) {
  return this->readLocalTime(value);
}

bool ShelfClockComponent::rtcLostPower() {
  return false;
}

void ShelfClockComponent::writeRtc(const struct tm &value) {
  (void) value;
}

void ShelfClockComponent::beginEnvironmentSensor() {}

shelfclock::runtime::EnvironmentReading ShelfClockComponent::readEnvironment() {
  const float temperature =
      this->temperature_sensor_ != nullptr && this->temperature_sensor_->has_state()
          ? this->temperature_sensor_->state
          : NAN;
  const float humidity = this->humidity_sensor_ != nullptr && this->humidity_sensor_->has_state()
                             ? this->humidity_sensor_->state
                             : NAN;
  return {humidity, temperature, temperature * 1.8F + 32.0F};
}

void ShelfClockComponent::cooperateDuringSetup() {
  App.feed_wdt();
}

void ShelfClockComponent::requestNetworkRecovery() {
  wifi::global_wifi_component->save_wifi_sta("", "");
  App.safe_reboot();
}

}  // namespace shelfclock_component
}  // namespace esphome
