#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "zero_buddy_power.h"
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
  void setBacklightBrightness(uint8_t brightness);
  uint8_t backlightBrightness() const;
  void setPowerSnapshot(const power::PowerSnapshot& snapshot);

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
  void render_screen_recording_sent(const std::string& user_text);
  void render_screen_recording_aborted();
  void render_screen_recording_failed(const char* detail);
  void render_screen_checking_messages();
  void clear_checking_messages_indicator();
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
    int16_t battery_percent = -2;
    bool battery_percent_mode = false;
    bool battery_visible = false;
    bool checking_indicator_visible = false;
    bool battery_followup_refresh_scheduled = false;
    uint32_t battery_followup_due_ms = 0;
    mutable bool wrapped_text_height_cache_valid = false;
    mutable uint32_t wrapped_text_height_cache_hash = 0;
    mutable size_t wrapped_text_height_cache_size = 0;
    mutable size_t wrapped_text_height_cache_width = 0;
    mutable size_t wrapped_text_height_cache_value = 0;
  };

  struct Rect {
    Rect() = default;
    Rect(int x_value, int y_value, int w_value, int h_value)
        : x(x_value), y(y_value), w(w_value), h(h_value) {}

    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
  };

  struct Point {
    Point() = default;
    Point(int x_value, int y_value) : x(x_value), y(y_value) {}

    int x = 0;
    int y = 0;
  };

  enum class BubbleTailDirection : uint8_t {
    Top,
    Right,
  };

  struct AvatarDialogueLayout {
    Rect screen;
    Rect top_bar;
    Rect avatar;
    Rect bubble;
    Rect battery;
    Point tail;
    BubbleTailDirection tail_direction = BubbleTailDirection::Top;
    uint8_t avatar_scale = 1;
    int avatar_crop_left = 0;
    int avatar_visible_width = 0;
  };

  struct ReadLayout {
    Rect screen;
    Rect top_bar;
    Rect header;
    Rect body;
    Rect battery;
    int divider_y = 0;
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
  ReadLayout readLayout() const;
  Rect screenRect() const;
  Rect insetRect(const Rect& rect, int inset) const;
  Rect batteryRect(const Rect& screen) const;
  Rect topBarAvatarRect(const Rect& screen, uint8_t avatar_scale) const;
  uint8_t effectiveAvatarScale(uint8_t requested_scale) const;
  AvatarDialogueLayout render_avatar_dialogue_shell(uint8_t avatar_scale,
                                                    bool reuse_shell = false);
  void clear_avatar_dialogue_content(const AvatarDialogueLayout& layout);
  void render_dialogue_bubble(const AvatarDialogueLayout& layout);
  void render_checking_indicator(bool visible);
  Rect checkingIndicatorRect() const;

  uint16_t avatarBackgroundColor() const;
  uint16_t dialogueBorderColor() const;
  uint16_t avatarColor(uint8_t row, int col) const;
  bool batteryPercentMode() const;
  int16_t batteryLevelPercent() const;
  uint8_t batteryLevelBars() const;
  uint8_t batteryLevelBarsFor(int32_t level) const;
  size_t readBodyTop() const;
  size_t readViewportHeight() const;
  void renderWrappedChatText(const std::string& text,
                             int x,
                             int y,
                             int width_px,
                             int height_px,
                             size_t scroll_top);
  size_t estimateWrappedTextHeight(const std::string& text, size_t width_px) const;
  size_t utf8DisplayWidthPx(const std::string& text, size_t* offset) const;
  uint32_t hashText(const char* text) const;

  state::GlobalState* shared_state_;
  state::RenderScreenState fallback_render_state_;
  power::PowerSnapshot power_snapshot_;
  bool power_snapshot_valid_ = false;
  bool screen_on_ = false;
  LocalState local_;
  uint8_t backlight_brightness_ = 96;
};

}  // namespace screen
}  // namespace zero_buddy
