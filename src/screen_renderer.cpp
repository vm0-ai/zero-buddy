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
constexpr uint8_t kReadLineHeight = 18;
constexpr uint8_t kZeroAvatarWidth = 80;
constexpr uint8_t kZeroAvatarHeight = 45;
constexpr uint8_t kZeroAvatarScale = 2;
constexpr int kZeroAvatarCropLeft = kZeroAvatarWidth / 4;
constexpr int kZeroAvatarCropRight = kZeroAvatarWidth / 4;
constexpr int kZeroAvatarVisibleWidth =
    kZeroAvatarWidth - kZeroAvatarCropLeft - kZeroAvatarCropRight;
constexpr int kZeroAvatarRightPadding = 8;
constexpr int kDialoguePadding = 5;
constexpr int kTinyFontGlyphWidth = 5;
constexpr int kTinyFontGlyphHeight = 7;
constexpr int kTinyFontScale = 3;
constexpr int kTinyFontSpacing = 1;

struct TinyFontGlyph {
  char ch;
  uint8_t columns[kTinyFontGlyphWidth];
};

bool sameRenderScreenState(const state::RenderScreenState& lhs,
                           const state::RenderScreenState& rhs) {
  return lhs.kind == rhs.kind &&
         lhs.value1 == rhs.value1 &&
         lhs.value2 == rhs.value2 &&
         lhs.value3 == rhs.value3 &&
         lhs.value4 == rhs.value4;
}

constexpr TinyFontGlyph kTinyFont[] = {
    {'0', {0x3E, 0x49, 0x49, 0x49, 0x3E}},
    {'1', {0x00, 0x42, 0x7F, 0x40, 0x00}},
    {'2', {0x62, 0x51, 0x49, 0x49, 0x46}},
    {'3', {0x22, 0x41, 0x49, 0x49, 0x36}},
    {'4', {0x18, 0x14, 0x12, 0x7F, 0x10}},
    {'5', {0x2F, 0x49, 0x49, 0x49, 0x31}},
    {'6', {0x3E, 0x49, 0x49, 0x49, 0x32}},
    {'7', {0x01, 0x71, 0x09, 0x05, 0x03}},
    {'8', {0x36, 0x49, 0x49, 0x49, 0x36}},
    {'9', {0x26, 0x49, 0x49, 0x49, 0x3E}},
    {'-', {0x08, 0x08, 0x08, 0x08, 0x08}},
    {'.', {0x00, 0x60, 0x60, 0x00, 0x00}},
    {'A', {0x7E, 0x09, 0x09, 0x09, 0x7E}},
    {'B', {0x7F, 0x49, 0x49, 0x49, 0x36}},
    {'C', {0x3E, 0x41, 0x41, 0x41, 0x22}},
    {'D', {0x7F, 0x41, 0x41, 0x22, 0x1C}},
    {'E', {0x7F, 0x49, 0x49, 0x49, 0x41}},
    {'F', {0x7F, 0x09, 0x09, 0x09, 0x01}},
    {'G', {0x3E, 0x41, 0x49, 0x49, 0x7A}},
    {'H', {0x7F, 0x08, 0x08, 0x08, 0x7F}},
    {'I', {0x00, 0x41, 0x7F, 0x41, 0x00}},
    {'J', {0x20, 0x40, 0x41, 0x3F, 0x01}},
    {'K', {0x7F, 0x08, 0x14, 0x22, 0x41}},
    {'L', {0x7F, 0x40, 0x40, 0x40, 0x40}},
    {'M', {0x7F, 0x02, 0x0C, 0x02, 0x7F}},
    {'N', {0x7F, 0x04, 0x08, 0x10, 0x7F}},
    {'O', {0x3E, 0x41, 0x41, 0x41, 0x3E}},
    {'P', {0x7F, 0x09, 0x09, 0x09, 0x06}},
    {'Q', {0x3E, 0x41, 0x51, 0x21, 0x5E}},
    {'R', {0x7F, 0x09, 0x19, 0x29, 0x46}},
    {'S', {0x46, 0x49, 0x49, 0x49, 0x31}},
    {'T', {0x01, 0x01, 0x7F, 0x01, 0x01}},
    {'U', {0x3F, 0x40, 0x40, 0x40, 0x3F}},
    {'V', {0x1F, 0x20, 0x40, 0x20, 0x1F}},
    {'W', {0x3F, 0x40, 0x38, 0x40, 0x3F}},
    {'X', {0x63, 0x14, 0x08, 0x14, 0x63}},
    {'Y', {0x07, 0x08, 0x70, 0x08, 0x07}},
    {'Z', {0x61, 0x51, 0x49, 0x45, 0x43}},
};

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

void printCenteredFittedLine(const char* text,
                             int x,
                             int y,
                             int max_width,
                             bool bold = false) {
  const String line = fittedLine(text, max_width);
  const int line_x =
      x + std::max(0, (max_width - M5.Display.textWidth(line.c_str())) / 2);
  M5.Display.setCursor(line_x, y);
  M5.Display.print(line);
  if (bold && line.length() > 0) {
    M5.Display.setCursor(line_x + 1, y);
    M5.Display.print(line);
  }
}

const uint8_t* tinyFontGlyphFor(char ch) {
  if (ch >= 'a' && ch <= 'z') {
    ch = static_cast<char>(ch - 'a' + 'A');
  }
  for (const auto& glyph : kTinyFont) {
    if (glyph.ch == ch) {
      return glyph.columns;
    }
  }
  return nullptr;
}

int tinyFontTextWidth(const char* text, int scale, int spacing) {
  if (text == nullptr || text[0] == '\0') {
    return 0;
  }
  int width = 0;
  for (size_t i = 0; text[i] != '\0'; ++i) {
    width += (text[i] == ' ' ? 3 : kTinyFontGlyphWidth) * scale;
    if (text[i + 1] != '\0') {
      width += spacing * scale;
    }
  }
  return width;
}

String uppercaseAscii(const char* text) {
  String out(text != nullptr ? text : "");
  for (size_t i = 0; i < out.length(); ++i) {
    if (out[i] >= 'a' && out[i] <= 'z') {
      out.setCharAt(i, static_cast<char>(out[i] - 'a' + 'A'));
    }
  }
  return out;
}

void drawTinyFontText(const char* text,
                      int x,
                      int y,
                      int scale,
                      int spacing,
                      uint16_t color) {
  if (text == nullptr) {
    return;
  }
  int cursor_x = x;
  for (size_t i = 0; text[i] != '\0'; ++i) {
    const uint8_t* columns = tinyFontGlyphFor(text[i]);
    if (columns != nullptr) {
      for (int col = 0; col < kTinyFontGlyphWidth; ++col) {
        for (int row = 0; row < kTinyFontGlyphHeight; ++row) {
          if ((columns[col] & (1U << row)) != 0) {
            M5.Display.fillRect(cursor_x + col * scale,
                                y + row * scale,
                                scale,
                                scale,
                                color);
          }
        }
      }
    }
    cursor_x += ((text[i] == ' ' ? 3 : kTinyFontGlyphWidth) + spacing) * scale;
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
  render_avatar_layout_brand(state::RenderScreenKind::Boot, "Zero");
}

void ScreenRenderer::render_screen_read_empty() {
  render_avatar_layout_status(state::RenderScreenKind::ReadEmpty,
                              "all caught up",
                              "no message");
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
  if (sameRenderScreenState(previous, next)) {
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
  render_avatar_layout_status(state::RenderScreenKind::RecordingPrompt,
                              "recording",
                              "hold BtnA");
}

void ScreenRenderer::render_screen_recording_active() {
  render_avatar_layout_status(state::RenderScreenKind::RecordingActive,
                              "listening",
                              "speak now...");
}

void ScreenRenderer::render_screen_recording_wifi() {
  render_avatar_layout_status(state::RenderScreenKind::RecordingWifi,
                              "connecting",
                              "Wi-Fi...");
}

void ScreenRenderer::render_screen_recording_transcribing() {
  render_avatar_layout_status(state::RenderScreenKind::RecordingTranscribing,
                              "thinking",
                              "speech to text...");
}

void ScreenRenderer::render_screen_recording_sending(const std::string& user_text) {
  render_avatar_layout_preview(state::RenderScreenKind::RecordingSending,
                               "sending",
                               user_text);
}

void ScreenRenderer::render_screen_recording_sent() {
  render_avatar_layout_status(state::RenderScreenKind::RecordingSent,
                              "sent",
                              "message sent");
}

void ScreenRenderer::render_screen_recording_aborted() {
  render_avatar_layout_status(state::RenderScreenKind::RecordingAborted,
                              "aborted",
                              "recording");
}

void ScreenRenderer::render_screen_recording_failed(const char* detail) {
  render_avatar_layout_preview(state::RenderScreenKind::RecordingFailed,
                               "failed",
                               detail != nullptr ? detail : "");
}

void ScreenRenderer::render_screen_checking_messages() {
  render_avatar_layout_status(state::RenderScreenKind::CheckingMessages,
                              "checking",
                              "messages...");
}

void ScreenRenderer::render_screen_setup_wifi() {
  render_avatar_layout_instruction(state::RenderScreenKind::SetupWifi,
                                   "visit",
                                   "BB0.AI",
                                   "in Google Chrome",
                                   kTinyFontScale);
}

void ScreenRenderer::render_screen_setup_device_code(const char* device_code) {
  const String display_code = uppercaseAscii(device_code);
  render_avatar_layout_instruction(state::RenderScreenKind::SetupDeviceCode,
                                   "INPUT",
                                   display_code.c_str(),
                                   "IN BB0.AI",
                                   2);
}

void ScreenRenderer::render_screen_setup_status(const char* line1, const char* line2) {
  render_avatar_layout_status(state::RenderScreenKind::SetupStatus, line1, line2);
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

void ScreenRenderer::render_element_brand_text(const char* title,
                                               int bubble_x,
                                               int bubble_y,
                                               int bubble_w,
                                               int bubble_h) {
  const uint16_t border = dialogueBorderColor();
  M5.Display.setTextColor(border, TFT_WHITE);
  M5.Display.setTextWrap(false);
  M5.Display.setFont(&fonts::Font4);

  constexpr int kFont4Height = 26;
  const int text_x = bubble_x + 10;
  const int text_w = bubble_w - 20;
  const int text_y = bubble_y + std::max(0, (bubble_h - kFont4Height) / 2);
  printCenteredFittedLine(title, text_x, text_y, text_w, true);
}

void ScreenRenderer::render_element_instruction_text(const char* line1,
                                                     const char* emphasis,
                                                     const char* line3,
                                                     uint8_t emphasis_scale,
                                                     int bubble_x,
                                                     int bubble_y,
                                                     int bubble_w,
                                                     int bubble_h) {
  const uint16_t border = dialogueBorderColor();
  M5.Display.setTextColor(border, TFT_WHITE);
  M5.Display.setTextWrap(false);

  emphasis_scale = std::max<uint8_t>(1, emphasis_scale);
  constexpr int kInstructionLineGap = 12;
  const int text_x = bubble_x + 9;
  const int text_w = bubble_w - 18;
  const int normal_h = 20;
  const int emphasis_h = kTinyFontGlyphHeight * emphasis_scale;
  const int block_h = normal_h * 2 + emphasis_h + kInstructionLineGap * 2;
  const int block_y = bubble_y + std::max(0, (bubble_h - block_h) / 2);

  auto drawCenteredTiny = [&](const char* text, int y, uint8_t scale) {
    const int text_width =
        tinyFontTextWidth(text, scale, kTinyFontSpacing);
    const int x = text_x + std::max(0, (text_w - text_width) / 2);
    drawTinyFontText(text, x, y, scale, kTinyFontSpacing, border);
  };

  M5.Display.setFont(&fonts::Font2);
  printCenteredFittedLine(line1, text_x, block_y, text_w);
  drawCenteredTiny(emphasis, block_y + normal_h + kInstructionLineGap, emphasis_scale);
  M5.Display.setFont(&fonts::Font2);
  printCenteredFittedLine(line3,
                          text_x,
                          block_y + normal_h + kInstructionLineGap +
                              emphasis_h + kInstructionLineGap,
                          text_w);
}

void ScreenRenderer::render_element_status_pair_text(const char* title,
                                                     const char* detail,
                                                     int bubble_x,
                                                     int bubble_y,
                                                     int bubble_w,
                                                     int bubble_h) {
  const uint16_t border = dialogueBorderColor();
  M5.Display.setTextColor(border, TFT_WHITE);
  M5.Display.setTextWrap(false);

  const bool has_detail = detail != nullptr && detail[0] != '\0';
  const int text_x = bubble_x + 10;
  const int text_w = bubble_w - 20;
  const int block_h = has_detail ? 54 : 24;
  const int block_y = bubble_y + std::max(0, (bubble_h - block_h) / 2);
  M5.Display.setFont(&fonts::Font2);
  printCenteredFittedLine(title, text_x, block_y, text_w, true);
  if (has_detail) {
    M5.Display.setFont(&fonts::Font0);
    printCenteredFittedLine(detail, text_x, block_y + 38, text_w);
  }
}

void ScreenRenderer::render_element_preview_text(const char* title,
                                                 const std::string& body,
                                                 int bubble_x,
                                                 int bubble_y,
                                                 int bubble_w,
                                                 int bubble_h) {
  const uint16_t border = dialogueBorderColor();
  const int text_x = bubble_x + 10;
  const int title_y = bubble_y + 12;
  const int text_y = bubble_y + 44;
  const int text_w = bubble_w - 20;
  const int text_h = std::max(1, bubble_y + bubble_h - text_y - 10);

  M5.Display.setTextWrap(false);
  M5.Display.setTextColor(border, TFT_WHITE);
  M5.Display.setFont(&fonts::Font2);
  printFittedLine(title, text_x, title_y, text_w, true);

  M5.Display.setClipRect(text_x, text_y, text_w, text_h);
  M5.Display.setTextColor(border, TFT_WHITE);
  M5.Display.setFont(&fonts::efontCN_12);
  renderWrappedChatText(body, text_x, text_y, text_w, 0);
  M5.Display.clearClipRect();
  M5.Display.setTextWrap(false);
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
  M5.Display.setFont(&fonts::efontCN_12);
  renderWrappedChatText(message, kReadPaddingLeft, body_y, body_w, scroll_top);
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
    shared_state_->lastRenderScreenState = next;
  } else {
    fallback_render_state_ = next;
  }
}

void ScreenRenderer::clearRenderState() {
  if (shared_state_ != nullptr) {
    shared_state_->lastRenderScreenState = state::RenderScreenState();
  } else {
    fallback_render_state_ = state::RenderScreenState();
  }
  local_ = LocalState();
}

bool ScreenRenderer::sameRenderState(const state::RenderScreenState& next) const {
  return sameRenderScreenState(currentRenderState(), next);
}

void ScreenRenderer::resetElementState() {
  local_.battery_bars = 0xFF;
  local_.battery_visible = false;
}

bool ScreenRenderer::prepare_avatar_dialogue_screen(
    const state::RenderScreenState& next,
    uint8_t avatar_scale,
    AvatarDialogueLayout* layout_out) {
  if (sameRenderState(next)) {
    render_element_battery_level();
    return false;
  }
  const state::RenderScreenState previous = currentRenderState();
  const bool reuse_shell = canReuseAvatarDialogueShell(previous, avatar_scale);
  commitRenderState(next);
  if (!reuse_shell) {
    resetElementState();
  }
  if (layout_out != nullptr) {
    *layout_out = render_avatar_dialogue_shell(avatar_scale, reuse_shell);
  }
  return true;
}

void ScreenRenderer::render_avatar_layout_brand(state::RenderScreenKind kind,
                                                const char* title) {
  state::RenderScreenState next;
  next.kind = kind;
  next.value1 = hashText(title);
  next.value2 = hashText("layout-brand-v1");
  next.value4 = kZeroAvatarScale;

  AvatarDialogueLayout layout;
  if (!prepare_avatar_dialogue_screen(next, kZeroAvatarScale, &layout)) {
    return;
  }
  render_element_brand_text(title,
                            layout.bubble_x,
                            layout.bubble_y,
                            layout.bubble_w,
                            layout.bubble_h);
  render_element_battery_level();
}

void ScreenRenderer::render_avatar_layout_instruction(state::RenderScreenKind kind,
                                                      const char* line1,
                                                      const char* emphasis,
                                                      const char* line3,
                                                      uint8_t emphasis_scale) {
  state::RenderScreenState next;
  next.kind = kind;
  next.value1 = hashText(line1);
  next.value2 = hashText(emphasis);
  next.value3 = hashText(line3);
  next.value4 = static_cast<uint32_t>(emphasis_scale);

  AvatarDialogueLayout layout;
  if (!prepare_avatar_dialogue_screen(next, kZeroAvatarScale, &layout)) {
    return;
  }
  render_element_instruction_text(line1,
                                  emphasis,
                                  line3,
                                  emphasis_scale,
                                  layout.bubble_x,
                                  layout.bubble_y,
                                  layout.bubble_w,
                                  layout.bubble_h);
  render_element_battery_level();
}

void ScreenRenderer::render_avatar_layout_status(state::RenderScreenKind kind,
                                                 const char* title,
                                                 const char* detail) {
  state::RenderScreenState next;
  next.kind = kind;
  next.value1 = hashText(title);
  next.value2 = hashText(detail);
  next.value3 = hashText("layout-status-v1");
  next.value4 = kZeroAvatarScale;

  AvatarDialogueLayout layout;
  if (!prepare_avatar_dialogue_screen(next, kZeroAvatarScale, &layout)) {
    return;
  }
  render_element_status_pair_text(title,
                                  detail,
                                  layout.bubble_x,
                                  layout.bubble_y,
                                  layout.bubble_w,
                                  layout.bubble_h);
  render_element_battery_level();
}

void ScreenRenderer::render_avatar_layout_preview(state::RenderScreenKind kind,
                                                  const char* title,
                                                  const std::string& body) {
  state::RenderScreenState next;
  next.kind = kind;
  next.value1 = hashText(title);
  next.value2 = hashString(body);
  next.value3 = hashText("layout-preview-v1");
  next.value4 = kZeroAvatarScale;

  AvatarDialogueLayout layout;
  if (!prepare_avatar_dialogue_screen(next, kZeroAvatarScale, &layout)) {
    return;
  }
  render_element_preview_text(title,
                              body,
                              layout.bubble_x,
                              layout.bubble_y,
                              layout.bubble_w,
                              layout.bubble_h);
  render_element_battery_level();
}

bool ScreenRenderer::isAvatarDialogueScreenKind(state::RenderScreenKind kind) const {
  switch (kind) {
    case state::RenderScreenKind::Boot:
    case state::RenderScreenKind::ReadEmpty:
    case state::RenderScreenKind::RecordingPrompt:
    case state::RenderScreenKind::RecordingActive:
    case state::RenderScreenKind::RecordingWifi:
    case state::RenderScreenKind::RecordingTranscribing:
    case state::RenderScreenKind::RecordingSending:
    case state::RenderScreenKind::RecordingSent:
    case state::RenderScreenKind::RecordingAborted:
    case state::RenderScreenKind::RecordingFailed:
    case state::RenderScreenKind::CheckingMessages:
    case state::RenderScreenKind::SetupWifi:
    case state::RenderScreenKind::SetupDeviceCode:
    case state::RenderScreenKind::SetupStatus:
      return true;
    default:
      return false;
  }
}

bool ScreenRenderer::canReuseAvatarDialogueShell(
    const state::RenderScreenState& previous,
    uint8_t avatar_scale) const {
  return isAvatarDialogueScreenKind(previous.kind) &&
         previous.value4 == static_cast<uint32_t>(avatar_scale);
}

ScreenRenderer::AvatarDialogueLayout ScreenRenderer::avatarDialogueLayout(
    uint8_t avatar_scale) const {
  avatar_scale = std::max<uint8_t>(1, avatar_scale);
  AvatarDialogueLayout layout;
  const int avatar_w = kZeroAvatarVisibleWidth * avatar_scale;
  const int avatar_h = static_cast<int>(kZeroAvatarHeight) * avatar_scale;
  layout.avatar_x = M5.Display.width() - avatar_w - kZeroAvatarRightPadding;
  layout.avatar_y = (M5.Display.height() - avatar_h) / 2;
  layout.bubble_x = kDialoguePadding;
  layout.bubble_y = kDialoguePadding;
  layout.bubble_w = std::max(58, layout.avatar_x - kDialoguePadding - layout.bubble_x);
  layout.bubble_h = M5.Display.height() - kDialoguePadding * 2;
  layout.tail_y = layout.bubble_y + layout.bubble_h / 2;
  return layout;
}

ScreenRenderer::AvatarDialogueLayout ScreenRenderer::render_avatar_dialogue_shell(
    uint8_t avatar_scale,
    bool reuse_shell) {
  screenOn();
  const AvatarDialogueLayout layout = avatarDialogueLayout(avatar_scale);
  if (reuse_shell) {
    clear_avatar_dialogue_content(layout);
    return layout;
  }

  const uint16_t bg = avatarBackgroundColor();
  M5.Display.fillScreen(bg);
  render_element_zero_avatar(layout.avatar_x,
                             layout.avatar_y,
                             avatar_scale,
                             kZeroAvatarCropLeft,
                             kZeroAvatarVisibleWidth);
  render_element_dialogue_bubble(layout.bubble_x,
                                 layout.bubble_y,
                                 layout.bubble_w,
                                 layout.bubble_h,
                                 layout.avatar_x - 1,
                                 layout.tail_y);
  return layout;
}

void ScreenRenderer::clear_avatar_dialogue_content(
    const AvatarDialogueLayout& layout) {
  const int x = layout.bubble_x + 4;
  const int y = layout.bubble_y + 4;
  const int w = std::max(0, layout.bubble_w - 8);
  const int h = std::max(0, layout.bubble_h - 8);
  if (w <= 0 || h <= 0) {
    return;
  }
  M5.Display.fillRoundRect(x, y, w, h, 4, TFT_WHITE);
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

void ScreenRenderer::renderWrappedChatText(const std::string& text,
                                           int x,
                                           int y,
                                           int width_px,
                                           size_t scroll_top) {
  const int line_height = static_cast<int>(kReadLineHeight);
  int line_y = y - static_cast<int>(scroll_top);
  size_t line_width = 0;
  std::string line;
  M5.Display.setTextWrap(false);

  auto flush_line = [&]() {
    if (!line.empty()) {
      M5.Display.setCursor(x, line_y);
      M5.Display.print(line.c_str());
      line.clear();
    }
    line_width = 0;
    line_y += line_height;
  };

  for (size_t i = 0; i < text.size();) {
    if (text[i] == '\n') {
      ++i;
      flush_line();
      continue;
    }

    const size_t glyph_start = i;
    const size_t glyph_width = utf8DisplayWidthPx(text, &i);
    if (line_width > 0 &&
        line_width + glyph_width > static_cast<size_t>(std::max(1, width_px))) {
      flush_line();
    }
    line.append(text, glyph_start, i - glyph_start);
    line_width += glyph_width;
  }

  flush_line();
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
