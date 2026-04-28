#include "zero_buddy_modes.h"

#include <algorithm>

namespace zero_buddy {
namespace modes {
namespace {

constexpr uint32_t kReadIdleTimeoutMs = 15UL * 1000UL;
ModeRunResult completed() {
  ModeRunResult result;
  result.status = ModeRunStatus::Completed;
  result.error = ModeRunError::None;
  return result;
}

ModeRunResult aborted() {
  ModeRunResult result;
  result.status = ModeRunStatus::Aborted;
  result.error = ModeRunError::None;
  return result;
}

ModeRunResult failed(ModeRunError error) {
  ModeRunResult result;
  result.status = ModeRunStatus::Failed;
  result.error = error;
  return result;
}

}  // namespace

CheckAssistantMessageMode::CheckAssistantMessageMode(state::GlobalState* state,
                                                     CheckAssistantMessageOps* ops)
    : state_(state), ops_(ops) {}

ModeRunResult CheckAssistantMessageMode::main() {
  if (state_ == nullptr || ops_ == nullptr) {
    return failed(ModeRunError::InvalidStateCommit);
  }
  if (shouldStop()) {
    return abortedResult();
  }

  if (!state::hasLastMessageId(*state_)) {
    advanceBackoffAfterEmptyOrFailedCheck();
    return completed();
  }

  ops_->setCpuForNetwork();
  if (shouldStop()) {
    return abortedResult();
  }

  if (!ops_->ensureWifiConnected()) {
    advanceBackoffAfterEmptyOrFailedCheck();
    return shouldStop() ? abortedResult() : failed(ModeRunError::WifiUnavailable);
  }
  if (shouldStop()) {
    return abortedResult();
  }

  state::AssistantCheckResult result;
  const std::string since_id = state::copyLastMessageId(*state_);
  if (!ops_->pollAssistantMessages(since_id, &result)) {
    advanceBackoffAfterEmptyOrFailedCheck();
    return shouldStop() ? abortedResult() : failed(ModeRunError::AssistantPollFailed);
  }
  if (shouldStop()) {
    return abortedResult();
  }

  if (result.hasNewAssistantMessages && !ops_->appendAssistantMessages(result)) {
    advanceBackoffAfterEmptyOrFailedCheck();
    ops_->cleanupTempFiles();
    return shouldStop() ? abortedResult() : failed(ModeRunError::AssistantStorageFailed);
  }
  if (shouldStop()) {
    return abortedResult();
  }

  if (!state::commitAssistantCheck(state_, result)) {
    ops_->cleanupTempFiles();
    return failed(ModeRunError::InvalidStateCommit);
  }
  return completed();
}

void CheckAssistantMessageMode::abort(const char* reason) {
  abort_requested_ = true;
  abort_reason_ = reason;
  if (abort_cleanup_done_ || ops_ == nullptr) {
    return;
  }
  abort_cleanup_done_ = true;
  ops_->cancelNetwork();
  ops_->closeFiles();
  ops_->cleanupTempFiles();
  state::abortAssistantCheck(state_);
}

bool CheckAssistantMessageMode::abortRequested() const {
  return abort_requested_;
}

const char* CheckAssistantMessageMode::abortReason() const {
  return abort_reason_;
}

bool CheckAssistantMessageMode::shouldStop() const {
  return abort_requested_;
}

ModeRunResult CheckAssistantMessageMode::abortedResult() const {
  return aborted();
}

void CheckAssistantMessageMode::advanceBackoffAfterEmptyOrFailedCheck() {
  state::advanceCheckDelay(state_);
}

DeepSleepMode::DeepSleepMode(state::GlobalState* state, DeepSleepOps* ops)
    : state_(state), ops_(ops) {}

ModeRunResult DeepSleepMode::main() {
  if (state_ == nullptr || ops_ == nullptr) {
    return failed(ModeRunError::InvalidStateCommit);
  }
  if (shouldStop()) {
    return abortedResult();
  }

  const state::DeepSleepPlan plan = state::makeDeepSleepPlan(*state_);
  ops_->configureRtcWake(plan.rtcDelayMs);
  if (shouldStop()) {
    return abortedResult();
  }

  const bool charging = ops_->isCharging();
  if (shouldStop()) {
    return abortedResult();
  }
  if (charging) {
    return completed();
  }

  ops_->configureBtnAWake();
  if (shouldStop()) {
    return abortedResult();
  }

  ops_->screenOff();
  if (shouldStop()) {
    return abortedResult();
  }

  ops_->disconnectWifi();
  if (shouldStop()) {
    return abortedResult();
  }

  // This must remain the final operation in this mode. On real ESP32 hardware
  // it does not return after entering deep sleep.
  ops_->enterCpuHibernate();
  return shouldStop() ? abortedResult() : completed();
}

void DeepSleepMode::abort(const char* reason) {
  abort_requested_ = true;
  abort_reason_ = reason;
  if (abort_cleanup_done_ || ops_ == nullptr) {
    return;
  }
  abort_cleanup_done_ = true;
  ops_->cancelRtcWake();
  state::abortDeepSleep(state_);
}

bool DeepSleepMode::abortRequested() const {
  return abort_requested_;
}

const char* DeepSleepMode::abortReason() const {
  return abort_reason_;
}

bool DeepSleepMode::shouldStop() const {
  return abort_requested_;
}

ModeRunResult DeepSleepMode::abortedResult() const {
  return aborted();
}

ReadMode::ReadMode(state::GlobalState* state, ReadOps* ops)
    : state_(state), ops_(ops) {}

ModeRunResult ReadMode::main() {
  if (state_ == nullptr || ops_ == nullptr) {
    return failed(ModeRunError::InvalidStateCommit);
  }
  if (shouldStop()) {
    return abortedResult();
  }

  ops_->screenOn();
  if (shouldStop()) {
    return abortedResult();
  }

  ops_->setCpuForReading();
  if (shouldStop()) {
    return abortedResult();
  }

  const size_t count = ops_->storedAssistantMessageCount();
  if (shouldStop()) {
    return abortedResult();
  }

  ReadProgress progress;
  if (!ops_->loadReadProgress(&progress)) {
    return shouldStop() ? abortedResult() : failed(ModeRunError::AssistantStorageFailed);
  }
  if (shouldStop()) {
    return abortedResult();
  }

  if (count == 0) {
    return renderEmptyUntilIdle();
  }

  if (progress.messageIndex >= count) {
    progress.messageIndex = count - 1;
    progress.scrollTop = 0;
    if (!persistProgress(count, progress)) {
      return shouldStop() ? abortedResult() : failed(ModeRunError::AssistantStorageFailed);
    }
  }

  while (!shouldStop()) {
    std::string message;
    if (!loadCurrentMessage(progress.messageIndex, &message)) {
      return shouldStop() ? abortedResult() : failed(ModeRunError::AssistantStorageFailed);
    }
    if (!clampAndPersistProgress(count, message, &progress)) {
      return shouldStop() ? abortedResult() : failed(ModeRunError::AssistantStorageFailed);
    }

    ops_->renderAssistantMessage(progress.messageIndex, count, message, progress.scrollTop);
    if (shouldStop()) {
      return abortedResult();
    }
    const ReadInput input = ops_->waitForInput(kReadIdleTimeoutMs);
    if (input == ReadInput::LongPress) {
      abort("btn_a_long_press");
      return abortedResult();
    }
    if (input == ReadInput::CheckDue) {
      abort("check_due");
      return abortedResult();
    }
    if (input == ReadInput::Timeout) {
      return shouldStop() ? abortedResult() : completed();
    }
    if (shouldStop()) {
      return abortedResult();
    }

    const size_t max_scroll_top = ops_->maxScrollTopForMessage(message);
    if (progress.scrollTop < max_scroll_top) {
      const size_t configured_step = ops_->scrollStep();
      const size_t step = configured_step == 0 ? max_scroll_top : configured_step;
      progress.scrollTop =
          std::min(max_scroll_top, progress.scrollTop + step);
    } else if (progress.messageIndex + 1 < count) {
      ++progress.messageIndex;
      progress.scrollTop = 0;
    } else {
      if (!ops_->clearAssistantMessages()) {
        return shouldStop() ? abortedResult() : failed(ModeRunError::AssistantClearFailed);
      }
      return renderEmptyUntilIdle();
    }

    if (!persistProgress(count, progress)) {
      return shouldStop() ? abortedResult() : failed(ModeRunError::AssistantStorageFailed);
    }
  }

  return abortedResult();
}

void ReadMode::abort(const char* reason) {
  abort_requested_ = true;
  abort_reason_ = reason;
  if (abort_cleanup_done_ || ops_ == nullptr) {
    return;
  }
  abort_cleanup_done_ = true;
  ops_->cancelIdleTimer();
  ops_->closeFiles();
  state::abortRead(state_);
}

bool ReadMode::abortRequested() const {
  return abort_requested_;
}

const char* ReadMode::abortReason() const {
  return abort_reason_;
}

bool ReadMode::shouldStop() const {
  return abort_requested_;
}

ModeRunResult ReadMode::abortedResult() const {
  return aborted();
}

ModeRunResult ReadMode::renderEmptyUntilIdle() {
  while (!shouldStop()) {
    ops_->renderNoAssistantMessage();
    if (shouldStop()) {
      return abortedResult();
    }
    const ReadInput input = ops_->waitForInput(kReadIdleTimeoutMs);
    if (input == ReadInput::LongPress) {
      abort("btn_a_long_press");
      return abortedResult();
    }
    if (input == ReadInput::CheckDue) {
      abort("check_due");
      return abortedResult();
    }
    if (input == ReadInput::Timeout) {
      return shouldStop() ? abortedResult() : completed();
    }
  }
  return abortedResult();
}

bool ReadMode::loadCurrentMessage(size_t index, std::string* message_out) const {
  return ops_ != nullptr && ops_->loadAssistantMessage(index, message_out);
}

bool ReadMode::clampAndPersistProgress(size_t count,
                                       const std::string& message,
                                       ReadProgress* progress) {
  if (progress == nullptr || ops_ == nullptr || count == 0) {
    return false;
  }
  bool changed = false;
  if (progress->messageIndex >= count) {
    progress->messageIndex = count - 1;
    progress->scrollTop = 0;
    changed = true;
  }
  const size_t max_scroll_top = ops_->maxScrollTopForMessage(message);
  if (progress->scrollTop > max_scroll_top) {
    progress->scrollTop = max_scroll_top;
    changed = true;
  }
  return !changed || persistProgress(count, *progress);
}

bool ReadMode::persistProgress(size_t count, const ReadProgress& progress) {
  return ops_ != nullptr &&
         ops_->saveReadProgress(count, progress.messageIndex, progress.scrollTop);
}

RecordingMode::RecordingMode(state::GlobalState* state, RecordingOps* ops)
    : state_(state), ops_(ops) {}

ModeRunResult RecordingMode::main() {
  if (state_ == nullptr || ops_ == nullptr) {
    return failed(ModeRunError::InvalidStateCommit);
  }
  if (shouldStop()) {
    return abortedResult();
  }

  ops_->screenOn();
  if (shouldStop()) {
    return abortedResult();
  }

  ops_->setCpuForRecording();
  if (shouldStop()) {
    return abortedResult();
  }

  ops_->cancelRtcWake();
  if (shouldStop()) {
    return abortedResult();
  }

  if (!ops_->clearAssistantMessages()) {
    return shouldStop() ? abortedResult() : failed(ModeRunError::AssistantClearFailed);
  }
  state::beginRecordingTurn(state_);
  if (shouldStop()) {
    return abortedResult();
  }

  voice_file_may_exist_ = true;
  if (!ops_->recordVoiceToFile()) {
    cleanupVoiceFile();
    return shouldStop() ? abortedResult() : failed(ModeRunError::VoiceCaptureFailed);
  }
  if (shouldStop()) {
    return abortedResult();
  }

  if (!ops_->ensureWifiConnected()) {
    cleanupVoiceFile();
    return shouldStop() ? abortedResult() : failed(ModeRunError::WifiUnavailable);
  }
  if (shouldStop()) {
    return abortedResult();
  }

  std::string text;
  if (!ops_->voiceToText(&text)) {
    cleanupVoiceFile();
    return shouldStop() ? abortedResult() : failed(ModeRunError::VoiceToTextFailed);
  }
  cleanupVoiceFile();
  if (shouldStop()) {
    return abortedResult();
  }

  std::string user_message_id;
  if (!ops_->sendTextMessage(text, &user_message_id)) {
    return shouldStop() ? abortedResult() : failed(ModeRunError::MessageSendFailed);
  }

  // Commit immediately after the send is accepted. There is intentionally no
  // abort checkpoint between send success and cursor/backoff update.
  if (!state::commitRecordingMessageSent(state_, user_message_id)) {
    return failed(ModeRunError::InvalidStateCommit);
  }
  return completed();
}

void RecordingMode::abort(const char* reason) {
  abort_requested_ = true;
  abort_reason_ = reason;
  if (abort_cleanup_done_ || ops_ == nullptr) {
    return;
  }
  abort_cleanup_done_ = true;
  ops_->stopVoiceRecording();
  ops_->cancelVoiceToText();
  ops_->cancelTextMessageSend();
  ops_->closeFiles();
  cleanupVoiceFile();
  state::abortRecording(state_);
}

bool RecordingMode::abortRequested() const {
  return abort_requested_;
}

const char* RecordingMode::abortReason() const {
  return abort_reason_;
}

bool RecordingMode::shouldStop() const {
  return abort_requested_;
}

ModeRunResult RecordingMode::abortedResult() const {
  return aborted();
}

void RecordingMode::cleanupVoiceFile() {
  if (ops_ == nullptr || !voice_file_may_exist_) {
    return;
  }
  ops_->deleteVoiceFile();
  voice_file_may_exist_ = false;
}

}  // namespace modes
}  // namespace zero_buddy
