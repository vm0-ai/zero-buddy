#include "zero_buddy_power.h"

#include <algorithm>
#include <cstdlib>

namespace zero_buddy {
namespace power {
namespace {

bool vbusChanged(int32_t previous_mv, int32_t current_mv) {
  if (previous_mv == current_mv) {
    return false;
  }
  if (previous_mv <= 0 || current_mv <= 0) {
    return true;
  }
  return std::abs(current_mv - previous_mv) > 100;
}

}  // namespace

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
  events.vbus_changed = vbusChanged(previous->vbus_mv, current.vbus_mv);
  return events;
}

}  // namespace power
}  // namespace zero_buddy
