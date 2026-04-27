#include <Arduino.h>
#include <HTTPClient.h>
#include <LittleFS.h>
#include <M5Unified.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <driver/i2s.h>
#include <driver/gpio.h>
#include <esp_heap_caps.h>
#include <esp_sleep.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lwip/sockets.h>
#include <lwip/netdb.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/error.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/ssl.h>
#include <vector>

#include "base64.h"
#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
#endif
#include "zero_buddy_core.h"

#ifndef ZERO_BUDDY_SERIAL_LOGS
#define ZERO_BUDDY_SERIAL_LOGS 0
#endif

#if ZERO_BUDDY_SERIAL_LOGS
#define ZB_SERIAL_BEGIN(...) Serial.begin(__VA_ARGS__)
#define ZB_LOG_PRINTF(...) Serial.printf(__VA_ARGS__)
#define ZB_LOG_PRINTLN(...) Serial.println(__VA_ARGS__)
#else
#define ZB_SERIAL_BEGIN(...) do {} while (0)
#define ZB_LOG_PRINTF(...) do {} while (0)
#define ZB_LOG_PRINTLN(...) do {} while (0)
#endif

namespace {
constexpr int kLedPin = 10;
constexpr int kBuzzerPin = 2;
constexpr int kMicClockPin = 0;
constexpr int kMicDataPin = 34;
constexpr gpio_num_t kBtnAWakePin = GPIO_NUM_37;
constexpr gpio_num_t kBtnBWakePin = GPIO_NUM_39;
constexpr uint8_t kScreenBrightnessLevels[] = {24, 60, 110, 170, 235};
constexpr uint8_t kScreenBrightnessLevelCount =
    sizeof(kScreenBrightnessLevels) / sizeof(kScreenBrightnessLevels[0]);
constexpr uint8_t kDefaultScreenBrightnessLevel = 2;
constexpr int kScreenWidth = 240;
constexpr int kScreenHeight = 135;
constexpr int kUiOffsetX = 1;
constexpr int kAnimFrameMs = 180;
constexpr int kReplyBubbleX = 122;
constexpr int kReplyBubbleY = 18;
constexpr int kReplyBubbleW = 108;
constexpr int kReplyBubbleH = 86;
constexpr int kReplyLineHeight = 13;
constexpr int kReplyVisibleLines = 5;
constexpr int kBodyTop = 12;
constexpr int kBodyLeft = 5;
constexpr int kBodyWidth = 229;
constexpr int kBodyHeight = 108;
constexpr int kBodyLineHeight = 16;
constexpr int kBodyVisibleLines = 6;
constexpr int kBatteryIconW = 24;
constexpr int kBatteryIconH = 9;
constexpr int kBatteryIconTipW = 2;
constexpr uint32_t kBatteryReadIntervalMs = 30000;
constexpr int kSampleRate = 16000;
constexpr size_t kReadLen = 512;
constexpr size_t kStreamChunkBytes = kSampleRate * 2 / 5;  // 200ms PCM16 mono
constexpr size_t kRecordBacklogBytes = 64 * 1024;  // 2 seconds of PCM16 mono at 16kHz.
constexpr size_t kMaxPendingAssistantMessages = 5;
constexpr size_t kMaxAssistantMessageBytes = 4096;
constexpr unsigned long kAsrChunkMinIntervalMs = 100;
constexpr unsigned long kAsrConnectDelayMs = 0;
constexpr unsigned long kAsrRecordConnectWaitMs = 9000;
constexpr size_t kMicRecordSamplesPerRead = 256;
constexpr unsigned long kMicTestDurationMs = 1500;
constexpr unsigned long kAsrTestDurationMs = 2000;
constexpr uint32_t kAssistantLedBlinkIntervalMs = 3000;
constexpr uint32_t kAssistantLedBlinkPulseMs = 120;
constexpr uint32_t kAwakeWindowMs = 10UL * 60UL * 1000UL;
constexpr uint32_t kAssistantPollWindowMs = 10UL * 60UL * 1000UL;

constexpr char kAsrWsHost[] = "openspeech.bytedance.com";
constexpr uint16_t kAsrWsPort = 443;
constexpr char kAsrWsPath[] = "/api/v3/sauc/bigmodel_nostream";

constexpr char kZeroApiUrl[] = "https://api.vm0.ai/api/v1/chat-threads/messages";
constexpr char kZeroHost[] = "api.vm0.ai";
constexpr uint16_t kZeroPort = 443;
constexpr unsigned long kZeroPollIntervalMs = 10000;
constexpr unsigned long kZeroRequestDeadlineMs = 10000;
constexpr bool kBootZeroSelfTest = false;
constexpr char kZeroResponsePath[] = "/zero_resp.json";
constexpr char kAsrRecordPcmPath[] = "/asr_rec.pcm";

uint8_t g_audio_buffer[kReadLen] = {0};
uint8_t g_stream_chunk[kStreamChunkBytes] = {0};
uint8_t g_record_backlog_storage[kRecordBacklogBytes] = {0};
zero_buddy::FixedByteQueue g_record_backlog(g_record_backlog_storage, sizeof(g_record_backlog_storage));
size_t g_recorded_bytes = 0;
size_t g_record_sent_bytes = 0;
size_t g_stream_chunk_fill = 0;
uint32_t g_asr_chunks_sent = 0;
unsigned long g_last_asr_chunk_sent_ms = 0;
unsigned long g_first_audio_ms = 0;
unsigned long g_last_audio_ms = 0;
size_t g_last_asr_rec_recorded_bytes = 0;
size_t g_last_asr_rec_flash_bytes = 0;
size_t g_last_asr_rec_sent_bytes = 0;
size_t g_last_asr_rec_dropped_bytes = 0;
uint32_t g_last_asr_rec_chunks = 0;
unsigned long g_last_asr_rec_connect_ms = 0;
bool g_record_to_flash = false;
bool g_record_flash_failed = false;
size_t g_record_flash_bytes = 0;
File g_record_flash_file;
uint32_t g_record_peak = 0;
uint32_t g_record_avg_abs = 0;
uint32_t g_record_clipped_samples = 0;
uint32_t g_raw_peak = 0;
uint64_t g_raw_abs_sum = 0;
uint32_t g_raw_sample_count = 0;
uint32_t g_raw_clipped_samples = 0;
bool g_wifi_connected = false;
bool g_wifi_connecting = false;
bool g_recording = false;
bool g_uploading = false;
bool g_asr_connect_started = false;
bool g_waiting_for_assistant = false;
unsigned long g_buzzer_stop_ms = 0;
unsigned long g_record_start_ms = 0;
String g_ip_address = "-";
String g_status = "booting";
String g_result = "hold BtnA to record";
String g_zero_last_seen_message_id = "";
String g_current_assistant_message = "";
size_t g_pending_assistant_count = 0;
size_t g_pending_message_index = 0;
unsigned long g_last_zero_poll_ms = 0;
unsigned long g_last_scroll_ms = 0;
unsigned long g_last_anim_ms = 0;
size_t g_scroll_line = 0;
uint8_t g_screen_brightness_level = kDefaultScreenBrightnessLevel;
bool g_btn_b_hold_seen = false;
bool g_suppress_wake_short_action = false;
std::vector<String> g_current_message_lines;
bool g_flash_store_ready = false;
zero_buddy::NotificationBlinkState g_assistant_led_blink;
zero_buddy::PowerWindowState g_power;

struct BatteryUiState {
  int32_t level_percent = -1;
  bool charging = false;
  bool known = false;
  uint32_t last_read_ms = 0;
};

BatteryUiState g_battery_ui;

enum class TestMode : uint8_t {
  Normal = 0,
  Mic = 1,
  Asr = 2,
  AsrLive = 3,
  AsrFlash = 4,
  ZeroPost = 5,
  ZeroPoll = 6,
  Count = 7,
};

enum class NetworkTask : uint8_t {
  None,
  Asr,
  Zero,
};

enum class AsrConnectState : uint8_t {
  Idle,
  Connecting,
  Ready,
  Failed,
};

TestMode g_test_mode = TestMode::Mic;
NetworkTask g_network_task = NetworkTask::None;
volatile AsrConnectState g_asr_connect_state = AsrConnectState::Idle;
TaskHandle_t g_asr_connect_task = nullptr;
char g_asr_connect_error[96] = {0};
unsigned long g_asr_async_started_ms = 0;
unsigned long g_asr_async_finished_ms = 0;
unsigned long g_test_capture_deadline_ms = 0;

void drawUiFrame();
void drawBatteryStatus();
void refreshBatteryStatus(bool force);
bool pollZeroAssistantMessages(bool baseline_only, String* error_out);
void closeStreamAsrSession();
void clearRecordBacklog();
void removeFlashFileIfExists(const char* path);
void applyScreenBrightness();
void cycleScreenBrightness();
void wakeScreen(bool user_action, const char* reason);
void enterScreenSleep(const char* reason);
void runLightSleepIfIdle();
void beginAssistantPollingWindow();
void stopAssistantPollingWindow();
bool assistantPollingWindowActive();
void clearAssistantLedBlinkForUserAction();
String currentPendingAssistantMessage();
void advancePendingAssistantMessage();
void resetMessageScroll();
void rebuildCurrentMessageLayout();
bool startAsyncAsrConnect(String* error_out);
bool waitForAsyncAsrReady(String* error_out, uint32_t timeout_ms);
void waitForAsrConnectTask(uint32_t timeout_ms);
void resetAsrConnectStateIfDone();
bool writeRecordFlash(const uint8_t* data, size_t data_len);
void closeRecordFlash();

enum class NormalScene : uint8_t {
  WifiConnecting,
  WifiOffline,
  Idle,
  Recording,
  Recognizing,
  Sending,
  Waiting,
  Reply,
  Failed,
};

NormalScene currentNormalScene() {
  if (!g_wifi_connected) {
    return g_wifi_connecting ? NormalScene::WifiConnecting : NormalScene::WifiOffline;
  }
  if (!currentPendingAssistantMessage().isEmpty()) {
    return NormalScene::Reply;
  }
  if (g_recording) {
    return NormalScene::Recording;
  }
  if (g_uploading && g_status == "recognizing") {
    return NormalScene::Recognizing;
  }
  if (g_status == "sending zero") {
    return NormalScene::Sending;
  }
  if (g_waiting_for_assistant || g_status == "sent to zero") {
    return NormalScene::Waiting;
  }
  if (g_status == "failed" || g_status == "zero failed" || g_status == "audio low") {
    return NormalScene::Failed;
  }
  return NormalScene::Idle;
}

uint16_t spriteColor(char code) {
  switch (code) {
    case '.':
      return TFT_TRANSPARENT;
    case 'S':
      return TFT_BLACK;
    case 'H':
      return 0xFC00;
    case 'F':
      return 0xF596;
    case 'E':
      return 0x5ACB;
    case 'M':
      return 0x4208;
    case 'W':
      return TFT_WHITE;
    case 'Y':
      return 0xFEA0;
    case 'R':
      return TFT_RED;
    case 'B':
      return 0x4A9F;
    case 'P':
      return 0xC9DF;
  }
  return TFT_BLACK;
}

template <typename TDisplay>
void drawPixelSprite(TDisplay& d, int x, int y, int scale, const char* const* rows, int row_count) {
  for (int row = 0; row < row_count; ++row) {
    const char* line = rows[row];
    for (int col = 0; line[col] != '\0'; ++col) {
      const uint16_t color = spriteColor(line[col]);
      if (color == TFT_TRANSPARENT) {
        continue;
      }
      d.fillRect(x + col * scale, y + row * scale, scale, scale, color);
    }
  }
}

void drawNormalAnimation() {
  static const char* const kHeroIdleA[] = {
      "......HHHHHH......",
      "....HHHHHHHHHH....",
      "...HHHHHHHHHHHH...",
      "..HHHSSHHHHSSHHH..",
      "..HHFFFFFFFFFFHH..",
      ".HHFFSFFFFFSFFFHH.",
      ".HHFFFFFFSFFFFFHH.",
      ".HHFFFSSFFFFSFFHH.",
      "..HHFFFFFFFFFFHH..",
      "..HHHFFFFFFFHHH...",
      "...HHHMMMMMMHH....",
      "...HHMMMMMMMMHH...",
      "..HHMMMMMMMMMMHH..",
      "..HHMMMMMMMMMMHH..",
      "...HH........HH...",
      "..................",
  };
  static const char* const kHeroIdleB[] = {
      "......HHHHHH......",
      "....HHHHHHHHHH....",
      "...HHHHHHHHHHHH...",
      "..HHHSSHHHHSSHHH..",
      "..HHFFFFFFFFFFHH..",
      ".HHFFSFFFFFSFFFHH.",
      ".HHFFFFFFFFFFFFHH.",
      ".HHFFFSSSSFFSSFHH.",
      "..HHFFFFFFFFFFHH..",
      "..HHHFFFFFFFHHH...",
      "...HHHMMMMMMHH....",
      "...HHMMMMMMMMHH...",
      "..HHMMMMMMMMMMHH..",
      "..HHMMMMMMMMMMHH..",
      "...HH........HH...",
      "..................",
  };
  static const char* const kHeroTalk[] = {
      "......HHHHHH......",
      "....HHHHHHHHHH....",
      "...HHHHHHHHHHHH...",
      "..HHHSSHHHHSSHHH..",
      "..HHFFFFFFFFFFHH..",
      ".HHFFSFFFFFSFFFHH.",
      ".HHFFFFFFSFFFFFHH.",
      ".HHFFFSSFFFFSFFHH.",
      "..HHFFFFFFFFFFHH..",
      "..HHHFFFWWFFHHH...",
      "...HHHMMMMMMHH....",
      "...HHMMMMMMMMHH...",
      "..HHMMMMMMMMMMHH..",
      "..HHMMMMMMMMMMHH..",
      "...HH........HH...",
      "..................",
  };
  static const char* const kMicIcon[] = {
      "...SS....",
      "..SYYS...",
      "..SYYS...",
      "..SYYS...",
      "...SS....",
      "...SS....",
      "..SYYSS..",
      "...SS....",
  };
  static const char* const kBallA[] = {
      "..WWWW..",
      ".WRRRRW.",
      "WrrrrrrW",
      "WBBBBBBW",
      ".WBBBBW.",
      "..WWWW..",
  };
  static const char* const kBallB[] = {
      "..WWWW..",
      ".WRRRRW.",
      "WrrWWrrW",
      "WBBWWBBW",
      ".WBBBBW.",
      "..WWWW..",
  };
  static const char* const kWifiA[] = {
      "....W.......",
      "..WW.WW.....",
      ".W.....W....",
      "....W.......",
      "..WW.WW.....",
      "....W.......",
      ".....W......",
  };
  static const char* const kWifiB[] = {
      "............",
      "..WW.WW.....",
      ".W.....W....",
      "....W.......",
      "..WW.WW.....",
      "....W.......",
      ".....W......",
  };

  auto& d = M5.Display;
  const NormalScene scene = currentNormalScene();
  const int frame = (millis() / kAnimFrameMs) & 7;
  d.startWrite();
  d.fillScreen(TFT_BLACK);

  const bool blink = (frame == 3 || frame == 4);
  const char* const* hero = blink ? kHeroIdleB : kHeroIdleA;
  if (scene == NormalScene::Reply) {
    hero = kHeroTalk;
  }
  int hero_x = 18;
  int hero_y = 16;
  int hero_scale = 5;
  if (scene == NormalScene::Recording) {
    hero_y = 14 + ((frame & 1) ? 2 : 0);
  } else if (scene == NormalScene::Waiting) {
    hero_y = 16 + ((frame & 1) ? 1 : 0);
  } else if (scene == NormalScene::WifiConnecting) {
    hero_y = 17;
  }
  drawPixelSprite(d, hero_x, hero_y, hero_scale, hero, 16);

  if (scene == NormalScene::WifiConnecting) {
    drawPixelSprite(d, 170, 28, 4, (frame & 1) ? kWifiA : kWifiB, 7);
  } else if (scene == NormalScene::WifiOffline) {
    drawPixelSprite(d, 168, 28, 4, kWifiA, 7);
    d.drawLine(164, 26, 214, 74, TFT_RED);
    d.drawLine(214, 26, 164, 74, TFT_RED);
  } else if (scene == NormalScene::Recording) {
    drawPixelSprite(d, 170, 38 + ((frame & 1) ? 0 : 2), 3, kMicIcon, 8);
    d.fillCircle(192, 30 + ((frame & 1) ? 0 : 2), 3, TFT_RED);
  } else if (scene == NormalScene::Recognizing) {
    d.fillCircle(54, 44, 10, 0xFFFF);
    d.fillCircle(76, 36, 8, 0xFFFF);
    d.fillCircle(92, 50, 6, 0xFFFF);
    d.fillCircle(64 + (frame % 3) * 18, 42, 2, TFT_BLACK);
    d.fillCircle(64 + ((frame + 1) % 3) * 18, 42, 2, TFT_BLACK);
    d.fillCircle(64 + ((frame + 2) % 3) * 18, 42, 2, TFT_BLACK);
  } else if (scene == NormalScene::Sending) {
    drawPixelSprite(d, 28 + frame * 6, 56 - (frame > 3 ? 4 : 0), 3, (frame & 1) ? kBallA : kBallB, 6);
  } else if (scene == NormalScene::Waiting) {
    d.fillCircle(180, 28, 10, 0xFFFF);
    d.drawCircle(180, 28, 10, TFT_BLACK);
    d.fillRect(177, 20, 6, 12, TFT_BLACK);
    d.fillRect(177, 34, 6, 6, TFT_BLACK);
  } else if (scene == NormalScene::Reply) {
    d.fillRoundRect(kReplyBubbleX, kReplyBubbleY, kReplyBubbleW, kReplyBubbleH, 8, TFT_WHITE);
    d.drawRoundRect(kReplyBubbleX, kReplyBubbleY, kReplyBubbleW, kReplyBubbleH, 8, TFT_BLACK);
    d.fillTriangle(kReplyBubbleX, 78, kReplyBubbleX - 16, 88, kReplyBubbleX, 94, TFT_WHITE);
    d.drawLine(kReplyBubbleX, 78, kReplyBubbleX - 16, 88, TFT_BLACK);
    d.drawLine(kReplyBubbleX - 16, 88, kReplyBubbleX, 94, TFT_BLACK);
    d.setFont(&fonts::efontCN_14);
    d.setTextColor(TFT_BLACK, TFT_WHITE);
    const size_t start = min(g_scroll_line, g_current_message_lines.size());
    const size_t end = min(start + static_cast<size_t>(kReplyVisibleLines), g_current_message_lines.size());
    int y = kReplyBubbleY + 8;
    for (size_t i = start; i < end; ++i) {
      d.setCursor(kReplyBubbleX + 6, y);
      d.print(g_current_message_lines[i]);
      y += kReplyLineHeight;
    }
    d.setFont(&fonts::Font0);
    d.setTextColor(TFT_DARKGREY, TFT_WHITE);
    d.setCursor(kReplyBubbleX + 8, kReplyBubbleY + kReplyBubbleH - 10);
    d.printf("%u/%u",
             static_cast<unsigned>(min(g_scroll_line + 1, g_current_message_lines.size())),
             static_cast<unsigned>(g_current_message_lines.size()));
  } else if (scene == NormalScene::Failed) {
    d.fillRoundRect(24, 18, 52, 52, 8, TFT_WHITE);
    d.drawRoundRect(24, 18, 52, 52, 8, TFT_BLACK);
    d.drawLine(36, 30, 62, 56, TFT_RED);
    d.drawLine(62, 30, 36, 56, TFT_RED);
  } else {
    d.fillCircle(186, 42, 8, TFT_YELLOW);
    d.drawCircle(186, 42, 8, TFT_BLACK);
    d.fillCircle(184, 40, 1, TFT_BLACK);
  }
  d.endWrite();
}

const char* currentTestModeLabel() {
  switch (g_test_mode) {
    case TestMode::Normal:
      return "normal";
    case TestMode::Mic:
      return "mic";
    case TestMode::Asr:
      return "asr";
    case TestMode::AsrLive:
      return "asr live";
    case TestMode::AsrFlash:
      return "asr flash";
    case TestMode::ZeroPost:
      return "zero post";
    case TestMode::ZeroPoll:
      return "zero poll";
    case TestMode::Count:
      break;
  }
  return "?";
}

void nextTestMode() {
  const uint8_t next = (static_cast<uint8_t>(g_test_mode) + 1) %
                       static_cast<uint8_t>(TestMode::Count);
  g_test_mode = static_cast<TestMode>(next);
}

bool usesAsyncAsrCaptureMode() {
  return g_test_mode == TestMode::AsrLive || g_test_mode == TestMode::AsrFlash;
}

bool usesAsyncLiveAsrMode() {
  return g_test_mode == TestMode::AsrLive;
}

const char* asrCaptureStatusLabel(bool flash_replay) {
  return flash_replay ? "asr flash" : "asr live";
}

zero_buddy::AsrCaptureStrategy asrCaptureStrategy(bool flash_replay) {
  return flash_replay ? zero_buddy::AsrCaptureStrategy::FlashReplay
                      : zero_buddy::AsrCaptureStrategy::LiveBacklog;
}

void analyzePcmBuffer(const uint8_t* pcm_bytes,
                      size_t pcm_len,
                      uint32_t* peak_out,
                      uint32_t* avg_abs_out,
                      uint32_t* clipped_out) {
  int32_t peak = 0;
  uint64_t abs_sum = 0;
  size_t sample_count = 0;
  uint32_t clipped = 0;
  for (size_t i = 0; i + 1 < pcm_len; i += 2) {
    const int16_t sample =
        static_cast<int16_t>(pcm_bytes[i] | (static_cast<uint16_t>(pcm_bytes[i + 1]) << 8));
    const int32_t mag = sample < 0 ? -static_cast<int32_t>(sample) : static_cast<int32_t>(sample);
    if (mag >= 32760) {
      ++clipped;
    }
    if (mag > peak) {
      peak = mag;
    }
    abs_sum += mag;
    ++sample_count;
  }
  *peak_out = static_cast<uint32_t>(peak);
  *avg_abs_out = sample_count > 0 ? static_cast<uint32_t>(abs_sum / sample_count) : 0;
  *clipped_out = clipped;
}

void accumulateRawMicStats(const uint8_t* pcm_bytes, size_t pcm_len) {
  for (size_t i = 0; i + 1 < pcm_len; i += 2) {
    const int16_t sample =
        static_cast<int16_t>(pcm_bytes[i] | (static_cast<uint16_t>(pcm_bytes[i + 1]) << 8));
    const uint32_t mag =
        sample < 0 ? static_cast<uint32_t>(-static_cast<int32_t>(sample))
                   : static_cast<uint32_t>(sample);
    if (mag > g_raw_peak) {
      g_raw_peak = mag;
    }
    if (mag >= 32760) {
      ++g_raw_clipped_samples;
    }
    g_raw_abs_sum += mag;
    ++g_raw_sample_count;
  }
}

uint32_t currentRawAvgAbs() {
  return g_raw_sample_count > 0 ? static_cast<uint32_t>(g_raw_abs_sum / g_raw_sample_count) : 0;
}

void releaseAudioResources() {
  waitForAsrConnectTask(kAsrRecordConnectWaitMs);
  closeRecordFlash();
  removeFlashFileIfExists(kAsrRecordPcmPath);
  g_record_to_flash = false;
  closeStreamAsrSession();
  resetAsrConnectStateIfDone();
  clearRecordBacklog();
  g_recorded_bytes = 0;
  g_record_sent_bytes = 0;
  g_stream_chunk_fill = 0;
  g_asr_chunks_sent = 0;
  g_last_asr_chunk_sent_ms = 0;
  g_first_audio_ms = 0;
  g_last_audio_ms = 0;
  g_record_flash_bytes = 0;
  g_record_flash_failed = false;
  while (M5.Mic.isRecording()) {
    M5.delay(1);
  }
  M5.Mic.end();
}

bool writeRecordFlash(const uint8_t* data, size_t data_len) {
  if (!g_record_to_flash || data == nullptr || data_len == 0 || g_record_flash_failed) {
    return !g_record_flash_failed;
  }
  if (!g_flash_store_ready) {
    g_record_flash_failed = true;
    return false;
  }
  if (!g_record_flash_file) {
    g_record_flash_file = LittleFS.open(kAsrRecordPcmPath, "w");
    if (!g_record_flash_file) {
      g_record_flash_failed = true;
      return false;
    }
  }
  const size_t written = g_record_flash_file.write(data, data_len);
  if (written != data_len) {
    g_record_flash_failed = true;
    return false;
  }
  g_record_flash_bytes += written;
  return true;
}

void closeRecordFlash() {
  if (g_record_flash_file) {
    g_record_flash_file.flush();
    g_record_flash_file.close();
  }
}


void clearRecordBacklog() {
  g_record_backlog.clear();
}

void removeFlashFileIfExists(const char* path) {
  if (!g_flash_store_ready || path == nullptr || path[0] == '\0') {
    return;
  }
  if (LittleFS.exists(path)) {
    LittleFS.remove(path);
  }
}

uint8_t currentScreenBrightnessValue() {
  if (g_screen_brightness_level >= kScreenBrightnessLevelCount) {
    g_screen_brightness_level = kDefaultScreenBrightnessLevel;
  }
  return kScreenBrightnessLevels[g_screen_brightness_level];
}

String screenBrightnessLabel() {
  return String(static_cast<unsigned>(g_screen_brightness_level) + 1) + "/" +
         String(static_cast<unsigned>(kScreenBrightnessLevelCount));
}

void applyScreenBrightness() {
  M5.Display.setBrightness(currentScreenBrightnessValue());
}

void cycleScreenBrightness() {
  g_screen_brightness_level = zero_buddy::nextBrightnessLevel(g_screen_brightness_level,
                                                              kScreenBrightnessLevelCount);
  applyScreenBrightness();
  g_status = "backlight";
  g_result = "level " + screenBrightnessLabel();
  ZB_LOG_PRINTF("Backlight level %s value=%u\n",
                screenBrightnessLabel().c_str(),
                static_cast<unsigned>(currentScreenBrightnessValue()));
}

void wakeScreen(bool user_action, const char* reason) {
  const uint32_t now = millis();
  const bool was_awake = g_power.screen_awake;
  zero_buddy::wakePowerWindow(&g_power, now, kAwakeWindowMs);
  if (g_waiting_for_assistant) {
    zero_buddy::startAssistantPollWindow(&g_power, now, kAssistantPollWindowMs);
  }
  applyScreenBrightness();
  if (!was_awake) {
    ZB_LOG_PRINTF("Power wake: %s\n", reason != nullptr ? reason : "unknown");
  }
  if (user_action) {
    clearAssistantLedBlinkForUserAction();
  }
  drawUiFrame();
}

void enterScreenSleep(const char* reason) {
  if (!g_power.screen_awake || g_recording || g_uploading) {
    return;
  }
  ZB_LOG_PRINTF("Power sleep: %s\n", reason != nullptr ? reason : "idle");
  zero_buddy::sleepPowerWindow(&g_power);
  ledcWriteTone(0, 0);
  g_buzzer_stop_ms = 0;
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setBrightness(0);
}

void beginAssistantPollingWindow() {
  const uint32_t now = millis();
  g_waiting_for_assistant = true;
  g_last_zero_poll_ms = now;
  zero_buddy::startAssistantPollWindow(&g_power, now, kAssistantPollWindowMs);
}

void stopAssistantPollingWindow() {
  g_waiting_for_assistant = false;
  zero_buddy::stopAssistantPollWindow(&g_power);
}

bool assistantPollingWindowActive() {
  return zero_buddy::assistantPollWindowActive(g_power, millis());
}

void runLightSleepIfIdle() {
  if (g_power.screen_awake || g_recording || g_uploading) {
    return;
  }

  uint64_t sleep_us = 0;
  if (g_waiting_for_assistant && assistantPollingWindowActive() &&
      currentPendingAssistantMessage().isEmpty()) {
    const uint32_t now = millis();
    const uint32_t next_poll_ms = g_last_zero_poll_ms + kZeroPollIntervalMs;
    uint32_t wake_at_ms = next_poll_ms;
    if (static_cast<int32_t>(g_power.assistant_poll_until_ms - wake_at_ms) < 0) {
      wake_at_ms = g_power.assistant_poll_until_ms;
    }
    if (static_cast<int32_t>(wake_at_ms - now) <= 0) {
      return;
    }
    sleep_us = static_cast<uint64_t>(wake_at_ms - now) * 1000ULL;
  }

  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  gpio_wakeup_enable(kBtnAWakePin, GPIO_INTR_LOW_LEVEL);
  gpio_wakeup_enable(kBtnBWakePin, GPIO_INTR_LOW_LEVEL);
  esp_sleep_enable_gpio_wakeup();
  if (sleep_us > 0) {
    esp_sleep_enable_timer_wakeup(sleep_us);
  }
  const esp_err_t sleep_err = esp_light_sleep_start();
  gpio_wakeup_disable(kBtnAWakePin);
  gpio_wakeup_disable(kBtnBWakePin);
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);

  if (sleep_err != ESP_OK) {
    ZB_LOG_PRINTF("Power light sleep failed: %d\n", static_cast<int>(sleep_err));
    return;
  }
  if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_GPIO) {
    g_suppress_wake_short_action = true;
    wakeScreen(true, "button");
  }
}

String trimToWidth(const String& text, size_t max_len) {
  if (text.length() <= max_len) {
    return text;
  }
  if (max_len <= 3) {
    return text.substring(0, max_len);
  }
  return text.substring(0, max_len - 3) + "...";
}

size_t decodeUtf8Codepoint(const String& text, size_t index, uint32_t* codepoint_out) {
  const uint8_t b0 = static_cast<uint8_t>(text[index]);
  if (b0 < 0x80) {
    *codepoint_out = b0;
    return 1;
  }
  if ((b0 & 0xE0) == 0xC0 && index + 1 < text.length()) {
    *codepoint_out = ((b0 & 0x1F) << 6) |
                     (static_cast<uint8_t>(text[index + 1]) & 0x3F);
    return 2;
  }
  if ((b0 & 0xF0) == 0xE0 && index + 2 < text.length()) {
    *codepoint_out = ((b0 & 0x0F) << 12) |
                     ((static_cast<uint8_t>(text[index + 1]) & 0x3F) << 6) |
                     (static_cast<uint8_t>(text[index + 2]) & 0x3F);
    return 3;
  }
  if ((b0 & 0xF8) == 0xF0 && index + 3 < text.length()) {
    *codepoint_out = ((b0 & 0x07) << 18) |
                     ((static_cast<uint8_t>(text[index + 1]) & 0x3F) << 12) |
                     ((static_cast<uint8_t>(text[index + 2]) & 0x3F) << 6) |
                     (static_cast<uint8_t>(text[index + 3]) & 0x3F);
    return 4;
  }
  *codepoint_out = '?';
  return 1;
}

bool isSupportedDisplayCodepoint(uint32_t cp) {
  if (cp == '\n') {
    return true;
  }
  if (cp >= 0x20 && cp <= 0x7E) {
    return true;
  }
  if (cp >= 0x3000 && cp <= 0x303F) {
    return true;
  }
  if (cp >= 0x3400 && cp <= 0x4DBF) {
    return true;
  }
  if (cp >= 0x4E00 && cp <= 0x9FFF) {
    return true;
  }
  if (cp >= 0xFF00 && cp <= 0xFFEF) {
    return true;
  }
  return false;
}

String collapseWhitespace(const String& text) {
  String out;
  out.reserve(text.length());
  bool last_was_space = false;
  for (size_t i = 0; i < text.length(); ++i) {
    const char ch = text[i];
    if (ch == ' ' || ch == '\t') {
      if (!last_was_space) {
        out += ' ';
        last_was_space = true;
      }
      continue;
    }
    out += ch;
    last_was_space = false;
  }
  return out;
}

String removeMarkdownMarkers(String text) {
  text.replace("\r\n", "\n");
  text.replace("\r", "\n");
  text.replace("```", "\n[code]\n");
  text.replace("**", "");
  text.replace("__", "");
  text.replace("## ", "");
  text.replace("# ", "");
  text.replace("### ", "");
  text.replace("#### ", "");
  text.replace("`", "");
  text.replace("- [ ] ", "- ");
  text.replace("- [x] ", "- ");
  text.replace("* ", "- ");
  text.replace("• ", "- ");
  while (text.indexOf("\n\n\n") >= 0) {
    text.replace("\n\n\n", "\n\n");
  }
  return text;
}

String stripMarkdownLinks(const String& text) {
  String out;
  out.reserve(text.length());
  for (size_t i = 0; i < text.length(); ++i) {
    if (text[i] == '[') {
      const int mid = text.indexOf("](", i);
      const int end = text.indexOf(')', i);
      if (mid > static_cast<int>(i) && end > mid) {
        out += text.substring(i + 1, mid);
        i = end;
        continue;
      }
    }
    out += text[i];
  }
  return out;
}

String preprocessAssistantForDisplay(String text) {
  text = stripMarkdownLinks(text);
  text = removeMarkdownMarkers(text);

  String out;
  out.reserve(text.length());
  bool last_was_space = false;
  bool last_was_newline = false;
  for (size_t i = 0; i < text.length();) {
    uint32_t cp = 0;
    const size_t n = decodeUtf8Codepoint(text, i, &cp);
    if (cp == '\r') {
      i += n;
      continue;
    }
    if (cp == '\n') {
      if (!last_was_newline) {
        out += '\n';
        last_was_newline = true;
      }
      last_was_space = false;
      i += n;
      continue;
    }
    if (cp == '\t') {
      cp = ' ';
    }
    if (cp == ' ') {
      if (!last_was_space) {
        out += ' ';
        last_was_space = true;
      }
      last_was_newline = false;
      i += n;
      continue;
    }
    if (isSupportedDisplayCodepoint(cp)) {
      out += text.substring(i, i + n);
      last_was_space = false;
      last_was_newline = false;
    } else if (!last_was_space) {
      out += ' ';
      last_was_space = true;
      last_was_newline = false;
    }
    i += n;
  }

  out = collapseWhitespace(out);
  out.replace("\n ", "\n");
  out.replace(" \n", "\n");
  out.trim();
  if (out.isEmpty()) {
    return "[empty reply]";
  }
  return out;
}

bool chatFontHasGlyph(uint32_t cp) {
  if (cp == '\n' || cp == ' ') {
    return true;
  }
  if (cp > 0xFFFF) {
    return false;
  }
  lgfx::FontMetrics metrics;
  return fonts::efontCN_14.updateFontMetric(&metrics, static_cast<uint16_t>(cp));
}

String filterTextForChatFont(const String& text, size_t* missing_glyphs_out) {
  String out;
  out.reserve(text.length());
  bool last_was_space = false;
  bool last_was_newline = false;
  size_t missing = 0;

  for (size_t i = 0; i < text.length();) {
    uint32_t cp = 0;
    const size_t n = decodeUtf8Codepoint(text, i, &cp);
    if (cp == '\n') {
      if (!last_was_newline) {
        out += '\n';
      }
      last_was_newline = true;
      last_was_space = false;
      i += n;
      continue;
    }
    if (cp == '\t') {
      cp = ' ';
    }
    if (cp == ' ') {
      if (!last_was_space && !last_was_newline) {
        out += ' ';
      }
      last_was_space = true;
      i += n;
      continue;
    }
    if (chatFontHasGlyph(cp)) {
      out += text.substring(i, i + n);
      last_was_space = false;
      last_was_newline = false;
    } else {
      ++missing;
      if (!last_was_space && !last_was_newline) {
        out += ' ';
        last_was_space = true;
      }
    }
    i += n;
  }

  out = collapseWhitespace(out);
  out.replace("\n ", "\n");
  out.replace(" \n", "\n");
  out.trim();
  if (missing_glyphs_out != nullptr) {
    *missing_glyphs_out = missing;
  }
  return out.isEmpty() ? "[empty reply]" : out;
}

bool isMostlyAscii(const String& text) {
  if (text.isEmpty()) {
    return true;
  }
  size_t ascii_count = 0;
  for (size_t i = 0; i < text.length(); ++i) {
    const unsigned char ch = static_cast<unsigned char>(text[i]);
    if (ch >= 32 && ch <= 126) {
      ++ascii_count;
    }
  }
  return ascii_count * 2 >= text.length();
}

String jsonEscape(const String& text) {
  String escaped;
  escaped.reserve(text.length() + 16);
  for (size_t i = 0; i < text.length(); ++i) {
    const char ch = text[i];
    if (ch == '"' || ch == '\\') {
      escaped += '\\';
      escaped += ch;
    } else if (ch == '\n') {
      escaped += "\\n";
    } else if (ch == '\r') {
      escaped += "\\r";
    } else {
      escaped += ch;
    }
  }
  return escaped;
}

String hexPreview(const String& text, size_t max_bytes) {
  static const char kHex[] = "0123456789ABCDEF";
  String out;
  const size_t limit = min(text.length(), max_bytes);
  out.reserve(limit * 3 + 4);
  for (size_t i = 0; i < limit; ++i) {
    const uint8_t ch = static_cast<uint8_t>(text[i]);
    out += kHex[(ch >> 4) & 0x0F];
    out += kHex[ch & 0x0F];
    if (i + 1 < limit) {
      out += ' ';
    }
  }
  if (text.length() > limit) {
    out += " ...";
  }
  return out;
}

String extractJsonString(const String& body, const char* key) {
  const String key_needle = String("\"") + key + "\"";
  const int key_pos = body.indexOf(key_needle);
  if (key_pos < 0) {
    return "";
  }
  int cursor = key_pos + key_needle.length();
  while (cursor < body.length() &&
         (body[cursor] == ' ' || body[cursor] == '\t' || body[cursor] == '\r' || body[cursor] == '\n')) {
    ++cursor;
  }
  if (cursor >= body.length() || body[cursor] != ':') {
    return "";
  }
  ++cursor;
  while (cursor < body.length() &&
         (body[cursor] == ' ' || body[cursor] == '\t' || body[cursor] == '\r' || body[cursor] == '\n')) {
    ++cursor;
  }
  if (cursor >= body.length() || body[cursor] != '"') {
    return "";
  }
  ++cursor;

  String value;
  bool escaped = false;
  for (int i = cursor; i < body.length(); ++i) {
    const char ch = body[i];
    if (escaped) {
      value += ch;
      escaped = false;
      continue;
    }
    if (ch == '\\') {
      escaped = true;
      continue;
    }
    if (ch == '"') {
      return value;
    }
    value += ch;
  }
  return "";
}

String extractNestedResultText(const String& body) {
  const int result_pos = body.indexOf("\"result\"");
  if (result_pos < 0) {
    return "";
  }
  const String sliced = body.substring(result_pos);
  return extractJsonString(sliced, "text");
}

String extractFirstUtteranceText(const String& body) {
  const int utter_pos = body.indexOf("\"utterances\"");
  if (utter_pos < 0) {
    return "";
  }
  const String sliced = body.substring(utter_pos);
  return extractJsonString(sliced, "text");
}

String extractFirstResultArrayText(const String& body) {
  const int result_pos = body.indexOf("\"result\"");
  if (result_pos < 0) {
    return "";
  }
  int bracket_pos = body.indexOf('[', result_pos);
  if (bracket_pos < 0) {
    return "";
  }
  const int first_obj_pos = body.indexOf('{', bracket_pos);
  if (first_obj_pos < 0) {
    return "";
  }
  const String sliced = body.substring(first_obj_pos);
  return extractJsonString(sliced, "text");
}

String currentPendingAssistantMessage() {
  if (g_pending_message_index >= g_pending_assistant_count) {
    return "";
  }
  return g_current_assistant_message;
}

String assistantMessagePath(size_t index) {
  return "/assistant_" + String(static_cast<unsigned>(index)) + ".txt";
}

void clearAssistantMessages() {
  if (g_flash_store_ready) {
    for (size_t i = 0; i < kMaxPendingAssistantMessages; ++i) {
      const String path = assistantMessagePath(i);
      removeFlashFileIfExists(path.c_str());
    }
  }
  g_pending_assistant_count = 0;
  g_pending_message_index = 0;
  g_current_assistant_message = "";
  g_current_message_lines.clear();
  resetMessageScroll();
}

bool saveAssistantMessageToFlash(size_t index, const String& message, String* error_out) {
  if (!g_flash_store_ready) {
    *error_out = "flash unavailable";
    return false;
  }
  if (index >= kMaxPendingAssistantMessages) {
    *error_out = "assistant queue full";
    return false;
  }
  const String path = assistantMessagePath(index);
  File file = LittleFS.open(path, "w");
  if (!file) {
    *error_out = "assistant file open failed";
    return false;
  }
  const size_t limit = min(static_cast<size_t>(message.length()), kMaxAssistantMessageBytes);
  const size_t written = file.write(reinterpret_cast<const uint8_t*>(message.c_str()), limit);
  file.close();
  if (written != limit) {
    *error_out = "assistant file write failed";
    return false;
  }
  return true;
}

bool loadCurrentAssistantMessage(String* error_out) {
  g_current_assistant_message = "";
  g_current_message_lines.clear();
  if (g_pending_message_index >= g_pending_assistant_count) {
    return true;
  }
  if (!g_flash_store_ready) {
    *error_out = "flash unavailable";
    return false;
  }
  const String path = assistantMessagePath(g_pending_message_index);
  File file = LittleFS.open(path, FILE_READ);
  if (!file) {
    *error_out = "assistant file read failed";
    return false;
  }
  g_current_assistant_message.reserve(min(static_cast<size_t>(file.size()), kMaxAssistantMessageBytes));
  while (file.available() && g_current_assistant_message.length() < kMaxAssistantMessageBytes) {
    g_current_assistant_message += static_cast<char>(file.read());
  }
  file.close();
  resetMessageScroll();
  rebuildCurrentMessageLayout();
  return true;
}

void logHeap(const char* tag) {
  ZB_LOG_PRINTF("HEAP %s free=%u min=%u cap_free=%u cap_min=%u largest=%u\n",
                tag,
                static_cast<unsigned>(ESP.getFreeHeap()),
                static_cast<unsigned>(ESP.getMinFreeHeap()),
                static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_8BIT)),
                static_cast<unsigned>(heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT)),
                static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)));
}

const char* networkTaskLabel(NetworkTask task) {
  switch (task) {
    case NetworkTask::Asr:
      return "asr";
    case NetworkTask::Zero:
      return "zero";
    case NetworkTask::None:
    default:
      return "none";
  }
}

bool acquireNetworkTask(NetworkTask task, String* error_out) {
  if (g_network_task != NetworkTask::None && g_network_task != task) {
    *error_out = String("network busy: ") + networkTaskLabel(g_network_task);
    ZB_LOG_PRINTF("NETWORK acquire %s failed, owner=%s\n",
                  networkTaskLabel(task),
                  networkTaskLabel(g_network_task));
    return false;
  }
  g_network_task = task;
  ZB_LOG_PRINTF("NETWORK acquire %s\n", networkTaskLabel(task));
  return true;
}

void releaseNetworkTask(NetworkTask task) {
  if (g_network_task == task) {
    ZB_LOG_PRINTF("NETWORK release %s\n", networkTaskLabel(task));
    g_network_task = NetworkTask::None;
  }
}

void setStatusLed(bool on) {
  digitalWrite(kLedPin, on ? LOW : HIGH);
}

void applyAssistantLedBlinkEvent(zero_buddy::NotificationBlinkEvent event) {
  const auto result = zero_buddy::updateNotificationBlink(&g_assistant_led_blink,
                                                          event,
                                                          static_cast<uint32_t>(millis()),
                                                          kAssistantLedBlinkIntervalMs,
                                                          kAssistantLedBlinkPulseMs);
  if (result.led_changed) {
    setStatusLed(result.led_on);
  }
}

void serviceAssistantLedBlink() {
  applyAssistantLedBlinkEvent(zero_buddy::NotificationBlinkEvent::Tick);
}

void notifyAssistantMessageArrived() {
  applyAssistantLedBlinkEvent(zero_buddy::NotificationBlinkEvent::AssistantArrived);
}

void clearAssistantLedBlinkForUserAction() {
  applyAssistantLedBlinkEvent(zero_buddy::NotificationBlinkEvent::UserAction);
}

void resetMessageScroll() {
  g_scroll_line = 0;
  g_last_scroll_ms = millis();
}

void rebuildCurrentMessageLayout() {
  g_current_message_lines.clear();
  M5.Display.setFont(&fonts::efontCN_14);
  const String pending = currentPendingAssistantMessage();
  if (pending.isEmpty()) {
    return;
  }

  String paragraph;
  auto flushParagraph = [&]() {
    paragraph.trim();
    if (paragraph.isEmpty()) {
      if (!g_current_message_lines.empty() && !g_current_message_lines.back().isEmpty()) {
        g_current_message_lines.push_back("");
      }
      paragraph = "";
      return;
    }

    int start = 0;
    while (start < paragraph.length()) {
      int best = start + 1;
      for (int end = start + 1; end <= paragraph.length(); ++end) {
        String candidate = paragraph.substring(start, end);
        if (M5.Display.textWidth(candidate) > (kBodyWidth - 16)) {
          break;
        }
        best = end;
      }
      if (best <= start) {
        best = start + 1;
      }
      String line = paragraph.substring(start, best);
      line.trim();
      if (!line.isEmpty()) {
        g_current_message_lines.push_back(line);
      }
      start = best;
      while (start < paragraph.length() && paragraph[start] == ' ') {
        ++start;
      }
    }
    paragraph = "";
  };

  for (size_t i = 0; i < pending.length(); ++i) {
    const char ch = pending[i];
    if (ch == '\n') {
      flushParagraph();
      continue;
    }
    paragraph += ch;
  }
  flushParagraph();

  if (g_current_message_lines.empty()) {
    g_current_message_lines.push_back("[empty]");
  }
}

bool canScrollCurrentAssistantMessage() {
  return g_current_message_lines.size() > static_cast<size_t>(kBodyVisibleLines) &&
         g_scroll_line + kBodyVisibleLines < g_current_message_lines.size();
}

void handleAssistantAdvance(bool force_next) {
  if (!currentPendingAssistantMessage().isEmpty() && !force_next && canScrollCurrentAssistantMessage()) {
    ++g_scroll_line;
    g_last_scroll_ms = millis();
    g_status = "assistant reply";
    g_result = "scroll";
    return;
  }
  advancePendingAssistantMessage();
  if (currentPendingAssistantMessage().isEmpty()) {
    g_status = "ready";
    g_result = "hold BtnA to record";
  } else {
    g_status = "assistant reply";
    g_result = "next";
  }
}

void advancePendingAssistantMessage() {
  if (g_pending_message_index < g_pending_assistant_count) {
    ++g_pending_message_index;
  }
  if (g_pending_message_index >= g_pending_assistant_count) {
    clearAssistantMessages();
    stopAssistantPollingWindow();
    g_status = "ready";
    g_result = "hold BtnA to record";
  } else {
    String load_error;
    if (!loadCurrentAssistantMessage(&load_error)) {
      ZB_LOG_PRINTF("assistant load failed: %s\n", load_error.c_str());
    }
    g_status = "assistant reply";
    g_result = "BtnA next";
  }
}

struct TlsContext {
  mbedtls_ssl_context ssl;
  mbedtls_ssl_config conf;
  mbedtls_ctr_drbg_context drbg;
  mbedtls_entropy_context entropy;
  int fd = -1;
};

struct StreamAsrSession {
  int proxy_fd = -1;
  std::unique_ptr<TlsContext> proxy_tls;
  std::unique_ptr<TlsContext> target_tls;
  String best_text;
  uint32_t frames_read = 0;
  bool saw_final = false;
  bool active = false;
};

StreamAsrSession g_asr_session;

void initTlsContext(TlsContext* ctx) {
  mbedtls_ssl_init(&ctx->ssl);
  mbedtls_ssl_config_init(&ctx->conf);
  mbedtls_ctr_drbg_init(&ctx->drbg);
  mbedtls_entropy_init(&ctx->entropy);
  ctx->fd = -1;
}

void freeTlsContext(TlsContext* ctx) {
  mbedtls_ssl_free(&ctx->ssl);
  mbedtls_ssl_config_free(&ctx->conf);
  mbedtls_ctr_drbg_free(&ctx->drbg);
  mbedtls_entropy_free(&ctx->entropy);
}

String tlsErrorString(int code) {
  char buf[128];
  mbedtls_strerror(code, buf, sizeof(buf));
  return String(buf);
}

bool isTimeoutTlsError(int code) {
  if (code == MBEDTLS_ERR_SSL_TIMEOUT) {
    return true;
  }
  const String msg = tlsErrorString(code);
  return msg.indexOf("timed out") >= 0 || msg.indexOf("timeout") >= 0;
}

bool isTimeoutErrorString(const String& msg) {
  return msg.indexOf("timed out") >= 0 || msg.indexOf("timeout") >= 0;
}

bool isConnResetErrorString(const String& msg) {
  return msg.indexOf("0050") >= 0 || msg.indexOf("conn reset") >= 0 ||
         msg.indexOf("connection reset") >= 0;
}

String normalizeTlsError(const String& msg) {
  if (isConnResetErrorString(msg)) {
    return "conn reset";
  }
  return msg;
}

bool seedTlsContext(TlsContext* ctx, const char* personal, String* error_out) {
  logHeap("tls seed before");
  const int ret = mbedtls_ctr_drbg_seed(&ctx->drbg,
                                        mbedtls_entropy_func,
                                        &ctx->entropy,
                                        reinterpret_cast<const unsigned char*>(personal),
                                        strlen(personal));
  if (ret != 0) {
    *error_out = "drbg " + tlsErrorString(ret);
    logHeap("tls seed failed");
    return false;
  }
  logHeap("tls seed ok");
  return true;
}

bool configureClientTls(TlsContext* ctx, bool insecure, String* error_out) {
  logHeap("tls config before");
  int ret = mbedtls_ssl_config_defaults(&ctx->conf,
                                        MBEDTLS_SSL_IS_CLIENT,
                                        MBEDTLS_SSL_TRANSPORT_STREAM,
                                        MBEDTLS_SSL_PRESET_DEFAULT);
  if (ret != 0) {
    *error_out = "conf " + tlsErrorString(ret);
    logHeap("tls config failed");
    return false;
  }
  mbedtls_ssl_conf_rng(&ctx->conf, mbedtls_ctr_drbg_random, &ctx->drbg);
  mbedtls_ssl_conf_authmode(&ctx->conf,
                            insecure ? MBEDTLS_SSL_VERIFY_NONE : MBEDTLS_SSL_VERIFY_REQUIRED);
  logHeap("tls setup before");
  ret = mbedtls_ssl_setup(&ctx->ssl, &ctx->conf);
  if (ret != 0) {
    *error_out = "setup " + tlsErrorString(ret);
    logHeap("tls setup failed");
    return false;
  }
  logHeap("tls setup ok");
  return true;
}

bool connectSocket(const char* host, uint16_t port, int* out_fd, String* error_out) {
  ZB_LOG_PRINTF("SOCKET connect begin: %s:%u\n", host, static_cast<unsigned>(port));
  logHeap("socket connect begin");
  IPAddress ip;
  if (!WiFi.hostByName(host, ip)) {
    *error_out = "dns failed";
    logHeap("socket dns failed");
    return false;
  }

  const int fd = lwip_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (fd < 0) {
    *error_out = "socket failed";
    logHeap("socket alloc failed");
    return false;
  }

  struct timeval tv;
  tv.tv_sec = 5;
  tv.tv_usec = 0;
  lwip_setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  lwip_setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = ip;
  addr.sin_port = htons(port);

  if (lwip_connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) != 0) {
    *error_out = "connect failed";
    lwip_close(fd);
    logHeap("socket connect failed");
    return false;
  }

  *out_fd = fd;
  logHeap("socket connect ok");
  return true;
}

void setSocketTimeoutMs(int fd, int recv_ms, int send_ms) {
  struct timeval rcv_tv;
  rcv_tv.tv_sec = recv_ms / 1000;
  rcv_tv.tv_usec = (recv_ms % 1000) * 1000;
  lwip_setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &rcv_tv, sizeof(rcv_tv));

  struct timeval snd_tv;
  snd_tv.tv_sec = send_ms / 1000;
  snd_tv.tv_usec = (send_ms % 1000) * 1000;
  lwip_setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &snd_tv, sizeof(snd_tv));
}

int outerTlsSend(void* ctx, const unsigned char* buf, size_t len) {
  auto* outer = static_cast<mbedtls_ssl_context*>(ctx);
  return mbedtls_ssl_write(outer, buf, len);
}

int outerTlsRecv(void* ctx, unsigned char* buf, size_t len) {
  auto* outer = static_cast<mbedtls_ssl_context*>(ctx);
  return mbedtls_ssl_read(outer, buf, len);
}

bool handshakeTlsOnSocket(TlsContext* ctx,
                          int fd,
                          const char* hostname,
                          bool insecure,
                          String* error_out) {
  ZB_LOG_PRINTF("TLS socket handshake begin: %s\n", hostname);
  logHeap("tls socket hs begin");
  if (!seedTlsContext(ctx, hostname, error_out)) {
    return false;
  }
  if (!configureClientTls(ctx, insecure, error_out)) {
    return false;
  }
  int ret = mbedtls_ssl_set_hostname(&ctx->ssl, hostname);
  if (ret != 0) {
    *error_out = "sni " + tlsErrorString(ret);
    return false;
  }
  ctx->fd = fd;
  mbedtls_ssl_set_bio(&ctx->ssl, &ctx->fd, mbedtls_net_send, mbedtls_net_recv, nullptr);
  while ((ret = mbedtls_ssl_handshake(&ctx->ssl)) != 0) {
    if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
      *error_out = "hs " + tlsErrorString(ret);
      logHeap("tls socket hs failed");
      return false;
    }
    delay(2);
  }
  logHeap("tls socket hs ok");
  return true;
}

bool handshakeTlsOverTls(TlsContext* inner,
                         TlsContext* outer,
                         const char* hostname,
                         String* error_out) {
  ZB_LOG_PRINTF("TLS nested handshake begin: %s\n", hostname);
  logHeap("tls nested hs begin");
  if (!seedTlsContext(inner, hostname, error_out)) {
    return false;
  }
  if (!configureClientTls(inner, true, error_out)) {
    return false;
  }
  int ret = mbedtls_ssl_set_hostname(&inner->ssl, hostname);
  if (ret != 0) {
    *error_out = "inner sni " + tlsErrorString(ret);
    return false;
  }
  mbedtls_ssl_set_bio(&inner->ssl, &outer->ssl, outerTlsSend, outerTlsRecv, nullptr);
  while ((ret = mbedtls_ssl_handshake(&inner->ssl)) != 0) {
    if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
      *error_out = "inner hs " + tlsErrorString(ret);
      logHeap("tls nested hs failed");
      return false;
    }
    delay(2);
  }
  logHeap("tls nested hs ok");
  return true;
}

bool handshakeTlsOverTlsWithDeadline(TlsContext* inner,
                                     TlsContext* outer,
                                     const char* hostname,
                                     unsigned long deadline_ms,
                                     String* error_out) {
  ZB_LOG_PRINTF("TLS nested handshake begin: %s\n", hostname);
  logHeap("tls nested hs begin");
  if (!seedTlsContext(inner, hostname, error_out)) {
    return false;
  }
  if (!configureClientTls(inner, true, error_out)) {
    return false;
  }
  int ret = mbedtls_ssl_set_hostname(&inner->ssl, hostname);
  if (ret != 0) {
    *error_out = "inner sni " + tlsErrorString(ret);
    return false;
  }
  mbedtls_ssl_set_bio(&inner->ssl, &outer->ssl, outerTlsSend, outerTlsRecv, nullptr);
  while ((ret = mbedtls_ssl_handshake(&inner->ssl)) != 0) {
    if (static_cast<long>(millis() - deadline_ms) > 0) {
      *error_out = "inner hs timeout";
      logHeap("tls nested hs timeout");
      return false;
    }
    if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
      *error_out = "inner hs " + tlsErrorString(ret);
      logHeap("tls nested hs failed");
      return false;
    }
    delay(2);
  }
  logHeap("tls nested hs ok");
  return true;
}

bool tlsWriteAll(mbedtls_ssl_context* ssl,
                 const uint8_t* data,
                 size_t data_len,
                 String* error_out);

bool tlsWriteAll(mbedtls_ssl_context* ssl, const String& data, String* error_out) {
  return tlsWriteAll(ssl,
                     reinterpret_cast<const uint8_t*>(data.c_str()),
                     data.length(),
                     error_out);
}

bool tlsWriteAll(mbedtls_ssl_context* ssl,
                 const uint8_t* data,
                 size_t data_len,
                 String* error_out) {
  size_t offset = 0;
  while (offset < data_len) {
    const int ret =
        mbedtls_ssl_write(ssl, reinterpret_cast<const unsigned char*>(data) + offset, data_len - offset);
    if (ret > 0) {
      offset += ret;
      continue;
    }
    if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
      delay(2);
      continue;
    }
    *error_out = "write " + tlsErrorString(ret);
    return false;
  }
  return true;
}

bool tlsWriteParts(mbedtls_ssl_context* ssl,
                   const std::vector<String>& parts,
                   String* error_out) {
  for (size_t i = 0; i < parts.size(); ++i) {
    ZB_LOG_PRINTF("TLS write part %u len=%u\n",
                  static_cast<unsigned>(i),
                  static_cast<unsigned>(parts[i].length()));
    if (!tlsWriteAll(ssl, parts[i], error_out)) {
      return false;
    }
  }
  return true;
}

String tlsReadHeaders(mbedtls_ssl_context* ssl, String* error_out) {
  String out;
  uint8_t buf[256];
  while (out.indexOf("\r\n\r\n") < 0) {
    const int ret = mbedtls_ssl_read(ssl, buf, sizeof(buf));
    if (ret > 0) {
      out.concat(reinterpret_cast<const char*>(buf), ret);
      continue;
    }
    if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
      delay(2);
      continue;
    }
    if (ret == 0 || ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
      return out;
    }
    *error_out = "read " + tlsErrorString(ret);
    return out;
  }
  return out;
}

bool tlsReadExact(mbedtls_ssl_context* ssl, uint8_t* buf, size_t len, String* error_out) {
  size_t offset = 0;
  while (offset < len) {
    const int ret = mbedtls_ssl_read(ssl, buf + offset, len - offset);
    if (ret > 0) {
      offset += ret;
      continue;
    }
    if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
      delay(2);
      continue;
    }
    if (ret == 0 || ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
      *error_out = "peer closed";
      return false;
    }
    *error_out = "read " + tlsErrorString(ret);
    return false;
  }
  return true;
}

void appendU32(std::vector<uint8_t>* out, uint32_t value) {
  out->push_back((value >> 24) & 0xff);
  out->push_back((value >> 16) & 0xff);
  out->push_back((value >> 8) & 0xff);
  out->push_back(value & 0xff);
}

bool wsSendBinaryFrame(mbedtls_ssl_context* ssl,
                       const uint8_t* payload,
                       size_t payload_len,
                       String* error_out) {
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
  const uint8_t mask[4] = {static_cast<uint8_t>(esp_random() & 0xff),
                           static_cast<uint8_t>(esp_random() & 0xff),
                           static_cast<uint8_t>(esp_random() & 0xff),
                           static_cast<uint8_t>(esp_random() & 0xff)};
  frame.insert(frame.end(), mask, mask + 4);
  for (size_t i = 0; i < payload_len; ++i) {
    frame.push_back(payload[i] ^ mask[i % 4]);
  }
  return tlsWriteAll(ssl, frame.data(), frame.size(), error_out);
}

bool wsReadFrame(mbedtls_ssl_context* ssl,
                 uint8_t* opcode_out,
                 std::vector<uint8_t>* payload_out,
                 String* error_out) {
  uint8_t hdr[2];
  if (!tlsReadExact(ssl, hdr, sizeof(hdr), error_out)) {
    return false;
  }
  *opcode_out = hdr[0] & 0x0f;
  const bool masked = (hdr[1] & 0x80) != 0;
  uint64_t payload_len = hdr[1] & 0x7f;
  if (payload_len == 126) {
    uint8_t ext[2];
    if (!tlsReadExact(ssl, ext, sizeof(ext), error_out)) {
      return false;
    }
    payload_len = (static_cast<uint64_t>(ext[0]) << 8) | ext[1];
  } else if (payload_len == 127) {
    *error_out = "ws len too big";
    return false;
  }
  uint8_t mask[4] = {0};
  if (masked && !tlsReadExact(ssl, mask, sizeof(mask), error_out)) {
    return false;
  }
  payload_out->assign(payload_len, 0);
  if (payload_len > 0 &&
      !tlsReadExact(ssl, payload_out->data(), static_cast<size_t>(payload_len), error_out)) {
    return false;
  }
  if (masked) {
    for (size_t i = 0; i < payload_out->size(); ++i) {
      (*payload_out)[i] ^= mask[i % 4];
    }
  }
  return true;
}

bool parseAsrServerPayload(const std::vector<uint8_t>& frame_payload,
                           bool* final_out,
                           String* text_out,
                           String* error_out) {
  const auto parsed =
      zero_buddy::parseAsrServerPayload(frame_payload.data(), frame_payload.size());
  if (!parsed.ok) {
    *error_out = parsed.error.c_str();
    return false;
  }
  if (!parsed.text.empty()) {
    *text_out = parsed.text.c_str();
    ZB_LOG_PRINTF("ASR chosen text: %s\n", text_out->c_str());
  } else {
    ZB_LOG_PRINTLN("ASR chosen text: <empty>");
  }
  *final_out = parsed.final;
  ZB_LOG_PRINTF("ASR frame final=%d\n", *final_out ? 1 : 0);
  return true;
}

struct HttpBodySink {
  String* text = nullptr;
  File* file = nullptr;
  size_t bytes = 0;

  bool write(const uint8_t* data, size_t len) {
    if (len == 0) {
      return true;
    }
    if (file != nullptr) {
      const size_t written = file->write(data, len);
      if (written != len) {
        return false;
      }
    } else if (text != nullptr) {
      text->concat(reinterpret_cast<const char*>(data), len);
    }
    bytes += len;
    return true;
  }
};

bool tlsReadByte(mbedtls_ssl_context* ssl, uint8_t* byte_out, String* error_out) {
  while (true) {
    const int ret = mbedtls_ssl_read(ssl, byte_out, 1);
    if (ret == 1) {
      return true;
    }
    if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
      delay(2);
      continue;
    }
    if (ret == 0 || ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
      *error_out = "peer closed";
      return false;
    }
    *error_out = "read " + tlsErrorString(ret);
    return false;
  }
}

bool tlsReadLine(mbedtls_ssl_context* ssl, String* line_out, String* error_out) {
  line_out->remove(0);
  while (line_out->length() < 128) {
    uint8_t ch = 0;
    if (!tlsReadByte(ssl, &ch, error_out)) {
      return false;
    }
    if (ch == '\n') {
      if (line_out->endsWith("\r")) {
        line_out->remove(line_out->length() - 1);
      }
      return true;
    }
    *line_out += static_cast<char>(ch);
  }
  *error_out = "line too long";
  return false;
}

bool tlsReadToSink(mbedtls_ssl_context* ssl, size_t bytes_to_read, HttpBodySink* sink, String* error_out) {
  uint8_t buf[256];
  size_t remaining = bytes_to_read;
  while (remaining > 0) {
    const size_t want = min(remaining, sizeof(buf));
    if (!tlsReadExact(ssl, buf, want, error_out)) {
      return false;
    }
    if (!sink->write(buf, want)) {
      *error_out = "body write failed";
      return false;
    }
    remaining -= want;
  }
  return true;
}

int parseContentLength(const String& headers_lower) {
  const int pos = headers_lower.indexOf("content-length:");
  if (pos < 0) {
    return -1;
  }
  int cursor = pos + 15;
  while (cursor < headers_lower.length() && headers_lower[cursor] == ' ') {
    ++cursor;
  }
  const int end = headers_lower.indexOf("\r\n", cursor);
  const String value = end >= 0 ? headers_lower.substring(cursor, end) : headers_lower.substring(cursor);
  return value.toInt();
}

bool readHttpResponseToSink(mbedtls_ssl_context* ssl,
                            int* status_out,
                            String* body_out,
                            const char* body_file_path,
                            String* error_out) {
  if (body_out != nullptr) {
    body_out->remove(0);
  }

  String headers;
  while (headers.indexOf("\r\n\r\n") < 0 && headers.length() < 4096) {
    uint8_t ch = 0;
    if (!tlsReadByte(ssl, &ch, error_out)) {
      return false;
    }
    headers += static_cast<char>(ch);
  }
  if (headers.indexOf("\r\n\r\n") < 0) {
    *error_out = "headers too long";
    return false;
  }

  const int status_end = headers.indexOf("\r\n");
  const String status_line = status_end >= 0 ? headers.substring(0, status_end) : headers;
  const int first_space = status_line.indexOf(' ');
  if (first_space < 0) {
    *error_out = "bad status";
    return false;
  }
  const int second_space = status_line.indexOf(' ', first_space + 1);
  const String code_str = second_space > first_space
                              ? status_line.substring(first_space + 1, second_space)
                              : status_line.substring(first_space + 1);
  *status_out = code_str.toInt();

  String lower = headers;
  lower.toLowerCase();
  const bool chunked = lower.indexOf("transfer-encoding: chunked") >= 0;
  const int content_length = parseContentLength(lower);

  File body_file;
  if (body_file_path != nullptr) {
    if (!g_flash_store_ready) {
      *error_out = "flash unavailable";
      return false;
    }
    body_file = LittleFS.open(body_file_path, "w");
    if (!body_file) {
      *error_out = "body file open failed";
      return false;
    }
  }

  HttpBodySink sink;
  sink.text = body_file_path == nullptr ? body_out : nullptr;
  sink.file = body_file_path == nullptr ? nullptr : &body_file;

  if (chunked) {
    while (true) {
      String size_line;
      if (!tlsReadLine(ssl, &size_line, error_out)) {
        if (body_file) {
          body_file.close();
        }
        return false;
      }
      const int ext_pos = size_line.indexOf(';');
      if (ext_pos >= 0) {
        size_line = size_line.substring(0, ext_pos);
      }
      const size_t chunk_size = static_cast<size_t>(strtoul(size_line.c_str(), nullptr, 16));
      if (chunk_size == 0) {
        String trailer;
        do {
          if (!tlsReadLine(ssl, &trailer, error_out)) {
            if (body_file) {
              body_file.close();
            }
            return false;
          }
        } while (trailer.length() > 0);
        break;
      }
      if (!tlsReadToSink(ssl, chunk_size, &sink, error_out)) {
        if (body_file) {
          body_file.close();
        }
        return false;
      }
      uint8_t crlf[2];
      if (!tlsReadExact(ssl, crlf, sizeof(crlf), error_out)) {
        if (body_file) {
          body_file.close();
        }
        return false;
      }
    }
  } else if (content_length >= 0) {
    if (!tlsReadToSink(ssl, static_cast<size_t>(content_length), &sink, error_out)) {
      if (body_file) {
        body_file.close();
      }
      return false;
    }
  } else {
    uint8_t buf[256];
    while (true) {
      const int ret = mbedtls_ssl_read(ssl, buf, sizeof(buf));
      if (ret > 0) {
        if (!sink.write(buf, static_cast<size_t>(ret))) {
          *error_out = "body write failed";
          if (body_file) {
            body_file.close();
          }
          return false;
        }
        continue;
      }
      if (ret == 0 || ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY || isTimeoutTlsError(ret)) {
        break;
      }
      if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
        delay(2);
        continue;
      }
      *error_out = "read " + tlsErrorString(ret);
      if (body_file) {
        body_file.close();
      }
      return false;
    }
  }

  if (body_file) {
    body_file.close();
  }
  ZB_LOG_PRINTF("ZERO response body bytes: %u storage=%s\n",
                static_cast<unsigned>(sink.bytes),
                body_file_path == nullptr ? "ram" : "flash");
  return true;
}

void beepTone(int freq_hz, int duration_ms) {
  ledcAttachPin(kBuzzerPin, 0);
  ledcWriteTone(0, freq_hz);
  delay(duration_ms);
  ledcWriteTone(0, 0);
}

void beepToneAsync(int freq_hz, int duration_ms) {
  ledcAttachPin(kBuzzerPin, 0);
  ledcWriteTone(0, freq_hz);
  g_buzzer_stop_ms = millis() + duration_ms;
}

void beepListenStart() {
  // Keep this synchronous. If the mic/network path blocks after starting a PWM tone,
  // the async stop in loop() cannot run and the startup beep turns into a long tone.
  beepTone(1760, 35);
}

void beepSendStop() {
  beepTone(880, 90);
}

void beepAssistantReady() {
  beepTone(1568, 50);
  delay(35);
  beepTone(2093, 70);
}

bool parseZeroMessagesResponse(const String& body,
                               String* newest_message_id_out,
                               std::vector<String>* assistant_messages_out) {
  const auto parsed = zero_buddy::parseZeroMessagesResponse(body.c_str());
  *newest_message_id_out = parsed.newest_message_id.c_str();
  for (const auto& msg : parsed.assistant_messages) {
    assistant_messages_out->push_back(String(msg.c_str()));
  }
  return parsed.found_any;
}

bool parseZeroMessagesFileToFlash(bool baseline_only,
                                  String* newest_message_id_out,
                                  size_t* added_count_out,
                                  String* error_out) {
  *added_count_out = 0;
  newest_message_id_out->remove(0);
  if (!g_flash_store_ready) {
    *error_out = "flash unavailable";
    return false;
  }
  File file = LittleFS.open(kZeroResponsePath, FILE_READ);
  if (!file) {
    *error_out = "zero response file missing";
    return false;
  }

  bool saw_any = false;
  bool waiting_for_array = false;
  bool in_messages = false;
  bool in_object = false;
  bool in_string = false;
  bool escaped = false;
  int object_depth = 0;
  String recent;
  String object;

  auto handleObject = [&]() {
    const auto message = zero_buddy::parseZeroMessageObject(object.c_str());
    if (!message.ok) {
      return;
    }
    *newest_message_id_out = message.id.c_str();
    saw_any = true;
    if (baseline_only || message.role != "assistant" || message.content.empty()) {
      return;
    }
    if (g_pending_assistant_count >= kMaxPendingAssistantMessages) {
      return;
    }
    const String preprocessed_text =
        zero_buddy::preprocessAssistantForDisplay(message.content).c_str();
    size_t missing_glyphs = 0;
    const String display_text = filterTextForChatFont(preprocessed_text, &missing_glyphs);
    if (missing_glyphs > 0) {
      ZB_LOG_PRINTF("assistant display missing glyphs: %u\n",
                    static_cast<unsigned>(missing_glyphs));
    }
    String save_error;
    if (saveAssistantMessageToFlash(g_pending_assistant_count, display_text, &save_error)) {
      ++g_pending_assistant_count;
      ++(*added_count_out);
    } else {
      ZB_LOG_PRINTF("assistant save failed: %s\n", save_error.c_str());
    }
  };

  while (file.available()) {
    const char ch = static_cast<char>(file.read());
    if (!in_messages) {
      recent += ch;
      if (recent.length() > 16) {
        recent.remove(0, recent.length() - 16);
      }
      if (recent.endsWith("\"messages\"")) {
        waiting_for_array = true;
      } else if (waiting_for_array && ch == '[') {
        in_messages = true;
        waiting_for_array = false;
      }
      continue;
    }

    if (!in_object) {
      if (ch == '{') {
        in_object = true;
        in_string = false;
        escaped = false;
        object_depth = 1;
        object = "{";
      } else if (ch == ']') {
        break;
      }
      continue;
    }

    object += ch;
    if (escaped) {
      escaped = false;
      continue;
    }
    if (ch == '\\') {
      escaped = true;
      continue;
    }
    if (ch == '"') {
      in_string = !in_string;
      continue;
    }
    if (in_string) {
      continue;
    }
    if (ch == '{') {
      ++object_depth;
    } else if (ch == '}') {
      --object_depth;
      if (object_depth == 0) {
        handleObject();
        object = "";
        in_object = false;
      }
    }
  }
  file.close();
  return saw_any;
}

String generateRequestId() {
  char buf[37];
  snprintf(buf,
           sizeof(buf),
           "%08lx-%04x-%04x-%04x-%04x%08lx",
           random(0x10000000UL, 0xffffffffUL),
           static_cast<unsigned>(random(0x1000, 0xffff)),
           static_cast<unsigned>(random(0x1000, 0xffff)),
           static_cast<unsigned>(random(0x1000, 0xffff)),
           static_cast<unsigned>(random(0x1000, 0xffff)),
           random(0x10000000UL, 0xffffffffUL));
  return String(buf);
}

void writeWavHeader(uint8_t* out, uint32_t pcm_bytes) {
  const uint32_t riff_size = 36 + pcm_bytes;
  const uint32_t byte_rate = kSampleRate * 2;
  const uint16_t block_align = 2;
  const uint16_t bits_per_sample = 16;

  memcpy(out + 0, "RIFF", 4);
  out[4] = riff_size & 0xff;
  out[5] = (riff_size >> 8) & 0xff;
  out[6] = (riff_size >> 16) & 0xff;
  out[7] = (riff_size >> 24) & 0xff;
  memcpy(out + 8, "WAVEfmt ", 8);
  out[16] = 16;
  out[17] = 0;
  out[18] = 0;
  out[19] = 0;
  out[20] = 1;
  out[21] = 0;
  out[22] = 1;
  out[23] = 0;
  out[24] = kSampleRate & 0xff;
  out[25] = (kSampleRate >> 8) & 0xff;
  out[26] = (kSampleRate >> 16) & 0xff;
  out[27] = (kSampleRate >> 24) & 0xff;
  out[28] = byte_rate & 0xff;
  out[29] = (byte_rate >> 8) & 0xff;
  out[30] = (byte_rate >> 16) & 0xff;
  out[31] = (byte_rate >> 24) & 0xff;
  out[32] = block_align & 0xff;
  out[33] = (block_align >> 8) & 0xff;
  out[34] = bits_per_sample & 0xff;
  out[35] = (bits_per_sample >> 8) & 0xff;
  memcpy(out + 36, "data", 4);
  out[40] = pcm_bytes & 0xff;
  out[41] = (pcm_bytes >> 8) & 0xff;
  out[42] = (pcm_bytes >> 16) & 0xff;
  out[43] = (pcm_bytes >> 24) & 0xff;
}

String encodeAudioAsBase64(const uint8_t* pcm_data, size_t pcm_bytes) {
  const size_t wav_bytes = pcm_bytes + 44;
  std::unique_ptr<uint8_t[]> wav_data(new uint8_t[wav_bytes]);
  writeWavHeader(wav_data.get(), static_cast<uint32_t>(pcm_bytes));
  memcpy(wav_data.get() + 44, pcm_data, pcm_bytes);
  return base64::encode(wav_data.get(), wav_bytes);
}

bool zeroProxyRequest(const String& method,
                      const String& path,
                      const String& body,
                      int* code_out,
                      String* response_out,
                      String* error_out,
                      const char* response_file_path = nullptr) {
  if (!g_wifi_connected) {
    *error_out = "wifi offline";
    return false;
  }
  if (strlen(kZeroApiKey) == 0) {
    *error_out = "zero config missing";
    return false;
  }
  if (!acquireNetworkTask(NetworkTask::Zero, error_out)) {
    return false;
  }

  int proxy_fd = -1;
  std::unique_ptr<TlsContext> proxy_tls(new TlsContext());
  std::unique_ptr<TlsContext> target_tls(new TlsContext());
  if (!proxy_tls || !target_tls) {
    *error_out = "tls alloc failed";
    releaseNetworkTask(NetworkTask::Zero);
    return false;
  }
  initTlsContext(proxy_tls.get());
  initTlsContext(target_tls.get());
  bool proxy_tls_open = false;
  bool target_tls_open = false;
  auto cleanup = [&](const char* tag) {
    if (target_tls_open) {
      mbedtls_ssl_close_notify(&target_tls->ssl);
      target_tls_open = false;
    }
    if (proxy_tls_open) {
      mbedtls_ssl_close_notify(&proxy_tls->ssl);
      proxy_tls_open = false;
    }
    if (proxy_fd >= 0) {
      lwip_close(proxy_fd);
      proxy_fd = -1;
    }
    freeTlsContext(proxy_tls.get());
    freeTlsContext(target_tls.get());
    releaseNetworkTask(NetworkTask::Zero);
    logHeap(tag);
  };
  const unsigned long deadline = millis() + kZeroRequestDeadlineMs;
  const bool show_ui_stages = (method == "POST");
  auto zeroStage = [&](const char* stage) {
    ZB_LOG_PRINTF("ZERO stage: %s\n", stage);
    logHeap(stage);
    if (show_ui_stages) {
      g_status = "sending zero";
      g_result = stage;
      drawUiFrame();
    }
  };
  auto checkDeadline = [&]() -> bool {
    if (static_cast<long>(millis() - deadline) > 0) {
      *error_out = "zero timeout";
      return false;
    }
    return true;
  };

  zeroStage("proxy socket");
  if (!connectSocket(kProxyHost, kProxyPort, &proxy_fd, error_out)) {
    cleanup("zero cleanup socket fail");
    return false;
  }
  setSocketTimeoutMs(proxy_fd, 1500, 2500);
  if (!checkDeadline()) {
    cleanup("zero cleanup timeout");
    return false;
  }
  zeroStage("proxy tls");
  if (!handshakeTlsOnSocket(proxy_tls.get(), proxy_fd, kProxyHost, true, error_out)) {
    cleanup("zero cleanup proxy tls fail");
    return false;
  }
  proxy_tls_open = true;
  logHeap("zero proxy tls ok");

  const String connect_request =
      String("CONNECT ") + kZeroHost + ":" + String(kZeroPort) + " HTTP/1.1\r\n" +
      "Host: " + kZeroHost + ":" + String(kZeroPort) + "\r\n" +
      "Proxy-Authorization: " + kProxyBasicAuth + "\r\n" +
      "Proxy-Connection: Keep-Alive\r\n\r\n";
  if (!checkDeadline()) {
    cleanup("zero cleanup timeout");
    return false;
  }
  zeroStage("proxy connect");
  if (!tlsWriteAll(&proxy_tls->ssl, connect_request, error_out)) {
    cleanup("zero cleanup proxy connect write fail");
    return false;
  }

  String connect_error;
  const String connect_raw = tlsReadHeaders(&proxy_tls->ssl, &connect_error);
  const int connect_hdr_end = connect_raw.indexOf("\r\n\r\n");
  const String connect_headers =
      connect_hdr_end >= 0 ? connect_raw.substring(0, connect_hdr_end) : connect_raw;
  ZB_LOG_PRINTLN("ZERO proxy CONNECT response:");
  ZB_LOG_PRINTLN(connect_headers);
  if (connect_error.length() > 0) {
    *error_out = connect_error;
    cleanup("zero cleanup proxy connect read fail");
    return false;
  }
  if (!connect_headers.startsWith("HTTP/1.1 200") && !connect_headers.startsWith("HTTP/1.0 200")) {
    *error_out = trimToWidth("proxy " + connect_headers, 40);
    cleanup("zero cleanup proxy connect bad status");
    return false;
  }

  if (!checkDeadline()) {
    cleanup("zero cleanup timeout");
    return false;
  }
  zeroStage("target tls");
  setSocketTimeoutMs(proxy_fd, 1800, 2500);
  if (!handshakeTlsOverTlsWithDeadline(target_tls.get(),
                                       proxy_tls.get(),
                                       kZeroHost,
                                       deadline,
                                       error_out)) {
    cleanup("zero cleanup target tls fail");
    return false;
  }
  target_tls_open = true;
  logHeap("zero target tls ok");
  setSocketTimeoutMs(proxy_fd, 900, 2500);

  const String request =
      method + " " + path + " HTTP/1.1\r\n" +
      "Host: " + kZeroHost + "\r\n" +
      "Authorization: Bearer " + kZeroApiKey + "\r\n" +
      (body.length() > 0 ? "Content-Type: application/json\r\n" : "") +
      "Content-Length: " + String(body.length()) + "\r\n" +
      "Connection: close\r\n\r\n" + body;
  if (!checkDeadline()) {
    cleanup("zero cleanup timeout");
    return false;
  }
  zeroStage("post req");
  if (!tlsWriteAll(&target_tls->ssl, request, error_out)) {
    cleanup("zero cleanup request write fail");
    return false;
  }

  if (!checkDeadline()) {
    cleanup("zero cleanup timeout");
    return false;
  }
  zeroStage("read resp");
  String read_error;
  if (!readHttpResponseToSink(
          &target_tls->ssl, code_out, response_out, response_file_path, &read_error)) {
    *error_out = read_error.length() > 0 ? read_error : "bad zero resp";
    cleanup("zero cleanup bad response");
    return false;
  }

  if (read_error.length() > 0) {
    ZB_LOG_PRINTF("ZERO read tail: %s\n", read_error.c_str());
  }
  cleanup("zero cleanup success");

  ZB_LOG_PRINTF("ZERO HTTP code: %d\n", *code_out);
  if (response_file_path == nullptr) {
    ZB_LOG_PRINTF("ZERO response RAM bytes: %u\n", static_cast<unsigned>(response_out->length()));
    ZB_LOG_PRINTLN(trimToWidth(*response_out, 320));
  } else {
    ZB_LOG_PRINTF("ZERO response file: %s\n", response_file_path);
  }
  return true;
}

bool postTextToZero(const String& prompt, String* sent_message_id_out, String* error_out) {
  String normalized = prompt;
  normalized.trim();
  if (normalized.isEmpty()) {
    *error_out = "empty asr text";
    return false;
  }
  if (strlen(kZeroThreadId) == 0) {
    *error_out = "zero config missing";
    return false;
  }

  const String body =
      String("{\"prompt\":\"") + jsonEscape(normalized) + "\",\"threadId\":\"" +
      kZeroThreadId + "\"}";

  ZB_LOG_PRINTF("ZERO prompt length: %u\n", static_cast<unsigned>(normalized.length()));
  ZB_LOG_PRINTLN("ZERO prompt preview:");
  ZB_LOG_PRINTLN(trimToWidth(normalized, 120));
  ZB_LOG_PRINTLN("ZERO prompt hex:");
  ZB_LOG_PRINTLN(hexPreview(normalized, 48));
  ZB_LOG_PRINTLN("ZERO request body:");
  ZB_LOG_PRINTLN(body);

  int code = 0;
  String response;
  for (int attempt = 0; attempt < 2; ++attempt) {
    if (zeroProxyRequest("POST", "/api/v1/chat-threads/messages", body, &code, &response, error_out)) {
      break;
    }
    const String normalized = normalizeTlsError(*error_out);
    ZB_LOG_PRINTF("ZERO post attempt %d failed: %s\n", attempt + 1, normalized.c_str());
    if (attempt == 0 && isConnResetErrorString(normalized)) {
      delay(150);
      continue;
    }
    *error_out = normalized;
    return false;
  }
  if (code <= 0) {
    *error_out = "zero http " + String(code);
    return false;
  }
  if (code != 201 && code != 200) {
    *error_out = trimToWidth("zero " + String(code) + " " + response, 40);
    return false;
  }
  *sent_message_id_out = extractJsonString(response, "messageId");
  return true;
}

void runBootZeroSelfTest() {
  ZB_LOG_PRINTLN("ZERO self-test: begin");
  logHeap("selftest-start");

  String poll_error;
  const bool poll_ok = pollZeroAssistantMessages(true, &poll_error);
  ZB_LOG_PRINTF("ZERO self-test poll: %s\n", poll_ok ? "ok" : poll_error.c_str());
  logHeap("selftest-after-poll");

  String sent_message_id;
  String post_error;
  const bool post_ok = postTextToZero("m5 zero selftest ping", &sent_message_id, &post_error);
  ZB_LOG_PRINTF("ZERO self-test post: %s\n", post_ok ? "ok" : post_error.c_str());
  if (post_ok) {
    ZB_LOG_PRINTF("ZERO self-test messageId: %s\n", sent_message_id.c_str());
  }
  logHeap("selftest-after-post");
}

bool pollZeroAssistantMessages(bool baseline_only, String* error_out) {
  String path = String("/api/v1/chat-threads/") + kZeroThreadId +
                (baseline_only ? "/messages?limit=5" : "/messages?limit=10");
  if (!g_zero_last_seen_message_id.isEmpty()) {
    path += "&sinceId=" + g_zero_last_seen_message_id;
  }

  int code = 0;
  String response;
  if (!zeroProxyRequest("GET", path, "", &code, &response, error_out, kZeroResponsePath)) {
    removeFlashFileIfExists(kZeroResponsePath);
    return false;
  }
  if (code != 200) {
    *error_out = "poll " + String(code);
    removeFlashFileIfExists(kZeroResponsePath);
    return false;
  }

  String newest_message_id;
  const size_t before_count = g_pending_assistant_count;
  size_t added_count = 0;
  parseZeroMessagesFileToFlash(baseline_only, &newest_message_id, &added_count, error_out);
  removeFlashFileIfExists(kZeroResponsePath);
  if (!newest_message_id.isEmpty()) {
    g_zero_last_seen_message_id = newest_message_id;
  }
  if (!baseline_only) {
    if (added_count > 0) {
      notifyAssistantMessageArrived();
      wakeScreen(false, "assistant");
    }
    if (added_count > 0 && before_count == 0) {
      g_pending_message_index = 0;
      g_waiting_for_assistant = true;
      g_status = "assistant reply";
      g_result = "BtnA next";
      beepAssistantReady();
      String load_error;
      if (!loadCurrentAssistantMessage(&load_error)) {
        ZB_LOG_PRINTF("assistant load failed: %s\n", load_error.c_str());
      }
      drawUiFrame();
    }
  }
  return true;
}

void initMic() {
  M5.Speaker.end();
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
  M5.Mic.begin();
  ZB_LOG_PRINTLN("MIC init: M5.Mic.begin");
}

void connectWifi() {
  logHeap("wifi connect begin");
  WiFi.mode(WIFI_STA);
  g_wifi_connected = false;
  g_wifi_connecting = true;
  g_ip_address = "offline";
  for (const auto& cred : kWifiCreds) {
    ZB_LOG_PRINTF("WiFi try: %s\n", cred.ssid);
    WiFi.disconnect(true, true);
    delay(150);
    WiFi.begin(cred.ssid, cred.password);
    const unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 12000) {
      delay(250);
    }
    if (WiFi.status() == WL_CONNECTED) {
      g_wifi_connected = true;
      g_wifi_connecting = false;
      g_ip_address = WiFi.localIP().toString();
      ZB_LOG_PRINTF("WiFi connected: %s ip=%s\n", cred.ssid, g_ip_address.c_str());
      logHeap("wifi connected");
      return;
    }
    logHeap("wifi attempt failed");
  }
  g_wifi_connecting = false;
  logHeap("wifi failed");
}

void refreshBatteryStatus(bool force) {
  const uint32_t now = millis();
  if (!force && g_battery_ui.last_read_ms != 0 &&
      static_cast<uint32_t>(now - g_battery_ui.last_read_ms) < kBatteryReadIntervalMs) {
    return;
  }

  int32_t level = M5.Power.getBatteryLevel();
  const auto charging_state = M5.Power.isCharging();
  g_battery_ui.charging = charging_state == m5::Power_Class::is_charging;
  g_battery_ui.known = level >= 0;
  if (g_battery_ui.known) {
    if (level > 100) {
      level = 100;
    }
    g_battery_ui.level_percent = level;
  } else {
    g_battery_ui.level_percent = -1;
  }
  g_battery_ui.last_read_ms = now == 0 ? 1 : now;
}

uint16_t batteryFillColor() {
  if (g_battery_ui.charging) {
    return TFT_CYAN;
  }
  if (!g_battery_ui.known) {
    return TFT_DARKGREY;
  }
  if (g_battery_ui.level_percent <= 15) {
    return TFT_RED;
  }
  if (g_battery_ui.level_percent <= 35) {
    return TFT_ORANGE;
  }
  return TFT_GREEN;
}

void drawBatteryStatus() {
  refreshBatteryStatus(false);

  auto& display = M5.Display;
  const int icon_x = kUiOffsetX + kScreenWidth - kBatteryIconW - kBatteryIconTipW - 5;
  const int icon_y = 2;
  const uint16_t outline = g_battery_ui.known ? TFT_LIGHTGREY : TFT_DARKGREY;

  display.setFont(&fonts::Font0);
  display.setTextColor(outline, TFT_BLACK);
  display.setCursor(icon_x - 20, icon_y + 1);
  if (g_battery_ui.known) {
    display.printf("%3d", static_cast<int>(g_battery_ui.level_percent));
  } else {
    display.print("  ?");
  }

  display.drawRect(icon_x, icon_y, kBatteryIconW, kBatteryIconH, outline);
  display.fillRect(icon_x + kBatteryIconW, icon_y + 3, kBatteryIconTipW, kBatteryIconH - 6, outline);
  const uint8_t fill_w =
      g_battery_ui.known
          ? zero_buddy::batteryFillPixels(g_battery_ui.level_percent,
                                          static_cast<uint8_t>(kBatteryIconW - 4))
          : 0;
  if (fill_w > 0) {
    display.fillRect(icon_x + 2, icon_y + 2, fill_w, kBatteryIconH - 4, batteryFillColor());
  }
  if (g_battery_ui.charging) {
    const int lx = icon_x + 12;
    display.drawLine(lx + 1, icon_y + 1, lx - 2, icon_y + 5, TFT_YELLOW);
    display.drawLine(lx - 2, icon_y + 5, lx + 2, icon_y + 5, TFT_YELLOW);
    display.drawLine(lx + 2, icon_y + 5, lx - 1, icon_y + 8, TFT_YELLOW);
  }
}

void drawUiFrame() {
  if (!g_power.screen_awake) {
    return;
  }
  auto& display = M5.Display;
  display.setRotation(3);
  display.fillScreen(TFT_BLACK);
  const bool normal_mode = g_test_mode == TestMode::Normal;
  display.setTextWrap(false);
  drawBatteryStatus();

  if (normal_mode && g_wifi_connecting) {
    display.setTextColor(TFT_CYAN, TFT_BLACK);
    display.setFont(&fonts::efontCN_14);
    display.setCursor(kUiOffsetX + 48, 50);
    display.print("Wi-Fi connecting");
    display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    display.setCursor(kUiOffsetX + 58, 72);
    display.print("please wait");
    display.setTextWrap(true);
    return;
  }

  if (normal_mode && !g_wifi_connected) {
    display.setTextColor(TFT_RED, TFT_BLACK);
    display.setFont(&fonts::efontCN_14);
    display.setCursor(kUiOffsetX + 76, 50);
    display.print("No Wi-Fi");
    display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    display.setCursor(kUiOffsetX + 44, 72);
    display.print("retrying automatically");
    display.setTextWrap(true);
    return;
  }

  const String pending = currentPendingAssistantMessage();
  if (normal_mode && !pending.isEmpty()) {
    display.drawRoundRect(kUiOffsetX + kBodyLeft, kBodyTop, kBodyWidth, kBodyHeight, 6, TFT_DARKCYAN);
    display.setTextColor(TFT_WHITE, TFT_BLACK);
    display.setFont(&fonts::efontCN_14);
    const size_t start = min(g_scroll_line, g_current_message_lines.size());
    const size_t end = min(start + static_cast<size_t>(kBodyVisibleLines), g_current_message_lines.size());
    int y = kBodyTop + 8;
    for (size_t i = start; i < end; ++i) {
      display.setCursor(kUiOffsetX + kBodyLeft + 8, y);
      display.print(g_current_message_lines[i]);
      y += kBodyLineHeight;
    }
    display.setFont(&fonts::Font0);
    display.setTextColor(TFT_CYAN, TFT_BLACK);
    display.setCursor(kUiOffsetX + kScreenWidth - 48, kScreenHeight - 11);
    display.printf("%u/%u",
                   static_cast<unsigned>(min(g_scroll_line + 1, g_current_message_lines.size())),
                   static_cast<unsigned>(g_current_message_lines.size()));
  } else {
    display.setTextColor(TFT_YELLOW, TFT_BLACK);
    display.setFont(&fonts::Font2);
    display.setCursor(kUiOffsetX + 8, 22);
    display.printf("%s", trimToWidth(g_status, 24).c_str());
    display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    display.setFont(&fonts::Font0);
    display.setCursor(kUiOffsetX + 8, 48);
    display.printf("%s", normal_mode ? "Hold BtnA to talk" : "BtnA run, hold BtnB mode");
    display.drawRoundRect(kUiOffsetX + kBodyLeft, 72, kBodyWidth, 48, 6,
                          normal_mode ? TFT_DARKGREY : TFT_DARKGREEN);
    display.setTextColor(TFT_WHITE, TFT_BLACK);
    display.setFont(&fonts::efontCN_14);
    display.setCursor(kUiOffsetX + kBodyLeft + 8, 84);
    display.printf("%s", trimToWidth(g_result, 60).c_str());
  }
  display.setTextWrap(true);
}

void closeStreamAsrSession() {
  logHeap("asr close begin");
  if (g_asr_session.target_tls) {
    freeTlsContext(g_asr_session.target_tls.get());
    g_asr_session.target_tls.reset();
    logHeap("asr target tls freed");
  }
  if (g_asr_session.proxy_tls) {
    freeTlsContext(g_asr_session.proxy_tls.get());
    g_asr_session.proxy_tls.reset();
    logHeap("asr proxy tls freed");
  }
  if (g_asr_session.proxy_fd >= 0) {
    lwip_close(g_asr_session.proxy_fd);
    g_asr_session.proxy_fd = -1;
    logHeap("asr socket closed");
  }
  g_asr_session.best_text = "";
  g_asr_session.frames_read = 0;
  g_asr_session.saw_final = false;
  g_asr_session.active = false;
  releaseNetworkTask(NetworkTask::Asr);
  logHeap("asr close end");
}

bool startStreamAsrSession(String* error_out) {
  ZB_LOG_PRINTLN("ASR start: begin");
  logHeap("asr start begin");
  if (!g_wifi_connected) {
    *error_out = "wifi offline";
    return false;
  }
  closeStreamAsrSession();
  if (!acquireNetworkTask(NetworkTask::Asr, error_out)) {
    return false;
  }
  g_asr_session.proxy_tls.reset(new TlsContext());
  g_asr_session.target_tls.reset(new TlsContext());
  if (!g_asr_session.proxy_tls || !g_asr_session.target_tls) {
    *error_out = "tls alloc failed";
    logHeap("asr tls alloc failed");
    closeStreamAsrSession();
    return false;
  }
  logHeap("asr tls allocated");
  initTlsContext(g_asr_session.proxy_tls.get());
  initTlsContext(g_asr_session.target_tls.get());
  logHeap("asr tls init ok");
  ZB_LOG_PRINTLN("ASR start: direct socket");
  if (!connectSocket(kAsrWsHost, kAsrWsPort, &g_asr_session.proxy_fd, error_out)) {
    ZB_LOG_PRINTF("ASR start: direct socket failed: %s\n", error_out->c_str());
    closeStreamAsrSession();
    return false;
  }
  ZB_LOG_PRINTLN("ASR start: direct tls handshake");
  if (!handshakeTlsOnSocket(
          g_asr_session.target_tls.get(), g_asr_session.proxy_fd, kAsrWsHost, true, error_out)) {
    ZB_LOG_PRINTF("ASR start: direct tls failed: %s\n", error_out->c_str());
    closeStreamAsrSession();
    return false;
  }
  setSocketTimeoutMs(g_asr_session.proxy_fd, 800, 3000);
  ZB_LOG_PRINTLN("ASR start: direct tls ok");
  logHeap("asr tls ok");

  const String ws_key = "M5StickCPlusWs==";
  const String connect_id = generateRequestId();
  std::vector<String> req_parts = {
      String("GET ") + kAsrWsPath + " HTTP/1.1\r\n",
      String("Host: ") + kAsrWsHost + "\r\n",
      "Upgrade: websocket\r\n",
      "Connection: Upgrade\r\n",
      "Sec-WebSocket-Version: 13\r\n",
      String("Sec-WebSocket-Key: ") + ws_key + "\r\n",
      String("X-Api-App-Key: ") + kAsrWsAppKey + "\r\n",
      String("X-Api-Access-Key: ") + kAsrWsAccessKey + "\r\n",
      String("X-Api-Resource-Id: ") + kAsrWsResourceId + "\r\n",
      String("X-Api-Connect-Id: ") + connect_id + "\r\n",
      "\r\n"};
  size_t req_len = 0;
  for (const auto& p : req_parts) {
    req_len += p.length();
  }
  ZB_LOG_PRINTF("ASR start: ws upgrade total len=%u\n", static_cast<unsigned>(req_len));
  ZB_LOG_PRINTLN("ASR start: write ws upgrade");
  if (!tlsWriteParts(&g_asr_session.target_tls->ssl, req_parts, error_out)) {
    ZB_LOG_PRINTF("ASR start: ws upgrade write failed: %s\n", error_out->c_str());
    closeStreamAsrSession();
    return false;
  }
  ZB_LOG_PRINTLN("ASR start: read ws upgrade response");
  String ws_error;
  const String ws_resp = tlsReadHeaders(&g_asr_session.target_tls->ssl, &ws_error);
  ZB_LOG_PRINTLN("ASR WS upgrade response:");
  ZB_LOG_PRINTLN(ws_resp);
  if (ws_error.length() > 0 || !ws_resp.startsWith("HTTP/1.1 101")) {
    *error_out = ws_error.length() > 0 ? ws_error : trimToWidth(ws_resp, 40);
    ZB_LOG_PRINTF("ASR start: ws upgrade failed: %s\n", error_out->c_str());
    closeStreamAsrSession();
    return false;
  }
  logHeap("asr ws upgrade ok");

  const String full_json =
      String("{\"user\":{\"uid\":\"m5stickcplus\",\"did\":\"M5StickCPlus\",") +
      "\"platform\":\"arduino-esp32\",\"sdk_version\":\"m5-demo\",\"app_version\":\"1\"},"
      "\"audio\":{\"format\":\"pcm\",\"codec\":\"raw\",\"rate\":16000,\"bits\":16,"
      "\"channel\":1,\"language\":\"zh-CN\"},"
      "\"request\":{\"model_name\":\"bigmodel\",\"enable_itn\":true,"
      "\"enable_punc\":true,\"enable_ddc\":false}}";
  std::vector<uint8_t> packet;
  packet.reserve(full_json.length() + 8);
  packet.push_back(0x11);
  packet.push_back(0x10);
  packet.push_back(0x10);
  packet.push_back(0x00);
  appendU32(&packet, full_json.length());
  packet.insert(packet.end(), full_json.begin(), full_json.end());
  ZB_LOG_PRINTLN("ASR start: send full client request");
  if (!wsSendBinaryFrame(&g_asr_session.target_tls->ssl, packet.data(), packet.size(), error_out)) {
    ZB_LOG_PRINTF("ASR start: full request send failed: %s\n", error_out->c_str());
    closeStreamAsrSession();
    return false;
  }
  logHeap("asr full request sent");

  ZB_LOG_PRINTLN("ASR start: session active");
  g_asr_session.active = true;
  logHeap("asr start active");
  return true;
}

bool sendStreamAudioChunk(const uint8_t* data, size_t data_len, bool is_final, String* error_out) {
  if (!g_asr_session.active) {
    *error_out = "asr not active";
    return false;
  }
  const std::vector<uint8_t> packet =
      zero_buddy::buildStreamAudioPacket(data, data_len, is_final);
  if (!wsSendBinaryFrame(&g_asr_session.target_tls->ssl, packet.data(), packet.size(), error_out)) {
    return false;
  }
  g_last_asr_chunk_sent_ms = millis();
  ++g_asr_chunks_sent;
  ZB_LOG_PRINTF("ASR audio send chunk=%u final=%d bytes=%u recorded=%u sent=%u backlog=%u dropped=%u\n",
                static_cast<unsigned>(g_asr_chunks_sent),
                is_final ? 1 : 0,
                static_cast<unsigned>(data_len),
                static_cast<unsigned>(g_recorded_bytes),
                static_cast<unsigned>(g_record_sent_bytes),
                static_cast<unsigned>(g_record_backlog.size()),
                static_cast<unsigned>(g_record_backlog.droppedBytes()));
  return true;
}

bool readOneAsrResponse(String* error_out, bool* got_frame_out, bool* saw_final_out) {
  *got_frame_out = false;
  *saw_final_out = false;
  if (!g_asr_session.active) {
    *error_out = "asr not active";
    return false;
  }

  uint8_t opcode = 0;
  std::vector<uint8_t> frame_payload;
  if (!wsReadFrame(&g_asr_session.target_tls->ssl, &opcode, &frame_payload, error_out)) {
    if (isTimeoutErrorString(*error_out)) {
      *error_out = "";
      return true;
    }
    return false;
  }

  *got_frame_out = true;
  if (opcode == 0x9) {
    return true;
  }
  if (opcode != 0x2 && opcode != 0x1) {
    return true;
  }

  bool is_final = false;
  String frame_text;
  if (!parseAsrServerPayload(frame_payload, &is_final, &frame_text, error_out)) {
    return false;
  }
  ++g_asr_session.frames_read;
  if (!frame_text.isEmpty()) {
    g_asr_session.best_text = frame_text;
  }
  if (is_final) {
    g_asr_session.saw_final = true;
    *saw_final_out = true;
  }
  return true;
}

bool drainAsrResponses(uint32_t window_ms,
                       int recv_timeout_ms,
                       bool stop_on_text,
                       bool stop_on_final,
                       String* error_out) {
  if (!g_asr_session.active) {
    return true;
  }
  setSocketTimeoutMs(g_asr_session.proxy_fd, recv_timeout_ms, 3000);
  const unsigned long deadline = millis() + window_ms;
  do {
    bool got_frame = false;
    bool saw_final = false;
    if (!readOneAsrResponse(error_out, &got_frame, &saw_final)) {
      return false;
    }
    if (!got_frame) {
      continue;
    }
    if ((stop_on_text && !g_asr_session.best_text.isEmpty()) ||
        (stop_on_final && saw_final)) {
      return true;
    }
  } while (static_cast<long>(millis() - deadline) < 0);
  return true;
}

bool finishStreamAsr(String* error_out, String* text_out) {
  if (!g_asr_session.active) {
    *error_out = "asr not active";
    return false;
  }
  logHeap("asr finish begin");
  const bool drained = drainAsrResponses(7000, 250, true, true, error_out);
  if (!drained) {
    closeStreamAsrSession();
    return false;
  }
  if (!g_asr_session.best_text.isEmpty() || g_asr_session.saw_final) {
    *text_out = g_asr_session.best_text;
    logHeap("asr finish got text");
    closeStreamAsrSession();
    return true;
  }
  closeStreamAsrSession();
  *error_out = "asr final timeout";
  logHeap("asr finish timeout");
  return false;
}

bool ensureAsrSessionStarted(String* error_out) {
  if (g_asr_session.active) {
    return true;
  }
  if (!g_asr_connect_started) {
    g_asr_connect_started = true;
    g_status = "recording";
    g_result = "connecting asr";
    drawUiFrame();
  }
  for (int attempt = 0; attempt < 2; ++attempt) {
    if (startStreamAsrSession(error_out)) {
      g_status = "recording";
      g_result = "release BtnA";
      drawUiFrame();
      return true;
    }
    const String normalized = normalizeTlsError(*error_out);
    ZB_LOG_PRINTF("ASR session attempt %d failed: %s\n", attempt + 1, normalized.c_str());
    if (attempt == 0 && isConnResetErrorString(normalized)) {
      closeStreamAsrSession();
      delay(120);
      continue;
    }
    *error_out = normalized;
    return false;
  }
  return false;
}

void copyAsrConnectError(const String& error) {
  const String trimmed = trimToWidth(error, sizeof(g_asr_connect_error) - 1);
  strncpy(g_asr_connect_error, trimmed.c_str(), sizeof(g_asr_connect_error) - 1);
  g_asr_connect_error[sizeof(g_asr_connect_error) - 1] = '\0';
}

void asrConnectTaskMain(void*) {
  String error;
  const bool ok = startStreamAsrSession(&error);
  if (!ok) {
    copyAsrConnectError(error);
  } else {
    g_asr_connect_error[0] = '\0';
  }
  g_asr_async_finished_ms = millis();
  g_asr_connect_state = ok ? AsrConnectState::Ready : AsrConnectState::Failed;
  ZB_LOG_PRINTF("ASR async connect %s ms=%u backlog=%u dropped=%u\n",
                ok ? "ready" : "failed",
                static_cast<unsigned>(g_asr_async_finished_ms - g_asr_async_started_ms),
                static_cast<unsigned>(g_record_backlog.size()),
                static_cast<unsigned>(g_record_backlog.droppedBytes()));
  g_asr_connect_task = nullptr;
  vTaskDelete(nullptr);
}

bool startAsyncAsrConnect(String* error_out) {
  if (g_asr_session.active || g_asr_connect_state == AsrConnectState::Ready) {
    return true;
  }
  if (g_asr_connect_state == AsrConnectState::Connecting) {
    return true;
  }
  if (!g_wifi_connected) {
    *error_out = "wifi offline";
    return false;
  }

  g_asr_connect_started = true;
  g_asr_connect_error[0] = '\0';
  g_asr_async_started_ms = millis();
  g_asr_async_finished_ms = 0;
  g_asr_connect_state = AsrConnectState::Connecting;
  const BaseType_t created = xTaskCreatePinnedToCore(asrConnectTaskMain,
                                                     "asr_conn",
                                                     12288,
                                                     nullptr,
                                                     1,
                                                     &g_asr_connect_task,
                                                     0);
  if (created != pdPASS) {
    g_asr_connect_task = nullptr;
    g_asr_connect_state = AsrConnectState::Failed;
    *error_out = "asr task failed";
    copyAsrConnectError(*error_out);
    return false;
  }
  ZB_LOG_PRINTLN("ASR async connect started");
  return true;
}

void waitForAsrConnectTask(uint32_t timeout_ms) {
  if (g_asr_connect_state != AsrConnectState::Connecting) {
    return;
  }
  const unsigned long deadline = millis() + timeout_ms;
  while (g_asr_connect_state == AsrConnectState::Connecting &&
         static_cast<long>(millis() - deadline) < 0) {
    delay(10);
  }
}

void resetAsrConnectStateIfDone() {
  if (g_asr_connect_state != AsrConnectState::Connecting) {
    g_asr_connect_state = AsrConnectState::Idle;
    g_asr_connect_task = nullptr;
    g_asr_connect_error[0] = '\0';
  }
}

bool waitForAsyncAsrReady(String* error_out, uint32_t timeout_ms) {
  waitForAsrConnectTask(timeout_ms);
  if (g_asr_connect_state == AsrConnectState::Ready && g_asr_session.active) {
    return true;
  }
  if (g_asr_connect_state == AsrConnectState::Failed) {
    *error_out = g_asr_connect_error[0] != '\0' ? String(g_asr_connect_error)
                                                 : String("asr connect failed");
    return false;
  }
  if (g_asr_connect_state == AsrConnectState::Connecting) {
    *error_out = "asr connect timeout";
    return false;
  }
  *error_out = "asr not connected";
  return false;
}

bool waitForAsrChunkWindow(bool blocking) {
  if (g_last_asr_chunk_sent_ms == 0) {
    return true;
  }
  const unsigned long elapsed = millis() - g_last_asr_chunk_sent_ms;
  if (elapsed >= kAsrChunkMinIntervalMs) {
    return true;
  }
  if (!blocking) {
    return false;
  }
  delay(kAsrChunkMinIntervalMs - elapsed);
  return true;
}

bool sendBufferedAsrChunk(size_t data_len, bool is_final, bool blocking, String* error_out) {
  if (!waitForAsrChunkWindow(blocking)) {
    return true;
  }
  if (!sendStreamAudioChunk(data_len > 0 ? g_stream_chunk : nullptr, data_len, is_final, error_out)) {
    return false;
  }
  g_stream_chunk_fill = 0;
  if (!drainAsrResponses(40, 20, false, false, error_out)) {
    return false;
  }
  return true;
}

bool pumpBufferedAudioToAsr(bool finalize, String* error_out) {
  if (!g_asr_session.active) {
    return true;
  }
  while (g_stream_chunk_fill == kStreamChunkBytes || g_record_backlog.size() > 0) {
    if (g_stream_chunk_fill == kStreamChunkBytes) {
      if (!sendBufferedAsrChunk(g_stream_chunk_fill, false, finalize, error_out)) {
        return false;
      }
      if (!finalize && g_stream_chunk_fill == kStreamChunkBytes) {
        return true;
      }
      continue;
    }

    const size_t writable = kStreamChunkBytes - g_stream_chunk_fill;
    const size_t popped = g_record_backlog.pop(g_stream_chunk + g_stream_chunk_fill, writable);
    if (popped == 0) {
      break;
    }
    g_stream_chunk_fill += popped;
    g_record_sent_bytes += popped;
  }

  if (finalize) {
    if (g_stream_chunk_fill > 0) {
      if (!sendBufferedAsrChunk(g_stream_chunk_fill, true, true, error_out)) {
        return false;
      }
    } else {
      if (!sendBufferedAsrChunk(0, true, true, error_out)) {
        return false;
      }
    }
  }
  return true;
}

void analyzeRecordedAudio() {
  g_record_peak = g_raw_peak;
  g_record_avg_abs = currentRawAvgAbs();
  g_record_clipped_samples = g_raw_clipped_samples;
  ZB_LOG_PRINTF("REC audio bytes=%u backlog=%u dropped=%u peak=%u avg=%u clipped=%u\n",
                static_cast<unsigned>(g_recorded_bytes),
                static_cast<unsigned>(g_record_backlog.size()),
                static_cast<unsigned>(g_record_backlog.droppedBytes()),
                static_cast<unsigned>(g_record_peak),
                static_cast<unsigned>(g_record_avg_abs),
                static_cast<unsigned>(g_record_clipped_samples));
}

bool recordedAudioLooksSilent() {
  return g_record_peak < 1200 || g_record_avg_abs < 120;
}

unsigned long currentAsrConnectMs() {
  if (g_asr_async_started_ms == 0) {
    return 0;
  }
  if (g_asr_async_finished_ms > g_asr_async_started_ms) {
    return g_asr_async_finished_ms - g_asr_async_started_ms;
  }
  return millis() - g_asr_async_started_ms;
}

zero_buddy::AsrCaptureMetrics currentAsrCaptureMetrics() {
  zero_buddy::AsrCaptureMetrics metrics;
  metrics.recorded_bytes = g_recorded_bytes;
  metrics.flash_bytes = g_record_flash_bytes;
  metrics.sent_bytes = g_record_sent_bytes;
  metrics.dropped_bytes = g_record_backlog.droppedBytes();
  metrics.flash_failed = g_record_flash_failed;
  return metrics;
}

void updateLastAsrCaptureMetrics(unsigned long connect_ms) {
  g_last_asr_rec_recorded_bytes = g_recorded_bytes;
  g_last_asr_rec_flash_bytes = g_record_flash_bytes;
  g_last_asr_rec_sent_bytes = g_record_sent_bytes;
  g_last_asr_rec_dropped_bytes = g_record_backlog.droppedBytes();
  g_last_asr_rec_chunks = g_asr_chunks_sent;
  g_last_asr_rec_connect_ms = connect_ms;
}

void logAsrCaptureMetrics(const char* label,
                          unsigned long connect_ms,
                          const zero_buddy::AsrCaptureAssessment& assessment) {
  ZB_LOG_PRINTF("%s metrics recorded=%u flash=%u sent=%u backlog=%u dropped=%u chunks=%u "
                "connect_ms=%u first_audio_ms=%u last_audio_ms=%u flash_failed=%d "
                "input_complete=%d upload_complete=%d reason=%s\n",
                label,
                static_cast<unsigned>(g_recorded_bytes),
                static_cast<unsigned>(g_record_flash_bytes),
                static_cast<unsigned>(g_record_sent_bytes),
                static_cast<unsigned>(g_record_backlog.size()),
                static_cast<unsigned>(g_record_backlog.droppedBytes()),
                static_cast<unsigned>(g_asr_chunks_sent),
                static_cast<unsigned>(connect_ms),
                static_cast<unsigned>(g_first_audio_ms > g_record_start_ms
                                          ? g_first_audio_ms - g_record_start_ms
                                          : 0),
                static_cast<unsigned>(g_last_audio_ms > g_record_start_ms
                                          ? g_last_audio_ms - g_record_start_ms
                                          : 0),
                g_record_flash_failed ? 1 : 0,
                assessment.input_complete ? 1 : 0,
                assessment.upload_complete ? 1 : 0,
                assessment.reason.c_str());
}

bool transcribeRecordedBuffer(String* text_out, String* error_out) {
  if (!g_asr_session.active) {
    if (!startStreamAsrSession(error_out)) {
      return false;
    }
  }
  if (!pumpBufferedAudioToAsr(true, error_out)) {
    closeStreamAsrSession();
    return false;
  }
  return finishStreamAsr(error_out, text_out);
}

bool transcribeLiveBacklog(String* text_out, String* error_out) {
  if (!g_asr_session.active) {
    *error_out = "asr not active";
    return false;
  }
  if (!pumpBufferedAudioToAsr(true, error_out)) {
    closeStreamAsrSession();
    return false;
  }
  return finishStreamAsr(error_out, text_out);
}

bool transcribeFlashRecording(String* text_out, String* error_out) {
  if (!g_asr_session.active) {
    if (!startStreamAsrSession(error_out)) {
      return false;
    }
  }
  File file = LittleFS.open(kAsrRecordPcmPath, FILE_READ);
  if (!file) {
    *error_out = "record file missing";
    closeStreamAsrSession();
    return false;
  }
  const size_t total = static_cast<size_t>(file.size());
  if (total == 0) {
    file.close();
    *error_out = "empty record file";
    closeStreamAsrSession();
    return false;
  }

  g_record_sent_bytes = 0;
  g_stream_chunk_fill = 0;
  while (file.available()) {
    const size_t read_len = file.read(g_stream_chunk, kStreamChunkBytes);
    if (read_len == 0) {
      break;
    }
    g_stream_chunk_fill = read_len;
    g_record_sent_bytes += read_len;
    const bool final = g_record_sent_bytes >= total;
    if (!sendBufferedAsrChunk(read_len, final, true, error_out)) {
      file.close();
      closeStreamAsrSession();
      return false;
    }
  }
  file.close();
  return finishStreamAsr(error_out, text_out);
}

void startRecording() {
  closeStreamAsrSession();
  stopAssistantPollingWindow();
  beepListenStart();
  clearRecordBacklog();
  g_recorded_bytes = 0;
  g_record_sent_bytes = 0;
  g_stream_chunk_fill = 0;
  g_asr_chunks_sent = 0;
  g_last_asr_chunk_sent_ms = 0;
  g_first_audio_ms = 0;
  g_last_audio_ms = 0;
  g_record_flash_bytes = 0;
  g_record_flash_failed = false;
  g_record_peak = 0;
  g_record_avg_abs = 0;
  g_record_clipped_samples = 0;
  g_raw_peak = 0;
  g_raw_abs_sum = 0;
  g_raw_sample_count = 0;
  g_raw_clipped_samples = 0;
  g_asr_connect_started = false;
  g_record_start_ms = millis();
  initMic();
  g_recording = true;
  g_status = "recording";
  g_result = "start speaking";
}

void finishMicOnlyTest() {
  g_recording = false;
  analyzeRecordedAudio();
  g_status = recordedAudioLooksSilent() ? "audio low" : "audio ok";
  g_result = "r" + String(g_raw_peak) + "/" + String(currentRawAvgAbs()) + "/" +
             String(g_raw_clipped_samples) + " p" + String(g_record_peak) + "/" +
             String(g_record_avg_abs) + "/" + String(g_record_clipped_samples);
  releaseAudioResources();
  drawUiFrame();
}

void finishAsrOnlyTest() {
  g_recording = false;
  beepSendStop();
  g_uploading = true;
  g_status = "asr test";
  g_result = "finishing";
  drawUiFrame();

  analyzeRecordedAudio();
  if (recordedAudioLooksSilent()) {
    g_uploading = false;
    g_status = "audio low";
    g_result = "pk " + String(g_record_peak) + " av " + String(g_record_avg_abs) +
               " cl " + String(g_record_clipped_samples);
    releaseAudioResources();
    drawUiFrame();
    return;
  }

  String error;
  String text;
  bool ok = ensureAsrSessionStarted(&error) && transcribeRecordedBuffer(&text, &error);
  String normalized_text = text;
  normalized_text.trim();
  if (ok && normalized_text.isEmpty()) {
    ok = false;
    error = "empty asr text";
  }
  g_uploading = false;
  if (ok) {
    ZB_LOG_PRINTF("ASR final text: %s\n", text.c_str());
    g_status = "asr ok";
    g_result = trimToWidth(text, 60);
  } else {
    ZB_LOG_PRINTF("ASR finish failed: %s\n", error.c_str());
    g_status = "failed";
    g_result = trimToWidth(error, 30);
  }
  releaseAudioResources();
  drawUiFrame();
}

void startAsyncAsrCaptureTest(bool flash_replay) {
  closeRecordFlash();
  g_record_to_flash = flash_replay;
  g_record_flash_failed = false;
  g_record_flash_bytes = 0;
  startRecording();
  g_status = asrCaptureStatusLabel(flash_replay);
  g_result = flash_replay ? "record+replay" : "record+stream";
  drawUiFrame();
  String error;
  if (!startAsyncAsrConnect(&error)) {
    ZB_LOG_PRINTF("ASR async start failed: %s\n", error.c_str());
    g_recording = false;
    g_status = "failed";
    g_result = trimToWidth(error, 30);
    releaseAudioResources();
    drawUiFrame();
  }
}

void startAsrLiveTest() {
  startAsyncAsrCaptureTest(false);
}

void startAsrFlashTest() {
  startAsyncAsrCaptureTest(true);
}

void startNormalConversationCapture() {
  closeRecordFlash();
  g_record_to_flash = true;
  g_record_flash_failed = false;
  g_record_flash_bytes = 0;
  startRecording();
  g_status = "recording";
  g_result = "local capture";
  drawUiFrame();
}

void finishAsyncAsrCaptureTest(bool flash_replay) {
  g_recording = false;
  closeRecordFlash();
  g_record_to_flash = false;
  beepSendStop();
  g_uploading = true;
  g_status = asrCaptureStatusLabel(flash_replay);
  g_result = "finishing";
  drawUiFrame();

  analyzeRecordedAudio();
  if (recordedAudioLooksSilent()) {
    String wait_error;
    waitForAsyncAsrReady(&wait_error, kAsrRecordConnectWaitMs);
    const unsigned long connect_ms = currentAsrConnectMs();
    updateLastAsrCaptureMetrics(connect_ms);
    const auto assessment =
        zero_buddy::assessAsrCapture(asrCaptureStrategy(flash_replay), currentAsrCaptureMetrics());
    logAsrCaptureMetrics(flash_replay ? "ASR FLASH" : "ASR LIVE", connect_ms, assessment);
    g_uploading = false;
    g_status = "audio low";
    g_result = "pk " + String(g_record_peak) + " av " + String(g_record_avg_abs);
    releaseAudioResources();
    drawUiFrame();
    return;
  }

  String error;
  String text;
  const bool ready = waitForAsyncAsrReady(&error, kAsrRecordConnectWaitMs);
  const unsigned long connect_ms = currentAsrConnectMs();
  bool ok = ready && (flash_replay ? transcribeFlashRecording(&text, &error)
                                   : transcribeLiveBacklog(&text, &error));
  if (g_record_flash_failed) {
    error = "record flash failed";
  }
  String normalized_text = text;
  normalized_text.trim();
  if (ok && normalized_text.isEmpty()) {
    ok = false;
    error = "empty asr text";
  }

  updateLastAsrCaptureMetrics(connect_ms);
  const auto assessment =
      zero_buddy::assessAsrCapture(asrCaptureStrategy(flash_replay), currentAsrCaptureMetrics());
  logAsrCaptureMetrics(flash_replay ? "ASR FLASH" : "ASR LIVE", connect_ms, assessment);

  g_uploading = false;
  if (ok) {
    ZB_LOG_PRINTF("%s final text: %s\n", flash_replay ? "ASR FLASH" : "ASR LIVE", text.c_str());
    g_status = assessment.ok() ? String(asrCaptureStatusLabel(flash_replay)) + " ok"
                               : String(asrCaptureStatusLabel(flash_replay)) + " lossy";
    g_result = assessment.ok() ? trimToWidth(text, 60)
                               : trimToWidth(String(assessment.reason.c_str()) + " " + text, 60);
  } else {
    ZB_LOG_PRINTF("%s failed: %s\n", flash_replay ? "ASR FLASH" : "ASR LIVE", error.c_str());
    g_status = "failed";
    g_result = trimToWidth(error, 36);
  }
  releaseAudioResources();
  drawUiFrame();
}

void finishAsrLiveTest() {
  finishAsyncAsrCaptureTest(false);
}

void finishAsrFlashTest() {
  finishAsyncAsrCaptureTest(true);
}

void finishNormalConversation() {
  g_recording = false;
  closeRecordFlash();
  g_record_to_flash = false;
  beepSendStop();
  g_uploading = true;
  g_status = "recognizing";
  g_result = "finishing";
  drawUiFrame();

  analyzeRecordedAudio();
  if (recordedAudioLooksSilent()) {
    g_uploading = false;
    g_status = "audio low";
    g_result = "pk " + String(g_record_peak) + " av " + String(g_record_avg_abs);
    releaseAudioResources();
    drawUiFrame();
    return;
  }

  String error;
  String text;
  const bool flash_complete =
      !g_record_flash_failed && g_record_flash_bytes == g_recorded_bytes && g_record_flash_bytes > 0;
  g_record_sent_bytes = 0;
  g_stream_chunk_fill = 0;
  g_asr_chunks_sent = 0;
  g_last_asr_chunk_sent_ms = 0;
  const unsigned long asr_begin_ms = millis();
  bool ok = flash_complete && transcribeFlashRecording(&text, &error);
  const unsigned long asr_elapsed_ms = millis() - asr_begin_ms;
  if (!flash_complete) {
    error = g_record_flash_failed ? "record flash failed" : "record flash incomplete";
  }
  String normalized_text = text;
  normalized_text.trim();
  if (ok && normalized_text.isEmpty()) {
    ok = false;
    error = "empty asr text";
  }
  const unsigned long connect_ms = asr_elapsed_ms;
  updateLastAsrCaptureMetrics(connect_ms);
  const auto assessment = zero_buddy::assessAsrCapture(
      zero_buddy::AsrCaptureStrategy::FlashReplay,
      currentAsrCaptureMetrics());
  logAsrCaptureMetrics("NORMAL ASR FLASH", connect_ms, assessment);

  releaseAudioResources();
  g_uploading = false;
  if (!ok) {
    ZB_LOG_PRINTF("ASR finish failed: %s\n", error.c_str());
    stopAssistantPollingWindow();
    g_status = "failed";
    g_result = trimToWidth(error, 36);
    drawUiFrame();
    return;
  }

  String sent_message_id;
  String zero_error;
  g_status = "sending zero";
  g_result = "posting...";
  drawUiFrame();
  const bool zero_ok = postTextToZero(text, &sent_message_id, &zero_error);
  if (zero_ok) {
    if (!sent_message_id.isEmpty()) {
      g_zero_last_seen_message_id = sent_message_id;
    }
    beginAssistantPollingWindow();
    g_status = "sent to zero";
    g_result = "waiting assistant";
  } else {
    stopAssistantPollingWindow();
    g_status = "zero failed";
    g_result = trimToWidth(zero_error, 36);
  }
  drawUiFrame();
}

void runZeroPostTest() {
  releaseAudioResources();
  if (!g_wifi_connected) {
    g_status = "wifi failed";
    g_result = "offline";
    drawUiFrame();
    return;
  }
  g_status = "zero post";
  g_result = "posting";
  drawUiFrame();
  String sent_message_id;
  String error;
  const String prompt = String("m5 zero post test ") + generateRequestId();
  if (postTextToZero(prompt, &sent_message_id, &error)) {
    g_status = "zero ok";
    g_result = sent_message_id.isEmpty() ? "posted" : trimToWidth(sent_message_id, 36);
  } else {
    g_status = "zero failed";
    g_result = trimToWidth(error, 36);
  }
  drawUiFrame();
}

void runZeroPollTest() {
  releaseAudioResources();
  if (!g_wifi_connected) {
    g_status = "wifi failed";
    g_result = "offline";
    drawUiFrame();
    return;
  }
  g_status = "zero poll";
  g_result = "requesting";
  drawUiFrame();
  String error;
  if (pollZeroAssistantMessages(true, &error)) {
    g_status = "poll ok";
    g_result = g_zero_last_seen_message_id.isEmpty() ? "no latest id"
                                                      : trimToWidth(g_zero_last_seen_message_id, 36);
  } else {
    g_status = "poll failed";
    g_result = trimToWidth(error, 36);
  }
  drawUiFrame();
}

void startCurrentTest() {
  stopAssistantPollingWindow();
  clearAssistantMessages();
  switch (g_test_mode) {
    case TestMode::Normal:
      startNormalConversationCapture();
      break;
    case TestMode::Mic:
      startRecording();
      g_status = "mic test";
      g_result = "capturing 1.5s";
      g_test_capture_deadline_ms = millis() + kMicTestDurationMs;
      drawUiFrame();
      break;
    case TestMode::Asr:
      startRecording();
      g_status = "asr test";
      g_result = "capturing 2.0s";
      g_test_capture_deadline_ms = millis() + kAsrTestDurationMs;
      drawUiFrame();
      break;
    case TestMode::AsrLive:
      startAsrLiveTest();
      break;
    case TestMode::AsrFlash:
      startAsrFlashTest();
      break;
    case TestMode::ZeroPost:
      runZeroPostTest();
      break;
    case TestMode::ZeroPoll:
      runZeroPollTest();
      break;
    case TestMode::Count:
      break;
  }
}
}  // namespace

void setup() {
  auto cfg = M5.config();
  cfg.clear_display = true;
  M5.begin(cfg);
  g_power.screen_awake = true;
  zero_buddy::wakePowerWindow(&g_power, millis(), kAwakeWindowMs);
  applyScreenBrightness();
  M5.Display.setRotation(3);
  pinMode(static_cast<int>(kBtnAWakePin), INPUT);
  pinMode(static_cast<int>(kBtnBWakePin), INPUT);
  pinMode(kLedPin, OUTPUT);
  digitalWrite(kLedPin, HIGH);
  ledcSetup(0, 2000, 8);
  ledcAttachPin(kBuzzerPin, 0);
  ledcWriteTone(0, 0);

  ZB_SERIAL_BEGIN(115200);
  delay(300);
  ZB_LOG_PRINTLN();
  ZB_LOG_PRINTLN("M5StickC Plus Doubao ASR demo booted");
  g_flash_store_ready = LittleFS.begin(true);
  ZB_LOG_PRINTF("LittleFS: %s\n", g_flash_store_ready ? "ok" : "failed");
  clearAssistantMessages();
  removeFlashFileIfExists(kZeroResponsePath);
  removeFlashFileIfExists(kAsrRecordPcmPath);

  randomSeed(esp_random());
  connectWifi();
  g_status = g_wifi_connected ? "ready" : "wifi failed";
  g_test_mode = TestMode::Normal;
  g_result = "hold BtnA to record";
  if (g_wifi_connected) {
    String poll_error;
    pollZeroAssistantMessages(true, &poll_error);
  }
  drawUiFrame();
  enterScreenSleep("boot");
}

void loop() {
  M5.update();
  if (g_buzzer_stop_ms > 0 && static_cast<long>(millis() - g_buzzer_stop_ms) >= 0) {
    ledcWriteTone(0, 0);
    g_buzzer_stop_ms = 0;
  }

  static unsigned long last_heartbeat = 0;
  if (millis() - last_heartbeat > 5000) {
    last_heartbeat = millis();
    ZB_LOG_PRINTF("alive wifi=%d ip=%s status=%s rec=%u backlog=%u drop=%u "
                  "last_rec=%u last_flash=%u last_sent=%u last_drop=%u last_chunks=%u last_conn=%u "
                  "heap=%u min=%u largest=%u\n",
                  g_wifi_connected ? 1 : 0,
                  g_ip_address.c_str(),
                  g_status.c_str(),
                  static_cast<unsigned>(g_recorded_bytes),
                  static_cast<unsigned>(g_record_backlog.size()),
                  static_cast<unsigned>(g_record_backlog.droppedBytes()),
                  static_cast<unsigned>(g_last_asr_rec_recorded_bytes),
                  static_cast<unsigned>(g_last_asr_rec_flash_bytes),
                  static_cast<unsigned>(g_last_asr_rec_sent_bytes),
                  static_cast<unsigned>(g_last_asr_rec_dropped_bytes),
                  static_cast<unsigned>(g_last_asr_rec_chunks),
                  static_cast<unsigned>(g_last_asr_rec_connect_ms),
                  static_cast<unsigned>(ESP.getFreeHeap()),
                  static_cast<unsigned>(ESP.getMinFreeHeap()),
                  static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)));
  }

  static unsigned long last_wifi_retry_ms = 0;
  if (g_test_mode == TestMode::Normal && !g_wifi_connected && !g_wifi_connecting &&
      millis() - last_wifi_retry_ms >= 8000) {
    last_wifi_retry_ms = millis();
    g_status = "wifi connecting";
    drawUiFrame();
    connectWifi();
    drawUiFrame();
  }

  if (g_test_mode == TestMode::Normal && g_waiting_for_assistant &&
      !assistantPollingWindowActive() && currentPendingAssistantMessage().isEmpty() &&
      !g_recording && !g_uploading) {
    stopAssistantPollingWindow();
    g_status = "ready";
    g_result = "hold BtnA to record";
    drawUiFrame();
  }

  if (g_test_mode == TestMode::Normal && g_wifi_connected && g_waiting_for_assistant && !g_recording &&
      !g_uploading && currentPendingAssistantMessage().isEmpty() &&
      assistantPollingWindowActive() &&
      millis() - g_last_zero_poll_ms >= kZeroPollIntervalMs) {
    g_last_zero_poll_ms = millis();
    String poll_error;
    if (!pollZeroAssistantMessages(false, &poll_error)) {
      ZB_LOG_PRINTF("ZERO poll failed: %s\n", poll_error.c_str());
    }
  }

  if (g_test_mode == TestMode::Normal) {
    const String pending = currentPendingAssistantMessage();
    if (!pending.isEmpty() && g_current_message_lines.empty()) {
      rebuildCurrentMessageLayout();
      drawUiFrame();
    }
  }

  const bool btn_a_pressed = M5.BtnA.wasPressed();
  const bool btn_a_released = M5.BtnA.wasReleased();
  const bool btn_a_held = M5.BtnA.wasHold();
  const bool btn_b_pressed = M5.BtnB.wasPressed();
  const bool btn_b_released = M5.BtnB.wasReleased();
  const bool btn_b_held = M5.BtnB.wasHold();
  const bool btn_a_down = M5.BtnA.isPressed();
  const bool btn_b_down = M5.BtnB.isPressed();
  if (btn_a_pressed || btn_a_released || btn_a_held ||
      btn_b_pressed || btn_b_released || btn_b_held) {
    wakeScreen(true, "button");
  }

  size_t bytes_read = 0;
  if (g_recording && M5.Mic.isEnabled()) {
    const bool got =
        M5.Mic.record(reinterpret_cast<int16_t*>(g_audio_buffer), kMicRecordSamplesPerRead, kSampleRate);
    bytes_read = got ? (kMicRecordSamplesPerRead * sizeof(int16_t)) : 0;
  }

  if (btn_b_pressed) {
    g_btn_b_hold_seen = false;
  }

  if (btn_b_held && !g_suppress_wake_short_action && !g_btn_b_hold_seen && !g_recording && !g_uploading) {
    g_btn_b_hold_seen = true;
    nextTestMode();
    g_status = (g_test_mode == TestMode::Normal) ? "ready" : "test bench";
    g_result = (g_test_mode == TestMode::Normal) ? String("hold BtnA to record")
                                                 : String("mode: ") + currentTestModeLabel();
    drawUiFrame();
  }

  if (btn_b_released && !g_recording && !g_uploading) {
    if (g_suppress_wake_short_action) {
      g_btn_b_hold_seen = false;
    } else if (g_btn_b_hold_seen) {
      g_btn_b_hold_seen = false;
    } else {
      cycleScreenBrightness();
      drawUiFrame();
    }
  }

  if (btn_a_pressed && !g_recording && !g_uploading) {
    if (g_test_mode != TestMode::Normal) {
      startCurrentTest();
    }
  }

  if (g_test_mode == TestMode::Normal && btn_a_held && !g_recording && !g_uploading) {
    g_suppress_wake_short_action = false;
    startCurrentTest();
  }

  if (g_test_mode == TestMode::Normal && btn_a_released && !g_recording && !g_uploading &&
      !currentPendingAssistantMessage().isEmpty()) {
    if (!g_suppress_wake_short_action) {
      handleAssistantAdvance(false);
      drawUiFrame();
    }
  }

  if (!btn_a_down && !btn_b_down) {
    g_suppress_wake_short_action = false;
  }

  if (g_recording && bytes_read > 0) {
    if (g_recorded_bytes == 0) {
      g_first_audio_ms = millis();
      ZB_LOG_PRINTF("REC first audio delay=%u ms\n",
                    static_cast<unsigned>(g_first_audio_ms - g_record_start_ms));
    }
    g_last_audio_ms = millis();
    accumulateRawMicStats(g_audio_buffer, bytes_read);
    g_recorded_bytes += bytes_read;
    g_record_backlog.pushNewest(g_audio_buffer, bytes_read);
    if (g_record_to_flash && !writeRecordFlash(g_audio_buffer, bytes_read)) {
      ZB_LOG_PRINTLN("record flash write failed");
    }
    const bool async_asr_mode = usesAsyncAsrCaptureMode();
    const bool async_live_asr_mode = usesAsyncLiveAsrMode();
    const bool sync_asr_mode = g_test_mode == TestMode::Asr;
    if (async_asr_mode && g_asr_connect_state == AsrConnectState::Failed) {
      ZB_LOG_PRINTF("ASR async connect failed while recording: %s\n", g_asr_connect_error);
      g_recording = false;
      g_uploading = false;
      g_status = "failed";
      g_result = trimToWidth(String(g_asr_connect_error), 30);
      releaseAudioResources();
      drawUiFrame();
    }
    if (async_live_asr_mode && g_recording && g_asr_connect_state == AsrConnectState::Ready &&
        g_asr_session.active) {
      String send_error;
      if (!pumpBufferedAudioToAsr(false, &send_error)) {
        ZB_LOG_PRINTF("ASR live chunk send failed: %s\n", send_error.c_str());
        g_recording = false;
        g_uploading = false;
        g_status = "failed";
        g_result = trimToWidth(send_error, 30);
        closeStreamAsrSession();
        drawUiFrame();
      }
    }
    if (sync_asr_mode && !g_asr_session.active && !g_asr_connect_started &&
        millis() - g_record_start_ms >= kAsrConnectDelayMs) {
      String start_error;
      if (!ensureAsrSessionStarted(&start_error)) {
        ZB_LOG_PRINTF("ASR delayed start failed: %s\n", start_error.c_str());
        g_recording = false;
        g_status = "failed";
        g_result = trimToWidth(start_error, 30);
        drawUiFrame();
      }
    }
    if (sync_asr_mode && g_recording && g_asr_session.active) {
      String send_error;
      if (!pumpBufferedAudioToAsr(false, &send_error)) {
        ZB_LOG_PRINTF("ASR chunk send failed: %s\n", send_error.c_str());
        g_recording = false;
        g_uploading = false;
        g_status = "failed";
        g_result = trimToWidth(send_error, 30);
        closeStreamAsrSession();
        drawUiFrame();
      }
    }
  }

  if (g_recording && (g_test_mode == TestMode::Mic || g_test_mode == TestMode::Asr) &&
      static_cast<long>(millis() - g_test_capture_deadline_ms) >= 0) {
    if (g_test_mode == TestMode::Mic) {
      finishMicOnlyTest();
    } else if (g_test_mode == TestMode::Asr) {
      finishAsrOnlyTest();
    }
  }

  if (g_recording && g_test_mode == TestMode::Normal && btn_a_released) {
    finishNormalConversation();
  }

  if (g_recording && g_test_mode == TestMode::AsrLive && btn_a_released) {
    finishAsrLiveTest();
  }

  if (g_recording && g_test_mode == TestMode::AsrFlash && btn_a_released) {
    finishAsrFlashTest();
  }

  serviceAssistantLedBlink();
  if (zero_buddy::shouldAutoSleepScreen(g_power, g_recording || g_uploading, millis())) {
    enterScreenSleep("idle timeout");
  }
  runLightSleepIfIdle();
}
