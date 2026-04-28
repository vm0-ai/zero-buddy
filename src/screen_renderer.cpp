#include "screen_renderer.h"

#include <Arduino.h>
#include <M5Unified.h>

#include <algorithm>

namespace zero_buddy {
namespace screen {
namespace {

constexpr uint8_t kScreenBrightness = 90;
constexpr uint8_t kReadPaddingTop = 8;
constexpr uint8_t kReadPaddingRight = 8;
constexpr uint8_t kReadPaddingLeft = kReadPaddingRight + 2;
constexpr uint8_t kReadPaddingBottom = 8;
constexpr uint8_t kReadHeaderHeight = 14;
constexpr uint8_t kReadLineHeight = 16;
constexpr uint8_t kZeroAvatarWidth = 80;
constexpr uint8_t kZeroAvatarHeight = 45;
constexpr uint8_t kZeroAvatarScale = 2;
constexpr int kZeroAvatarCropLeft = kZeroAvatarWidth / 4;
constexpr int kZeroAvatarCropRight = kZeroAvatarWidth / 4;
constexpr int kZeroAvatarVisibleWidth =
    kZeroAvatarWidth - kZeroAvatarCropLeft - kZeroAvatarCropRight;
constexpr int kZeroAvatarRightPadding = 8;
constexpr int kDialoguePadding = 5;

constexpr char kZeroAvatar[kZeroAvatarHeight][kZeroAvatarWidth + 1] = {
    "BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB",
    "BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB",
    "BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBWBBBSSSSSBBBWBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB",
    "BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBSOOOOOOOOOOSSBWBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB",
    "BBBBBBBBBBBBBBBBBBBBBBBBBBBBSSOOOOOOOOOOOOOOOOOSBWBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB",
    "BBBBBBBBBBBBBBBBBBBBBBBBBBBOOOOOOOOOOOOOOOOOOOOOOSBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB",
    "BBBBBBBBBBBBBBBBBBBBBBBBBBOOOOOOOOOOOOOOOOOOOOOOOOOBBBBBBBBBBBBBBBBBBBBBBBBBBBBB",
    "BBBBBBBBBBBBBBBBBBBBBBBBBOOOOOOOOOOOOOOOOOOOOOOOOOOOBBBBBBBBBBBBBBBBBBBBBBBBBBBB",
    "BBBBBBBBBBBBBBBBBBBBBBBBSOOOOKKOOOOOOOOKOOOOOOOOOOOOOBBBBBBBBBBBBBBBBBBBBBBBBBBB",
    "BBBBBBBBBBBBBBBBBBBBBBBBOOOKKOOOKKOOOOKKKOOOOOOOOOOOOOBBBBBBBBBBBBBBBBBBBBBBBBBB",
    "BBBBBBBBBBBBBBBBBBBBBBBSOOOKOOOOKKOOOOKKOOOOOOOOOOOOOOSWBBBBBBBBBBBBBBBBBBBBBBBB",
    "BBBBBBBBBBBBBBBBBBBBBBBOOOKOOOOKKKOOOOKKOOOOOOOOOOOOOOOBBBBBBBBBBBBBBBBBBBBBBBBB",
    "BBBBBBBBBBBBBBBBBBBBBWSOOOKOOOOKKKOOOKKKKKOOOOOOOOOOOOOBBBBBBBBBBBBBBBBBBBBBBBBB",
    "BBBBBBBBBBBBBBBBBBBBBWSOOOKKKKKKKKKKKKOOOOOOOOOOOOOOOOOBBBBBBBBBBBBBBBBBBBBBBBBB",
    "BBBBBBBBBBBBBBBBBBBBBBSOOOOOKKKOOOOOOOOOOOOOOOOOOOOOOOOBBBBBBBBBBBBBBBBBBBBBBBBB",
    "BBBBBBBBBBBBBBBBBBBBBBOOOOOOOOOOOOOOOOOSSSSSSOOOOOOOOOOBBBBBBBBBBBBBBBBBBBBBBBBB",
    "BBBBBBBBBBBBBBBBBBBBBWSOOOOOOOOOOOOOOSSSSSSSSSOOOOOOOOSWBBBBBBBBBBBBBBBBBBBBBBBB",
    "BBBBBBBBBBBBBBBBBBBBBBBOOOOOKKKOOOOOOKKKKSSSSSSOOOOOOOBBBBBBBBBBBBBBBBBBBBBBBBBB",
    "BBBBBBBBBBBBBBBBBBBBBBBSOOOOOOOKOOSSKSSSSSSSSSSOOOOOOSBBBBBBBBBBBBBBBBBBBBBBBBBB",
    "BBBBBBBBBBBBBBBBBBBBBBBBBSSSSSSSSSSSSSSSSSSSSSSOOOOOSSSSBBBBBBBBBBBBBBBBBBBBBBBB",
    "BBBBBBBBBBBBBBBBBBBBBBBBBSSSSSSSSSSSSSSSSSSSSSSSOOSSSSSSBBBBBBBBBBBBBBBBBBBBBBBB",
    "BBBBBBBBBBBBBBBBBBBBBBBBBSSSOOSSOSSSSOOKKOSSSSSSSSSSKSSSSBBBBBBBBBBBBBBBBBBBBBBB",
    "BBBBBBBBBBBBBBBBBBBBBBBBBSSKKKKOKSSSOKKSBSOSSSSSSSSKSSSSSBBBBBBBBBBBBBBBBBBBBBBB",
    "BBBBBBBBBBBBBBBBBBBBBBBBBSSKKBWSKSSSSKKBWBSSSSSSSOKKKKSSBBBBBBBBBBBBBBBBBBBBBBBB",
    "BBBBBBBBBBBBBBBBBBBBBBBBBSSKKBWSKKSSSKKBWSSSSSSSSSOOOSSSBBBBBBBBBBBBBBBBBBBBBBBB",
    "BBBBBBBBBBBBBBBBBBBBBBBBBSSKKBBSKSSSSKKBWSSSSSSSSSSSSSSSBBBBBBBBBBBBBBBBBBBBBBBB",
    "BBBBBBBBBBBBBBBBBBBBBBBBBSSOSBSOOSSSSOSWSSSSSSSSSSSSSSSBBBBBBBBBBBBBBBBBBBBBBBBB",
    "BBBBBBBBBBBBBBBBBBBBBBBBBSSSSSSKKOSSSSSSSSSSSSSSSSSSSOOSBBBBBBBBBBBBBBBBBBBBBBBB",
    "BBBBBBBBBBBBBBBBBBBBBBBBBOSSSSSSKKSSSSSSSSSSSSSSSSOOOOOSWBBBBBBBBBBBBBBBBBBBBBBB",
    "BBBBBBBBBBBBBBBBBBBBBBBBSOOSSSSSSSSSSSSSSSSSSSSSSOOOOOOSBBBBBBBBBBBBBBBBBBBBBBBB",
    "BBBBBBBBBBBBBBBBBBBBBBBBSOOSSSSSSSSSSSSKSSSSSSSSOOOOOOOOBBBBBBBBBBBBBBBBBBBBBBBB",
    "BBBBBBBBBBBBBBBBBBBBBBBWSOOOSSSSSSOOSOKSSSSSSSSOOOOOOOOOBBBBBBBBBBBBBBBBBBBBBBBB",
    "BBBBBBBBBBBBBBBBBBBBBBBBSOOOOSSSSSOKKKSSSSSSSSOOOOOOOOOOBBBBBBBBBBBBBBBBBBBBBBBB",
    "BBBBBBBBBBBBBBBBBBBBBBBBSOOOOOSSSSSSSSSSSSSSOOOOOOOOOOOOBBBBBBBBBBBBBBBBBBBBBBBB",
    "BBBBBBBBBBBBBBBBBBBBBBBBBOOOOOOOSSSSSSSSSSSOOOOOOOOOOOOOBBBBBBBBBBBBBBBBBBBBBBBB",
    "BBBBBBBBBBBBBBBBBBBBBBBBBOOOOOOOOOOOOSSSSSOOOOOOOOOOOOOOBBBBBBBBBBBBBBBBBBBBBBBB",
    "BBBBBBBBBBBBBBBBBBBBBBBBBSOOOOOOOOOOOSSSSSOOOOOOOOOOOOOSWBBBBBBBBBBBBBBBBBBBBBBB",
    "BBBBBBBBBBBBBBBBBBBBBBBBBSOOOOOOOOOOKKKKKKKKKOOOOOOOOOOSBBBBBBBBBBBBBBBBBBBBBBBB",
    "BBBBBBBBBBBBBBBBBBBBBBBBBBSOOOOOOOKKKKKKKKKKKKOOOOOOOOOBBBBBBBBBBBBBBBBBBBBBBBBB",
    "BBBBBBBBBBBBBBBBBBBBBBBBBBBBSSOOOKKKKKKKKKKKKKKOSSOOOOBBBBBBBBBBBBBBBBBBBBBBBBBB",
    "BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBKKKKKKKKKKKKKKKBBBBSBBBBBBBBBBBBBBBBBBBBBBBBBBBB",
    "BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBWSKKKKKKKKKKKKKKKWBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB",
    "BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBWBKKKKKKKKKKKKKKBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB",
    "BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBWBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB",
    "BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBWWWWWWWWBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB",
};

String fittedLine(const char* text, int max_width) {
  String line(text != nullptr ? text : "");
  if (M5.Display.textWidth(line.c_str()) <= max_width) {
    return line;
  }
  while (line.length() > 0) {
    line.remove(line.length() - 1);
    const String candidate = line + "...";
    if (M5.Display.textWidth(candidate.c_str()) <= max_width) {
      return candidate;
    }
  }
  return "";
}

void printFittedLine(const char* text, int x, int y, int max_width, bool bold = false) {
  const String line = fittedLine(text, max_width);
  M5.Display.setCursor(x, y);
  M5.Display.print(line);
  if (bold && line.length() > 0) {
    M5.Display.setCursor(x + 1, y);
    M5.Display.print(line);
  }
}

}  // namespace

ScreenRenderer::ScreenRenderer(state::GlobalState* shared_state)
    : shared_state_(shared_state) {}

void ScreenRenderer::setSharedState(state::GlobalState* shared_state) {
  shared_state_ = shared_state;
}

void ScreenRenderer::screenOn() {
  M5.Display.setRotation(3);
  M5.Display.setBrightness(kScreenBrightness);
}

void ScreenRenderer::screenOff() {
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setBrightness(0);
  clearRenderState();
}

void ScreenRenderer::render_screen_boot() {
  state::RenderScreenState next;
  next.kind = state::RenderScreenKind::Boot;
  if (sameRenderState(next)) {
    render_element_battery_level();
    return;
  }
  commitRenderState(next);
  resetElementState();
  render_avatar_dialogue_body("Zero", "your trustworthy AI teammate", true, 1);
}

void ScreenRenderer::render_screen_read_empty() {
  render_screen_avatar_dialogue(state::RenderScreenKind::ReadEmpty, "no message", "");
}

void ScreenRenderer::render_screen_read_message(size_t index,
                                                size_t count,
                                                const std::string& message,
                                                size_t scroll_top) {
  state::RenderScreenState next;
  next.kind = state::RenderScreenKind::ReadAssistantMessage;
  next.value1 = static_cast<uint32_t>(index);
  next.value2 = static_cast<uint32_t>(count);
  next.value3 = static_cast<uint32_t>(scroll_top);
  next.value4 = hashString(message);

  const state::RenderScreenState previous = currentRenderState();
  if (state::sameRenderScreenState(previous, next)) {
    render_element_battery_level();
    return;
  }

  const bool body_only =
      previous.kind == state::RenderScreenKind::ReadAssistantMessage &&
      previous.value1 == next.value1 &&
      previous.value2 == next.value2 &&
      previous.value4 == next.value4;
  commitRenderState(next);

  if (!body_only) {
    resetElementState();
    screenOn();
    const uint16_t bg = avatarBackgroundColor();
    M5.Display.fillScreen(bg);
    render_element_chat_header(index, count);
  }

  render_element_chat_message(message, scroll_top);
  const bool has_next_page =
      scroll_top < maxScrollTopForMessage(message) || index + 1 < count;
  if (has_next_page) {
    render_element_next_page_arrow();
  }
  render_element_battery_level();
}

void ScreenRenderer::render_screen_recording_prompt() {
  render_screen_avatar_dialogue(state::RenderScreenKind::RecordingPrompt,
                                "recording",
                                "hold BtnA");
}

void ScreenRenderer::render_screen_recording_active() {
  render_screen_avatar_dialogue(state::RenderScreenKind::RecordingActive,
                                "recording",
                                "speak now");
}

void ScreenRenderer::render_screen_recording_wifi() {
  render_screen_avatar_dialogue(state::RenderScreenKind::RecordingWifi, "recording", "wifi");
}

void ScreenRenderer::render_screen_recording_transcribing() {
  render_screen_avatar_dialogue(state::RenderScreenKind::RecordingTranscribing,
                                "recording",
                                "speech to text");
}

void ScreenRenderer::render_screen_recording_sending() {
  render_screen_avatar_dialogue(state::RenderScreenKind::RecordingSending,
                                "recording",
                                "send message");
}

void ScreenRenderer::render_screen_recording_sent() {
  render_screen_avatar_dialogue(state::RenderScreenKind::RecordingSent,
                                "sent",
                                "message sent");
}

void ScreenRenderer::render_screen_recording_aborted() {
  render_screen_avatar_dialogue(state::RenderScreenKind::RecordingAborted,
                                "aborted",
                                "recording");
}

void ScreenRenderer::render_screen_recording_failed(const char* detail) {
  render_screen_avatar_dialogue(state::RenderScreenKind::RecordingFailed, "failed", detail);
}

void ScreenRenderer::render_element_battery_level() {
  const uint8_t bars = batteryLevelBars();
  if (local_.battery_visible && local_.battery_bars == bars) {
    return;
  }
  local_.battery_visible = true;
  local_.battery_bars = bars;

  const uint16_t bg = avatarBackgroundColor();
  const uint16_t fg = dialogueBorderColor();
  constexpr int kBodyWidth = 22;
  constexpr int kBodyHeight = 10;
  constexpr int kCapWidth = 2;
  constexpr int kBarWidth = 3;
  constexpr int kBarGap = 2;
  const int x = std::max(0, M5.Display.width() - kReadPaddingRight -
                                kBodyWidth - kCapWidth);
  const int y = kReadPaddingTop + 1;
  const int clear_x = std::max(0, x - 1);
  M5.Display.fillRect(clear_x,
                      0,
                      M5.Display.width() - clear_x,
                      kReadPaddingTop + kReadHeaderHeight,
                      bg);
  M5.Display.drawRect(x, y, kBodyWidth, kBodyHeight, fg);
  M5.Display.fillRect(x + kBodyWidth, y + 3, kCapWidth, 4, fg);

  const int bar_y = y + 2;
  for (uint8_t i = 0; i < 4; ++i) {
    const int bar_x = x + 2 + i * (kBarWidth + kBarGap);
    if (i < bars) {
      M5.Display.fillRect(bar_x, bar_y, kBarWidth, kBodyHeight - 4, fg);
    } else {
      M5.Display.drawRect(bar_x, bar_y, kBarWidth, kBodyHeight - 4, fg);
    }
  }
}

void ScreenRenderer::render_element_zero_avatar(int x,
                                                int y,
                                                uint8_t scale,
                                                int crop_left,
                                                int visible_width) {
  scale = std::max<uint8_t>(1, scale);
  crop_left = std::max(0, std::min(crop_left, static_cast<int>(kZeroAvatarWidth)));
  visible_width =
      std::max(0,
               std::min(visible_width, static_cast<int>(kZeroAvatarWidth) - crop_left));
  for (uint8_t row = 0; row < kZeroAvatarHeight; ++row) {
    for (int col = 0; col < visible_width; ++col) {
      const uint16_t color = avatarColor(kZeroAvatar[row][crop_left + col]);
      if (scale == 1) {
        M5.Display.drawPixel(x + col, y + row, color);
      } else {
        M5.Display.fillRect(x + col * scale, y + row * scale, scale, scale, color);
      }
    }
  }
}

void ScreenRenderer::render_element_dialogue_bubble(int bubble_x,
                                                    int bubble_y,
                                                    int bubble_w,
                                                    int bubble_h,
                                                    int tail_x,
                                                    int tail_y) {
  const uint16_t border = dialogueBorderColor();
  M5.Display.fillRoundRect(bubble_x, bubble_y, bubble_w, bubble_h, 7, TFT_WHITE);
  M5.Display.drawRoundRect(bubble_x, bubble_y, bubble_w, bubble_h, 7, border);
  M5.Display.drawRoundRect(bubble_x + 2, bubble_y + 2, bubble_w - 4, bubble_h - 4, 5, border);
  M5.Display.fillTriangle(bubble_x + bubble_w - 1,
                          tail_y - 7,
                          tail_x,
                          tail_y,
                          bubble_x + bubble_w - 1,
                          tail_y + 7,
                          TFT_WHITE);
  M5.Display.drawTriangle(bubble_x + bubble_w - 1,
                          tail_y - 7,
                          tail_x,
                          tail_y,
                          bubble_x + bubble_w - 1,
                          tail_y + 7,
                          border);
}

void ScreenRenderer::render_element_bubble_text(const char* line1,
                                                const char* line2,
                                                int x,
                                                int y,
                                                int width,
                                                bool large_title) {
  const uint16_t border = dialogueBorderColor();
  M5.Display.setTextColor(border, TFT_WHITE);
  M5.Display.setTextWrap(false);
  if (large_title) {
    M5.Display.setFont(&fonts::Font4);
    printFittedLine(line1, x, y + 13, width, true);
    M5.Display.setFont(&fonts::Font0);
    printFittedLine(line2, x, y + 67, width);
    return;
  }

  M5.Display.setFont(&fonts::Font2);
  printFittedLine(line1, x, y + 7, width);
  if (line2 != nullptr && line2[0] != '\0') {
    M5.Display.setFont(&fonts::Font0);
    printFittedLine(line2, x, y + 37, width);
  }
}

void ScreenRenderer::render_element_chat_header(size_t index, size_t count) {
  const uint16_t fg = dialogueBorderColor();
  M5.Display.setTextColor(fg, avatarBackgroundColor());
  M5.Display.setTextWrap(false);
  M5.Display.setFont(&fonts::Font0);
  M5.Display.setCursor(kReadPaddingLeft, kReadPaddingTop);
  M5.Display.print(String(static_cast<unsigned>(index + 1)) + "/" +
                   String(static_cast<unsigned>(count)));
  const int divider_y = static_cast<int>(kReadPaddingTop + kReadHeaderHeight);
  M5.Display.drawFastHLine(kReadPaddingLeft,
                           divider_y,
                           M5.Display.width() - kReadPaddingLeft - kReadPaddingRight,
                           fg);
}

void ScreenRenderer::render_element_chat_message(const std::string& message,
                                                 size_t scroll_top) {
  const uint16_t bg = avatarBackgroundColor();
  const uint16_t fg = dialogueBorderColor();
  const int body_y = static_cast<int>(readBodyTop());
  const int body_h = static_cast<int>(readViewportHeight());
  const int body_w = M5.Display.width() - kReadPaddingLeft - kReadPaddingRight;
  M5.Display.fillRect(kReadPaddingLeft, body_y, body_w, body_h, bg);
  M5.Display.setClipRect(kReadPaddingLeft, body_y, body_w, body_h);
  M5.Display.setTextColor(fg, bg);
  M5.Display.setTextWrap(true);
  M5.Display.setFont(&fonts::efontCN_12);
  M5.Display.setCursor(kReadPaddingLeft, body_y - static_cast<int>(scroll_top));
  M5.Display.print(message.c_str());
  M5.Display.clearClipRect();
  M5.Display.setTextWrap(false);
}

void ScreenRenderer::render_element_next_page_arrow() {
  const int right = M5.Display.width() - kReadPaddingRight - 1;
  const int bottom = M5.Display.height() - kReadPaddingBottom - 1;
  const int cx = right - 6;
  const int cy = bottom - 5;
  M5.Display.fillTriangle(cx - 5,
                          cy - 2,
                          cx + 5,
                          cy - 2,
                          cx,
                          cy + 5,
                          dialogueBorderColor());
}

void ScreenRenderer::scheduleBatteryFollowupRefresh(uint32_t now_ms,
                                                    uint32_t delay_ms) {
  local_.battery_followup_due_ms = now_ms + delay_ms;
  local_.battery_followup_refresh_scheduled = true;
}

void ScreenRenderer::renderBatteryFollowupIfDue(uint32_t now_ms) {
  if (!local_.battery_followup_refresh_scheduled) {
    return;
  }
  if (static_cast<int32_t>(now_ms - local_.battery_followup_due_ms) < 0) {
    return;
  }
  local_.battery_followup_refresh_scheduled = false;
  render_element_battery_level();
}

size_t ScreenRenderer::maxScrollTopForMessage(const std::string& message) const {
  const size_t width = static_cast<size_t>(
      std::max(1, M5.Display.width() -
                      static_cast<int>(kReadPaddingLeft + kReadPaddingRight)));
  const size_t viewport = readViewportHeight();
  const size_t text_height = estimateWrappedTextHeight(message, width);
  if (text_height <= viewport) {
    return 0;
  }
  return text_height - viewport;
}

size_t ScreenRenderer::scrollStep() const {
  return readViewportHeight();
}

uint32_t ScreenRenderer::hashString(const std::string& text) const {
  return hashText(text.c_str());
}

state::RenderScreenState ScreenRenderer::currentRenderState() const {
  if (shared_state_ != nullptr) {
    return shared_state_->lastRenderScreenState;
  }
  return fallback_render_state_;
}

void ScreenRenderer::commitRenderState(const state::RenderScreenState& next) {
  if (shared_state_ != nullptr) {
    state::setLastRenderScreenState(shared_state_, next);
  } else {
    fallback_render_state_ = next;
  }
}

void ScreenRenderer::clearRenderState() {
  if (shared_state_ != nullptr) {
    state::clearLastRenderScreenState(shared_state_);
  } else {
    fallback_render_state_ = state::RenderScreenState();
  }
  local_ = LocalState();
}

bool ScreenRenderer::sameRenderState(const state::RenderScreenState& next) const {
  return state::sameRenderScreenState(currentRenderState(), next);
}

void ScreenRenderer::resetElementState() {
  local_.battery_bars = 0xFF;
  local_.battery_visible = false;
}

void ScreenRenderer::render_screen_avatar_dialogue(state::RenderScreenKind kind,
                                                   const char* line1,
                                                   const char* line2) {
  state::RenderScreenState next;
  next.kind = kind;
  next.value1 = hashText(line1);
  next.value2 = hashText(line2);
  if (sameRenderState(next)) {
    render_element_battery_level();
    return;
  }
  commitRenderState(next);
  resetElementState();
  render_avatar_dialogue_body(line1, line2, false, kZeroAvatarScale);
}

void ScreenRenderer::render_avatar_dialogue_body(const char* line1,
                                                 const char* line2,
                                                 bool large_title,
                                                 uint8_t avatar_scale) {
  screenOn();
  const uint16_t bg = avatarBackgroundColor();
  M5.Display.fillScreen(bg);
  const int avatar_w = kZeroAvatarVisibleWidth * avatar_scale;
  const int avatar_h = static_cast<int>(kZeroAvatarHeight) * avatar_scale;
  const int avatar_x = M5.Display.width() - avatar_w - kZeroAvatarRightPadding;
  const int avatar_y = (M5.Display.height() - avatar_h) / 2;
  render_element_zero_avatar(
      avatar_x, avatar_y, avatar_scale, kZeroAvatarCropLeft, kZeroAvatarVisibleWidth);

  const int bubble_x = kDialoguePadding;
  const int bubble_y = kDialoguePadding;
  const int bubble_w = std::max(58, avatar_x - kDialoguePadding - bubble_x);
  const int bubble_h = M5.Display.height() - kDialoguePadding * 2;
  const int tail_y = bubble_y + bubble_h / 2;
  render_element_dialogue_bubble(bubble_x, bubble_y, bubble_w, bubble_h, avatar_x - 1, tail_y);

  const int text_x = bubble_x + (large_title ? 7 : 10);
  const int text_width = bubble_w - (large_title ? 14 : 20);
  render_element_bubble_text(line1, line2, text_x, bubble_y + 5, text_width, large_title);
  render_element_battery_level();
}

uint16_t ScreenRenderer::avatarBackgroundColor() const {
  return M5.Display.color565(220, 224, 230);
}

uint16_t ScreenRenderer::dialogueBorderColor() const {
  return M5.Display.color565(60, 55, 50);
}

uint16_t ScreenRenderer::avatarColor(char cell) const {
  switch (cell) {
    case 'O':
      return M5.Display.color565(235, 122, 35);
    case 'S':
      return M5.Display.color565(244, 206, 154);
    case 'K':
      return M5.Display.color565(60, 55, 50);
    case 'W':
      return TFT_WHITE;
    case 'B':
    default:
      return avatarBackgroundColor();
  }
}

uint8_t ScreenRenderer::batteryLevelBars() const {
  return batteryLevelBarsFor(M5.Power.getBatteryLevel());
}

uint8_t ScreenRenderer::batteryLevelBarsFor(int32_t level) const {
  if (level < 0) {
    return 0;
  }
  const int32_t clamped = std::max<int32_t>(0, std::min<int32_t>(100, level));
  if (clamped == 0) {
    return 0;
  }
  return static_cast<uint8_t>(std::min<int32_t>(4, (clamped + 24) / 25));
}

size_t ScreenRenderer::readBodyTop() const {
  return static_cast<size_t>(kReadPaddingTop + kReadHeaderHeight + 4);
}

size_t ScreenRenderer::readViewportHeight() const {
  return static_cast<size_t>(
      std::max(1, M5.Display.height() - static_cast<int>(readBodyTop()) -
                      static_cast<int>(kReadPaddingBottom)));
}

size_t ScreenRenderer::estimateWrappedTextHeight(const std::string& text,
                                                 size_t width_px) const {
  if (width_px == 0 || text.empty()) {
    return kReadLineHeight;
  }
  size_t lines = 1;
  size_t line_width = 0;
  for (size_t i = 0; i < text.size();) {
    if (text[i] == '\n') {
      ++lines;
      line_width = 0;
      ++i;
      continue;
    }
    const size_t glyph_width = utf8DisplayWidthPx(text, &i);
    if (line_width > 0 && line_width + glyph_width > width_px) {
      ++lines;
      line_width = glyph_width;
    } else {
      line_width += glyph_width;
    }
  }
  return lines * kReadLineHeight;
}

size_t ScreenRenderer::utf8DisplayWidthPx(const std::string& text, size_t* offset) const {
  if (offset == nullptr || *offset >= text.size()) {
    return 0;
  }
  const uint8_t first = static_cast<uint8_t>(text[*offset]);
  if (text[*offset] == '\n') {
    ++(*offset);
    return 0;
  }
  if (first < 0x80) {
    ++(*offset);
    return first == ' ' ? 4 : 6;
  }
  size_t len = 1;
  if ((first & 0xE0) == 0xC0) {
    len = 2;
  } else if ((first & 0xF0) == 0xE0) {
    len = 3;
  } else if ((first & 0xF8) == 0xF0) {
    len = 4;
  }
  *offset = std::min(text.size(), *offset + len);
  return 12;
}

uint32_t ScreenRenderer::hashText(const char* text) const {
  constexpr uint32_t kFnvOffset = 2166136261UL;
  constexpr uint32_t kFnvPrime = 16777619UL;
  uint32_t hash = kFnvOffset;
  if (text == nullptr) {
    return hash;
  }
  while (*text != '\0') {
    hash ^= static_cast<uint8_t>(*text);
    hash *= kFnvPrime;
    ++text;
  }
  return hash;
}

}  // namespace screen
}  // namespace zero_buddy
