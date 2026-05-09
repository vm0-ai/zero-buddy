#include "screen_renderer.h"

#include <Arduino.h>
#include <M5Unified.h>

#include "spleen_8x16.h"

#include <algorithm>
#include <vector>

namespace zero_buddy {
namespace screen {
namespace {

constexpr uint8_t kPortraitRotation = 0;
constexpr uint8_t kReadPaddingTop = 8;
constexpr uint8_t kReadPaddingRight = 8;
constexpr uint8_t kReadPaddingLeft = kReadPaddingRight + 2;
constexpr uint8_t kReadPaddingBottom = 8;
constexpr uint8_t kReadHeaderHeight = 14;
constexpr uint8_t kReadLineHeight = 24;
constexpr uint8_t kChatSpaceWidth = 8;
constexpr uint8_t kChatCjkWidth = 16;
constexpr int8_t kChatCjkYOffset = 2;
constexpr uint8_t kZeroAvatarWidth = 80;
constexpr uint8_t kZeroAvatarSpriteWidth = 40;
constexpr uint8_t kZeroAvatarHeight = 45;
constexpr uint8_t kZeroAvatarScale = 2;
constexpr uint8_t kZeroAvatarFrameCount = 5;
constexpr int kZeroAvatarCropLeft = (kZeroAvatarWidth - kZeroAvatarSpriteWidth) / 2;
constexpr int kZeroAvatarCropRight = kZeroAvatarCropLeft;
constexpr int kZeroAvatarVisibleWidth =
    kZeroAvatarWidth - kZeroAvatarCropLeft - kZeroAvatarCropRight;
constexpr uint32_t kAvatarBlinkIntervalMs = 3200;
constexpr uint32_t kAvatarBlinkFrameMs = 90;
constexpr uint8_t kAvatarBlinkOpenFrame = 0;
constexpr uint8_t kAvatarBlinkFinalFrame = kZeroAvatarFrameCount;
constexpr int kDialoguePadding = 5;
constexpr int kAvatarBubbleGap = 5;
constexpr int kMinDialogueBubbleHeight = 58;
constexpr int kTopAvatarRightPadding = 4;
constexpr int kTopAvatarTopPadding = 5;
constexpr int kTopBarBottomGap = 5;
constexpr int kBatteryBodyWidth = 15;
constexpr int kBatteryBodyHeight = 10;
constexpr int kBatteryCapWidth = 2;
constexpr int kBatteryPercentWidth = 28;
constexpr int kBatteryClearHeight = 13;
constexpr int kCheckingIconWidth = 10;
constexpr int kCheckingIconHeight = 14;
constexpr int kCheckingIconGap = 5;
constexpr int kCheckingIconClearPadding = 2;
constexpr int kTinyFontGlyphWidth = 5;
constexpr int kTinyFontGlyphHeight = 7;
constexpr int kTinyFontScale = 3;
constexpr int kTinyFontSpacing = 1;

struct TinyFontGlyph {
  char ch;
  uint8_t columns[kTinyFontGlyphWidth];
};

struct ChatGlyph {
  std::string text;
  const lgfx::IFont* font = &fonts::efontCN_16;
  size_t width = kChatCjkWidth;
  int8_t y_offset = 0;
  bool spleen_ascii = false;
  char ascii = '\0';
};

bool sameRenderScreenState(const state::RenderScreenState& lhs,
                           const state::RenderScreenState& rhs) {
  return lhs.kind == rhs.kind &&
         lhs.value1 == rhs.value1 &&
         lhs.value2 == rhs.value2 &&
         lhs.value3 == rhs.value3 &&
         lhs.value4 == rhs.value4;
}

bool fontSupportsCodepoint(const lgfx::IFont* font, uint32_t codepoint) {
  if (font == nullptr || codepoint > 0xFFFF) {
    return false;
  }
  lgfx::FontMetrics metrics;
  font->getDefaultMetric(&metrics);
  return font->updateFontMetric(&metrics, static_cast<uint16_t>(codepoint));
}

const lgfx::IFont* chatFontForCodepoint(uint32_t codepoint) {
  const lgfx::IFont* fallback_fonts[] = {
      &fonts::efontCN_16,
      &fonts::efontJA_16,
      &fonts::efontTW_16,
  };
  for (const lgfx::IFont* font : fallback_fonts) {
    if (fontSupportsCodepoint(font, codepoint)) {
      return font;
    }
  }
  return nullptr;
}

bool isSpleenAscii(uint32_t codepoint) {
  return codepoint >= kSpleen8x16First && codepoint <= kSpleen8x16Last;
}

const uint8_t* spleenGlyphFor(char ch) {
  const uint8_t code = static_cast<uint8_t>(ch);
  if (!isSpleenAscii(code)) {
    return nullptr;
  }
  return kSpleen8x16[code - kSpleen8x16First];
}

uint16_t currentTextColor() {
  return M5.Display.color24to16(M5.Display.getTextStyle().fore_rgb888);
}

void drawSpleenGlyph(char ch, int x, int y, uint16_t color) {
  const uint8_t* rows = spleenGlyphFor(ch);
  if (rows == nullptr) {
    return;
  }
  for (uint8_t row = 0; row < kSpleen8x16Height; ++row) {
    const uint8_t bits = rows[row];
    for (uint8_t col = 0; col < kSpleen8x16Width; ++col) {
      if ((bits & (0x80 >> col)) != 0) {
        M5.Display.drawPixel(x + col, y + row, color);
      }
    }
  }
}

size_t decodeUtf8Codepoint(const std::string& text,
                           size_t offset,
                           uint32_t* codepoint_out) {
  if (codepoint_out == nullptr || offset >= text.size()) {
    return 0;
  }
  const uint8_t first = static_cast<uint8_t>(text[offset]);
  if (first < 0x80) {
    *codepoint_out = first;
    return 1;
  }
  if ((first & 0xE0) == 0xC0 && offset + 1 < text.size()) {
    *codepoint_out =
        ((first & 0x1F) << 6) |
        (static_cast<uint8_t>(text[offset + 1]) & 0x3F);
    return 2;
  }
  if ((first & 0xF0) == 0xE0 && offset + 2 < text.size()) {
    *codepoint_out =
        ((first & 0x0F) << 12) |
        ((static_cast<uint8_t>(text[offset + 1]) & 0x3F) << 6) |
        (static_cast<uint8_t>(text[offset + 2]) & 0x3F);
    return 3;
  }
  if ((first & 0xF8) == 0xF0 && offset + 3 < text.size()) {
    *codepoint_out =
        ((first & 0x07) << 18) |
        ((static_cast<uint8_t>(text[offset + 1]) & 0x3F) << 12) |
        ((static_cast<uint8_t>(text[offset + 2]) & 0x3F) << 6) |
        (static_cast<uint8_t>(text[offset + 3]) & 0x3F);
    return 4;
  }
  *codepoint_out = '?';
  return 1;
}

ChatGlyph chatGlyphAt(const std::string& text, size_t* offset) {
  ChatGlyph glyph;
  if (offset == nullptr || *offset >= text.size()) {
    glyph.text = "";
    glyph.width = 0;
    return glyph;
  }

  uint32_t codepoint = 0;
  const size_t len = decodeUtf8Codepoint(text, *offset, &codepoint);
  const size_t safe_len = std::max<size_t>(1, len);
  *offset = std::min(text.size(), *offset + safe_len);

  if (codepoint < 0x80) {
    glyph.text.assign(1, static_cast<char>(codepoint));
    glyph.spleen_ascii = isSpleenAscii(codepoint);
    glyph.ascii = glyph.spleen_ascii ? static_cast<char>(codepoint) : '?';
    glyph.width = kSpleen8x16Width;
    return glyph;
  }

  const lgfx::IFont* font = chatFontForCodepoint(codepoint);
  if (font == nullptr) {
    glyph.text = "?";
    glyph.spleen_ascii = true;
    glyph.ascii = '?';
    glyph.width = kSpleen8x16Width;
    return glyph;
  }

  glyph.text.assign(text, *offset - safe_len, safe_len);
  glyph.font = font;
  glyph.y_offset = kChatCjkYOffset;
  glyph.width =
      std::max<int32_t>(1, M5.Display.textWidth(glyph.text.c_str(), font));
  return glyph;
}

std::vector<ChatGlyph> chatGlyphsForText(const char* text) {
  std::vector<ChatGlyph> glyphs;
  const std::string value(text != nullptr ? text : "");
  for (size_t i = 0; i < value.size();) {
    if (value[i] == '\n') {
      break;
    }
    glyphs.push_back(chatGlyphAt(value, &i));
  }
  return glyphs;
}

size_t chatGlyphsWidth(const std::vector<ChatGlyph>& glyphs) {
  size_t width = 0;
  for (const ChatGlyph& glyph : glyphs) {
    width += glyph.width;
  }
  return width;
}

void fitChatGlyphsToWidth(std::vector<ChatGlyph>* glyphs, int max_width) {
  if (glyphs == nullptr || max_width <= 0 ||
      chatGlyphsWidth(*glyphs) <= static_cast<size_t>(max_width)) {
    return;
  }
  std::vector<ChatGlyph> ellipsis = chatGlyphsForText("...");
  const size_t ellipsis_width = chatGlyphsWidth(ellipsis);
  while (!glyphs->empty() &&
         chatGlyphsWidth(*glyphs) + ellipsis_width >
             static_cast<size_t>(max_width)) {
    glyphs->pop_back();
  }
  if (ellipsis_width <= static_cast<size_t>(max_width)) {
    glyphs->insert(glyphs->end(), ellipsis.begin(), ellipsis.end());
  }
}

void drawChatGlyphs(const std::vector<ChatGlyph>& glyphs,
                    int x,
                    int y,
                    bool bold = false) {
  int cursor_x = x;
  const uint16_t fg = currentTextColor();
  for (const ChatGlyph& glyph : glyphs) {
    if (glyph.spleen_ascii) {
      drawSpleenGlyph(glyph.ascii, cursor_x, y + glyph.y_offset, fg);
    } else {
      M5.Display.setFont(glyph.font);
      M5.Display.setCursor(cursor_x, y + glyph.y_offset);
      M5.Display.print(glyph.text.c_str());
      if (bold && !glyph.text.empty()) {
        M5.Display.setCursor(cursor_x + 1, y + glyph.y_offset);
        M5.Display.print(glyph.text.c_str());
      }
    }
    cursor_x += static_cast<int>(glyph.width);
  }
}

void drawCenteredChatLine(const char* text,
                          int x,
                          int y,
                          int max_width,
                          bool bold = false) {
  std::vector<ChatGlyph> glyphs = chatGlyphsForText(text);
  fitChatGlyphsToWidth(&glyphs, max_width);
  const int line_x =
      x + std::max(0, (max_width - static_cast<int>(chatGlyphsWidth(glyphs))) / 2);
  drawChatGlyphs(glyphs, line_x, y, bold);
}

void drawFittedChatLine(const char* text,
                        int x,
                        int y,
                        int max_width,
                        bool bold = false) {
  std::vector<ChatGlyph> glyphs = chatGlyphsForText(text);
  fitChatGlyphsToWidth(&glyphs, max_width);
  drawChatGlyphs(glyphs, x, y, bold);
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

constexpr char kZeroAvatarFrames[kZeroAvatarFrameCount][kZeroAvatarHeight][kZeroAvatarSpriteWidth + 1] = {
    {
        "0000000000000000000000000000000000000000",
        "0000000000000000000000000000000000000000",
        "00000000000000000SSSSS000000000000000000",
        "0000000000000SOOOOOOOOOOSS00000000000000",
        "00000000SSOOOOOOOOOOOOOOOOOS000000000000",
        "0000000OOOOOOOOOOOOOOOOOOOOOOS0000000000",
        "000000OOOOOOOOOOOOOOOOOOOOOOOOO000000000",
        "00000OOOOOOOOOOOOOOOOOOOOOOOOOOO00000000",
        "0000SOOOOKKOOOOOOOOKOOOOOOOOOOOOO0000000",
        "0000OOOKKOOOKKOOOOKKKOOOOOOOOOOOOO000000",
        "000SOOOKOOOOKKOOOOKKOOOOOOOOOOOOOOS00000",
        "000OOOKOOOOKKKOOOOKKOOOOOOOOOOOOOOO00000",
        "00SOOOKOOOOKKKOOOKKKKKOOOOOOOOOOOOO00000",
        "00SOOOKKKKKKKKKKKKOOOOOOOOOOOOOOOOO00000",
        "00SOOOOOKKKOOOOOOOOOOOOOOOOOOOOOOOO00000",
        "00OOOOOOOOOOOOOOOOOSSSSSSOOOOOOOOOO00000",
        "00SOOOOOOOOOOOOOOSSSSSSSSSOOOOOOOOS00000",
        "000OOOOOKKKOOOOOOKKKKSSSSSSOOOOOOO000000",
        "000SOOOOOOOKOOSSKSSSSSSSSSSOOOOOOS000000",
        "00000SSSSSSSSSSSSSSSSSSSSSSOOOOOSSSS0000",
        "00000SSSSSSSSSSSSSSSSSSSSSSSOOSSSSSS0000",
        "00000SSSOOSSSSSSSSsskGSSSSSSSSSSKSSSS000",
        "00000SSKKKKKGSSSSGKKGGSSSSSSSSSKSSSSS000",
        "00000SSKKsWSkSSSSGKGwwwSSSSSSOKKKKSS0000",
        "00000SSKKSWSKGSSSGKGWWwSSSSSSSOOOSSS0000",
        "00000SSKKwWSkGSSSGKKWwSSSSSSSSSSSSSS0000",
        "00000SSKKwWSkSSSSGKGWSSSSSSSSSSSSSS00000",
        "00000SSSSSSooSSSSSSSSSSSSSSSSSSSSOOS0000",
        "00000sSSSSSGKoSSSSSSSSSSSSSSSSOOOOOS0000",
        "0000SOOSSSSSSSSSSSSSSSSSSSSSSOOOOOOS0000",
        "0000SOOSSSSSSSSSSSSKSSSSSSSSOOOOOOOO0000",
        "0000SOOOSSSSSSOOSOKSSSSSSSSOOOOOOOOO0000",
        "0000SOOOOSSSSSOKKKSSSSSSSSOOOOOOOOOO0000",
        "0000SOOOOOSSSSSSSSSSSSSSOOOOOOOOOOOO0000",
        "00000OOOOOOOSSSSSSSSSSSOOOOOOOOOOOOO0000",
        "00000OOOOOOOOOOOOSSSSSOOOOOOOOOOOOOO0000",
        "00000SOOOOOOOOOOOSSSSSOOOOOOOOOOOOOS0000",
        "00000SOOOOOOOOOOKKKKKKKKKOOOOOOOOOOS0000",
        "000000SOOOOOOOKKKKKKKKKKKKOOOOOOOOO00000",
        "00000000SSOOOKKKKKKKKKKKKKKOSSOOOO000000",
        "000000000000KKKKKKKKKKKKKKK0000S00000000",
        "00000000000SKKKKKKKKKKKKKKK0000000000000",
        "000000000000KKKKKKKKKKKKKK00000000000000",
        "0000000000000000000000000000000000000000",
        "0000000000000000000000000000000000000000",
    },
    {
        "0000000000000000000000000000000000000000",
        "0000000000000000000000000000000000000000",
        "00000000000000000SSSSS000000000000000000",
        "0000000000000SOOOOOOOOOOSS00000000000000",
        "00000000SSOOOOOOOOOOOOOOOOOS000000000000",
        "0000000OOOOOOOOOOOOOOOOOOOOOOS0000000000",
        "000000OOOOOOOOOOOOOOOOOOOOOOOOO000000000",
        "00000OOOOOOOOOOOOOOOOOOOOOOOOOOO00000000",
        "0000SOOOOKKOOOOOOOOKOOOOOOOOOOOOO0000000",
        "0000OOOKKOOOKKOOOOKKKOOOOOOOOOOOOO000000",
        "000SOOOKOOOOKKOOOOKKOOOOOOOOOOOOOOS00000",
        "000OOOKOOOOKKKOOOOKKOOOOOOOOOOOOOOO00000",
        "00SOOOKOOOOKKKOOOKKKKKOOOOOOOOOOOOO00000",
        "00SOOOKKKKKKKKKKKKOOOOOOOOOOOOOOOOO00000",
        "00SOOOOOKKKOOOOOOOOOOOOOOOOOOOOOOOO00000",
        "00OOOOOOOOOOOOOOOOOSSSSSSOOOOOOOOOO00000",
        "00SOOOOOOOOOOOOOOSSSSSSSSSOOOOOOOOS00000",
        "000OOOOOKKKOOOOOOKKKKSSSSSSOOOOOOO000000",
        "000SOOOOOOOKOOSSKSSSSSSSSSSOOOOOOS000000",
        "00000SSSSSSSSSSSSSSSSSSSSSSOOOOOSSSS0000",
        "00000SSSSSSSSSSSSSSSSSSSSSSSOOSSSSSS0000",
        "00000SSSSSSSSSSSSSSSSSSSSSSSSSSSKSSSS000",
        "00000SSSSSSSSSSSSSSSSSSSSSSSSSSKSSSSS000",
        "00000SSssOOssSSSSsOOssSSSSSSSOKKKKSS0000",
        "00000SsKKKKKKSSSSKKKKKkSSSSSSSOOOSSS0000",
        "00000SSKKSwSKSSSSKKwwwSSSSSSSSSSSSSS0000",
        "00000SSSGSwsoSSSSsGwwSSSSSSSSSSSSSS00000",
        "00000SSSSSSKKOSSSSSSSSSSSSSSSSSSSOOS0000",
        "00000OSSSSSSKkSSSSSSSSSSSSSSSSOOOOOS0000",
        "0000SOOSSSSSSSSSSSSSSSSSSSSSSOOOOOOS0000",
        "0000SOOSSSSSSSSSSSSKSSSSSSSSOOOOOOOO0000",
        "0000SOOOSSSSSSOOSOKSSSSSSSSOOOOOOOOO0000",
        "0000SOOOOSSSSSOKKKSSSSSSSSOOOOOOOOOO0000",
        "0000SOOOOOSSSSSSSSSSSSSSOOOOOOOOOOOO0000",
        "00000OOOOOOOSSSSSSSSSSSOOOOOOOOOOOOO0000",
        "00000OOOOOOOOOOOOSSSSSOOOOOOOOOOOOOO0000",
        "00000SOOOOOOOOOOOSSSSSOOOOOOOOOOOOOS0000",
        "00000SOOOOOOOOOOKKKKKKKKKOOOOOOOOOOS0000",
        "000000SOOOOOOOKKKKKKKKKKKKOOOOOOOOO00000",
        "00000000SSOOOKKKKKKKKKKKKKKOSSOOOO000000",
        "000000000000KKKKKKKKKKKKKKK0000S00000000",
        "00000000000SKKKKKKKKKKKKKKK0000000000000",
        "000000000000KKKKKKKKKKKKKK00000000000000",
        "0000000000000000000000000000000000000000",
        "0000000000000000000000000000000000000000",
    },
    {
        "0000000000000000000000000000000000000000",
        "0000000000000000000000000000000000000000",
        "00000000000000000SSSSS000000000000000000",
        "0000000000000SOOOOOOOOOOSS00000000000000",
        "00000000SSOOOOOOOOOOOOOOOOOS000000000000",
        "0000000OOOOOOOOOOOOOOOOOOOOOOS0000000000",
        "000000OOOOOOOOOOOOOOOOOOOOOOOOO000000000",
        "00000OOOOOOOOOOOOOOOOOOOOOOOOOOO00000000",
        "0000SOOOOKKOOOOOOOOKOOOOOOOOOOOOO0000000",
        "0000OOOKKOOOKKOOOOKKKOOOOOOOOOOOOO000000",
        "000SOOOKOOOOKKOOOOKKOOOOOOOOOOOOOOS00000",
        "000OOOKOOOOKKKOOOOKKOOOOOOOOOOOOOOO00000",
        "00SOOOKOOOOKKKOOOKKKKKOOOOOOOOOOOOO00000",
        "00SOOOKKKKKKKKKKKKOOOOOOOOOOOOOOOOO00000",
        "00SOOOOOKKKOOOOOOOOOOOOOOOOOOOOOOOO00000",
        "00OOOOOOOOOOOOOOOOOSSSSSSOOOOOOOOOO00000",
        "00SOOOOOOOOOOOOOOSSSSSSSSSOOOOOOOOS00000",
        "000OOOOOKKKOOOOOOKKKKSSSSSSOOOOOOO000000",
        "000SOOOOOOOKOOSSKSSSSSSSSSSOOOOOOS000000",
        "00000SSSSSSSSSSSSSSSSSSSSSSOOOOOSSSS0000",
        "00000SSSSSSSSSSSSSSSSSSSSSSSOOSSSSSS0000",
        "00000SSSSSSSSSSSSSSSSSSSSSSSSSSSKSSSS000",
        "00000SSSSSSSSSSSSSSSSSSSSSSSSSSKSSSSS000",
        "00000SSSSSSSSSSSSSSSSSSSSSSSSSOKKKSS0000",
        "00000SSSSSSSSSSSSSSSSSSSSSSSSSSOOSSS0000",
        "00000SSSSSSSsSSSSSSSSSSSSSSSSSSSSSSS0000",
        "00000SSkSSSSkSSSkSSSSkSSSSSSSSSSSSS00000",
        "00000SSSkkkkSSSSSkkkkSSSSSSSSSSSSOOS0000",
        "00000SSSSSSSSSSSSSSSSSSSSSSSSSOOOOOS0000",
        "0000SOOSSSSSSSSSSSSSSSSSSSSSSOOOOOOS0000",
        "0000SOOSSSSSSSSSSSSKSSSSSSSSOOOOOOOO0000",
        "0000SOOOSSSSSSOOSOKSSSSSSSSOOOOOOOOO0000",
        "0000SOOOOSSSSSOKKKSSSSSSSSOOOOOOOOOO0000",
        "0000SOOOOOSSSSSSSSSSSSSSOOOOOOOOOOOO0000",
        "00000OOOOOOOSSSSSSSSSSSOOOOOOOOOOOOO0000",
        "00000OOOOOOOOOOOOSSSSSOOOOOOOOOOOOOO0000",
        "00000SOOOOOOOOOOOSSSSSOOOOOOOOOOOOOS0000",
        "00000SOOOOOOOOOOKKKKKKKKKOOOOOOOOOOS0000",
        "000000SOOOOOOOKKKKKKKKKKKKOOOOOOOOO00000",
        "00000000SSOOOKKKKKKKKKKKKKKOSSOOOO000000",
        "000000000000KKKKKKKKKKKKKKK0000S00000000",
        "00000000000SKKKKKKKKKKKKKKK0000000000000",
        "000000000000KKKKKKKKKKKKKK00000000000000",
        "0000000000000000000000000000000000000000",
        "0000000000000000000000000000000000000000",
    },
    {
        "0000000000000000000000000000000000000000",
        "0000000000000000000000000000000000000000",
        "00000000000000000SSSSS000000000000000000",
        "0000000000000SOOOOOOOOOOSS00000000000000",
        "00000000SSOOOOOOOOOOOOOOOOOS000000000000",
        "0000000OOOOOOOOOOOOOOOOOOOOOOS0000000000",
        "000000OOOOOOOOOOOOOOOOOOOOOOOOO000000000",
        "00000OOOOOOOOOOOOOOOOOOOOOOOOOOO00000000",
        "0000SOOOOKKOOOOOOOOKOOOOOOOOOOOOO0000000",
        "0000OOOKKOOOKKOOOOKKKOOOOOOOOOOOOO000000",
        "000SOOOKOOOOKKOOOOKKOOOOOOOOOOOOOOS00000",
        "000OOOKOOOOKKKOOOOKKOOOOOOOOOOOOOOO00000",
        "00SOOOKOOOOKKKOOOKKKKKOOOOOOOOOOOOO00000",
        "00SOOOKKKKKKKKKKKKOOOOOOOOOOOOOOOOO00000",
        "00SOOOOOKKKOOOOOOOOOOOOOOOOOOOOOOOO00000",
        "00OOOOOOOOOOOOOOOOOSSSSSSOOOOOOOOOO00000",
        "00SOOOOOOOOOOOOOOSSSSSSSSSOOOOOOOOS00000",
        "000OOOOOKKKOOOOOOKKKKSSSSSSOOOOOOO000000",
        "000SOOOOOOOKOOSSKSSSSSSSSSSOOOOOOS000000",
        "00000SSSSSSSSSSSSSSSSSSSSSSOOOOOSSSS0000",
        "00000SSSSSSSSSSSSSSSSSSSSSSSOOSSSSSS0000",
        "00000SSSSSSSSSSSSSSSSSSSSSSSSSSSKSSSS000",
        "00000SSSSSSSSSSSSSSSSSSSSSSSSSSKSSSSS000",
        "00000SSSSSSSSSSSSSSSSSSSSSSSSOKKKKSS0000",
        "00000SkKKKKKKSSSSKKKKKkSSSSSSSOOOSSS0000",
        "00000SKKKKKKKSSSSKKKGGKSSSSSSSSSSSSS0000",
        "00000SSKkWWSKSSSSKKwWSSSSSSSSSSSSSS00000",
        "00000SSSSSSooSSSSSSSSSSSSSSSSSSSSOOS0000",
        "00000sSSSSSGKoSSSSSSSSSSSSSSSSOOOOOS0000",
        "0000SOOSSSSSSSSSSSSSSSSSSSSSSOOOOOOS0000",
        "0000SOOSSSSSSSSSSSSKSSSSSSSSOOOOOOOO0000",
        "0000SOOOSSSSSSOOSOKSSSSSSSSOOOOOOOOO0000",
        "0000SOOOOSSSSSOKKKSSSSSSSSOOOOOOOOOO0000",
        "0000SOOOOOSSSSSSSSSSSSSSOOOOOOOOOOOO0000",
        "00000OOOOOOOSSSSSSSSSSSOOOOOOOOOOOOO0000",
        "00000OOOOOOOOOOOOSSSSSOOOOOOOOOOOOOO0000",
        "00000SOOOOOOOOOOOSSSSSOOOOOOOOOOOOOS0000",
        "00000SOOOOOOOOOOKKKKKKKKKOOOOOOOOOOS0000",
        "000000SOOOOOOOKKKKKKKKKKKKOOOOOOOOO00000",
        "00000000SSOOOKKKKKKKKKKKKKKOSSOOOO000000",
        "000000000000KKKKKKKKKKKKKKK0000S00000000",
        "00000000000SKKKKKKKKKKKKKKK0000000000000",
        "000000000000KKKKKKKKKKKKKK00000000000000",
        "0000000000000000000000000000000000000000",
        "0000000000000000000000000000000000000000",
    },
    {
        "0000000000000000000000000000000000000000",
        "0000000000000000000000000000000000000000",
        "00000000000000000SSSSS000000000000000000",
        "0000000000000SOOOOOOOOOOSS00000000000000",
        "00000000SSOOOOOOOOOOOOOOOOOS000000000000",
        "0000000OOOOOOOOOOOOOOOOOOOOOOS0000000000",
        "000000OOOOOOOOOOOOOOOOOOOOOOOOO000000000",
        "00000OOOOOOOOOOOOOOOOOOOOOOOOOOO00000000",
        "0000SOOOOKKOOOOOOOOKOOOOOOOOOOOOO0000000",
        "0000OOOKKOOOKKOOOOKKKOOOOOOOOOOOOO000000",
        "000SOOOKOOOOKKOOOOKKOOOOOOOOOOOOOOS00000",
        "000OOOKOOOOKKKOOOOKKOOOOOOOOOOOOOOO00000",
        "00SOOOKOOOOKKKOOOKKKKKOOOOOOOOOOOOO00000",
        "00SOOOKKKKKKKKKKKKOOOOOOOOOOOOOOOOO00000",
        "00SOOOOOKKKOOOOOOOOOOOOOOOOOOOOOOOO00000",
        "00OOOOOOOOOOOOOOOOOSSSSSSOOOOOOOOOO00000",
        "00SOOOOOOOOOOOOOOSSSSSSSSSOOOOOOOOS00000",
        "000OOOOOKKKOOOOOOKKKKSSSSSSOOOOOOO000000",
        "000SOOOOOOOKOOSSKSSSSSSSSSSOOOOOOS000000",
        "00000SSSSSSSSSSSSSSSSSSSSSSOOOOOSSSS0000",
        "00000SSSSSSSSSSSSSSSSSSSSSSSOOSSSSSS0000",
        "00000SSSOOSSSSSSSssoksSSSSSSSSSSKSSSS000",
        "00000SSKKKKoKSSSsKKKGGsSSSSSSSSKSSSSS000",
        "00000SSKKsWSkSSSSKKGwWSSSSSSSOKKKKSS0000",
        "00000SSKKwWwkkSSSKKGWWSSSSSSSSOOOSSS0000",
        "00000SSKKwWwkGSSSKKGWWSSSSSSSSSSSSSS0000",
        "00000SSKKWWSkSSSSKKGWSSSSSSSSSSSSSS00000",
        "00000SSSSSSooSSSSSSSSSSSSSSSSSSSSOOS0000",
        "00000sSSSSSKKoSSSSSSSSSSSSSSSSOOOOOS0000",
        "0000SOOSSSSSSSSSSSSSSSSSSSSSSOOOOOOS0000",
        "0000SOOSSSSSSSSSSSSKSSSSSSSSOOOOOOOO0000",
        "0000SOOOSSSSSSOOSOKSSSSSSSSOOOOOOOOO0000",
        "0000SOOOOSSSSSOKKKSSSSSSSSOOOOOOOOOO0000",
        "0000SOOOOOSSSSSSSSSSSSSSOOOOOOOOOOOO0000",
        "00000OOOOOOOSSSSSSSSSSSOOOOOOOOOOOOO0000",
        "00000OOOOOOOOOOOOSSSSSOOOOOOOOOOOOOO0000",
        "00000SOOOOOOOOOOOSSSSSOOOOOOOOOOOOOS0000",
        "00000SOOOOOOOOOOKKKKKKKKKOOOOOOOOOOS0000",
        "000000SOOOOOOOKKKKKKKKKKKKOOOOOOOOO00000",
        "00000000SSOOOKKKKKKKKKKKKKKOSSOOOO000000",
        "000000000000KKKKKKKKKKKKKKK0000S00000000",
        "00000000000SKKKKKKKKKKKKKKK0000000000000",
        "000000000000KKKKKKKKKKKKKK00000000000000",
        "0000000000000000000000000000000000000000",
        "0000000000000000000000000000000000000000",
    },
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

void ScreenRenderer::setPowerSnapshot(const power::PowerSnapshot& snapshot) {
  power_snapshot_ = snapshot;
  power_snapshot_valid_ = true;
}

void ScreenRenderer::screenOn() {
  M5.Display.wakeup();
  M5.Display.setRotation(kPortraitRotation);
  M5.Display.setBrightness(backlight_brightness_);
  screen_on_ = true;
}

void ScreenRenderer::screenOff() {
  M5.Display.wakeup();
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setBrightness(0);
  M5.Display.sleep();
  screen_on_ = false;
  stopAvatarAnimation();
  clearRenderState();
}

void ScreenRenderer::setBacklightBrightness(uint8_t brightness) {
  backlight_brightness_ = brightness;
  if (screen_on_) {
    M5.Display.setBrightness(backlight_brightness_);
  }
}

uint8_t ScreenRenderer::backlightBrightness() const {
  return backlight_brightness_;
}

void ScreenRenderer::render_screen_boot() {
  render_avatar_layout_brand(state::RenderScreenKind::Boot, "Zero");
}

void ScreenRenderer::render_screen_read_empty() {
  render_avatar_layout_status(state::RenderScreenKind::ReadEmpty,
                              "all clear",
                              "nothing new");
}

void ScreenRenderer::render_screen_read_message(size_t index,
                                                size_t count,
                                                const std::string& message,
                                                size_t scroll_top) {
  stopAvatarAnimation();
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
                              "I got you",
                              "one sec...");
}

void ScreenRenderer::render_screen_recording_sending(const std::string& user_text) {
  render_avatar_layout_preview(state::RenderScreenKind::RecordingSending,
                               "Sending",
                               user_text);
}

void ScreenRenderer::render_screen_recording_sent(const std::string& user_text) {
  render_avatar_layout_preview(state::RenderScreenKind::RecordingSent,
                               "Sent",
                               user_text);
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
  render_checking_indicator(true);
}

void ScreenRenderer::clear_checking_messages_indicator() {
  render_checking_indicator(false);
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
  const int16_t percent = batteryLevelPercent();
  const bool percent_mode = batteryPercentMode();
  const uint8_t bars = batteryLevelBarsFor(percent);
  if (local_.battery_visible &&
      local_.battery_percent_mode == percent_mode &&
      ((percent_mode && local_.battery_percent == percent) ||
       (!percent_mode && local_.battery_bars == bars))) {
    return;
  }
  local_.battery_visible = true;
  local_.battery_percent_mode = percent_mode;
  local_.battery_percent = percent;
  local_.battery_bars = bars;

  const uint16_t bg = avatarBackgroundColor();
  const uint16_t fg = dialogueBorderColor();
  constexpr int kBarWidth = 3;
  constexpr int kBarGap = 1;
  const ReadLayout layout = readLayout();
  const int x = layout.battery.x;
  const int y = layout.battery.y;
  M5.Display.fillRect(std::max(0, x - 1),
                      std::max(0, y - 1),
                      layout.battery.w + 2,
                      kBatteryClearHeight,
                      bg);

  if (percent_mode) {
    M5.Display.setTextColor(fg, bg);
    M5.Display.setTextWrap(false);
    M5.Display.setFont(&fonts::Font0);
    const String text = percent < 0 ? String("--%") : String(percent) + "%";
    M5.Display.setCursor(x, y + 1);
    M5.Display.print(text);
    return;
  }

  M5.Display.drawRect(x, y, kBatteryBodyWidth, kBatteryBodyHeight, fg);
  M5.Display.fillRect(x + kBatteryBodyWidth, y + 3, kBatteryCapWidth, 4, fg);

  const int bar_y = y + 2;
  for (uint8_t i = 0; i < 3; ++i) {
    const int bar_x = x + 2 + i * (kBarWidth + kBarGap);
    if (i < bars) {
      M5.Display.fillRect(bar_x, bar_y, kBarWidth, kBatteryBodyHeight - 4, fg);
    } else {
      M5.Display.drawRect(bar_x, bar_y, kBarWidth, kBatteryBodyHeight - 4, fg);
    }
  }

  // Redraw the outer frame last so the terminal and inner bars never nick the
  // bottom-right outline on the tiny icon.
  M5.Display.drawRect(x, y, kBatteryBodyWidth, kBatteryBodyHeight, fg);
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
      const uint16_t color =
          avatarColor(row, crop_left + col, kAvatarBlinkOpenFrame);
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
  const int text_x = bubble_x + 9;
  const int text_w = bubble_w - 18;
  constexpr int kTitleHeight = 20;
  constexpr int kDetailHeight = 20;
  const int max_block_h = std::max(1, bubble_h - 10);
  while (emphasis_scale > 1) {
    const int emphasis_h = kTinyFontGlyphHeight * emphasis_scale;
    const int gap = std::max(4,
                             std::min(12,
                                      (max_block_h - kTitleHeight - kDetailHeight -
                                       emphasis_h) /
                                          2));
    if (kTitleHeight + emphasis_h + kDetailHeight + gap * 2 <= max_block_h) {
      break;
    }
    --emphasis_scale;
  }
  const int emphasis_h = kTinyFontGlyphHeight * emphasis_scale;
  const int instruction_gap =
      std::max(4,
               std::min(12,
                        (max_block_h - kTitleHeight - kDetailHeight - emphasis_h) /
                            2));
  const int block_h = kTitleHeight + emphasis_h + kDetailHeight +
                      instruction_gap * 2;
  const int block_y = bubble_y + std::max(0, (bubble_h - block_h) / 2);

  auto drawCenteredTiny = [&](const char* text, int y, uint8_t scale) {
    const int text_width =
        tinyFontTextWidth(text, scale, kTinyFontSpacing);
    const int x = text_x + std::max(0, (text_w - text_width) / 2);
    drawTinyFontText(text, x, y, scale, kTinyFontSpacing, border);
  };

  M5.Display.setFont(&fonts::Font2);
  printCenteredFittedLine(line1, text_x, block_y, text_w);
  drawCenteredTiny(emphasis,
                   block_y + kTitleHeight + instruction_gap,
                   emphasis_scale);
  M5.Display.setFont(&fonts::Font2);
  printCenteredFittedLine(line3,
                          text_x,
                          block_y + kTitleHeight + instruction_gap +
                              emphasis_h + instruction_gap,
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
  constexpr int kTitleHeight = 20;
  constexpr int kDetailHeight = 20;
  constexpr int kStatusLineGap = 10;
  const int block_h = has_detail ? kTitleHeight + kStatusLineGap + kDetailHeight
                                 : kTitleHeight;
  const int block_y = bubble_y + std::max(0, (bubble_h - block_h) / 2);
  drawCenteredChatLine(title, text_x, block_y, text_w, true);
  if (has_detail) {
    drawCenteredChatLine(detail,
                         text_x,
                         block_y + kTitleHeight + kStatusLineGap,
                         text_w);
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
  drawFittedChatLine(title, text_x, title_y, text_w, true);

  M5.Display.setClipRect(text_x, text_y, text_w, text_h);
  M5.Display.setTextColor(border, TFT_WHITE);
  M5.Display.setFont(&fonts::efontCN_16);
  renderWrappedChatText(body, text_x, text_y, text_w, text_h, 0);
  M5.Display.clearClipRect();
  M5.Display.setTextWrap(false);
}

void ScreenRenderer::render_element_chat_header(size_t index, size_t count) {
  const uint16_t fg = dialogueBorderColor();
  const ReadLayout layout = readLayout();
  M5.Display.setTextColor(fg, avatarBackgroundColor());
  M5.Display.setTextWrap(false);
  M5.Display.setFont(&fonts::Font0);
  const String page_text = String(static_cast<unsigned>(index + 1)) + "/" +
                           String(static_cast<unsigned>(count));
  const int page_w = M5.Display.textWidth(page_text.c_str());
  M5.Display.setCursor(layout.header.x + std::max(0, layout.header.w - page_w),
                       layout.header.y);
  M5.Display.print(page_text);
  M5.Display.drawFastHLine(layout.header.x,
                           layout.divider_y,
                           layout.header.w,
                           fg);
}

void ScreenRenderer::render_element_chat_message(const std::string& message,
                                                 size_t scroll_top) {
  const uint16_t bg = avatarBackgroundColor();
  const uint16_t fg = dialogueBorderColor();
  const ReadLayout layout = readLayout();
  M5.Display.startWrite();
  M5.Display.fillRect(layout.body.x, layout.body.y, layout.body.w, layout.body.h, bg);
  M5.Display.setClipRect(layout.body.x, layout.body.y, layout.body.w, layout.body.h);
  M5.Display.setTextColor(fg, bg);
  M5.Display.setFont(&fonts::efontCN_16);
  renderWrappedChatText(message,
                        layout.body.x,
                        layout.body.y,
                        layout.body.w,
                        layout.body.h,
                        scroll_top);
  M5.Display.clearClipRect();
  M5.Display.setTextWrap(false);
  M5.Display.endWrite();
}

void ScreenRenderer::render_element_next_page_arrow() {
  const ReadLayout layout = readLayout();
  const uint16_t bg = avatarBackgroundColor();
  const uint16_t fg = dialogueBorderColor();
  const int right = layout.body.x + layout.body.w - 1;
  const int bottom = layout.screen.h - kReadPaddingBottom - 1;
  const int cx = right - 6;
  const int cy = bottom - 5;
  constexpr int kArrowMaskWidth = 18;
  constexpr int kArrowMaskHeight = 16;
  M5.Display.fillRect(std::max(layout.body.x, right - kArrowMaskWidth + 1),
                      std::max(layout.body.y, bottom - kArrowMaskHeight + 1),
                      kArrowMaskWidth,
                      kArrowMaskHeight,
                      bg);
  M5.Display.fillTriangle(cx - 5,
                          cy - 2,
                          cx + 5,
                          cy - 2,
                          cx,
                          cy + 5,
                          fg);
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

void ScreenRenderer::tickAvatarAnimation(uint32_t now_ms) {
  if (!screen_on_ || !avatar_animation_.active ||
      !isAvatarDialogueScreenKind(currentRenderState().kind)) {
    stopAvatarAnimation();
    return;
  }

  if (!avatar_animation_.blinking) {
    if (static_cast<int32_t>(now_ms - avatar_animation_.next_blink_ms) < 0) {
      return;
    }
    avatar_animation_.blinking = true;
    avatar_animation_.frame = 1;
    avatar_animation_.next_frame_ms = now_ms + kAvatarBlinkFrameMs;
    renderAvatarAnimationFrame(avatar_animation_.frame);
    return;
  }

  if (static_cast<int32_t>(now_ms - avatar_animation_.next_frame_ms) < 0) {
    return;
  }

  ++avatar_animation_.frame;
  if (avatar_animation_.frame >= kAvatarBlinkFinalFrame) {
    avatar_animation_.blinking = false;
    avatar_animation_.frame = kAvatarBlinkOpenFrame;
    avatar_animation_.next_blink_ms = now_ms + kAvatarBlinkIntervalMs;
    renderAvatarAnimationFrame(kAvatarBlinkOpenFrame);
    return;
  }

  avatar_animation_.next_frame_ms = now_ms + kAvatarBlinkFrameMs;
  renderAvatarAnimationFrame(avatar_animation_.frame);
}

size_t ScreenRenderer::maxScrollTopForMessage(const std::string& message) const {
  const ReadLayout layout = readLayout();
  const size_t width = static_cast<size_t>(std::max(1, layout.body.w));
  const size_t viewport = static_cast<size_t>(std::max(1, layout.body.h));
  const size_t text_height = estimateWrappedTextHeight(message, width);
  if (text_height <= viewport) {
    return 0;
  }
  return text_height - viewport;
}

size_t ScreenRenderer::scrollStep() const {
  const size_t viewport = readViewportHeight();
  const size_t full_lines = std::max<size_t>(1, viewport / kReadLineHeight);
  if (full_lines <= 1) {
    return kReadLineHeight;
  }
  return (full_lines - 1) * kReadLineHeight;
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
  stopAvatarAnimation();
}

bool ScreenRenderer::sameRenderState(const state::RenderScreenState& next) const {
  return sameRenderScreenState(currentRenderState(), next);
}

void ScreenRenderer::resetElementState() {
  local_.battery_bars = 0xFF;
  local_.battery_percent = -2;
  local_.battery_percent_mode = false;
  local_.battery_visible = false;
  local_.checking_indicator_visible = false;
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
                            layout.bubble.x,
                            layout.bubble.y,
                            layout.bubble.w,
                            layout.bubble.h);
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
                                  layout.bubble.x,
                                  layout.bubble.y,
                                  layout.bubble.w,
                                  layout.bubble.h);
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
                                  layout.bubble.x,
                                  layout.bubble.y,
                                  layout.bubble.w,
                                  layout.bubble.h);
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
                              layout.bubble.x,
                              layout.bubble.y,
                              layout.bubble.w,
                              layout.bubble.h);
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

ScreenRenderer::Rect ScreenRenderer::screenRect() const {
  return Rect{0, 0, M5.Display.width(), M5.Display.height()};
}

ScreenRenderer::Rect ScreenRenderer::insetRect(const Rect& rect, int inset) const {
  inset = std::max(0, inset);
  return Rect{
      rect.x + inset,
      rect.y + inset,
      std::max(0, rect.w - inset * 2),
      std::max(0, rect.h - inset * 2),
  };
}

ScreenRenderer::Rect ScreenRenderer::batteryRect(const Rect& screen) const {
  return Rect{
      kReadPaddingLeft,
      kReadPaddingTop + 1,
      std::max(kBatteryBodyWidth + kBatteryCapWidth, kBatteryPercentWidth),
      kBatteryClearHeight,
  };
}

uint8_t ScreenRenderer::effectiveAvatarScale(uint8_t requested_scale) const {
  requested_scale = std::max<uint8_t>(1, requested_scale);
  const Rect screen = screenRect();
  const int max_by_width =
      std::max(1,
               (screen.w - kDialoguePadding - kTopAvatarRightPadding) /
                   kZeroAvatarVisibleWidth);
  const int max_by_height = std::max(
      1,
      (screen.h - kMinDialogueBubbleHeight - kDialoguePadding * 3 -
       kAvatarBubbleGap - kTopBarBottomGap) /
          static_cast<int>(kZeroAvatarHeight));
  return static_cast<uint8_t>(
      std::max(1, std::min<int>({requested_scale, max_by_width, max_by_height})));
}

ScreenRenderer::Rect ScreenRenderer::topBarAvatarRect(const Rect& screen,
                                                      uint8_t avatar_scale) const {
  avatar_scale = effectiveAvatarScale(avatar_scale);
  const int avatar_w = kZeroAvatarVisibleWidth * avatar_scale;
  const int avatar_h = static_cast<int>(kZeroAvatarHeight) * avatar_scale;
  return Rect{
      std::max(kDialoguePadding, screen.w - avatar_w - kTopAvatarRightPadding),
      kTopAvatarTopPadding,
      avatar_w,
      avatar_h,
  };
}

ScreenRenderer::AvatarDialogueLayout ScreenRenderer::avatarDialogueLayout(
    uint8_t avatar_scale) const {
  avatar_scale = effectiveAvatarScale(avatar_scale);
  const Rect screen = screenRect();
  AvatarDialogueLayout layout;
  layout.screen = screen;
  layout.avatar_scale = avatar_scale;
  layout.avatar_crop_left = kZeroAvatarCropLeft;
  layout.avatar_visible_width = kZeroAvatarVisibleWidth;
  layout.battery = batteryRect(screen);
  layout.avatar = topBarAvatarRect(screen, avatar_scale);
  layout.top_bar = Rect{
      0,
      0,
      screen.w,
      layout.avatar.y + layout.avatar.h + kTopBarBottomGap,
  };
  const int bubble_y = layout.top_bar.y + layout.top_bar.h + kAvatarBubbleGap;
  layout.bubble = Rect{
      kDialoguePadding,
      bubble_y,
      std::max(1, screen.w - kDialoguePadding * 2),
      std::max(1, screen.h - bubble_y - kDialoguePadding),
  };
  layout.tail = Point{
      layout.avatar.x + layout.avatar.w / 2,
      std::max(layout.avatar.y + layout.avatar.h - 1, layout.bubble.y - 8),
  };
  layout.tail_direction = BubbleTailDirection::Top;
  return layout;
}

ScreenRenderer::ReadLayout ScreenRenderer::readLayout() const {
  const Rect screen = screenRect();
  ReadLayout layout;
  layout.screen = screen;
  layout.battery = batteryRect(screen);
  layout.top_bar = Rect{
      0,
      0,
      screen.w,
      kReadPaddingTop + kReadHeaderHeight,
  };
  layout.header = Rect{
      kReadPaddingLeft,
      kReadPaddingTop,
      std::max(1, screen.w - static_cast<int>(kReadPaddingLeft + kReadPaddingRight)),
      kReadHeaderHeight,
  };
  layout.divider_y = layout.header.y + layout.header.h;
  const int body_y = layout.divider_y + 4;
  layout.body = Rect{
      kReadPaddingLeft,
      body_y,
      layout.header.w,
      std::max(1, screen.h - body_y - static_cast<int>(kReadPaddingBottom)),
  };
  return layout;
}

ScreenRenderer::AvatarDialogueLayout ScreenRenderer::render_avatar_dialogue_shell(
    uint8_t avatar_scale,
    bool reuse_shell) {
  screenOn();
  const AvatarDialogueLayout layout = avatarDialogueLayout(avatar_scale);
  if (reuse_shell) {
    startAvatarAnimation(layout, millis());
    clear_avatar_dialogue_content(layout);
    return layout;
  }

  const uint16_t bg = avatarBackgroundColor();
  M5.Display.fillScreen(bg);
  render_element_zero_avatar(layout.avatar.x,
                             layout.avatar.y,
                             layout.avatar_scale,
                             layout.avatar_crop_left,
                             layout.avatar_visible_width);
  startAvatarAnimation(layout, millis());
  render_dialogue_bubble(layout);
  return layout;
}

void ScreenRenderer::clear_avatar_dialogue_content(
    const AvatarDialogueLayout& layout) {
  const Rect content = insetRect(layout.bubble, 4);
  const int x = content.x;
  const int y = content.y;
  const int w = content.w;
  const int h = content.h;
  if (w <= 0 || h <= 0) {
    return;
  }
  M5.Display.fillRoundRect(x, y, w, h, 4, TFT_WHITE);
}

ScreenRenderer::Rect ScreenRenderer::checkingIndicatorRect() const {
  const Rect battery = batteryRect(screenRect());
  return Rect{
      std::max(0, battery.x + battery.w + kCheckingIconGap -
                      kCheckingIconClearPadding),
      std::max(0, battery.y - 1 - kCheckingIconClearPadding),
      kCheckingIconWidth + kCheckingIconClearPadding * 2,
      kCheckingIconHeight + kCheckingIconClearPadding * 2,
  };
}

void ScreenRenderer::render_checking_indicator(bool visible) {
  const Rect rect = checkingIndicatorRect();
  if (local_.checking_indicator_visible == visible) {
    return;
  }
  local_.checking_indicator_visible = visible;

  const uint16_t bg = avatarBackgroundColor();
  const uint16_t fg = dialogueBorderColor();
  M5.Display.fillRect(rect.x, rect.y, rect.w, rect.h, bg);
  if (!visible) {
    return;
  }

  const int icon_x = rect.x + kCheckingIconClearPadding;
  const int icon_y = rect.y + kCheckingIconClearPadding;
  const int cx = icon_x + kCheckingIconWidth / 2;
  const int top = icon_y + 1;
  const int bottom = icon_y + kCheckingIconHeight - 2;
  M5.Display.drawFastVLine(cx - 2, top + 4, kCheckingIconHeight - 7, fg);
  M5.Display.fillTriangle(cx - 5, top + 4, cx - 2, top, cx + 1, top + 4, fg);
  M5.Display.drawFastVLine(cx + 2, top + 1, kCheckingIconHeight - 7, fg);
  M5.Display.fillTriangle(cx - 1, bottom - 4, cx + 2, bottom, cx + 5, bottom - 4, fg);
}

void ScreenRenderer::render_dialogue_bubble(const AvatarDialogueLayout& layout) {
  const uint16_t border = dialogueBorderColor();
  M5.Display.fillRoundRect(layout.bubble.x,
                           layout.bubble.y,
                           layout.bubble.w,
                           layout.bubble.h,
                           7,
                           TFT_WHITE);
  M5.Display.drawRoundRect(layout.bubble.x,
                           layout.bubble.y,
                           layout.bubble.w,
                           layout.bubble.h,
                           7,
                           border);
  M5.Display.drawRoundRect(layout.bubble.x + 2,
                           layout.bubble.y + 2,
                           layout.bubble.w - 4,
                           layout.bubble.h - 4,
                           5,
                           border);

  if (layout.tail_direction != BubbleTailDirection::Top) {
    return;
  }

  constexpr int kTailHalfWidth = 8;
  M5.Display.fillTriangle(layout.tail.x - kTailHalfWidth,
                          layout.bubble.y,
                          layout.tail.x,
                          layout.tail.y,
                          layout.tail.x + kTailHalfWidth,
                          layout.bubble.y,
                          TFT_WHITE);
  M5.Display.drawTriangle(layout.tail.x - kTailHalfWidth,
                          layout.bubble.y,
                          layout.tail.x,
                          layout.tail.y,
                          layout.tail.x + kTailHalfWidth,
                          layout.bubble.y,
                          border);
}

uint16_t ScreenRenderer::avatarBackgroundColor() const {
  return M5.Display.color565(220, 224, 230);
}

uint16_t ScreenRenderer::dialogueBorderColor() const {
  return M5.Display.color565(60, 55, 50);
}

uint16_t ScreenRenderer::avatarColor(uint8_t row, int col, uint8_t frame) const {
  const uint8_t frame_index =
      frame < kZeroAvatarFrameCount ? frame : kAvatarBlinkOpenFrame;
  const int sprite_col = col - kZeroAvatarCropLeft;
  const char cell = row < kZeroAvatarHeight && sprite_col >= 0 &&
                            sprite_col < static_cast<int>(kZeroAvatarSpriteWidth)
                        ? kZeroAvatarFrames[frame_index][row][sprite_col]
                        : '0';
  switch (cell) {
    case 'O':
      return M5.Display.color565(246, 125, 19);
    case 'o':
      return M5.Display.color565(214, 85, 16);
    case 'S':
      return M5.Display.color565(248, 202, 145);
    case 's':
      return M5.Display.color565(217, 159, 92);
    case 'K':
      return M5.Display.color565(52, 49, 42);
    case 'k':
      return M5.Display.color565(12, 12, 10);
    case 'G':
      return M5.Display.color565(92, 88, 76);
    case 'W':
      return TFT_WHITE;
    case 'w':
      return M5.Display.color565(218, 218, 205);
    case '0':
    default:
      return avatarBackgroundColor();
  }
}

void ScreenRenderer::startAvatarAnimation(const AvatarDialogueLayout& layout,
                                          uint32_t now_ms) {
  avatar_animation_.active = true;
  avatar_animation_.rect = layout.avatar;
  avatar_animation_.scale = layout.avatar_scale;
  avatar_animation_.crop_left = layout.avatar_crop_left;
  avatar_animation_.visible_width = layout.avatar_visible_width;
  avatar_animation_.frame = kAvatarBlinkOpenFrame;
  avatar_animation_.blinking = false;
  avatar_animation_.next_blink_ms = now_ms + kAvatarBlinkIntervalMs;
  avatar_animation_.next_frame_ms = 0;
  renderAvatarAnimationFrame(kAvatarBlinkOpenFrame);
}

void ScreenRenderer::stopAvatarAnimation() {
  avatar_animation_ = AvatarAnimationState();
}

void ScreenRenderer::renderAvatarAnimationFrame(uint8_t frame) {
  if (!avatar_animation_.active || avatar_animation_.rect.w <= 0 ||
      avatar_animation_.rect.h <= 0) {
    return;
  }

  const uint8_t scale = std::max<uint8_t>(1, avatar_animation_.scale);
  const int crop_left =
      std::max(0,
               std::min(avatar_animation_.crop_left,
                        static_cast<int>(kZeroAvatarWidth)));
  const int visible_width = std::max(
      0,
      std::min(avatar_animation_.visible_width,
               static_cast<int>(kZeroAvatarWidth) - crop_left));

  for (uint8_t row = 0; row < kZeroAvatarHeight; ++row) {
    for (int col = 0; col < visible_width; ++col) {
      const uint16_t color = avatarColor(row, crop_left + col, frame);
      const int x = avatar_animation_.rect.x + col * scale;
      const int y = avatar_animation_.rect.y + row * scale;
      if (scale == 1) {
        M5.Display.drawPixel(x, y, color);
      } else {
        M5.Display.fillRect(x, y, scale, scale, color);
      }
    }
  }
}

bool ScreenRenderer::batteryPercentMode() const {
  return power_snapshot_valid_ &&
         power::shouldShowBatteryPercent(power_snapshot_);
}

int16_t ScreenRenderer::batteryLevelPercent() const {
  if (!power_snapshot_valid_ || power_snapshot_.battery_percent < 0) {
    return -1;
  }
  return static_cast<int16_t>(
      std::max<int32_t>(0, std::min<int32_t>(100, power_snapshot_.battery_percent)));
}

uint8_t ScreenRenderer::batteryLevelBars() const {
  return batteryLevelBarsFor(batteryLevelPercent());
}

uint8_t ScreenRenderer::batteryLevelBarsFor(int32_t level) const {
  return power::batteryBarsForPercent(level);
}

size_t ScreenRenderer::readBodyTop() const {
  return static_cast<size_t>(std::max(0, readLayout().body.y));
}

size_t ScreenRenderer::readViewportHeight() const {
  return static_cast<size_t>(std::max(1, readLayout().body.h));
}

void ScreenRenderer::renderWrappedChatText(const std::string& text,
                                           int x,
                                           int y,
                                           int width_px,
                                           int height_px,
                                           size_t scroll_top) {
  const int line_height = static_cast<int>(kReadLineHeight);
  const int bottom = y + std::max(1, height_px);
  int line_y = y - static_cast<int>(scroll_top);
  size_t line_width = 0;
  std::vector<ChatGlyph> line;
  M5.Display.setTextWrap(false);

  auto lineVisible = [&]() {
    return line_y + line_height > y && line_y < bottom;
  };

  auto flush_line = [&]() {
    if (!line.empty() && lineVisible()) {
      drawChatGlyphs(line, x, line_y);
    }
    line.clear();
    line_width = 0;
    line_y += line_height;
    return line_y < bottom;
  };

  for (size_t i = 0; i < text.size();) {
    if (line_y >= bottom) {
      break;
    }

    if (text[i] == '\n') {
      ++i;
      if (!flush_line()) {
        break;
      }
      continue;
    }

    ChatGlyph glyph = chatGlyphAt(text, &i);
    const size_t glyph_width = glyph.width;
    if (line_width > 0 &&
        line_width + glyph_width > static_cast<size_t>(std::max(1, width_px))) {
      if (!flush_line()) {
        break;
      }
    }
    if (lineVisible()) {
      line.push_back(glyph);
    }
    line_width += glyph_width;
  }

  flush_line();
}

size_t ScreenRenderer::estimateWrappedTextHeight(const std::string& text,
                                                 size_t width_px) const {
  if (width_px == 0 || text.empty()) {
    return kReadLineHeight;
  }
  const uint32_t text_hash = hashText(text.c_str());
  if (local_.wrapped_text_height_cache_valid &&
      local_.wrapped_text_height_cache_hash == text_hash &&
      local_.wrapped_text_height_cache_size == text.size() &&
      local_.wrapped_text_height_cache_width == width_px) {
    return local_.wrapped_text_height_cache_value;
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
  const size_t height = lines * kReadLineHeight;
  local_.wrapped_text_height_cache_valid = true;
  local_.wrapped_text_height_cache_hash = text_hash;
  local_.wrapped_text_height_cache_size = text.size();
  local_.wrapped_text_height_cache_width = width_px;
  local_.wrapped_text_height_cache_value = height;
  return height;
}

size_t ScreenRenderer::utf8DisplayWidthPx(const std::string& text, size_t* offset) const {
  if (offset == nullptr || *offset >= text.size()) {
    return 0;
  }
  if (text[*offset] == '\n') {
    ++(*offset);
    return 0;
  }
  return chatGlyphAt(text, offset).width;
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
