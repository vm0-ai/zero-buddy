#include <unity.h>

#include <string>

#include "zero_buddy_state.h"

namespace {

using zero_buddy::state::AssistantCheckResult;
using zero_buddy::state::Event;
using zero_buddy::state::GlobalState;
using zero_buddy::state::Mode;

void assertMode(Mode expected, Mode actual) {
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(expected), static_cast<uint8_t>(actual));
}

void test_default_global_state() {
  const auto state = zero_buddy::state::makeDefaultGlobalState();
  assertMode(Mode::DeepSleep, state.currentMode);
  TEST_ASSERT_EQUAL_UINT32(zero_buddy::state::kInitialCheckDelayMs, state.checkDelayMs);
  TEST_ASSERT_FALSE(zero_buddy::state::hasLastMessageId(state));
  TEST_ASSERT_EQUAL_STRING("", zero_buddy::state::copyLastMessageId(state).c_str());
  TEST_ASSERT_EQUAL_UINT(0, state.assistantMessageCount);
  TEST_ASSERT_EQUAL_UINT(0, state.assistantMessageIndex);
  TEST_ASSERT_FALSE(zero_buddy::state::hasUnreadAssistantMessages(state));
}

void test_message_cursor_copy_and_truncation() {
  auto state = zero_buddy::state::makeDefaultGlobalState();

  TEST_ASSERT_TRUE(zero_buddy::state::setLastMessageId(&state, "msg-1"));
  TEST_ASSERT_TRUE(zero_buddy::state::hasLastMessageId(state));
  TEST_ASSERT_EQUAL_STRING("msg-1", zero_buddy::state::copyLastMessageId(state).c_str());

  const std::string long_id(zero_buddy::state::kLastMessageIdBytes + 20, 'x');
  TEST_ASSERT_FALSE(zero_buddy::state::setLastMessageId(&state, long_id));
  TEST_ASSERT_EQUAL_UINT(zero_buddy::state::kLastMessageIdBytes - 1,
                         zero_buddy::state::copyLastMessageId(state).size());

  zero_buddy::state::clearLastMessageId(&state);
  TEST_ASSERT_FALSE(zero_buddy::state::hasLastMessageId(state));
  TEST_ASSERT_EQUAL_STRING("", zero_buddy::state::copyLastMessageId(state).c_str());
}

void test_check_delay_backoff() {
  auto state = zero_buddy::state::makeDefaultGlobalState();

  TEST_ASSERT_EQUAL_UINT32(zero_buddy::state::kInitialCheckDelayMs,
                           zero_buddy::state::nextCheckDelay(0));
  TEST_ASSERT_EQUAL_UINT32(60UL * 1000UL,
                           zero_buddy::state::nextCheckDelay(30UL * 1000UL));
  TEST_ASSERT_EQUAL_UINT32(zero_buddy::state::kMaxCheckDelayMs,
                           zero_buddy::state::nextCheckDelay(30UL * 60UL * 1000UL));
  TEST_ASSERT_EQUAL_UINT32(zero_buddy::state::kMaxCheckDelayMs,
                           zero_buddy::state::nextCheckDelay(zero_buddy::state::kMaxCheckDelayMs));

  zero_buddy::state::advanceCheckDelay(&state);
  TEST_ASSERT_EQUAL_UINT32(60UL * 1000UL, state.checkDelayMs);
  zero_buddy::state::resetCheckDelay(&state);
  TEST_ASSERT_EQUAL_UINT32(zero_buddy::state::kInitialCheckDelayMs, state.checkDelayMs);
}

void test_assistant_queue_helpers() {
  auto state = zero_buddy::state::makeDefaultGlobalState();

  TEST_ASSERT_TRUE(zero_buddy::state::setAssistantMessages(&state, 3, 0));
  TEST_ASSERT_TRUE(zero_buddy::state::hasUnreadAssistantMessages(state));
  TEST_ASSERT_EQUAL_UINT(3, state.assistantMessageCount);
  TEST_ASSERT_EQUAL_UINT(0, state.assistantMessageIndex);

  TEST_ASSERT_TRUE(zero_buddy::state::advanceAssistantMessageIndex(&state));
  TEST_ASSERT_EQUAL_UINT(1, state.assistantMessageIndex);
  TEST_ASSERT_TRUE(zero_buddy::state::advanceAssistantMessageIndex(&state));
  TEST_ASSERT_TRUE(zero_buddy::state::advanceAssistantMessageIndex(&state));
  TEST_ASSERT_FALSE(zero_buddy::state::hasUnreadAssistantMessages(state));
  TEST_ASSERT_FALSE(zero_buddy::state::advanceAssistantMessageIndex(&state));
  TEST_ASSERT_EQUAL_UINT(3, state.assistantMessageIndex);

  TEST_ASSERT_FALSE(zero_buddy::state::setAssistantMessages(&state, 2, 3));
  TEST_ASSERT_EQUAL_UINT(3, state.assistantMessageCount);
  TEST_ASSERT_EQUAL_UINT(3, state.assistantMessageIndex);

  zero_buddy::state::clearAssistantMessages(&state);
  TEST_ASSERT_EQUAL_UINT(0, state.assistantMessageCount);
  TEST_ASSERT_EQUAL_UINT(0, state.assistantMessageIndex);
}

void test_recording_turn_and_commit() {
  auto state = zero_buddy::state::makeDefaultGlobalState();
  TEST_ASSERT_TRUE(zero_buddy::state::setAssistantMessages(&state, 2, 1));
  TEST_ASSERT_TRUE(zero_buddy::state::setLastMessageId(&state, "old-message"));
  state.checkDelayMs = 8UL * 60UL * 1000UL;

  zero_buddy::state::beginRecordingTurn(&state);
  TEST_ASSERT_EQUAL_UINT(0, state.assistantMessageCount);
  TEST_ASSERT_EQUAL_UINT(0, state.assistantMessageIndex);
  TEST_ASSERT_EQUAL_STRING("old-message", zero_buddy::state::copyLastMessageId(state).c_str());
  TEST_ASSERT_EQUAL_UINT32(8UL * 60UL * 1000UL, state.checkDelayMs);

  TEST_ASSERT_FALSE(zero_buddy::state::commitRecordingMessageSent(&state, ""));
  TEST_ASSERT_EQUAL_STRING("old-message", zero_buddy::state::copyLastMessageId(state).c_str());
  TEST_ASSERT_EQUAL_UINT32(8UL * 60UL * 1000UL, state.checkDelayMs);

  TEST_ASSERT_TRUE(zero_buddy::state::commitRecordingMessageSent(&state, "user-2"));
  TEST_ASSERT_EQUAL_STRING("user-2", zero_buddy::state::copyLastMessageId(state).c_str());
  TEST_ASSERT_EQUAL_UINT32(zero_buddy::state::kInitialCheckDelayMs, state.checkDelayMs);
}

void test_recording_commit_rejects_overlong_id_without_mutating() {
  auto state = zero_buddy::state::makeDefaultGlobalState();
  TEST_ASSERT_TRUE(zero_buddy::state::setLastMessageId(&state, "old"));
  state.checkDelayMs = 4UL * 60UL * 1000UL;

  const std::string long_id(zero_buddy::state::kLastMessageIdBytes, 'm');
  TEST_ASSERT_FALSE(zero_buddy::state::commitRecordingMessageSent(&state, long_id));
  TEST_ASSERT_EQUAL_STRING("old", zero_buddy::state::copyLastMessageId(state).c_str());
  TEST_ASSERT_EQUAL_UINT32(4UL * 60UL * 1000UL, state.checkDelayMs);
}

void test_recording_abort_does_not_mutate_global_state() {
  auto state = zero_buddy::state::makeDefaultGlobalState();
  TEST_ASSERT_TRUE(zero_buddy::state::setLastMessageId(&state, "old"));
  TEST_ASSERT_TRUE(zero_buddy::state::setAssistantMessages(&state, 2, 0));
  state.checkDelayMs = 2UL * 60UL * 1000UL;

  zero_buddy::state::abortRecording(&state);
  TEST_ASSERT_EQUAL_STRING("old", zero_buddy::state::copyLastMessageId(state).c_str());
  TEST_ASSERT_EQUAL_UINT(2, state.assistantMessageCount);
  TEST_ASSERT_EQUAL_UINT(0, state.assistantMessageIndex);
  TEST_ASSERT_EQUAL_UINT32(2UL * 60UL * 1000UL, state.checkDelayMs);
}

void test_commit_assistant_check_with_new_messages() {
  auto state = zero_buddy::state::makeDefaultGlobalState();
  TEST_ASSERT_TRUE(zero_buddy::state::setLastMessageId(&state, "user-1"));
  state.checkDelayMs = 4UL * 60UL * 1000UL;

  AssistantCheckResult result;
  result.hasNewAssistantMessages = true;
  result.assistantMessageCount = 2;
  result.newestMessageId = "assistant-2";

  TEST_ASSERT_TRUE(zero_buddy::state::commitAssistantCheck(&state, result));
  TEST_ASSERT_EQUAL_STRING("assistant-2", zero_buddy::state::copyLastMessageId(state).c_str());
  TEST_ASSERT_EQUAL_UINT(2, state.assistantMessageCount);
  TEST_ASSERT_EQUAL_UINT(0, state.assistantMessageIndex);
  TEST_ASSERT_TRUE(zero_buddy::state::hasUnreadAssistantMessages(state));
  TEST_ASSERT_EQUAL_UINT32(zero_buddy::state::kInitialCheckDelayMs, state.checkDelayMs);
}

void test_commit_assistant_check_appends_to_existing_messages() {
  auto state = zero_buddy::state::makeDefaultGlobalState();
  TEST_ASSERT_TRUE(zero_buddy::state::setLastMessageId(&state, "assistant-1"));
  TEST_ASSERT_TRUE(zero_buddy::state::setAssistantMessages(&state, 1, 0));
  state.checkDelayMs = 4UL * 60UL * 1000UL;

  AssistantCheckResult result;
  result.hasNewAssistantMessages = true;
  result.assistantMessageCount = 2;
  result.newestMessageId = "assistant-3";

  TEST_ASSERT_TRUE(zero_buddy::state::commitAssistantCheck(&state, result));
  TEST_ASSERT_EQUAL_STRING("assistant-3", zero_buddy::state::copyLastMessageId(state).c_str());
  TEST_ASSERT_EQUAL_UINT(3, state.assistantMessageCount);
  TEST_ASSERT_EQUAL_UINT(0, state.assistantMessageIndex);
  TEST_ASSERT_TRUE(zero_buddy::state::hasUnreadAssistantMessages(state));
  TEST_ASSERT_EQUAL_UINT32(zero_buddy::state::kInitialCheckDelayMs, state.checkDelayMs);
}

void test_commit_assistant_check_without_new_messages_advances_backoff() {
  auto state = zero_buddy::state::makeDefaultGlobalState();
  TEST_ASSERT_TRUE(zero_buddy::state::setLastMessageId(&state, "user-1"));
  state.checkDelayMs = 60UL * 1000UL;

  AssistantCheckResult result;
  result.hasNewAssistantMessages = false;
  result.assistantMessageCount = 0;
  result.newestMessageId = "user-1";

  TEST_ASSERT_TRUE(zero_buddy::state::commitAssistantCheck(&state, result));
  TEST_ASSERT_EQUAL_STRING("user-1", zero_buddy::state::copyLastMessageId(state).c_str());
  TEST_ASSERT_EQUAL_UINT(0, state.assistantMessageCount);
  TEST_ASSERT_EQUAL_UINT(0, state.assistantMessageIndex);
  TEST_ASSERT_EQUAL_UINT32(120UL * 1000UL, state.checkDelayMs);
}

void test_commit_assistant_check_rejects_invalid_results_without_mutating() {
  auto state = zero_buddy::state::makeDefaultGlobalState();
  TEST_ASSERT_TRUE(zero_buddy::state::setLastMessageId(&state, "old"));
  TEST_ASSERT_TRUE(zero_buddy::state::setAssistantMessages(&state, 1, 0));
  state.checkDelayMs = 60UL * 1000UL;

  AssistantCheckResult invalid_count;
  invalid_count.hasNewAssistantMessages = false;
  invalid_count.assistantMessageCount = 1;
  invalid_count.newestMessageId = "new";
  TEST_ASSERT_FALSE(zero_buddy::state::commitAssistantCheck(&state, invalid_count));
  TEST_ASSERT_EQUAL_STRING("old", zero_buddy::state::copyLastMessageId(state).c_str());
  TEST_ASSERT_EQUAL_UINT(1, state.assistantMessageCount);
  TEST_ASSERT_EQUAL_UINT32(60UL * 1000UL, state.checkDelayMs);

  AssistantCheckResult invalid_id;
  invalid_id.hasNewAssistantMessages = true;
  invalid_id.assistantMessageCount = 2;
  invalid_id.newestMessageId = std::string(zero_buddy::state::kLastMessageIdBytes, 'x');
  TEST_ASSERT_FALSE(zero_buddy::state::commitAssistantCheck(&state, invalid_id));
  TEST_ASSERT_EQUAL_STRING("old", zero_buddy::state::copyLastMessageId(state).c_str());
  TEST_ASSERT_EQUAL_UINT(1, state.assistantMessageCount);
  TEST_ASSERT_EQUAL_UINT32(60UL * 1000UL, state.checkDelayMs);
}

void test_assistant_check_abort_does_not_mutate_global_state() {
  auto state = zero_buddy::state::makeDefaultGlobalState();
  TEST_ASSERT_TRUE(zero_buddy::state::setLastMessageId(&state, "old"));
  TEST_ASSERT_TRUE(zero_buddy::state::setAssistantMessages(&state, 3, 1));
  state.checkDelayMs = 120UL * 1000UL;

  zero_buddy::state::abortAssistantCheck(&state);
  TEST_ASSERT_EQUAL_STRING("old", zero_buddy::state::copyLastMessageId(state).c_str());
  TEST_ASSERT_EQUAL_UINT(3, state.assistantMessageCount);
  TEST_ASSERT_EQUAL_UINT(1, state.assistantMessageIndex);
  TEST_ASSERT_EQUAL_UINT32(120UL * 1000UL, state.checkDelayMs);
}

void test_deep_sleep_plan_and_abort() {
  auto state = zero_buddy::state::makeDefaultGlobalState();
  state.checkDelayMs = 5UL * 60UL * 1000UL;
  TEST_ASSERT_TRUE(zero_buddy::state::setAssistantMessages(&state, 2, 1));

  const auto plan = zero_buddy::state::makeDeepSleepPlan(state);
  TEST_ASSERT_EQUAL_UINT32(5UL * 60UL * 1000UL, plan.rtcDelayMs);

  zero_buddy::state::abortDeepSleep(&state);
  TEST_ASSERT_EQUAL_UINT32(5UL * 60UL * 1000UL, state.checkDelayMs);
  TEST_ASSERT_EQUAL_UINT(2, state.assistantMessageCount);
  TEST_ASSERT_EQUAL_UINT(1, state.assistantMessageIndex);
}

void test_state_transitions() {
  auto state = zero_buddy::state::makeDefaultGlobalState();

  auto transition = zero_buddy::state::transitionForEvent(state.currentMode, Event::RtcWake);
  TEST_ASSERT_TRUE(transition.valid);
  TEST_ASSERT_FALSE(transition.requiresAbort);
  assertMode(Mode::CheckAssistantMessage, transition.nextMode);
  TEST_ASSERT_TRUE(zero_buddy::state::applyTransition(&state, transition));
  assertMode(Mode::CheckAssistantMessage, state.currentMode);

  transition = zero_buddy::state::transitionForEvent(state.currentMode, Event::BtnALongPress);
  TEST_ASSERT_TRUE(transition.valid);
  TEST_ASSERT_TRUE(transition.requiresAbort);
  assertMode(Mode::Recording, transition.nextMode);
  TEST_ASSERT_TRUE(zero_buddy::state::applyTransition(&state, transition));
  assertMode(Mode::Recording, state.currentMode);

  transition = zero_buddy::state::transitionForEvent(state.currentMode, Event::RecordingComplete);
  TEST_ASSERT_TRUE(transition.valid);
  TEST_ASSERT_FALSE(transition.requiresAbort);
  assertMode(Mode::DeepSleep, transition.nextMode);
  TEST_ASSERT_TRUE(zero_buddy::state::applyTransition(&state, transition));
  assertMode(Mode::DeepSleep, state.currentMode);

  transition = zero_buddy::state::transitionForEvent(state.currentMode, Event::CheckComplete);
  TEST_ASSERT_FALSE(transition.valid);
  TEST_ASSERT_FALSE(zero_buddy::state::applyTransition(&state, transition));
  assertMode(Mode::DeepSleep, state.currentMode);
}

}  // namespace

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_default_global_state);
  RUN_TEST(test_message_cursor_copy_and_truncation);
  RUN_TEST(test_check_delay_backoff);
  RUN_TEST(test_assistant_queue_helpers);
  RUN_TEST(test_recording_turn_and_commit);
  RUN_TEST(test_recording_commit_rejects_overlong_id_without_mutating);
  RUN_TEST(test_recording_abort_does_not_mutate_global_state);
  RUN_TEST(test_commit_assistant_check_with_new_messages);
  RUN_TEST(test_commit_assistant_check_appends_to_existing_messages);
  RUN_TEST(test_commit_assistant_check_without_new_messages_advances_backoff);
  RUN_TEST(test_commit_assistant_check_rejects_invalid_results_without_mutating);
  RUN_TEST(test_assistant_check_abort_does_not_mutate_global_state);
  RUN_TEST(test_deep_sleep_plan_and_abort);
  RUN_TEST(test_state_transitions);
  return UNITY_END();
}
