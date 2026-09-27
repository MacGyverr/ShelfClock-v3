#include "ScheduleTime.h"

namespace shelfclock {

bool scheduleTimeMatches(int scheduledHour, int scheduledMinute, int currentHour, int currentMinute) {
  return (scheduledHour == currentHour && scheduledMinute == currentMinute) ||
         (scheduledHour == 99 && scheduledMinute == currentMinute) ||
         (scheduledHour == currentHour && scheduledMinute == 99);
}

}  // namespace shelfclock
