#include <Arduino.h>

#include "ShelfClockRuntime.h"

void setup() {
  shelfclock::runtime::setupStandalone();
}

void loop() {
  shelfclock::runtime::serviceStandalone();
}
