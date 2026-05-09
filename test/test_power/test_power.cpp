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

void test_percent_mode_follows_external_power_or_charging() {
  PowerSnapshot snapshot;
  TEST_ASSERT_FALSE(zero_buddy::power::shouldShowBatteryPercent(snapshot));

  snapshot.charge_state = ChargeState::Charging;
  TEST_ASSERT_TRUE(zero_buddy::power::shouldShowBatteryPercent(snapshot));

  snapshot.charge_state = ChargeState::Discharging;
  snapshot.external_power = true;
  TEST_ASSERT_TRUE(zero_buddy::power::shouldShowBatteryPercent(snapshot));

  snapshot.external_power = false;
  snapshot.vbus_mv = 4300;
  TEST_ASSERT_FALSE(zero_buddy::power::shouldShowBatteryPercent(snapshot));
}

void test_low_battery_only_applies_while_unplugged() {
  PowerSnapshot snapshot;
  snapshot.battery_percent = 15;
  TEST_ASSERT_TRUE(zero_buddy::power::isLowBattery(snapshot));

  snapshot.external_power = true;
  TEST_ASSERT_FALSE(zero_buddy::power::isLowBattery(snapshot));

  snapshot.external_power = false;
  snapshot.battery_percent = -1;
  TEST_ASSERT_FALSE(zero_buddy::power::isLowBattery(snapshot));
}

void test_m5pm1_power_source_bits_detect_external_power() {
  TEST_ASSERT_FALSE(zero_buddy::power::m5pm1PowerSourceHasExternal(0x00));
  TEST_ASSERT_TRUE(zero_buddy::power::m5pm1PowerSourceHasExternal(0x01));
  TEST_ASSERT_TRUE(zero_buddy::power::m5pm1PowerSourceHasExternal(0x02));
  TEST_ASSERT_FALSE(zero_buddy::power::m5pm1PowerSourceHasExternal(0x04));
  TEST_ASSERT_TRUE(zero_buddy::power::m5pm1PowerSourceHasExternal(0x05));
  TEST_ASSERT_TRUE(zero_buddy::power::m5pm1PowerSourceHasExternal(0x07));
}

void test_power_events_compare_snapshots() {
  PowerSnapshot previous;
  previous.battery_percent = 70;
  previous.charge_state = ChargeState::Discharging;
  previous.external_power = false;

  PowerSnapshot current = previous;
  current.battery_percent = 69;
  current.charge_state = ChargeState::Charging;
  current.external_power = true;

  const auto initial = zero_buddy::power::detectPowerEvents(nullptr, current);
  TEST_ASSERT_TRUE(initial.initialized);
  TEST_ASSERT_FALSE(initial.external_power_changed);

  const auto changed = zero_buddy::power::detectPowerEvents(&previous, current);
  TEST_ASSERT_FALSE(changed.initialized);
  TEST_ASSERT_TRUE(changed.external_power_changed);
  TEST_ASSERT_TRUE(changed.charge_state_changed);
  TEST_ASSERT_TRUE(changed.battery_percent_changed);
  TEST_ASSERT_FALSE(changed.low_battery_changed);
}

}  // namespace

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;

  UNITY_BEGIN();
  RUN_TEST(test_battery_bars_are_three_step_display);
  RUN_TEST(test_percent_mode_follows_external_power_or_charging);
  RUN_TEST(test_low_battery_only_applies_while_unplugged);
  RUN_TEST(test_m5pm1_power_source_bits_detect_external_power);
  RUN_TEST(test_power_events_compare_snapshots);
  return UNITY_END();
}
