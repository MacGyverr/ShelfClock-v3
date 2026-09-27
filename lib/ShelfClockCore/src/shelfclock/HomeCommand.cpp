#include "HomeCommand.h"

namespace shelfclock {

const char *presetStorageName(int presetNumber) {
  static const char *const names[] = {"preset1", "preset2", "preset3", "preset4"};
  if (presetNumber < 1 || presetNumber > 4) {
    return nullptr;
  }
  return names[presetNumber - 1];
}

}  // namespace shelfclock
