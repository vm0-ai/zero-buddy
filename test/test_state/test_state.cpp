#include <unity.h>

#include <string>

#include "zero_buddy_state.h"

namespace {

using zero_buddy::state::AssistantCheckResult;
using zero_buddy::state::Event;
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
  TEST_ASSERT_FALSE(zero_buddy::state::hasAssistantMessage(state));
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

void test_assistant_message_flag_helper() {
  auto state = zero_buddy::state::makeDefaultGlobalState();

  TEST_ASSERT_FALSE(zero_buddy::state::hasAssistantMessage(state));
  zero_buddy::state::setHasAssistantMessage(&state, true);
  TEST_ASSERT_TRUE(zero_buddy::state::hasAssistantMessage(state));
  zero_buddy::state::setHasAssistantMessage(&state, false);
  TEST_ASSERT_FALSE(zero_buddy::state::hasAssistantMessage(state));
}

void test_recording_turn_and_commit() {
  auto state = zero_buddy::state::makeDefaultGlobalState();
  zero_buddy::state::setHasAssistantMessage(&state, true);
  TEST_ASSERT_TRUE(zero_buddy::state::setLastMessageId(&state, "old-message"));
  state.checkDelayMs = 8UL * 60UL * 1000UL;

  zero_buddy::state::beginRecordingTurn(&state);
  TEST_ASSERT_TRUE(zero_buddy::state::hasAssistantMessage(state));
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
  zero_buddy::state::setHasAssistantMessage(&state, true);
  state.checkDelayMs = 2UL * 60UL * 1000UL;

  zero_buddy::state::abortRecording(&state);
  TEST_ASSERT_EQUAL_STRING("old", zero_buddy::state::copyLastMessageId(state).c_str());
  TEST_ASSERT_TRUE(zero_buddy::state::hasAssistantMessage(state));
  TEST_ASSERT_EQUAL_UINT32(2UL * 60UL * 1000UL, state.checkDelayMs);
}

void test_commit_assistant_check_with_new_messages_does_not_own_presence_flag() {
  auto state = zero_buddy::state::makeDefaultGlobalState();
  TEST_ASSERT_TRUE(zero_buddy::state::setLastMessageId(&state, "user-1"));
  state.checkDelayMs = 4UL * 60UL * 1000UL;

  AssistantCheckResult result;
  result.hasNewAssistantMessages = true;
  result.assistantMessages = {"hello", "world"};
  result.newestMessageId = "assistant-2";

  TEST_ASSERT_TRUE(zero_buddy::state::commitAssistantCheck(&state, result));
  TEST_ASSERT_EQUAL_STRING("assistant-2", zero_buddy::state::copyLastMessageId(state).c_str());
  TEST_ASSERT_FALSE(zero_buddy::state::hasAssistantMessage(state));
  TEST_ASSERT_EQUAL_UINT32(zero_buddy::state::kInitialCheckDelayMs, state.checkDelayMs);
}

void test_commit_assistant_check_without_new_messages_advances_backoff() {
  auto state = zero_buddy::state::makeDefaultGlobalState();
  TEST_ASSERT_TRUE(zero_buddy::state::setLastMessageId(&state, "user-1"));
  state.checkDelayMs = 60UL * 1000UL;

  AssistantCheckResult result;
  result.hasNewAssistantMessages = false;
  result.newestMessageId = "user-1";

  TEST_ASSERT_TRUE(zero_buddy::state::commitAssistantCheck(&state, result));
  TEST_ASSERT_EQUAL_STRING("user-1", zero_buddy::state::copyLastMessageId(state).c_str());
  TEST_ASSERT_FALSE(zero_buddy::state::hasAssistantMessage(state));
  TEST_ASSERT_EQUAL_UINT32(120UL * 1000UL, state.checkDelayMs);
}

void test_commit_assistant_check_rejects_invalid_results_without_mutating() {
  auto state = zero_buddy::state::makeDefaultGlobalState();
  TEST_ASSERT_TRUE(zero_buddy::state::setLastMessageId(&state, "old"));
  zero_buddy::state::setHasAssistantMessage(&state, true);
  state.checkDelayMs = 60UL * 1000UL;

  AssistantCheckResult invalid_empty_new;
  invalid_empty_new.hasNewAssistantMessages = true;
  invalid_empty_new.newestMessageId = "new";
  TEST_ASSERT_FALSE(zero_buddy::state::commitAssistantCheck(&state, invalid_empty_new));
  TEST_ASSERT_EQUAL_STRING("old", zero_buddy::state::copyLastMessageId(state).c_str());
  TEST_ASSERT_TRUE(zero_buddy::state::hasAssistantMessage(state));
  TEST_ASSERT_EQUAL_UINT32(60UL * 1000UL, state.checkDelayMs);

  AssistantCheckResult invalid_non_empty_empty_check;
  invalid_non_empty_empty_check.hasNewAssistantMessages = false;
  invalid_non_empty_empty_check.assistantMessages = {"unexpected"};
  invalid_non_empty_empty_check.newestMessageId = "new";
  TEST_ASSERT_FALSE(zero_buddy::state::commitAssistantCheck(&state,
                                                            invalid_non_empty_empty_check));
  TEST_ASSERT_EQUAL_STRING("old", zero_buddy::state::copyLastMessageId(state).c_str());
  TEST_ASSERT_TRUE(zero_buddy::state::hasAssistantMessage(state));
  TEST_ASSERT_EQUAL_UINT32(60UL * 1000UL, state.checkDelayMs);

  AssistantCheckResult invalid_id;
  invalid_id.hasNewAssistantMessages = true;
  invalid_id.assistantMessages = {"assistant"};
  invalid_id.newestMessageId = std::string(zero_buddy::state::kLastMessageIdBytes, 'x');
  TEST_ASSERT_FALSE(zero_buddy::state::commitAssistantCheck(&state, invalid_id));
  TEST_ASSERT_EQUAL_STRING("old", zero_buddy::state::copyLastMessageId(state).c_str());
  TEST_ASSERT_TRUE(zero_buddy::state::hasAssistantMessage(state));
  TEST_ASSERT_EQUAL_UINT32(60UL * 1000UL, state.checkDelayMs);
}

void test_assistant_check_abort_does_not_mutate_global_state() {
  auto state = zero_buddy::state::makeDefaultGlobalState();
  TEST_ASSERT_TRUE(zero_buddy::state::setLastMessageId(&state, "old"));
  zero_buddy::state::setHasAssistantMessage(&state, true);
  state.checkDelayMs = 120UL * 1000UL;

  zero_buddy::state::abortAssistantCheck(&state);
  TEST_ASSERT_EQUAL_STRING("old", zero_buddy::state::copyLastMessageId(state).c_str());
  TEST_ASSERT_TRUE(zero_buddy::state::hasAssistantMessage(state));
  TEST_ASSERT_EQUAL_UINT32(120UL * 1000UL, state.checkDelayMs);
}

void test_read_abort_does_not_mutate_global_state() {
  auto state = zero_buddy::state::makeDefaultGlobalState();
  TEST_ASSERT_TRUE(zero_buddy::state::setLastMessageId(&state, "old"));
  zero_buddy::state::setHasAssistantMessage(&state, true);
  state.checkDelayMs = 120UL * 1000UL;

  zero_buddy::state::abortRead(&state);
  TEST_ASSERT_EQUAL_STRING("old", zero_buddy::state::copyLastMessageId(state).c_str());
  TEST_ASSERT_TRUE(zero_buddy::state::hasAssistantMessage(state));
  TEST_ASSERT_EQUAL_UINT32(120UL * 1000UL, state.checkDelayMs);
}

void test_deep_sleep_plan_and_abort() {
  auto state = zero_buddy::state::makeDefaultGlobalState();
  state.checkDelayMs = 5UL * 60UL * 1000UL;
  zero_buddy::state::setHasAssistantMessage(&state, true);

  const auto plan = zero_buddy::state::makeDeepSleepPlan(state);
  TEST_ASSERT_EQUAL_UINT32(5UL * 60UL * 1000UL, plan.rtcDelayMs);

  zero_buddy::state::abortDeepSleep(&state);
  TEST_ASSERT_EQUAL_UINT32(5UL * 60UL * 1000UL, state.checkDelayMs);
  TEST_ASSERT_TRUE(zero_buddy::state::hasAssistantMessage(state));
}

void test_state_transitions() {
  auto state = zero_buddy::state::makeDefaultGlobalState();

  auto transition = zero_buddy::state::transitionForEvent(state.currentMode, Event::RtcWake);
  TEST_ASSERT_TRUE(transition.valid);
  TEST_ASSERT_FALSE(transition.requiresAbort);
  assertMode(Mode::CheckAssistantMessage, transition.nextMode);
  TEST_ASSERT_TRUE(zero_buddy::state::applyTransition(&state, transition));
  assertMode(Mode::CheckAssistantMessage, state.currentMode);

  transition = zero_buddy::state::transitionForEvent(state.currentMode, Event::BtnAShortPress);
  TEST_ASSERT_TRUE(transition.valid);
  TEST_ASSERT_TRUE(transition.requiresAbort);
  assertMode(Mode::Read, transition.nextMode);

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

  transition = zero_buddy::state::transitionForEvent(state.currentMode, Event::BtnAShortPress);
  TEST_ASSERT_TRUE(transition.valid);
  TEST_ASSERT_TRUE(transition.requiresAbort);
  assertMode(Mode::Read, transition.nextMode);
  TEST_ASSERT_TRUE(zero_buddy::state::applyTransition(&state, transition));
  assertMode(Mode::Read, state.currentMode);

  transition = zero_buddy::state::transitionForEvent(state.currentMode, Event::BtnALongPress);
  TEST_ASSERT_TRUE(transition.valid);
  TEST_ASSERT_TRUE(transition.requiresAbort);
  assertMode(Mode::Recording, transition.nextMode);

  transition = zero_buddy::state::transitionForEvent(state.currentMode, Event::ReadComplete);
  TEST_ASSERT_TRUE(transition.valid);
  TEST_ASSERT_FALSE(transition.requiresAbort);
  assertMode(Mode::DeepSleep, transition.nextMode);
}

}  // namespace

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_default_global_state);
  RUN_TEST(test_message_cursor_copy_and_truncation);
  RUN_TEST(test_check_delay_backoff);
  RUN_TEST(test_assistant_message_flag_helper);
  RUN_TEST(test_recording_turn_and_commit);
  RUN_TEST(test_recording_commit_rejects_overlong_id_without_mutating);
  RUN_TEST(test_recording_abort_does_not_mutate_global_state);
  RUN_TEST(test_commit_assistant_check_with_new_messages_does_not_own_presence_flag);
  RUN_TEST(test_commit_assistant_check_without_new_messages_advances_backoff);
  RUN_TEST(test_commit_assistant_check_rejects_invalid_results_without_mutating);
  RUN_TEST(test_assistant_check_abort_does_not_mutate_global_state);
  RUN_TEST(test_read_abort_does_not_mutate_global_state);
  RUN_TEST(test_deep_sleep_plan_and_abort);
  RUN_TEST(test_state_transitions);
  return UNITY_END();
}
