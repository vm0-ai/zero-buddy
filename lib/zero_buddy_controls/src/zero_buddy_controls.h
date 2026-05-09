#pragma once

#include <cstdint>

namespace zero_buddy {
namespace controls {

constexpr uint8_t kBacklightVisibleStepCount = 5;
constexpr uint8_t kBacklightOffStep = kBacklightVisibleStepCount;
constexpr uint8_t kBacklightDefaultStep = 3;
constexpr uint32_t kResetButtonLongPressMs = 2000;

enum class ResetButtonAction : uint8_t {
  None,
  Restart,
  ClearRuntimeConfigAndRestart,
};

struct ResetButtonState {
  bool was_down = false;
  bool long_handled = false;
  uint32_t down_since_ms = 0;
};

uint8_t normalizeBacklightStep(uint8_t step);
uint8_t nextBacklightStep(uint8_t step);
uint8_t backlightBrightnessForStep(uint8_t step);

ResetButtonAction updateResetButtonState(ResetButtonState* state,
                                         bool down,
                                         uint32_t now_ms,
                                         uint32_t long_press_ms = kResetButtonLongPressMs);

}  // namespace controls
}  // namespace zero_buddy
