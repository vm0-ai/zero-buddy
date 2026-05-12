#include "zero_buddy_power.h"

#include <algorithm>

namespace zero_buddy {
namespace power {

uint8_t batteryBarsForPercent(int32_t percent) {
  if (percent < 0) {
    return 0;
  }
  const int32_t clamped = std::max<int32_t>(0, std::min<int32_t>(100, percent));
  if (clamped == 0) {
    return 0;
  }
  if (clamped <= 33) {
    return 1;
  }
  if (clamped <= 66) {
    return 2;
  }
  return 3;
}

PowerEvents detectPowerEvents(const PowerSnapshot* previous,
                              const PowerSnapshot& current) {
  PowerEvents events;
  if (previous == nullptr) {
    events.initialized = true;
    return events;
  }

  events.charge_state_changed = previous->charge_state != current.charge_state;
  events.battery_percent_changed =
      previous->battery_percent != current.battery_percent;
  return events;
}

}  // namespace power
}  // namespace zero_buddy
