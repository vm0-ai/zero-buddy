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
  void render_screen_recording_sending(const std::string& user_text);
  void render_screen_recording_sent();
  void render_screen_recording_aborted();
  void render_screen_recording_failed(const char* detail);
  void render_screen_checking_messages();
  void render_screen_setup_wifi();
  void render_screen_setup_device_code(const char* device_code);
  void render_screen_setup_status(const char* line1, const char* line2);

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

  struct AvatarDialogueLayout {
    int avatar_x = 0;
    int avatar_y = 0;
    int bubble_x = 0;
    int bubble_y = 0;
    int bubble_w = 0;
    int bubble_h = 0;
    int tail_y = 0;
  };

  state::RenderScreenState currentRenderState() const;
  void commitRenderState(const state::RenderScreenState& next);
  void clearRenderState();
  bool sameRenderState(const state::RenderScreenState& next) const;
  void resetElementState();

  bool prepare_avatar_dialogue_screen(const state::RenderScreenState& next,
                                      uint8_t avatar_scale,
                                      AvatarDialogueLayout* layout_out);
  void render_avatar_layout_brand(state::RenderScreenKind kind, const char* title);
  void render_avatar_layout_instruction(state::RenderScreenKind kind,
                                        const char* line1,
                                        const char* emphasis,
                                        const char* line3,
                                        uint8_t emphasis_scale);
  void render_avatar_layout_status(state::RenderScreenKind kind,
                                   const char* title,
                                   const char* detail);
  void render_avatar_layout_preview(state::RenderScreenKind kind,
                                    const char* title,
                                    const std::string& body);
  void render_element_brand_text(const char* title,
                                 int bubble_x,
                                 int bubble_y,
                                 int bubble_w,
                                 int bubble_h);
  void render_element_instruction_text(const char* line1,
                                       const char* emphasis,
                                       const char* line3,
                                       uint8_t emphasis_scale,
                                       int bubble_x,
                                       int bubble_y,
                                       int bubble_w,
                                       int bubble_h);
  void render_element_status_pair_text(const char* title,
                                       const char* detail,
                                       int bubble_x,
                                       int bubble_y,
                                       int bubble_w,
                                       int bubble_h);
  void render_element_preview_text(const char* title,
                                   const std::string& body,
                                   int bubble_x,
                                   int bubble_y,
                                   int bubble_w,
                                   int bubble_h);
  bool isAvatarDialogueScreenKind(state::RenderScreenKind kind) const;
  bool canReuseAvatarDialogueShell(const state::RenderScreenState& previous,
                                   uint8_t avatar_scale) const;
  AvatarDialogueLayout avatarDialogueLayout(uint8_t avatar_scale) const;
  AvatarDialogueLayout render_avatar_dialogue_shell(uint8_t avatar_scale,
                                                    bool reuse_shell = false);
  void clear_avatar_dialogue_content(const AvatarDialogueLayout& layout);

  uint16_t avatarBackgroundColor() const;
  uint16_t dialogueBorderColor() const;
  uint16_t avatarColor(char cell) const;
  uint8_t batteryLevelBars() const;
  uint8_t batteryLevelBarsFor(int32_t level) const;
  size_t readBodyTop() const;
  size_t readViewportHeight() const;
  void renderWrappedChatText(const std::string& text,
                             int x,
                             int y,
                             int width_px,
                             size_t scroll_top);
  size_t estimateWrappedTextHeight(const std::string& text, size_t width_px) const;
  size_t utf8DisplayWidthPx(const std::string& text, size_t* offset) const;
  uint32_t hashText(const char* text) const;

  state::GlobalState* shared_state_;
  state::RenderScreenState fallback_render_state_;
  LocalState local_;
};

}  // namespace screen
}  // namespace zero_buddy
