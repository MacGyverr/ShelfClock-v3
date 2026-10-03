#pragma once

namespace shelfclock {

// For hour/minute matching only; other schedule sentinels stay with the caller.
bool scheduleTimeMatches(int scheduledHour, int scheduledMinute, int currentHour, int currentMinute);

}  // namespace shelfclock
