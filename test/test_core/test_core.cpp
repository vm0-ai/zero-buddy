#include <unity.h>

#include <cstdio>
#include <string>
#include <vector>

#include "zero_buddy_core.h"

namespace {

std::vector<uint8_t> makeAsrServerFrame(const std::string& json) {
  std::vector<uint8_t> frame = {0x11, 0x93, 0x10, 0x00, 0x00, 0x00, 0x00, 0x01};
  zero_buddy::appendU32(frame, static_cast<uint32_t>(json.size()));
  frame.insert(frame.end(), json.begin(), json.end());
  return frame;
}

void test_memory_safe_conversation_pipeline() {
  uint8_t storage[8] = {0};
  zero_buddy::FixedByteQueue backlog(storage, sizeof(storage));

  const uint8_t first_audio[] = {1, 2, 3, 4, 5};
  TEST_ASSERT_EQUAL_UINT(5, backlog.pushNewest(first_audio, sizeof(first_audio)));
  uint8_t popped[16] = {0};
  TEST_ASSERT_EQUAL_UINT(2, backlog.pop(popped, 2));
  TEST_ASSERT_EQUAL_UINT8(1, popped[0]);
  TEST_ASSERT_EQUAL_UINT8(2, popped[1]);

  const uint8_t later_audio[] = {6, 7, 8, 9, 10, 11, 12};
  TEST_ASSERT_EQUAL_UINT(7, backlog.pushNewest(later_audio, sizeof(later_audio)));
  TEST_ASSERT_EQUAL_UINT(2, backlog.droppedBytes());
  TEST_ASSERT_EQUAL_UINT(8, backlog.pop(popped, sizeof(popped)));
  const uint8_t expected_audio[] = {5, 6, 7, 8, 9, 10, 11, 12};
  TEST_ASSERT_EQUAL_MEMORY(expected_audio, popped, sizeof(expected_audio));

  const auto audio_packet = zero_buddy::buildStreamAudioPacket(popped, sizeof(expected_audio), false);
  TEST_ASSERT_EQUAL_UINT8(0x11, audio_packet[0]);
  TEST_ASSERT_EQUAL_UINT8(0x20, audio_packet[1]);
  TEST_ASSERT_EQUAL_UINT8(sizeof(expected_audio), audio_packet[7]);

  const auto final_packet = zero_buddy::buildStreamAudioPacket(nullptr, 0, true);
  const uint8_t expected_final[] = {0x11, 0x22, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  TEST_ASSERT_EQUAL_MEMORY(expected_final, final_packet.data(), sizeof(expected_final));

  const auto asr_frame = makeAsrServerFrame(R"({"result":[{"text":"把线上 release 合进去"}]})");
  const auto asr = zero_buddy::parseAsrServerPayload(asr_frame.data(), asr_frame.size());
  TEST_ASSERT_TRUE(asr.ok);
  TEST_ASSERT_TRUE(asr.final);
  TEST_ASSERT_EQUAL_STRING("把线上 release 合进去", asr.text.c_str());

  const std::string zero_json =
      R"({"messages":[)"
      R"({"id":"user-1","role":"user","content":"把线上 release 合进去","createdAt":"2026-04-24T00:00:00Z"},)"
      R"({"id":"assistant-1","role":"assistant","content":"# 结果\n**已处理** [PR](https://x.test) 🚀","createdAt":"2026-04-24T00:00:01Z"}]})";
  char rest_hex[16];
  snprintf(rest_hex, sizeof(rest_hex), "%x", static_cast<unsigned>(zero_json.size() - 16));
  const std::string chunked = "10\r\n" + zero_json.substr(0, 16) + "\r\n" +
                              std::string(rest_hex) + "\r\n" +
                              zero_json.substr(16) + "\r\n0\r\n\r\n";
  const auto decoded = zero_buddy::decodeChunkedBody(chunked);
  const auto messages = zero_buddy::parseZeroMessagesResponse(decoded);
  TEST_ASSERT_TRUE(messages.found_any);
  TEST_ASSERT_EQUAL_STRING("assistant-1", messages.newest_message_id.c_str());
  TEST_ASSERT_EQUAL_UINT(1, messages.assistant_messages.size());
  TEST_ASSERT_TRUE(messages.assistant_messages[0].find("结果") != std::string::npos);
  TEST_ASSERT_TRUE(messages.assistant_messages[0].find("已处理") != std::string::npos);
  TEST_ASSERT_TRUE(messages.assistant_messages[0].find("🚀") == std::string::npos);

  const auto single = zero_buddy::parseZeroMessageObject(
      R"({"id":"assistant-2","role":"assistant","content":"**下一条**","createdAt":"x"})");
  TEST_ASSERT_TRUE(single.ok);
  TEST_ASSERT_EQUAL_STRING("assistant", single.role.c_str());
  TEST_ASSERT_EQUAL_STRING("**下一条**", single.content.c_str());
}

void test_asr_capture_strategy_assessment() {
  zero_buddy::AsrCaptureMetrics live_ok;
  live_ok.recorded_bytes = 64000;
  live_ok.sent_bytes = 64000;
  const auto live_complete =
      zero_buddy::assessAsrCapture(zero_buddy::AsrCaptureStrategy::LiveBacklog, live_ok);
  TEST_ASSERT_TRUE(live_complete.ok());
  TEST_ASSERT_EQUAL_STRING("complete", live_complete.reason.c_str());

  zero_buddy::AsrCaptureMetrics live_dropped = live_ok;
  live_dropped.sent_bytes = 65536;
  live_dropped.dropped_bytes = 4096;
  const auto live_lossy =
      zero_buddy::assessAsrCapture(zero_buddy::AsrCaptureStrategy::LiveBacklog, live_dropped);
  TEST_ASSERT_FALSE(live_lossy.ok());
  TEST_ASSERT_FALSE(live_lossy.input_complete);
  TEST_ASSERT_EQUAL_STRING("backlog dropped", live_lossy.reason.c_str());

  zero_buddy::AsrCaptureMetrics flash_ok;
  flash_ok.recorded_bytes = 120320;
  flash_ok.flash_bytes = 120320;
  flash_ok.sent_bytes = 120320;
  flash_ok.dropped_bytes = 54784;
  const auto flash_complete =
      zero_buddy::assessAsrCapture(zero_buddy::AsrCaptureStrategy::FlashReplay, flash_ok);
  TEST_ASSERT_TRUE(flash_complete.ok());
  TEST_ASSERT_EQUAL_STRING("complete", flash_complete.reason.c_str());

  zero_buddy::AsrCaptureMetrics flash_mismatch = flash_ok;
  flash_mismatch.flash_bytes = 119808;
  const auto flash_lossy =
      zero_buddy::assessAsrCapture(zero_buddy::AsrCaptureStrategy::FlashReplay, flash_mismatch);
  TEST_ASSERT_FALSE(flash_lossy.ok());
  TEST_ASSERT_EQUAL_STRING("flash mismatch", flash_lossy.reason.c_str());
}

void test_notification_blink_state_machine() {
  zero_buddy::NotificationBlinkState state;
  auto result = zero_buddy::updateNotificationBlink(
      &state, zero_buddy::NotificationBlinkEvent::Tick, 1000, 3000, 120);
  TEST_ASSERT_FALSE(result.pending);
  TEST_ASSERT_FALSE(result.led_on);
  TEST_ASSERT_FALSE(result.led_changed);

  result = zero_buddy::updateNotificationBlink(
      &state, zero_buddy::NotificationBlinkEvent::AssistantArrived, 2000, 3000, 120);
  TEST_ASSERT_TRUE(result.pending);
  TEST_ASSERT_TRUE(result.led_on);
  TEST_ASSERT_TRUE(result.led_changed);

  result = zero_buddy::updateNotificationBlink(
      &state, zero_buddy::NotificationBlinkEvent::Tick, 2119, 3000, 120);
  TEST_ASSERT_TRUE(result.pending);
  TEST_ASSERT_TRUE(result.led_on);
  TEST_ASSERT_FALSE(result.led_changed);

  result = zero_buddy::updateNotificationBlink(
      &state, zero_buddy::NotificationBlinkEvent::Tick, 2120, 3000, 120);
  TEST_ASSERT_TRUE(result.pending);
  TEST_ASSERT_FALSE(result.led_on);
  TEST_ASSERT_TRUE(result.led_changed);

  result = zero_buddy::updateNotificationBlink(
      &state, zero_buddy::NotificationBlinkEvent::Tick, 4999, 3000, 120);
  TEST_ASSERT_TRUE(result.pending);
  TEST_ASSERT_FALSE(result.led_on);
  TEST_ASSERT_FALSE(result.led_changed);

  result = zero_buddy::updateNotificationBlink(
      &state, zero_buddy::NotificationBlinkEvent::Tick, 5000, 3000, 120);
  TEST_ASSERT_TRUE(result.pending);
  TEST_ASSERT_TRUE(result.led_on);
  TEST_ASSERT_TRUE(result.led_changed);

  result = zero_buddy::updateNotificationBlink(
      &state, zero_buddy::NotificationBlinkEvent::UserAction, 5050, 3000, 120);
  TEST_ASSERT_FALSE(result.pending);
  TEST_ASSERT_FALSE(result.led_on);
  TEST_ASSERT_TRUE(result.led_changed);

  result = zero_buddy::updateNotificationBlink(
      &state, zero_buddy::NotificationBlinkEvent::Tick, 9000, 3000, 120);
  TEST_ASSERT_FALSE(result.pending);
  TEST_ASSERT_FALSE(result.led_on);
  TEST_ASSERT_FALSE(result.led_changed);
}

void test_brightness_level_cycles() {
  TEST_ASSERT_EQUAL_UINT8(1, zero_buddy::nextBrightnessLevel(0, 5));
  TEST_ASSERT_EQUAL_UINT8(4, zero_buddy::nextBrightnessLevel(3, 5));
  TEST_ASSERT_EQUAL_UINT8(0, zero_buddy::nextBrightnessLevel(4, 5));
  TEST_ASSERT_EQUAL_UINT8(0, zero_buddy::nextBrightnessLevel(5, 5));
  TEST_ASSERT_EQUAL_UINT8(0, zero_buddy::nextBrightnessLevel(0, 0));
}

void test_power_window_state_machine() {
  zero_buddy::PowerWindowState state;
  TEST_ASSERT_FALSE(state.screen_awake);

  zero_buddy::wakePowerWindow(&state, 1000, 600000);
  TEST_ASSERT_TRUE(state.screen_awake);
  TEST_ASSERT_EQUAL_UINT32(601000, state.awake_until_ms);
  TEST_ASSERT_FALSE(zero_buddy::shouldAutoSleepScreen(state, false, 600999));
  TEST_ASSERT_TRUE(zero_buddy::shouldAutoSleepScreen(state, false, 601000));
  TEST_ASSERT_FALSE(zero_buddy::shouldAutoSleepScreen(state, true, 601000));

  zero_buddy::startAssistantPollWindow(&state, 2000, 600000);
  TEST_ASSERT_TRUE(zero_buddy::assistantPollWindowActive(state, 601999));
  TEST_ASSERT_FALSE(zero_buddy::assistantPollWindowActive(state, 602000));

  zero_buddy::stopAssistantPollWindow(&state);
  TEST_ASSERT_FALSE(zero_buddy::assistantPollWindowActive(state, 3000));

  zero_buddy::sleepPowerWindow(&state);
  TEST_ASSERT_FALSE(state.screen_awake);
}

}  // namespace

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_memory_safe_conversation_pipeline);
  RUN_TEST(test_asr_capture_strategy_assessment);
  RUN_TEST(test_notification_blink_state_machine);
  RUN_TEST(test_brightness_level_cycles);
  RUN_TEST(test_power_window_state_machine);
  return UNITY_END();
}
