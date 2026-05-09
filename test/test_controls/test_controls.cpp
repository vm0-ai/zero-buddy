#include <unity.h>

#include "zero_buddy_controls.h"

namespace {

using zero_buddy::controls::ResetButtonAction;
using zero_buddy::controls::ResetButtonState;

void test_backlight_steps_cycle_through_five_levels_then_off() {
  TEST_ASSERT_EQUAL_UINT8(0, zero_buddy::controls::normalizeBacklightStep(0));
  TEST_ASSERT_EQUAL_UINT8(zero_buddy::controls::kBacklightOffStep,
                          zero_buddy::controls::normalizeBacklightStep(
                              zero_buddy::controls::kBacklightOffStep));
  TEST_ASSERT_EQUAL_UINT8(zero_buddy::controls::kBacklightDefaultStep,
                          zero_buddy::controls::normalizeBacklightStep(99));

  TEST_ASSERT_EQUAL_UINT8(1, zero_buddy::controls::nextBacklightStep(0));
  TEST_ASSERT_EQUAL_UINT8(2, zero_buddy::controls::nextBacklightStep(1));
  TEST_ASSERT_EQUAL_UINT8(3, zero_buddy::controls::nextBacklightStep(2));
  TEST_ASSERT_EQUAL_UINT8(4, zero_buddy::controls::nextBacklightStep(3));
  TEST_ASSERT_EQUAL_UINT8(zero_buddy::controls::kBacklightOffStep,
                          zero_buddy::controls::nextBacklightStep(4));
  TEST_ASSERT_EQUAL_UINT8(0, zero_buddy::controls::nextBacklightStep(
                                 zero_buddy::controls::kBacklightOffStep));
}

void test_backlight_brightness_mapping() {
  TEST_ASSERT_EQUAL_UINT8(24, zero_buddy::controls::backlightBrightnessForStep(0));
  TEST_ASSERT_EQUAL_UINT8(48, zero_buddy::controls::backlightBrightnessForStep(1));
  TEST_ASSERT_EQUAL_UINT8(72, zero_buddy::controls::backlightBrightnessForStep(2));
  TEST_ASSERT_EQUAL_UINT8(96, zero_buddy::controls::backlightBrightnessForStep(3));
  TEST_ASSERT_EQUAL_UINT8(128, zero_buddy::controls::backlightBrightnessForStep(4));
  TEST_ASSERT_EQUAL_UINT8(0, zero_buddy::controls::backlightBrightnessForStep(
                                zero_buddy::controls::kBacklightOffStep));
}

void test_reset_button_short_press_restarts_on_release() {
  ResetButtonState state;
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(ResetButtonAction::None),
      static_cast<uint8_t>(zero_buddy::controls::updateResetButtonState(&state, true, 100)));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(ResetButtonAction::None),
      static_cast<uint8_t>(zero_buddy::controls::updateResetButtonState(&state, true, 800)));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(ResetButtonAction::Restart),
      static_cast<uint8_t>(zero_buddy::controls::updateResetButtonState(&state, false, 900)));
}

void test_reset_button_long_press_clears_once() {
  ResetButtonState state;
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(ResetButtonAction::None),
      static_cast<uint8_t>(zero_buddy::controls::updateResetButtonState(&state, true, 100)));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(ResetButtonAction::None),
      static_cast<uint8_t>(zero_buddy::controls::updateResetButtonState(&state, true, 2099)));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(ResetButtonAction::ClearRuntimeConfigAndRestart),
      static_cast<uint8_t>(zero_buddy::controls::updateResetButtonState(&state, true, 2100)));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(ResetButtonAction::None),
      static_cast<uint8_t>(zero_buddy::controls::updateResetButtonState(&state, true, 2600)));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(ResetButtonAction::None),
      static_cast<uint8_t>(zero_buddy::controls::updateResetButtonState(&state, false, 2700)));
}

}  // namespace

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_backlight_steps_cycle_through_five_levels_then_off);
  RUN_TEST(test_backlight_brightness_mapping);
  RUN_TEST(test_reset_button_short_press_restarts_on_release);
  RUN_TEST(test_reset_button_long_press_clears_once);
  return UNITY_END();
}
