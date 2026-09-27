#pragma once

#include <stdint.h>

namespace shelfclock {

enum class HomeCommandType : uint8_t {
  Clock,
  Date,
  Temperature,
  Humidity,
  Scrolling,
  DisplayOff,
  Countdown,
  Stopwatch,
  Scoreboard,
  Lightshow,
  Spectrum,
  SavePreset,
  LoadPreset,
};

struct HomeCommand {
  HomeCommandType type;
  int primary;
  int secondary;

  HomeCommand(HomeCommandType commandType, int primaryValue = 0, int secondaryValue = 0)
      : type(commandType), primary(primaryValue), secondary(secondaryValue) {}
};

struct HomeCommandResult {
  bool accepted = false;
  bool saveSettingsNow = false;
};

struct HomeState {
  uint8_t clockMode = 0;
  int realtimeMode = 0;
  int scoreboardLeft = 0;
  int scoreboardRight = 0;
  int lightshowMode = 0;
  int spectrumMode = 0;
};

// Implemented by the active runtime adapter. Both the web and ESPHome
// adapters submit transport-neutral commands through this entry point.
HomeCommandResult applyHomeCommand(const HomeCommand &command);
HomeState getHomeState();

// Presets are deliberately limited to the four existing storage names.
const char *presetStorageName(int presetNumber);

}  // namespace shelfclock
