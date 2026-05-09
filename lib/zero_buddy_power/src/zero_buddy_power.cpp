#include "zero_buddy_power.h"

#include <algorithm>

namespace zero_buddy {
namespace power {

bool shouldShowBatteryPercent(const PowerSnapshot& snapshot) {
  return snapshot.charge_state == ChargeState::Charging ||
         snapshot.external_power;
}

bool isLowBattery(const PowerSnapshot& snapshot, int32_t threshold_percent) {
  if (snapshot.battery_percent < 0) {
    return false;
  }
  return snapshot.battery_percent <= threshold_percent &&
         !shouldShowBatteryPercent(snapshot);
}

bool m5pm1PowerSourceHasExternal(uint8_t source_bits) {
  constexpr uint8_t k5VinBit = 1U << 0;
  constexpr uint8_t k5VinOutBit = 1U << 1;
  return (source_bits & (k5VinBit | k5VinOutBit)) != 0;
}

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

  events.external_power_changed =
      previous->external_power != current.external_power;
  events.charge_state_changed = previous->charge_state != current.charge_state;
  events.battery_percent_changed =
      previous->battery_percent != current.battery_percent;
  events.low_battery_changed =
      isLowBattery(*previous) != isLowBattery(current);
  return events;
}

}  // namespace power
}  // namespace zero_buddy
