#include <Arduino.h>
#include <HTTPClient.h>
#include <LittleFS.h>
#include <M5Unified.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <driver/gpio.h>
#include <esp_sleep.h>
#include <esp_wifi.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
#endif

#include "zero_buddy_core.h"
#include "zero_buddy_modes.h"
#include "zero_buddy_state.h"

namespace {

using zero_buddy::modes::CheckAssistantMessageMode;
using zero_buddy::modes::CheckAssistantMessageOps;
using zero_buddy::modes::DeepSleepMode;
using zero_buddy::modes::DeepSleepOps;
using zero_buddy::modes::ModeRunError;
using zero_buddy::modes::ModeRunResult;
using zero_buddy::modes::ModeRunStatus;
using zero_buddy::modes::ReadInput;
using zero_buddy::modes::ReadMode;
using zero_buddy::modes::ReadOps;
using zero_buddy::modes::ReadProgress;
using zero_buddy::modes::RecordingMode;
using zero_buddy::modes::RecordingOps;
using zero_buddy::state::AssistantCheckResult;
using zero_buddy::state::GlobalState;

constexpr uint32_t kRtcMagic = 0x5A0B2026UL;
constexpr gpio_num_t kBtnAWakePin = GPIO_NUM_37;
constexpr gpio_num_t kLedGpio = GPIO_NUM_10;
constexpr int kLedPin = static_cast<int>(kLedGpio);
constexpr int kBuzzerPin = 2;
constexpr int kSampleRate = 16000;
constexpr size_t kMicSamplesPerRead = 256;
constexpr size_t kPcmBufferBytes = kMicSamplesPerRead * sizeof(int16_t);
constexpr size_t kAsrChunkBytes = kSampleRate * 2 / 5;
constexpr uint32_t kLongPressMs = 550;
constexpr uint32_t kMaxRecordingMs = 60000;
constexpr uint32_t kBootSplashMs = 3000;
constexpr uint32_t kWifiConnectAttemptMs = 12000;
constexpr uint8_t kRecordingCpuMhz = 240;
constexpr uint8_t kNetworkCpuMhz = 80;
constexpr uint8_t kReadCpuMhz = 80;
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
constexpr size_t kMaxAssistantMessages = 5;
constexpr size_t kMaxAssistantMessageBytes = 4096;

constexpr char kVoicePcmPath[] = "/voice.pcm";
constexpr char kAssistantQueueManifestPath[] = "/assistant_queue.txt";
constexpr char kAsrWsHost[] = "openspeech.bytedance.com";
constexpr uint16_t kAsrWsPort = 443;
constexpr char kAsrWsPath[] = "/api/v3/sauc/bigmodel_nostream";
constexpr char kZeroHost[] = "api.vm0.ai";
constexpr char kZeroPostUrl[] = "https://api.vm0.ai/api/v1/chat-threads/messages";

RTC_DATA_ATTR uint32_t g_rtc_magic = 0;
RTC_DATA_ATTR GlobalState g_state;

uint8_t g_pcm_buffer[kPcmBufferBytes] = {0};
uint8_t g_asr_buffer[kAsrChunkBytes] = {0};

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

void beepAssistantLedOn();
bool writeTextFile(const char* path, const std::string& value);
bool writeAssistantQueueManifest(size_t count, size_t index, size_t scroll_top);

bool stateLooksValid(const GlobalState& state) {
  if (state.checkDelayMs == 0 ||
      state.checkDelayMs > zero_buddy::state::kMaxCheckDelayMs) {
    return false;
  }
  const auto mode = static_cast<uint8_t>(state.currentMode);
  return mode <= static_cast<uint8_t>(zero_buddy::state::Mode::Recording);
}

void ensureGlobalStateInitialized() {
  if (g_rtc_magic != kRtcMagic || !stateLooksValid(g_state)) {
    g_state = zero_buddy::state::makeDefaultGlobalState();
    g_rtc_magic = kRtcMagic;
  }
}

void setCpu(uint8_t mhz) {
  setCpuFrequencyMhz(mhz);
}

void setStatusLed(bool on) {
  pinMode(kLedPin, OUTPUT);
  digitalWrite(kLedPin, on ? LOW : HIGH);
}

void releaseLedHold() {
  gpio_deep_sleep_hold_dis();
  gpio_hold_dis(kLedGpio);
}

void holdLedThroughDeepSleep() {
  gpio_hold_en(kLedGpio);
  gpio_deep_sleep_hold_en();
}

void setAssistantLedOn() {
  releaseLedHold();
  beepAssistantLedOn();
  setStatusLed(true);
  holdLedThroughDeepSleep();
}

void setAssistantLedOff() {
  releaseLedHold();
  setStatusLed(false);
}

void beepTone(int freq_hz, int duration_ms) {
  ledcAttachPin(kBuzzerPin, 0);
  ledcWriteTone(0, freq_hz);
  delay(duration_ms);
  ledcWriteTone(0, 0);
}

void beepMicOn() {
  beepTone(1760, 35);
}

void beepMicOff() {
  beepTone(880, 70);
}

void beepAssistantLedOn() {
  beepTone(1568, 45);
}

bool buttonADown() {
  return digitalRead(static_cast<int>(kBtnAWakePin)) == LOW;
}

bool waitForBtnALongPress(uint32_t hold_ms) {
  if (!buttonADown()) {
    return false;
  }
  const uint32_t start = millis();
  while (buttonADown()) {
    M5.update();
    if (millis() - start >= hold_ms) {
      return true;
    }
    delay(10);
  }
  return false;
}

uint16_t avatarBackgroundColor() {
  return M5.Display.color565(220, 224, 230);
}

uint16_t dialogueBorderColor() {
  return M5.Display.color565(60, 55, 50);
}

uint16_t avatarColor(char cell) {
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

void drawZeroAvatar(int x,
                    int y,
                    uint8_t scale = 1,
                    int crop_left = 0,
                    int visible_width = kZeroAvatarWidth) {
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

void drawAvatarDialogue(const char* line1, const char* line2 = "") {
  M5.Display.setRotation(3);
  M5.Display.setBrightness(kScreenBrightness);
  const uint16_t bg = avatarBackgroundColor();
  const uint16_t border = dialogueBorderColor();
  M5.Display.fillScreen(bg);
  const int avatar_w = kZeroAvatarVisibleWidth * kZeroAvatarScale;
  const int avatar_h = static_cast<int>(kZeroAvatarHeight) * kZeroAvatarScale;
  const int avatar_x = M5.Display.width() - avatar_w - kZeroAvatarRightPadding;
  const int avatar_y = (M5.Display.height() - avatar_h) / 2;
  drawZeroAvatar(
      avatar_x, avatar_y, kZeroAvatarScale, kZeroAvatarCropLeft, kZeroAvatarVisibleWidth);

  const int bubble_x = kDialoguePadding;
  const int bubble_y = kDialoguePadding;
  const int bubble_w = std::max(58, avatar_x - kDialoguePadding - bubble_x);
  const int bubble_h = M5.Display.height() - kDialoguePadding * 2;
  const int tail_y = bubble_y + bubble_h / 2;
  M5.Display.fillRoundRect(bubble_x, bubble_y, bubble_w, bubble_h, 7, TFT_WHITE);
  M5.Display.drawRoundRect(bubble_x, bubble_y, bubble_w, bubble_h, 7, border);
  M5.Display.drawRoundRect(bubble_x + 2, bubble_y + 2, bubble_w - 4, bubble_h - 4, 5, border);
  M5.Display.fillTriangle(bubble_x + bubble_w - 1,
                          tail_y - 7,
                          avatar_x - 1,
                          tail_y,
                          bubble_x + bubble_w - 1,
                          tail_y + 7,
                          TFT_WHITE);
  M5.Display.drawTriangle(bubble_x + bubble_w - 1,
                          tail_y - 7,
                          avatar_x - 1,
                          tail_y,
                          bubble_x + bubble_w - 1,
                          tail_y + 7,
                          border);

  const int text_x = bubble_x + 10;
  const int text_width = bubble_w - 20;
  M5.Display.setTextColor(border, TFT_WHITE);
  M5.Display.setTextWrap(false);
  M5.Display.setFont(&fonts::Font2);
  printFittedLine(line1, text_x, bubble_y + 12, text_width);
  if (line2 != nullptr && line2[0] != '\0') {
    M5.Display.setFont(&fonts::Font0);
    printFittedLine(line2, text_x, bubble_y + 42, text_width);
  }
}

void drawAvatarStatus(const char* line1, const char* line2 = "") {
  drawAvatarDialogue(line1, line2);
}

void drawEmptyReadScreen() {
  drawAvatarDialogue("no message");
}

void drawBootScreen() {
  M5.Display.setRotation(3);
  M5.Display.setBrightness(kScreenBrightness);
  const uint16_t bg = avatarBackgroundColor();
  const uint16_t border = dialogueBorderColor();
  M5.Display.fillScreen(bg);

  constexpr uint8_t kBootAvatarScale = 1;
  const int avatar_w = kZeroAvatarVisibleWidth * kBootAvatarScale;
  const int avatar_h = static_cast<int>(kZeroAvatarHeight) * kBootAvatarScale;
  const int avatar_x = M5.Display.width() - avatar_w - kZeroAvatarRightPadding;
  const int avatar_y = (M5.Display.height() - avatar_h) / 2;
  drawZeroAvatar(avatar_x,
                 avatar_y,
                 kBootAvatarScale,
                 kZeroAvatarCropLeft,
                 kZeroAvatarVisibleWidth);

  const int bubble_x = kDialoguePadding;
  const int bubble_y = kDialoguePadding;
  const int bubble_w = std::max(58, avatar_x - kDialoguePadding - bubble_x);
  const int bubble_h = M5.Display.height() - kDialoguePadding * 2;
  const int tail_y = bubble_y + bubble_h / 2;
  M5.Display.fillRoundRect(bubble_x, bubble_y, bubble_w, bubble_h, 7, TFT_WHITE);
  M5.Display.drawRoundRect(bubble_x, bubble_y, bubble_w, bubble_h, 7, border);
  M5.Display.drawRoundRect(bubble_x + 2, bubble_y + 2, bubble_w - 4, bubble_h - 4, 5, border);
  M5.Display.fillTriangle(bubble_x + bubble_w - 1,
                          tail_y - 7,
                          avatar_x - 1,
                          tail_y,
                          bubble_x + bubble_w - 1,
                          tail_y + 7,
                          TFT_WHITE);
  M5.Display.drawTriangle(bubble_x + bubble_w - 1,
                          tail_y - 7,
                          avatar_x - 1,
                          tail_y,
                          bubble_x + bubble_w - 1,
                          tail_y + 7,
                          border);

  const int text_x = bubble_x + 7;
  const int text_width = bubble_w - 14;
  M5.Display.setTextColor(border, TFT_WHITE);
  M5.Display.setTextWrap(false);
  M5.Display.setFont(&fonts::Font4);
  printFittedLine("Zero", text_x, bubble_y + 18, text_width, true);
  M5.Display.setFont(&fonts::Font0);
  printFittedLine("your trustworthy AI teammate", text_x, bubble_y + 72, text_width);
}

size_t utf8DisplayWidthPx(const std::string& text, size_t* offset) {
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

size_t estimateWrappedTextHeight(const std::string& text, size_t width_px) {
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

const char* modeRunErrorName(ModeRunError error) {
  switch (error) {
    case ModeRunError::None:
      return "none";
    case ModeRunError::WifiUnavailable:
      return "wifi-unavailable";
    case ModeRunError::AssistantPollFailed:
      return "assistant-poll";
    case ModeRunError::AssistantStorageFailed:
      return "assistant-storage";
    case ModeRunError::InvalidStateCommit:
      return "invalid-state";
    case ModeRunError::AssistantClearFailed:
      return "assistant-clear";
    case ModeRunError::VoiceCaptureFailed:
      return "voice-capture";
    case ModeRunError::VoiceToTextFailed:
      return "voice-to-text";
    case ModeRunError::MessageSendFailed:
      return "message-send";
  }
  return "unknown";
}

String assistantMessagePath(size_t index) {
  return "/assistant_" + String(static_cast<unsigned>(index)) + ".txt";
}

String assistantMessageTmpPath(size_t index) {
  return "/assistant_" + String(static_cast<unsigned>(index)) + ".tmp";
}

void removeIfExists(const char* path) {
  if (path != nullptr && LittleFS.exists(path)) {
    LittleFS.remove(path);
  }
}

void clearAssistantFiles() {
  for (size_t i = 0; i < kMaxAssistantMessages; ++i) {
    const String final_path = assistantMessagePath(i);
    const String tmp_path = assistantMessageTmpPath(i);
    removeIfExists(final_path.c_str());
    removeIfExists(tmp_path.c_str());
  }
  removeIfExists(kAssistantQueueManifestPath);
}

bool readTextFile(const char* path, std::string* value_out) {
  if (path == nullptr || value_out == nullptr) {
    return false;
  }
  File file = LittleFS.open(path, "r");
  if (!file) {
    return false;
  }
  value_out->clear();
  while (file.available()) {
    value_out->push_back(static_cast<char>(file.read()));
  }
  file.close();
  return true;
}

zero_buddy::AssistantQueueManifest readAssistantQueueManifest() {
  std::string manifest_body;
  if (!readTextFile(kAssistantQueueManifestPath, &manifest_body)) {
    return zero_buddy::AssistantQueueManifest();
  }
  return zero_buddy::parseAssistantQueueManifest(manifest_body, kMaxAssistantMessages);
}

size_t assistant_message_file_count() {
  size_t count = 0;
  for (size_t i = 0; i < kMaxAssistantMessages; ++i) {
    const String final_path = assistantMessagePath(i);
    if (LittleFS.exists(final_path.c_str())) {
      ++count;
    }
  }
  return count;
}

size_t assistant_message_count() {
  const auto manifest = readAssistantQueueManifest();
  if (manifest.ok) {
    return std::min(manifest.count, kMaxAssistantMessages);
  }
  return assistant_message_file_count();
}

bool assistant_message_exists() {
  return assistant_message_count() > 0;
}

bool read_assistant_message(size_t index, std::string* value_out) {
  if (index >= kMaxAssistantMessages || value_out == nullptr) {
    return false;
  }
  const String final_path = assistantMessagePath(index);
  return readTextFile(final_path.c_str(), value_out);
}

bool load_read_progress(ReadProgress* progress_out) {
  if (progress_out == nullptr) {
    return false;
  }
  progress_out->messageIndex = 0;
  progress_out->scrollTop = 0;

  const auto manifest = readAssistantQueueManifest();
  if (!manifest.ok) {
    return true;
  }
  progress_out->messageIndex = manifest.index;
  progress_out->scrollTop = manifest.scroll_top;
  return true;
}

bool append_assistant_message(const std::string& value) {
  const size_t index = assistant_message_count();
  if (index >= kMaxAssistantMessages) {
    return false;
  }
  const bool should_turn_led_on = !assistant_message_exists();
  const auto previous_manifest = readAssistantQueueManifest();
  const String tmp_path = assistantMessageTmpPath(index);
  const String final_path = assistantMessagePath(index);
  const size_t next_count = index + 1;
  const std::string trimmed = value.substr(0, kMaxAssistantMessageBytes);
  removeIfExists(tmp_path.c_str());
  if (!writeTextFile(tmp_path.c_str(), trimmed)) {
    removeIfExists(tmp_path.c_str());
    return false;
  }
  removeIfExists(final_path.c_str());
  if (!LittleFS.rename(tmp_path.c_str(), final_path.c_str())) {
    removeIfExists(tmp_path.c_str());
    return false;
  }
  size_t next_read_index = 0;
  size_t next_scroll_top = 0;
  if (previous_manifest.ok && index > 0) {
    next_read_index = previous_manifest.index;
    next_scroll_top = previous_manifest.scroll_top;
    if (next_read_index > next_count) {
      next_read_index = next_count - 1;
      next_scroll_top = 0;
    }
  }
  if (!writeAssistantQueueManifest(next_count, next_read_index, next_scroll_top)) {
    removeIfExists(final_path.c_str());
    return false;
  }
  zero_buddy::state::setHasAssistantMessage(&g_state, true);
  if (should_turn_led_on) {
    setAssistantLedOn();
  }
  return true;
}

bool clear_assistant_message() {
  clearAssistantFiles();
  zero_buddy::state::setHasAssistantMessage(&g_state, false);
  setAssistantLedOff();
  return true;
}

bool writeTextFile(const char* path, const std::string& value) {
  File file = LittleFS.open(path, "w");
  if (!file) {
    return false;
  }
  const size_t written =
      file.write(reinterpret_cast<const uint8_t*>(value.data()), value.size());
  file.close();
  return written == value.size();
}

bool writeAssistantQueueManifest(size_t count, size_t index, size_t scroll_top) {
  const std::string body =
      std::string("{\"count\":") + std::to_string(count) +
      ",\"index\":" + std::to_string(index) +
      ",\"scrollTop\":" + std::to_string(scroll_top) + "}";
  return writeTextFile(kAssistantQueueManifestPath, body);
}

String jsonEscape(const String& text) {
  String out;
  out.reserve(text.length() + 16);
  for (size_t i = 0; i < text.length(); ++i) {
    const char ch = text[i];
    if (ch == '"' || ch == '\\') {
      out += '\\';
      out += ch;
    } else if (ch == '\n') {
      out += "\\n";
    } else if (ch == '\r') {
      out += "\\r";
    } else if (ch == '\t') {
      out += "\\t";
    } else {
      out += ch;
    }
  }
  return out;
}

bool configLooksPlaceholder(const char* value) {
  return value == nullptr || value[0] == '\0' || strstr(value, "your-") == value;
}

enum class ButtonPressIntent {
  None,
  ShortPress,
  LongPress,
};

class ButtonAbortDetector {
 public:
  bool shouldAbort() {
    return intent() != ButtonPressIntent::None;
  }

  ButtonPressIntent intent() {
    if (latched_intent_ != ButtonPressIntent::None) {
      return latched_intent_;
    }

    const uint32_t now = millis();
    if (buttonADown()) {
      if (down_since_ms_ == 0) {
        down_since_ms_ = now == 0 ? 1 : now;
      }
      if (now - down_since_ms_ >= kLongPressMs) {
        latched_intent_ = ButtonPressIntent::LongPress;
      }
    } else if (down_since_ms_ != 0) {
      latched_intent_ = ButtonPressIntent::ShortPress;
      down_since_ms_ = 0;
    }
    return latched_intent_;
  }

  const char* abortReason() {
    switch (intent()) {
      case ButtonPressIntent::ShortPress:
        return "btn_a_short_press";
      case ButtonPressIntent::LongPress:
        return "btn_a_long_press";
      case ButtonPressIntent::None:
        break;
    }
    return nullptr;
  }

 private:
  uint32_t down_since_ms_ = 0;
  ButtonPressIntent latched_intent_ = ButtonPressIntent::None;
};

bool connectWifi(ButtonAbortDetector* abort_detector) {
  if (WiFi.status() == WL_CONNECTED) {
    return true;
  }
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  esp_wifi_set_ps(WIFI_PS_NONE);
  for (const auto& cred : kWifiCreds) {
    if (configLooksPlaceholder(cred.ssid)) {
      continue;
    }
    WiFi.disconnect(true, true);
    delay(100);
    WiFi.begin(cred.ssid, cred.password);
    const uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < kWifiConnectAttemptMs) {
      M5.update();
      if (abort_detector != nullptr && abort_detector->shouldAbort()) {
        return false;
      }
      delay(20);
    }
    if (WiFi.status() == WL_CONNECTED) {
      return true;
    }
  }
  return false;
}

bool httpGet(const String& url, String* body_out, ButtonAbortDetector* abort_detector) {
  if (body_out == nullptr || configLooksPlaceholder(kZeroApiKey)) {
    return false;
  }
  if (abort_detector != nullptr && abort_detector->shouldAbort()) {
    return false;
  }
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(12000);
  if (!http.begin(client, url)) {
    return false;
  }
  http.addHeader("Authorization", String("Bearer ") + kZeroApiKey);
  const int code = http.GET();
  if (abort_detector != nullptr && abort_detector->shouldAbort()) {
    http.end();
    return false;
  }
  if (code != 200) {
    http.end();
    return false;
  }
  *body_out = http.getString();
  http.end();
  return true;
}

bool httpPostJson(const String& url,
                  const String& body,
                  String* response_out,
                  ButtonAbortDetector* abort_detector) {
  if (response_out == nullptr || configLooksPlaceholder(kZeroApiKey)) {
    return false;
  }
  if (abort_detector != nullptr && abort_detector->shouldAbort()) {
    return false;
  }
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(20000);
  if (!http.begin(client, url)) {
    return false;
  }
  http.addHeader("Authorization", String("Bearer ") + kZeroApiKey);
  http.addHeader("Content-Type", "application/json");
  const int code = http.POST(body);
  if (abort_detector != nullptr && abort_detector->shouldAbort()) {
    http.end();
    return false;
  }
  if (code != 200 && code != 201) {
    http.end();
    return false;
  }
  *response_out = http.getString();
  http.end();
  return true;
}

bool readExact(WiFiClientSecure* client, uint8_t* out, size_t len, uint32_t timeout_ms) {
  if (client == nullptr || out == nullptr) {
    return false;
  }
  size_t offset = 0;
  const uint32_t deadline = millis() + timeout_ms;
  while (offset < len && static_cast<int32_t>(millis() - deadline) < 0) {
    const int available = client->available();
    if (available <= 0) {
      delay(2);
      continue;
    }
    const size_t want = std::min(len - offset, static_cast<size_t>(available));
    const int got = client->read(out + offset, want);
    if (got > 0) {
      offset += static_cast<size_t>(got);
    }
  }
  return offset == len;
}

bool readHttpHeaders(WiFiClientSecure* client, String* headers_out, uint32_t timeout_ms) {
  if (client == nullptr || headers_out == nullptr) {
    return false;
  }
  headers_out->remove(0);
  const uint32_t deadline = millis() + timeout_ms;
  while (headers_out->indexOf("\r\n\r\n") < 0 &&
         static_cast<int32_t>(millis() - deadline) < 0) {
    while (client->available() > 0) {
      *headers_out += static_cast<char>(client->read());
      if (headers_out->indexOf("\r\n\r\n") >= 0) {
        return true;
      }
      if (headers_out->length() > 4096) {
        return false;
      }
    }
    delay(2);
  }
  return headers_out->indexOf("\r\n\r\n") >= 0;
}

bool wsSendBinary(WiFiClientSecure* client, const uint8_t* payload, size_t payload_len) {
  if (client == nullptr || payload == nullptr || payload_len > 65535) {
    return false;
  }
  std::vector<uint8_t> frame;
  frame.reserve(payload_len + 16);
  frame.push_back(0x82);
  if (payload_len < 126) {
    frame.push_back(0x80 | static_cast<uint8_t>(payload_len));
  } else {
    frame.push_back(0x80 | 126);
    frame.push_back((payload_len >> 8) & 0xff);
    frame.push_back(payload_len & 0xff);
  }
  const uint8_t mask[4] = {
      static_cast<uint8_t>(esp_random() & 0xff),
      static_cast<uint8_t>(esp_random() & 0xff),
      static_cast<uint8_t>(esp_random() & 0xff),
      static_cast<uint8_t>(esp_random() & 0xff),
  };
  frame.insert(frame.end(), mask, mask + 4);
  for (size_t i = 0; i < payload_len; ++i) {
    frame.push_back(payload[i] ^ mask[i % 4]);
  }
  return client->write(frame.data(), frame.size()) == frame.size();
}

bool wsReadFrame(WiFiClientSecure* client,
                 uint8_t* opcode_out,
                 std::vector<uint8_t>* payload_out,
                 uint32_t timeout_ms) {
  uint8_t hdr[2] = {0};
  if (!readExact(client, hdr, sizeof(hdr), timeout_ms)) {
    return false;
  }
  *opcode_out = hdr[0] & 0x0f;
  const bool masked = (hdr[1] & 0x80) != 0;
  uint64_t payload_len = hdr[1] & 0x7f;
  if (payload_len == 126) {
    uint8_t ext[2] = {0};
    if (!readExact(client, ext, sizeof(ext), timeout_ms)) {
      return false;
    }
    payload_len = (static_cast<uint64_t>(ext[0]) << 8) | ext[1];
  } else if (payload_len == 127) {
    uint8_t ext[8] = {0};
    if (!readExact(client, ext, sizeof(ext), timeout_ms)) {
      return false;
    }
    payload_len = 0;
    for (uint8_t b : ext) {
      payload_len = (payload_len << 8) | b;
    }
  }
  if (payload_len > 32 * 1024) {
    return false;
  }
  uint8_t mask[4] = {0};
  if (masked && !readExact(client, mask, sizeof(mask), timeout_ms)) {
    return false;
  }
  payload_out->assign(static_cast<size_t>(payload_len), 0);
  if (payload_len > 0 &&
      !readExact(client, payload_out->data(), static_cast<size_t>(payload_len), timeout_ms)) {
    return false;
  }
  if (masked) {
    for (size_t i = 0; i < payload_out->size(); ++i) {
      (*payload_out)[i] ^= mask[i % 4];
    }
  }
  return true;
}

bool sendAsrClientRequest(WiFiClientSecure* client) {
  const String json =
      String("{\"user\":{\"uid\":\"m5stickcplus\",\"did\":\"M5StickCPlus\",") +
      "\"platform\":\"arduino-esp32\",\"sdk_version\":\"zero-buddy\",\"app_version\":\"2\"},"
      "\"audio\":{\"format\":\"pcm\",\"codec\":\"raw\",\"rate\":16000,\"bits\":16,"
      "\"channel\":1,\"language\":\"zh-CN\"},"
      "\"request\":{\"model_name\":\"bigmodel\",\"enable_itn\":true,"
      "\"enable_punc\":true,\"enable_ddc\":false}}";
  std::vector<uint8_t> packet;
  packet.reserve(json.length() + 8);
  packet.push_back(0x11);
  packet.push_back(0x10);
  packet.push_back(0x10);
  packet.push_back(0x00);
  zero_buddy::appendU32(packet, static_cast<uint32_t>(json.length()));
  packet.insert(packet.end(), json.begin(), json.end());
  return wsSendBinary(client, packet.data(), packet.size());
}

bool transcribePcmFile(const char* path, std::string* text_out) {
  if (text_out == nullptr || configLooksPlaceholder(kAsrWsAppKey) ||
      configLooksPlaceholder(kAsrWsAccessKey)) {
    return false;
  }
  File file = LittleFS.open(path, FILE_READ);
  if (!file || file.size() == 0) {
    if (file) {
      file.close();
    }
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(12000);
  if (!client.connect(kAsrWsHost, kAsrWsPort)) {
    file.close();
    return false;
  }

  const String connect_id = String(static_cast<uint32_t>(esp_random()), HEX) +
                            String(static_cast<uint32_t>(esp_random()), HEX);
  const String request =
      String("GET ") + kAsrWsPath + " HTTP/1.1\r\n" +
      "Host: " + kAsrWsHost + "\r\n" +
      "Upgrade: websocket\r\n"
      "Connection: Upgrade\r\n"
      "Sec-WebSocket-Version: 13\r\n"
      "Sec-WebSocket-Key: M5StickCPlusWs==\r\n" +
      "X-Api-App-Key: " + kAsrWsAppKey + "\r\n" +
      "X-Api-Access-Key: " + kAsrWsAccessKey + "\r\n" +
      "X-Api-Resource-Id: " + kAsrWsResourceId + "\r\n" +
      "X-Api-Connect-Id: " + connect_id + "\r\n\r\n";
  client.print(request);

  String headers;
  if (!readHttpHeaders(&client, &headers, 12000) || !headers.startsWith("HTTP/1.1 101")) {
    file.close();
    client.stop();
    return false;
  }
  if (!sendAsrClientRequest(&client)) {
    file.close();
    client.stop();
    return false;
  }

  const size_t total = static_cast<size_t>(file.size());
  size_t sent = 0;
  while (file.available()) {
    const size_t read_len = file.read(g_asr_buffer, sizeof(g_asr_buffer));
    if (read_len == 0) {
      break;
    }
    sent += read_len;
    const bool final = sent >= total;
    const auto packet = zero_buddy::buildStreamAudioPacket(g_asr_buffer, read_len, final);
    if (!wsSendBinary(&client, packet.data(), packet.size())) {
      file.close();
      client.stop();
      return false;
    }
    delay(80);
  }
  file.close();

  const uint32_t deadline = millis() + 10000;
  std::string best_text;
  while (static_cast<int32_t>(millis() - deadline) < 0) {
    uint8_t opcode = 0;
    std::vector<uint8_t> payload;
    if (!wsReadFrame(&client, &opcode, &payload, 1000)) {
      continue;
    }
    if (opcode != 0x1 && opcode != 0x2) {
      continue;
    }
    const auto parsed = zero_buddy::parseAsrServerPayload(payload.data(), payload.size());
    if (!parsed.ok) {
      continue;
    }
    if (!parsed.text.empty()) {
      best_text = parsed.text;
    }
    if (parsed.final) {
      break;
    }
  }
  client.stop();
  *text_out = best_text;
  return !text_out->empty();
}

class HardwareCheckOps : public CheckAssistantMessageOps {
 public:
  void setMode(CheckAssistantMessageMode* mode) {
    mode_ = mode;
  }

  void setCpuForNetwork() override {
    setCpu(kNetworkCpuMhz);
  }

  bool ensureWifiConnected() override {
    const bool ok = connectWifi(&abort_detector_);
    abortForButtonIntent();
    return ok && !isAborted();
  }

  bool pollAssistantMessages(const std::string& since_id,
                             AssistantCheckResult* result_out) override {
    if (result_out == nullptr || configLooksPlaceholder(kZeroThreadId)) {
      return false;
    }
    String url = String("https://") + kZeroHost + "/api/v1/chat-threads/" +
                 kZeroThreadId + "/messages?limit=10";
    if (!since_id.empty()) {
      url += "&sinceId=" + String(since_id.c_str());
    }
    String body;
    if (!httpGet(url, &body, &abort_detector_)) {
      abortForButtonIntent();
      return false;
    }
    abortForButtonIntent();
    if (isAborted()) {
      return false;
    }

    const auto parsed = zero_buddy::parseZeroMessagesResponse(body.c_str());
    if (parsed.newest_message_id.size() >= zero_buddy::state::kLastMessageIdBytes) {
      return false;
    }
    result_out->newestMessageId = parsed.newest_message_id;
    result_out->assistantMessages.clear();
    const size_t capacity = kMaxAssistantMessages - assistant_message_count();
    for (const auto& message : parsed.assistant_messages) {
      if (result_out->assistantMessages.size() >= capacity) {
        break;
      }
      result_out->assistantMessages.push_back(message);
    }
    result_out->hasNewAssistantMessages = !result_out->assistantMessages.empty();
    return true;
  }

  bool appendAssistantMessages(const AssistantCheckResult& result) override {
    cleanupTempFiles();
    const size_t capacity = kMaxAssistantMessages - assistant_message_count();
    if (result.assistantMessages.size() > capacity) {
      return false;
    }
    for (const auto& message : result.assistantMessages) {
      if (!append_assistant_message(message)) {
        return false;
      }
    }
    return true;
  }

  void cancelNetwork() override {
    // Current HTTP calls are synchronous and own their client locally.
    // There is no persistent network handle to close here.
  }

  void closeFiles() override {}

  void cleanupTempFiles() override {
    for (size_t i = 0; i < kMaxAssistantMessages; ++i) {
      const String tmp_path = assistantMessageTmpPath(i);
      removeIfExists(tmp_path.c_str());
    }
  }

 private:
  bool isAborted() const {
    return mode_ != nullptr && mode_->abortRequested();
  }

  void abortForButtonIntent() {
    if (mode_ != nullptr && abort_detector_.shouldAbort()) {
      mode_->abort(abort_detector_.abortReason());
    }
  }

  CheckAssistantMessageMode* mode_ = nullptr;
  ButtonAbortDetector abort_detector_;
};

class HardwareDeepSleepOps : public DeepSleepOps {
 public:
  void configureRtcWake(uint32_t delay_ms) override {
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(delay_ms) * 1000ULL);
  }

  void configureBtnAWake() override {
    esp_sleep_enable_ext0_wakeup(kBtnAWakePin, 0);
  }

  void screenOff() override {
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setBrightness(0);
  }

  void disconnectWifi() override {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
  }

  void enterCpuHibernate() override {
    delay(20);
    esp_deep_sleep_start();
  }

  void cancelRtcWake() override {
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
  }

};

class HardwareReadOps : public ReadOps {
 public:
  void screenOn() override {
    M5.Display.setRotation(3);
    M5.Display.setBrightness(kScreenBrightness);
  }

  void setCpuForReading() override {
    setCpu(kReadCpuMhz);
  }

  size_t storedAssistantMessageCount() override {
    return assistant_message_count();
  }

  bool loadReadProgress(ReadProgress* progress_out) override {
    return load_read_progress(progress_out);
  }

  bool loadAssistantMessage(size_t index, std::string* message_out) override {
    return read_assistant_message(index, message_out);
  }

  size_t maxScrollTopForMessage(const std::string& message) override {
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

  size_t scrollStep() override {
    return readViewportHeight();
  }

  bool saveReadProgress(size_t count, size_t index, size_t scroll_top) override {
    if (count == 0) {
      return writeAssistantQueueManifest(0, 0, 0);
    }
    if (index > count) {
      index = count - 1;
      scroll_top = 0;
    }
    return writeAssistantQueueManifest(count, index, scroll_top);
  }

  bool clearAssistantMessages() override {
    return clear_assistant_message();
  }

  void renderNoAssistantMessage() override {
    drawEmptyReadScreen();
  }

  void renderAssistantMessage(size_t index,
                              size_t count,
                              const std::string& message,
                              size_t scroll_top) override {
    M5.Display.setRotation(3);
    M5.Display.setBrightness(kScreenBrightness);
    const uint16_t bg = avatarBackgroundColor();
    const uint16_t fg = dialogueBorderColor();
    M5.Display.fillScreen(bg);
    M5.Display.setTextColor(fg, bg);
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

    const int body_y = readBodyTop();
    const int body_h = static_cast<int>(readViewportHeight());
    M5.Display.setClipRect(kReadPaddingLeft,
                           body_y,
                           M5.Display.width() - kReadPaddingLeft - kReadPaddingRight,
                           body_h);
    M5.Display.setTextWrap(true);
    M5.Display.setFont(&fonts::efontCN_12);
    M5.Display.setCursor(kReadPaddingLeft,
                         body_y - static_cast<int>(scroll_top));
    M5.Display.print(message.c_str());
    M5.Display.clearClipRect();
    M5.Display.setTextWrap(false);

    const bool has_next_page =
        scroll_top < maxScrollTopForMessage(message) || index + 1 < count;
    if (has_next_page) {
      drawNextPageArrow();
    }
  }

  ReadInput waitForInput(uint32_t timeout_ms) override {
    const uint32_t start = millis();
    uint32_t down_since_ms = 0;
    while (millis() - start < timeout_ms) {
      M5.update();
      if (buttonADown()) {
        const uint32_t now = millis();
        if (down_since_ms == 0) {
          down_since_ms = now == 0 ? 1 : now;
        }
        if (now - down_since_ms >= kLongPressMs) {
          return ReadInput::LongPress;
        }
      } else if (down_since_ms != 0) {
        return ReadInput::ShortPress;
      }
      delay(10);
    }
    return ReadInput::Timeout;
  }

  void cancelIdleTimer() override {}

  void closeFiles() override {}

 private:
  int readBodyTop() const {
    return static_cast<int>(kReadPaddingTop + kReadHeaderHeight + 4);
  }

  size_t readViewportHeight() const {
    return static_cast<size_t>(
        std::max(1, M5.Display.height() - readBodyTop() -
                    static_cast<int>(kReadPaddingBottom)));
  }

  void drawNextPageArrow() {
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
};

class HardwareRecordingOps : public RecordingOps {
 public:
  void screenOn() override {
    M5.Display.setRotation(3);
    M5.Display.setBrightness(kScreenBrightness);
    drawAvatarStatus("recording", "hold BtnA");
  }

  void showResult(const ModeRunResult& result) {
    if (result.status == ModeRunStatus::Completed) {
      drawAvatarStatus("sent", "message sent");
      Serial.println("recording completed");
      Serial.flush();
      delay(5000);
      return;
    }
    if (result.status == ModeRunStatus::Aborted) {
      drawAvatarStatus("aborted", "recording");
      Serial.print("recording aborted: ");
      Serial.println("abort");
      Serial.flush();
      delay(5000);
      return;
    }

    const char* error = modeRunErrorName(result.error);
    String detail(error);
    if (recording_failure_detail_ != nullptr && recording_failure_detail_[0] != '\0') {
      detail += ":";
      detail += recording_failure_detail_;
    }
    drawAvatarStatus("failed", detail.c_str());
    Serial.print("recording failed: ");
    Serial.println(error);
    if (recording_failure_detail_ != nullptr && recording_failure_detail_[0] != '\0') {
      Serial.print("recording detail: ");
      Serial.println(recording_failure_detail_);
    }
    Serial.flush();
    delay(5000);
  }

  void setCpuForRecording() override {
    setCpu(kRecordingCpuMhz);
  }

  void cancelRtcWake() override {
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
  }

  bool clearAssistantMessages() override {
    return clear_assistant_message();
  }

  bool recordVoiceToFile() override {
    recording_failure_detail_ = "";
    drawAvatarStatus("recording", "speak now");
    auto mic_cfg = M5.Mic.config();
    mic_cfg.pin_data_in = GPIO_NUM_34;
    mic_cfg.pin_ws = GPIO_NUM_0;
    mic_cfg.pin_mck = I2S_PIN_NO_CHANGE;
    mic_cfg.sample_rate = kSampleRate;
    mic_cfg.over_sampling = 1;
    mic_cfg.magnification = 16;
    mic_cfg.noise_filter_level = 0;
    mic_cfg.dma_buf_len = 128;
    mic_cfg.dma_buf_count = 8;
    mic_cfg.stereo = false;
    mic_cfg.left_channel = false;
    mic_cfg.use_adc = false;
    M5.Mic.config(mic_cfg);
    if (!M5.Mic.begin()) {
      recording_failure_detail_ = "mic-begin";
      return false;
    }

    File file = LittleFS.open(kVoicePcmPath, "w");
    if (!file) {
      M5.Mic.end();
      recording_failure_detail_ = "file-open";
      return false;
    }

    beepMicOn();
    size_t bytes_written = 0;
    const uint32_t start = millis();
    while (buttonADown() && millis() - start < kMaxRecordingMs) {
      M5.update();
      const bool got =
          M5.Mic.record(reinterpret_cast<int16_t*>(g_pcm_buffer),
                        kMicSamplesPerRead,
                        kSampleRate);
      if (!got) {
        delay(2);
        continue;
      }
      const size_t written = file.write(g_pcm_buffer, sizeof(g_pcm_buffer));
      if (written != sizeof(g_pcm_buffer)) {
        file.close();
        M5.Mic.end();
        beepMicOff();
        recording_failure_detail_ = "file-write";
        return false;
      }
      bytes_written += written;
    }
    file.flush();
    file.close();
    M5.Mic.end();
    beepMicOff();
    if (bytes_written == 0) {
      recording_failure_detail_ = "too-short";
    }
    return bytes_written > 0;
  }

  bool ensureWifiConnected() override {
    recording_failure_detail_ = "";
    drawAvatarStatus("recording", "wifi");
    const bool ok = connectWifi(nullptr);
    if (!ok) {
      recording_failure_detail_ = "connect";
    }
    return ok;
  }

  bool voiceToText(std::string* text_out) override {
    recording_failure_detail_ = "";
    drawAvatarStatus("recording", "speech to text");
    const bool ok = transcribePcmFile(kVoicePcmPath, text_out);
    if (!ok) {
      recording_failure_detail_ = "asr";
    }
    return ok;
  }

  void deleteVoiceFile() override {
    removeIfExists(kVoicePcmPath);
  }

  bool sendTextMessage(const std::string& text, std::string* user_message_id_out) override {
    if (user_message_id_out == nullptr || text.empty() || configLooksPlaceholder(kZeroThreadId)) {
      recording_failure_detail_ = "bad-message";
      return false;
    }
    recording_failure_detail_ = "";
    drawAvatarStatus("recording", "send message");
    const String body =
        String("{\"prompt\":\"") + jsonEscape(String(text.c_str())) +
        "\",\"threadId\":\"" + kZeroThreadId + "\"}";
    String response;
    if (!httpPostJson(kZeroPostUrl, body, &response, nullptr)) {
      recording_failure_detail_ = "http";
      return false;
    }
    const std::string id = zero_buddy::extractJsonString(response.c_str(), "messageId");
    if (id.empty()) {
      recording_failure_detail_ = "message-id";
      return false;
    }
    *user_message_id_out = id;
    return true;
  }

  void stopVoiceRecording() override {
    M5.Mic.end();
  }

  void cancelVoiceToText() override {
    // Current ASR calls are synchronous and own their client locally.
    // There is no persistent ASR handle to close here.
  }

  void cancelTextMessageSend() override {
    // Current HTTP calls are synchronous and own their client locally.
    // There is no persistent send handle to close here.
  }

  void closeFiles() override {}

 private:
  const char* recording_failure_detail_ = "";
};

void enterDeepSleep();
void runReadThenSleep();
void runRecordingOnce();

void runRecordingOnce() {
  zero_buddy::state::setMode(&g_state, zero_buddy::state::Mode::Recording);
  HardwareRecordingOps ops;
  RecordingMode mode(&g_state, &ops);
  const auto result = mode.main();
  ops.showResult(result);
}

void enterDeepSleep() {
  zero_buddy::state::setMode(&g_state, zero_buddy::state::Mode::DeepSleep);
  HardwareDeepSleepOps ops;
  DeepSleepMode mode(&g_state, &ops);
  mode.main();
}

void runCheckAssistantThenSleep() {
  zero_buddy::state::setMode(&g_state, zero_buddy::state::Mode::CheckAssistantMessage);
  HardwareCheckOps ops;
  CheckAssistantMessageMode mode(&g_state, &ops);
  ops.setMode(&mode);
  const auto result = mode.main();
  if (result.status == ModeRunStatus::Aborted && mode.abortRequested()) {
    const char* reason = mode.abortReason();
    if (reason != nullptr && strcmp(reason, "btn_a_short_press") == 0) {
      runReadThenSleep();
      return;
    }
    runRecordingOnce();
  }
  enterDeepSleep();
}

void runReadThenSleep() {
  zero_buddy::state::setMode(&g_state, zero_buddy::state::Mode::Read);
  HardwareReadOps ops;
  ReadMode mode(&g_state, &ops);
  const auto result = mode.main();
  if (result.status == ModeRunStatus::Aborted && mode.abortRequested()) {
    runRecordingOnce();
  } else {
    mode.abort("read_exit");
  }
  enterDeepSleep();
}

void runRecordingThenSleep() {
  runRecordingOnce();
  enterDeepSleep();
}

}  // namespace

void setup() {
  Serial.begin(115200);
  auto cfg = M5.config();
  cfg.clear_display = false;
  M5.begin(cfg);

  pinMode(static_cast<int>(kBtnAWakePin), INPUT);
  ledcSetup(0, 2000, 8);
  ledcAttachPin(kBuzzerPin, 0);
  ledcWriteTone(0, 0);

  setCpu(kRecordingCpuMhz);
  LittleFS.begin(true);
  ensureGlobalStateInitialized();
  zero_buddy::state::setHasAssistantMessage(&g_state, assistant_message_exists());

  const esp_sleep_wakeup_cause_t wake_cause = esp_sleep_get_wakeup_cause();
  if (wake_cause == ESP_SLEEP_WAKEUP_UNDEFINED) {
    drawBootScreen();
    delay(kBootSplashMs);
  }

  if (wake_cause == ESP_SLEEP_WAKEUP_TIMER) {
    runCheckAssistantThenSleep();
    return;
  }

  if (wake_cause == ESP_SLEEP_WAKEUP_EXT0 || buttonADown()) {
    if (waitForBtnALongPress(kLongPressMs)) {
      runRecordingThenSleep();
      return;
    }
    runReadThenSleep();
    return;
  }

  enterDeepSleep();
}

void loop() {}
