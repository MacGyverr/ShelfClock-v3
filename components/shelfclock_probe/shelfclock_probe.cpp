#include "shelfclock_probe.h"
#include "esphome/core/log.h"
#include "shelfclock/HomeCommand.h"
#include "shelfclock/ScheduleTime.h"
#include "shelfclock/ScrollSequence.h"

#if SHELFCLOCK_PROBE_GROUP
void shelfclockDependencyProbe();
#endif

namespace esphome {
namespace shelfclock_probe {

void ShelfClockProbe::setup() {
  const bool matches = shelfclock::scheduleTimeMatches(12, 5, 12, 5);
  const shelfclock::HomeCommand command(shelfclock::HomeCommandType::Clock);
  const char *preset = shelfclock::presetStorageName(1);
  shelfclock::ScrollSequence scroll;
  scroll.begin("ShelfClock", 0);
  const unsigned firstGlyph = scroll.glyph(0);
  ESP_LOGI("shelfclock_probe", "Shared core linked: %s, command: %u, preset: %s, glyph: %u",
           matches ? "yes" : "no", static_cast<unsigned>(command.type), preset, firstGlyph);
#if SHELFCLOCK_PROBE_GROUP
  // Retain linked dependency references without touching hardware or flash.
  void (*volatile entry)() = shelfclockDependencyProbe;
  (void) entry;
#endif
}

}  // namespace shelfclock_probe
}  // namespace esphome
