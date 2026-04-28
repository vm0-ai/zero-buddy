#include <unity.h>

#include <functional>
#include <string>
#include <vector>

#include "zero_buddy_modes.h"

namespace {

using zero_buddy::modes::CheckAssistantMessageMode;
using zero_buddy::modes::CheckAssistantMessageOps;
using zero_buddy::modes::DeepSleepMode;
using zero_buddy::modes::DeepSleepOps;
using zero_buddy::modes::ModeRunError;
using zero_buddy::modes::ModeRunResult;
using zero_buddy::modes::ModeRunStatus;
using zero_buddy::modes::RecordingMode;
using zero_buddy::modes::RecordingOps;
using zero_buddy::state::AssistantCheckResult;
using zero_buddy::state::GlobalState;

std::string joinCalls(const std::vector<std::string>& calls) {
  std::string out;
  for (size_t i = 0; i < calls.size(); ++i) {
    if (i > 0) {
      out += ",";
    }
    out += calls[i];
  }
  return out;
}

void assertResult(ModeRunStatus status, ModeRunError error, const ModeRunResult& result) {
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(status), static_cast<uint8_t>(result.status));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(error), static_cast<uint8_t>(result.error));
}

struct FakeCheckOps : CheckAssistantMessageOps {
  std::vector<std::string> calls;
  bool wifi_ok = true;
  bool poll_ok = true;
  bool append_ok = true;
  AssistantCheckResult poll_result;
  std::function<void()> on_cpu;
  std::function<void()> on_wifi;
  std::function<void()> on_poll;
  std::function<void()> on_write;

  void setCpuForNetwork() override {
    calls.push_back("cpu-network");
    if (on_cpu) {
      on_cpu();
    }
  }

  bool ensureWifiConnected() override {
    calls.push_back("wifi");
    if (on_wifi) {
      on_wifi();
    }
    return wifi_ok;
  }

  bool pollAssistantMessages(const std::string& since_id,
                             AssistantCheckResult* result_out) override {
    calls.push_back("poll:" + since_id);
    if (on_poll) {
      on_poll();
    }
    if (result_out != nullptr) {
      *result_out = poll_result;
    }
    return poll_ok;
  }

  bool appendAssistantMessages(const AssistantCheckResult&) override {
    calls.push_back("append-assistant-message");
    if (on_write) {
      on_write();
    }
    return append_ok;
  }

  void cancelNetwork() override {
    calls.push_back("cancel-network");
  }

  void closeFiles() override {
    calls.push_back("close-files");
  }

  void cleanupTempFiles() override {
    calls.push_back("cleanup-temp");
  }
};

struct FakeDeepSleepOps : DeepSleepOps {
  std::vector<std::string> calls;
  uint32_t rtc_delay_ms = 0;
  bool led = true;
  std::function<void()> on_rtc;
  std::function<void()> on_button;
  std::function<void()> on_screen;
  std::function<void()> on_disconnect;

  void configureRtcWake(uint32_t delay_ms) override {
    rtc_delay_ms = delay_ms;
    calls.push_back("rtc:" + std::to_string(delay_ms));
    if (on_rtc) {
      on_rtc();
    }
  }

  void configureBtnAWake() override {
    calls.push_back("btn-wake");
    if (on_button) {
      on_button();
    }
  }

  void screenOff() override {
    calls.push_back("screen-off");
    if (on_screen) {
      on_screen();
    }
  }

  void disconnectWifi() override {
    calls.push_back("wifi-disconnect");
    if (on_disconnect) {
      on_disconnect();
    }
  }

  void enterCpuHibernate() override {
    calls.push_back("hibernate");
  }

  void cancelRtcWake() override {
    calls.push_back("cancel-rtc");
  }
};

struct FakeRecordingOps : RecordingOps {
  std::vector<std::string> calls;
  bool clear_ok = true;
  bool record_ok = true;
  bool wifi_ok = true;
  bool asr_ok = true;
  bool send_ok = true;
  std::string asr_text = "hello";
  std::string sent_message_id = "user-2";
  std::function<void()> on_screen;
  std::function<void()> on_cpu;
  std::function<void()> on_cancel_rtc;
  std::function<void()> on_clear;
  std::function<void()> on_record;
  std::function<void()> on_wifi;
  std::function<void()> on_asr;
  std::function<void()> on_send;

  void screenOn() override {
    calls.push_back("screen-on");
    if (on_screen) {
      on_screen();
    }
  }

  void setCpuForRecording() override {
    calls.push_back("cpu-recording");
    if (on_cpu) {
      on_cpu();
    }
  }

  void cancelRtcWake() override {
    calls.push_back("cancel-rtc");
    if (on_cancel_rtc) {
      on_cancel_rtc();
    }
  }

  bool clearAssistantMessages() override {
    calls.push_back("clear-assistant-message");
    if (on_clear) {
      on_clear();
    }
    return clear_ok;
  }

  bool recordVoiceToFile() override {
    calls.push_back("record-voice");
    if (on_record) {
      on_record();
    }
    return record_ok;
  }

  bool ensureWifiConnected() override {
    calls.push_back("wifi");
    if (on_wifi) {
      on_wifi();
    }
    return wifi_ok;
  }

  bool voiceToText(std::string* text_out) override {
    calls.push_back("voice-to-text");
    if (on_asr) {
      on_asr();
    }
    if (text_out != nullptr) {
      *text_out = asr_text;
    }
    return asr_ok;
  }

  void deleteVoiceFile() override {
    calls.push_back("delete-voice");
  }

  bool sendTextMessage(const std::string& text, std::string* user_message_id_out) override {
    calls.push_back("send:" + text);
    if (on_send) {
      on_send();
    }
    if (user_message_id_out != nullptr) {
      *user_message_id_out = sent_message_id;
    }
    return send_ok;
  }

  void stopVoiceRecording() override {
    calls.push_back("stop-recording");
  }

  void cancelVoiceToText() override {
    calls.push_back("cancel-asr");
  }

  void cancelTextMessageSend() override {
    calls.push_back("cancel-send");
  }

  void closeFiles() override {
    calls.push_back("close-files");
  }
};

void test_check_assistant_main_commits_new_messages() {
  auto state = zero_buddy::state::makeDefaultGlobalState();
  zero_buddy::state::setLastMessageId(&state, "user-1");
  state.checkDelayMs = 4UL * 60UL * 1000UL;

  FakeCheckOps ops;
  ops.poll_result.hasNewAssistantMessages = true;
  ops.poll_result.assistantMessages = {"first", "second"};
  ops.poll_result.newestMessageId = "assistant-2";
  ops.on_write = [&]() {
    zero_buddy::state::setHasAssistantMessage(&state, true);
  };

  CheckAssistantMessageMode mode(&state, &ops);
  assertResult(ModeRunStatus::Completed, ModeRunError::None, mode.main());

  TEST_ASSERT_EQUAL_STRING("cpu-network,wifi,poll:user-1,append-assistant-message",
                           joinCalls(ops.calls).c_str());
  TEST_ASSERT_EQUAL_STRING("assistant-2", zero_buddy::state::copyLastMessageId(state).c_str());
  TEST_ASSERT_TRUE(zero_buddy::state::hasAssistantMessage(state));
  TEST_ASSERT_EQUAL_UINT32(zero_buddy::state::kInitialCheckDelayMs, state.checkDelayMs);
}

void test_check_assistant_skips_polling_without_last_message_id() {
  auto state = zero_buddy::state::makeDefaultGlobalState();

  FakeCheckOps ops;
  CheckAssistantMessageMode mode(&state, &ops);
  assertResult(ModeRunStatus::Completed, ModeRunError::None, mode.main());

  TEST_ASSERT_EQUAL_STRING("", joinCalls(ops.calls).c_str());
  TEST_ASSERT_EQUAL_UINT32(60UL * 1000UL, state.checkDelayMs);
  TEST_ASSERT_FALSE(zero_buddy::state::hasAssistantMessage(state));
}

void test_check_assistant_preserves_existing_message_presence() {
  auto state = zero_buddy::state::makeDefaultGlobalState();
  zero_buddy::state::setLastMessageId(&state, "assistant-1");
  zero_buddy::state::setHasAssistantMessage(&state, true);
  state.checkDelayMs = 4UL * 60UL * 1000UL;

  FakeCheckOps ops;
  ops.poll_result.hasNewAssistantMessages = true;
  ops.poll_result.assistantMessages = {"next"};
  ops.poll_result.newestMessageId = "assistant-3";

  CheckAssistantMessageMode mode(&state, &ops);
  assertResult(ModeRunStatus::Completed, ModeRunError::None, mode.main());

  TEST_ASSERT_EQUAL_STRING("cpu-network,wifi,poll:assistant-1,append-assistant-message",
                           joinCalls(ops.calls).c_str());
  TEST_ASSERT_EQUAL_STRING("assistant-3", zero_buddy::state::copyLastMessageId(state).c_str());
  TEST_ASSERT_TRUE(zero_buddy::state::hasAssistantMessage(state));
  TEST_ASSERT_EQUAL_UINT32(zero_buddy::state::kInitialCheckDelayMs, state.checkDelayMs);
}

void test_check_assistant_main_without_new_messages_advances_backoff() {
  auto state = zero_buddy::state::makeDefaultGlobalState();
  zero_buddy::state::setLastMessageId(&state, "user-1");
  state.checkDelayMs = 60UL * 1000UL;

  FakeCheckOps ops;
  ops.poll_result.hasNewAssistantMessages = false;
  ops.poll_result.newestMessageId = "user-1";

  CheckAssistantMessageMode mode(&state, &ops);
  assertResult(ModeRunStatus::Completed, ModeRunError::None, mode.main());

  TEST_ASSERT_EQUAL_STRING("cpu-network,wifi,poll:user-1", joinCalls(ops.calls).c_str());
  TEST_ASSERT_EQUAL_UINT32(120UL * 1000UL, state.checkDelayMs);
  TEST_ASSERT_EQUAL_STRING("user-1", zero_buddy::state::copyLastMessageId(state).c_str());
}

void test_check_assistant_storage_failure_does_not_commit_cursor_or_queue() {
  auto state = zero_buddy::state::makeDefaultGlobalState();
  zero_buddy::state::setLastMessageId(&state, "old");
  state.checkDelayMs = 60UL * 1000UL;

  FakeCheckOps ops;
  ops.append_ok = false;
  ops.poll_result.hasNewAssistantMessages = true;
  ops.poll_result.assistantMessages = {"assistant"};
  ops.poll_result.newestMessageId = "assistant-3";

  CheckAssistantMessageMode mode(&state, &ops);
  assertResult(ModeRunStatus::Failed, ModeRunError::AssistantStorageFailed, mode.main());

  TEST_ASSERT_EQUAL_STRING("cpu-network,wifi,poll:old,append-assistant-message,cleanup-temp",
                           joinCalls(ops.calls).c_str());
  TEST_ASSERT_EQUAL_STRING("old", zero_buddy::state::copyLastMessageId(state).c_str());
  TEST_ASSERT_FALSE(zero_buddy::state::hasAssistantMessage(state));
  TEST_ASSERT_EQUAL_UINT32(120UL * 1000UL, state.checkDelayMs);
}

void test_check_assistant_abort_during_poll_cleans_without_commit() {
  auto state = zero_buddy::state::makeDefaultGlobalState();
  zero_buddy::state::setLastMessageId(&state, "old");
  state.checkDelayMs = 60UL * 1000UL;

  FakeCheckOps ops;
  ops.poll_result.hasNewAssistantMessages = true;
  ops.poll_result.assistantMessages = {"assistant"};
  ops.poll_result.newestMessageId = "assistant-1";

  CheckAssistantMessageMode mode(&state, &ops);
  ops.on_poll = [&]() {
    mode.abort("btn_a_long_press");
  };

  assertResult(ModeRunStatus::Aborted, ModeRunError::None, mode.main());
  TEST_ASSERT_TRUE(mode.abortRequested());
  TEST_ASSERT_EQUAL_STRING("btn_a_long_press", mode.abortReason());
  TEST_ASSERT_EQUAL_STRING("cpu-network,wifi,poll:old,cancel-network,close-files,cleanup-temp",
                           joinCalls(ops.calls).c_str());
  TEST_ASSERT_EQUAL_STRING("old", zero_buddy::state::copyLastMessageId(state).c_str());
  TEST_ASSERT_EQUAL_UINT32(60UL * 1000UL, state.checkDelayMs);
}

void test_deep_sleep_main_runs_hibernate_last() {
  auto state = zero_buddy::state::makeDefaultGlobalState();
  state.checkDelayMs = 5UL * 60UL * 1000UL;
  zero_buddy::state::setHasAssistantMessage(&state, true);

  FakeDeepSleepOps ops;
  DeepSleepMode mode(&state, &ops);
  assertResult(ModeRunStatus::Completed, ModeRunError::None, mode.main());

  TEST_ASSERT_EQUAL_UINT32(5UL * 60UL * 1000UL, ops.rtc_delay_ms);
  TEST_ASSERT_FALSE(ops.calls.empty());
  TEST_ASSERT_EQUAL_STRING("hibernate", ops.calls.back().c_str());
  TEST_ASSERT_TRUE(ops.led);
  TEST_ASSERT_EQUAL_STRING("rtc:300000,btn-wake,screen-off,wifi-disconnect,hibernate",
                           joinCalls(ops.calls).c_str());
}

void test_deep_sleep_main_never_changes_led() {
  auto state = zero_buddy::state::makeDefaultGlobalState();

  FakeDeepSleepOps ops;
  ops.led = true;
  DeepSleepMode mode(&state, &ops);
  assertResult(ModeRunStatus::Completed, ModeRunError::None, mode.main());

  TEST_ASSERT_TRUE(ops.led);
  TEST_ASSERT_EQUAL_STRING("rtc:30000,btn-wake,screen-off,wifi-disconnect,hibernate",
                           joinCalls(ops.calls).c_str());
}

void test_deep_sleep_abort_after_screen_off_cancels_timer_without_touching_led() {
  auto state = zero_buddy::state::makeDefaultGlobalState();
  zero_buddy::state::setHasAssistantMessage(&state, true);

  FakeDeepSleepOps ops;
  DeepSleepMode mode(&state, &ops);
  ops.on_screen = [&]() {
    mode.abort("btn_a_long_press");
  };

  assertResult(ModeRunStatus::Aborted, ModeRunError::None, mode.main());
  TEST_ASSERT_TRUE(mode.abortRequested());
  TEST_ASSERT_TRUE(ops.led);
  TEST_ASSERT_EQUAL_STRING("rtc:30000,btn-wake,screen-off,cancel-rtc",
                           joinCalls(ops.calls).c_str());

  mode.abort("again");
  TEST_ASSERT_EQUAL_STRING("rtc:30000,btn-wake,screen-off,cancel-rtc",
                           joinCalls(ops.calls).c_str());
}

void test_recording_main_sends_message_and_commits_cursor() {
  auto state = zero_buddy::state::makeDefaultGlobalState();
  zero_buddy::state::setLastMessageId(&state, "old");
  zero_buddy::state::setHasAssistantMessage(&state, true);
  state.checkDelayMs = 4UL * 60UL * 1000UL;

  FakeRecordingOps ops;
  ops.asr_text = "turn on the light";
  ops.sent_message_id = "user-2";
  ops.on_clear = [&]() {
    zero_buddy::state::setHasAssistantMessage(&state, false);
  };

  RecordingMode mode(&state, &ops);
  assertResult(ModeRunStatus::Completed, ModeRunError::None, mode.main());

  TEST_ASSERT_EQUAL_STRING(
      "screen-on,cpu-recording,cancel-rtc,clear-assistant-message,record-voice,wifi,"
      "voice-to-text,delete-voice,send:turn on the light",
      joinCalls(ops.calls).c_str());
  TEST_ASSERT_EQUAL_STRING("user-2", zero_buddy::state::copyLastMessageId(state).c_str());
  TEST_ASSERT_EQUAL_UINT32(zero_buddy::state::kInitialCheckDelayMs, state.checkDelayMs);
  TEST_ASSERT_FALSE(zero_buddy::state::hasAssistantMessage(state));
}

void test_recording_voice_to_text_failure_deletes_voice_without_cursor_commit() {
  auto state = zero_buddy::state::makeDefaultGlobalState();
  zero_buddy::state::setLastMessageId(&state, "old");
  state.checkDelayMs = 4UL * 60UL * 1000UL;

  FakeRecordingOps ops;
  ops.asr_ok = false;

  RecordingMode mode(&state, &ops);
  assertResult(ModeRunStatus::Failed, ModeRunError::VoiceToTextFailed, mode.main());

  TEST_ASSERT_EQUAL_STRING(
      "screen-on,cpu-recording,cancel-rtc,clear-assistant-message,record-voice,wifi,"
      "voice-to-text,delete-voice",
      joinCalls(ops.calls).c_str());
  TEST_ASSERT_EQUAL_STRING("old", zero_buddy::state::copyLastMessageId(state).c_str());
  TEST_ASSERT_EQUAL_UINT32(4UL * 60UL * 1000UL, state.checkDelayMs);
}

void test_recording_send_failure_does_not_commit_cursor() {
  auto state = zero_buddy::state::makeDefaultGlobalState();
  zero_buddy::state::setLastMessageId(&state, "old");
  state.checkDelayMs = 2UL * 60UL * 1000UL;

  FakeRecordingOps ops;
  ops.send_ok = false;
  ops.asr_text = "hello";

  RecordingMode mode(&state, &ops);
  assertResult(ModeRunStatus::Failed, ModeRunError::MessageSendFailed, mode.main());

  TEST_ASSERT_EQUAL_STRING("old", zero_buddy::state::copyLastMessageId(state).c_str());
  TEST_ASSERT_EQUAL_UINT32(2UL * 60UL * 1000UL, state.checkDelayMs);
  TEST_ASSERT_EQUAL_STRING(
      "screen-on,cpu-recording,cancel-rtc,clear-assistant-message,record-voice,wifi,"
      "voice-to-text,delete-voice,send:hello",
      joinCalls(ops.calls).c_str());
}

void test_recording_abort_during_record_cleans_owned_resources() {
  auto state = zero_buddy::state::makeDefaultGlobalState();
  zero_buddy::state::setLastMessageId(&state, "old");
  state.checkDelayMs = 2UL * 60UL * 1000UL;

  FakeRecordingOps ops;
  RecordingMode mode(&state, &ops);
  ops.on_record = [&]() {
    mode.abort("cancel");
  };

  assertResult(ModeRunStatus::Aborted, ModeRunError::None, mode.main());
  TEST_ASSERT_TRUE(mode.abortRequested());
  TEST_ASSERT_EQUAL_STRING("cancel", mode.abortReason());
  TEST_ASSERT_EQUAL_STRING(
      "screen-on,cpu-recording,cancel-rtc,clear-assistant-message,record-voice,"
      "stop-recording,cancel-asr,cancel-send,close-files,delete-voice",
      joinCalls(ops.calls).c_str());
  TEST_ASSERT_EQUAL_STRING("old", zero_buddy::state::copyLastMessageId(state).c_str());
  TEST_ASSERT_EQUAL_UINT32(2UL * 60UL * 1000UL, state.checkDelayMs);
}

void test_recording_invalid_returned_message_id_fails_without_mutating_cursor() {
  auto state = zero_buddy::state::makeDefaultGlobalState();
  zero_buddy::state::setLastMessageId(&state, "old");
  state.checkDelayMs = 2UL * 60UL * 1000UL;

  FakeRecordingOps ops;
  ops.sent_message_id = std::string(zero_buddy::state::kLastMessageIdBytes, 'm');

  RecordingMode mode(&state, &ops);
  assertResult(ModeRunStatus::Failed, ModeRunError::InvalidStateCommit, mode.main());

  TEST_ASSERT_EQUAL_STRING("old", zero_buddy::state::copyLastMessageId(state).c_str());
  TEST_ASSERT_EQUAL_UINT32(2UL * 60UL * 1000UL, state.checkDelayMs);
}

}  // namespace

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_check_assistant_main_commits_new_messages);
  RUN_TEST(test_check_assistant_skips_polling_without_last_message_id);
  RUN_TEST(test_check_assistant_preserves_existing_message_presence);
  RUN_TEST(test_check_assistant_main_without_new_messages_advances_backoff);
  RUN_TEST(test_check_assistant_storage_failure_does_not_commit_cursor_or_queue);
  RUN_TEST(test_check_assistant_abort_during_poll_cleans_without_commit);
  RUN_TEST(test_deep_sleep_main_runs_hibernate_last);
  RUN_TEST(test_deep_sleep_main_never_changes_led);
  RUN_TEST(test_deep_sleep_abort_after_screen_off_cancels_timer_without_touching_led);
  RUN_TEST(test_recording_main_sends_message_and_commits_cursor);
  RUN_TEST(test_recording_voice_to_text_failure_deletes_voice_without_cursor_commit);
  RUN_TEST(test_recording_send_failure_does_not_commit_cursor);
  RUN_TEST(test_recording_abort_during_record_cleans_owned_resources);
  RUN_TEST(test_recording_invalid_returned_message_id_fails_without_mutating_cursor);
  return UNITY_END();
}
