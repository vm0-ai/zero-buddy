#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace zero_buddy {

struct AsrPayloadResult {
  bool ok = false;
  bool final = false;
  std::string text;
  std::string error;
};

struct ZeroMessagesResult {
  bool found_any = false;
  std::string newest_message_id;
  std::vector<std::string> assistant_messages;
};

struct ZeroMessage {
  bool ok = false;
  std::string id;
  std::string role;
  std::string content;
};

struct AssistantQueueManifest {
  bool ok = false;
  size_t count = 0;
  size_t index = 0;
  bool waiting = false;
  uint8_t next_delay_index = 0;
  std::string last_seen_message_id;
};

enum class AsrCaptureStrategy : uint8_t {
  LiveBacklog,
  FlashReplay,
};

struct AsrCaptureMetrics {
  size_t recorded_bytes = 0;
  size_t flash_bytes = 0;
  size_t sent_bytes = 0;
  size_t dropped_bytes = 0;
  bool flash_failed = false;
};

struct AsrCaptureAssessment {
  bool input_complete = false;
  bool upload_complete = false;
  std::string reason;

  bool ok() const {
    return input_complete && upload_complete;
  }
};

enum class NotificationBlinkEvent : uint8_t {
  Tick,
  AssistantArrived,
  UserAction,
};

struct NotificationBlinkState {
  bool pending = false;
  bool led_on = false;
  uint32_t last_pulse_ms = 0;
  uint32_t led_off_at_ms = 0;
};

struct NotificationBlinkResult {
  bool pending = false;
  bool led_on = false;
  bool led_changed = false;
};

struct PowerWindowState {
  bool screen_awake = false;
  uint32_t awake_until_ms = 0;
};

enum class BuddyMode : uint8_t {
  DeepSleep,
  Home,
  Recording,
  Recognizing,
  SendingZero,
  MessageSent,
  Polling,
};

enum class BuddyEvent : uint8_t {
  ShortPress,
  LongPress,
  RecordingStopped,
  RecordingFailed,
  AsrSucceeded,
  AsrFailed,
  ZeroSucceeded,
  ZeroFailed,
  MessageSentElapsed,
  PollTimerWake,
  PollTimerNoMessage,
  PollTimerMessage,
  UnreadConsumed,
  IdleTimeout,
};

struct BuddyState {
  BuddyMode mode = BuddyMode::DeepSleep;
  bool has_unread_assistant = false;
  uint8_t poll_backoff_index = 0;
};

struct BuddyActions {
  bool wake_screen = false;
  bool start_recording = false;
  bool start_recognizing = false;
  bool send_zero = false;
  bool show_message_sent = false;
  bool poll_now = false;
  bool schedule_poll = false;
  bool reset_poll_backoff = false;
  bool stop_polling = false;
  bool deep_sleep = false;
  uint32_t next_poll_delay_ms = 0;
};

struct BuddyTransition {
  BuddyState state;
  BuddyActions actions;
};

class FixedByteQueue {
 public:
  FixedByteQueue(uint8_t* storage, size_t capacity);

  size_t pushNewest(const uint8_t* data, size_t data_len);
  size_t pop(uint8_t* out, size_t max_len);
  void clear();

  size_t size() const;
  size_t capacity() const;
  size_t droppedBytes() const;

 private:
  uint8_t* storage_;
  size_t capacity_;
  size_t head_;
  size_t size_;
  size_t dropped_bytes_;
};

std::string trim(std::string value);
std::string trimToWidth(std::string text, size_t max_len);
std::string extractJsonString(std::string body, std::string key);
std::string extractNestedResultText(std::string body);
std::string extractFirstResultArrayText(std::string body);
std::string extractFirstUtteranceText(std::string body);
std::string preprocessAssistantForDisplay(std::string text);
std::string decodeChunkedBody(std::string body);
ZeroMessage parseZeroMessageObject(std::string object);
ZeroMessagesResult parseZeroMessagesResponse(std::string body);
std::string buildAssistantQueueManifest(size_t count,
                                        size_t index,
                                        bool waiting,
                                        uint8_t next_delay_index,
                                        std::string last_seen_message_id);
AssistantQueueManifest parseAssistantQueueManifest(std::string body, size_t max_count);
void appendU32(std::vector<uint8_t>& out, uint32_t value);
std::vector<uint8_t> buildStreamAudioPacket(const uint8_t* data, size_t data_len, bool is_final);
AsrPayloadResult parseAsrServerPayload(const uint8_t* frame_payload, size_t frame_payload_size);
AsrCaptureAssessment assessAsrCapture(AsrCaptureStrategy strategy,
                                      const AsrCaptureMetrics& metrics);
NotificationBlinkResult updateNotificationBlink(NotificationBlinkState* state,
                                                NotificationBlinkEvent event,
                                                uint32_t now_ms,
                                                uint32_t interval_ms,
                                                uint32_t pulse_ms);
uint8_t nextBrightnessLevel(uint8_t current_level, uint8_t level_count);
uint8_t batteryFillPixels(int32_t level_percent, uint8_t max_pixels);
void wakePowerWindow(PowerWindowState* state, uint32_t now_ms, uint32_t duration_ms);
void sleepPowerWindow(PowerWindowState* state);
bool shouldAutoSleepScreen(const PowerWindowState& state, bool busy, uint32_t now_ms);
BuddyTransition applyBuddyEvent(BuddyState state,
                                BuddyEvent event,
                                const uint32_t* poll_backoff_ms,
                                size_t poll_backoff_count);

}  // namespace zero_buddy
