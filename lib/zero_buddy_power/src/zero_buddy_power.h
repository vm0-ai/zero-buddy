#pragma once

#include <cstdint>

namespace zero_buddy {
namespace power {

enum class ChargeState : uint8_t {
  Unknown = 0,
  Discharging,
  Charging,
};

struct PowerSnapshot {
  int16_t battery_percent = -1;
  ChargeState charge_state = ChargeState::Unknown;
};

struct PowerEvents {
  bool initialized = false;
  bool charge_state_changed = false;
  bool battery_percent_changed = false;
};

uint8_t batteryBarsForPercent(int32_t percent);
PowerEvents detectPowerEvents(const PowerSnapshot* previous,
                              const PowerSnapshot& current);

}  // namespace power
}  // namespace zero_buddy
