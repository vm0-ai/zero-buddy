#include <unity.h>

#include "zero_buddy_power.h"

namespace {

using zero_buddy::power::ChargeState;
using zero_buddy::power::PowerSnapshot;

void test_battery_bars_are_three_step_display() {
  TEST_ASSERT_EQUAL_UINT8(0, zero_buddy::power::batteryBarsForPercent(-1));
  TEST_ASSERT_EQUAL_UINT8(0, zero_buddy::power::batteryBarsForPercent(0));
  TEST_ASSERT_EQUAL_UINT8(1, zero_buddy::power::batteryBarsForPercent(1));
  TEST_ASSERT_EQUAL_UINT8(1, zero_buddy::power::batteryBarsForPercent(33));
  TEST_ASSERT_EQUAL_UINT8(2, zero_buddy::power::batteryBarsForPercent(34));
  TEST_ASSERT_EQUAL_UINT8(2, zero_buddy::power::batteryBarsForPercent(66));
  TEST_ASSERT_EQUAL_UINT8(3, zero_buddy::power::batteryBarsForPercent(67));
  TEST_ASSERT_EQUAL_UINT8(3, zero_buddy::power::batteryBarsForPercent(120));
}

void test_power_events_compare_snapshots() {
  PowerSnapshot previous;
  previous.battery_percent = 70;
  previous.charge_state = ChargeState::Discharging;

  PowerSnapshot current = previous;
  current.battery_percent = 69;
  current.charge_state = ChargeState::Charging;

  const auto initial = zero_buddy::power::detectPowerEvents(nullptr, current);
  TEST_ASSERT_TRUE(initial.initialized);

  const auto changed = zero_buddy::power::detectPowerEvents(&previous, current);
  TEST_ASSERT_FALSE(changed.initialized);
  TEST_ASSERT_TRUE(changed.charge_state_changed);
  TEST_ASSERT_TRUE(changed.battery_percent_changed);
}

}  // namespace

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;

  UNITY_BEGIN();
  RUN_TEST(test_battery_bars_are_three_step_display);
  RUN_TEST(test_power_events_compare_snapshots);
  return UNITY_END();
}
