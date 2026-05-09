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
  int16_t battery_mv = -1;
  int32_t battery_current_ma = 0;
  int16_t vbus_mv = -1;
  ChargeState charge_state = ChargeState::Unknown;
  bool external_power = false;
  int8_t power_source = -1;
  bool charge_enabled = true;
};

struct PowerEvents {
  bool initialized = false;
  bool external_power_changed = false;
  bool charge_state_changed = false;
  bool battery_percent_changed = false;
  bool low_battery_changed = false;
};

bool shouldShowBatteryPercent(const PowerSnapshot& snapshot);
bool isLowBattery(const PowerSnapshot& snapshot, int32_t threshold_percent = 15);
bool m5pm1PowerSourceHasExternal(uint8_t source_bits);
uint8_t batteryBarsForPercent(int32_t percent);
PowerEvents detectPowerEvents(const PowerSnapshot* previous,
                              const PowerSnapshot& current);

}  // namespace power
}  // namespace zero_buddy
