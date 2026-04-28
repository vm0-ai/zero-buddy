#include "zero_buddy_modes.h"

namespace zero_buddy {
namespace modes {
namespace {

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
