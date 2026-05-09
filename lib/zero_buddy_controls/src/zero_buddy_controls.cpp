#include "zero_buddy_controls.h"

namespace zero_buddy {
namespace controls {
namespace {

constexpr uint8_t kBacklightLevels[kBacklightVisibleStepCount] = {
    24,
    48,
    72,
    96,
    128,
};

}  // namespace

uint8_t normalizeBacklightStep(uint8_t step) {
  return step <= kBacklightOffStep ? step : kBacklightDefaultStep;
}

uint8_t nextBacklightStep(uint8_t step) {
  step = normalizeBacklightStep(step);
  return step >= kBacklightOffStep ? 0 : static_cast<uint8_t>(step + 1);
}

uint8_t backlightBrightnessForStep(uint8_t step) {
  step = normalizeBacklightStep(step);
  if (step >= kBacklightVisibleStepCount) {
    return 0;
  }
  return kBacklightLevels[step];
}

ResetButtonAction updateResetButtonState(ResetButtonState* state,
                                         bool down,
                                         uint32_t now_ms,
                                         uint32_t long_press_ms) {
  if (state == nullptr) {
    return ResetButtonAction::None;
  }

  if (down) {
    if (!state->was_down) {
      state->was_down = true;
      state->long_handled = false;
      state->down_since_ms = now_ms;
      return ResetButtonAction::None;
    }
    if (!state->long_handled && now_ms - state->down_since_ms >= long_press_ms) {
      state->long_handled = true;
      return ResetButtonAction::ClearRuntimeConfigAndRestart;
    }
    return ResetButtonAction::None;
  }

  if (!state->was_down) {
    return ResetButtonAction::None;
  }

  const bool long_handled = state->long_handled;
  state->was_down = false;
  state->long_handled = false;
  state->down_since_ms = 0;
  return long_handled ? ResetButtonAction::None : ResetButtonAction::Restart;
}

}  // namespace controls
}  // namespace zero_buddy
