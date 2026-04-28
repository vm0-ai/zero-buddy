#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "zero_buddy_state.h"

namespace zero_buddy {
namespace screen {

constexpr uint32_t kBatteryFollowupRefreshMs = 5000;

class ScreenRenderer {
 public:
  explicit ScreenRenderer(state::GlobalState* shared_state = nullptr);

  void setSharedState(state::GlobalState* shared_state);

  void screenOn();
  void screenOff();

  void render_screen_boot();
  void render_screen_read_empty();
  void render_screen_read_message(size_t index,
                                  size_t count,
                                  const std::string& message,
                                  size_t scroll_top);
  void render_screen_recording_prompt();
  void render_screen_recording_active();
  void render_screen_recording_wifi();
  void render_screen_recording_transcribing();
  void render_screen_recording_sending();
  void render_screen_recording_sent();
  void render_screen_recording_aborted();
  void render_screen_recording_failed(const char* detail);

  void render_element_battery_level();
  void render_element_zero_avatar(int x,
                                  int y,
                                  uint8_t scale,
                                  int crop_left,
                                  int visible_width);
  void render_element_dialogue_bubble(int bubble_x,
                                      int bubble_y,
                                      int bubble_w,
                                      int bubble_h,
                                      int tail_x,
                                      int tail_y);
  void render_element_bubble_text(const char* line1,
                                  const char* line2,
                                  int x,
                                  int y,
                                  int width,
                                  bool large_title);
  void render_element_chat_header(size_t index, size_t count);
  void render_element_chat_message(const std::string& message, size_t scroll_top);
  void render_element_next_page_arrow();

  void scheduleBatteryFollowupRefresh(uint32_t now_ms,
                                      uint32_t delay_ms = kBatteryFollowupRefreshMs);
  void renderBatteryFollowupIfDue(uint32_t now_ms);

  size_t maxScrollTopForMessage(const std::string& message) const;
  size_t scrollStep() const;
  uint32_t hashString(const std::string& text) const;

 private:
  struct LocalState {
    uint8_t battery_bars = 0xFF;
    bool battery_visible = false;
    bool battery_followup_refresh_scheduled = false;
    uint32_t battery_followup_due_ms = 0;
  };

  state::RenderScreenState currentRenderState() const;
  void commitRenderState(const state::RenderScreenState& next);
  void clearRenderState();
  bool sameRenderState(const state::RenderScreenState& next) const;
  void resetElementState();

  void render_screen_avatar_dialogue(state::RenderScreenKind kind,
                                     const char* line1,
                                     const char* line2);
  void render_avatar_dialogue_body(const char* line1,
                                   const char* line2,
                                   bool large_title,
                                   uint8_t avatar_scale);

  uint16_t avatarBackgroundColor() const;
  uint16_t dialogueBorderColor() const;
  uint16_t avatarColor(char cell) const;
  uint8_t batteryLevelBars() const;
  uint8_t batteryLevelBarsFor(int32_t level) const;
  size_t readBodyTop() const;
  size_t readViewportHeight() const;
  size_t estimateWrappedTextHeight(const std::string& text, size_t width_px) const;
  size_t utf8DisplayWidthPx(const std::string& text, size_t* offset) const;
  uint32_t hashText(const char* text) const;

  state::GlobalState* shared_state_;
  state::RenderScreenState fallback_render_state_;
  LocalState local_;
};

}  // namespace screen
}  // namespace zero_buddy
