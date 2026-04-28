#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "zero_buddy_state.h"

namespace zero_buddy {
namespace modes {

enum class ModeRunStatus {
  Completed,
  Aborted,
  Failed,
};

enum class ModeRunError {
  None,
  WifiUnavailable,
  AssistantPollFailed,
  AssistantStorageFailed,
  InvalidStateCommit,
  AssistantClearFailed,
  VoiceCaptureFailed,
  VoiceToTextFailed,
  MessageSendFailed,
};

struct ModeRunResult {
  ModeRunStatus status = ModeRunStatus::Completed;
  ModeRunError error = ModeRunError::None;
};

class CheckAssistantMessageOps {
 public:
  virtual ~CheckAssistantMessageOps() = default;

  virtual void setCpuForNetwork() = 0;
  virtual bool ensureWifiConnected() = 0;
  virtual bool pollAssistantMessages(const std::string& since_id,
                                     state::AssistantCheckResult* result_out) = 0;
  virtual bool appendAssistantMessages(const state::AssistantCheckResult& result) = 0;

  virtual void cancelNetwork() = 0;
  virtual void closeFiles() = 0;
  virtual void cleanupTempFiles() = 0;
};

class CheckAssistantMessageMode {
 public:
  CheckAssistantMessageMode(state::GlobalState* state, CheckAssistantMessageOps* ops);

  ModeRunResult main();
  void abort(const char* reason);

  bool abortRequested() const;
  const char* abortReason() const;

 private:
  bool shouldStop() const;
  ModeRunResult abortedResult() const;
  void advanceBackoffAfterEmptyOrFailedCheck();

  state::GlobalState* state_;
  CheckAssistantMessageOps* ops_;
  bool abort_requested_ = false;
  bool abort_cleanup_done_ = false;
  const char* abort_reason_ = nullptr;
};

class DeepSleepOps {
 public:
  virtual ~DeepSleepOps() = default;

  virtual void configureRtcWake(uint32_t delay_ms) = 0;
  virtual void configureBtnAWake() = 0;
  virtual void screenOff() = 0;
  virtual void disconnectWifi() = 0;
  virtual void enterCpuHibernate() = 0;

  virtual void cancelRtcWake() = 0;
};

class DeepSleepMode {
 public:
  DeepSleepMode(state::GlobalState* state, DeepSleepOps* ops);

  ModeRunResult main();
  void abort(const char* reason);

  bool abortRequested() const;
  const char* abortReason() const;

 private:
  bool shouldStop() const;
  ModeRunResult abortedResult() const;

  state::GlobalState* state_;
  DeepSleepOps* ops_;
  bool abort_requested_ = false;
  bool abort_cleanup_done_ = false;
  const char* abort_reason_ = nullptr;
};

enum class ReadInput {
  Timeout,
  ShortPress,
  LongPress,
};

struct ReadProgress {
  size_t messageIndex = 0;
  size_t scrollTop = 0;
};

class ReadOps {
 public:
  virtual ~ReadOps() = default;

  virtual void screenOn() = 0;
  virtual void setCpuForReading() = 0;
  virtual size_t storedAssistantMessageCount() = 0;
  virtual bool loadReadProgress(ReadProgress* progress_out) = 0;
  virtual bool loadAssistantMessage(size_t index, std::string* message_out) = 0;
  virtual size_t maxScrollTopForMessage(const std::string& message) = 0;
  virtual size_t scrollStep() = 0;
  virtual bool saveReadProgress(size_t count, size_t index, size_t scroll_top) = 0;
  virtual bool clearAssistantMessages() = 0;
  virtual void renderNoAssistantMessage() = 0;
  virtual void renderAssistantMessage(size_t index,
                                      size_t count,
                                      const std::string& message,
                                      size_t scroll_top) = 0;
  virtual ReadInput waitForInput(uint32_t timeout_ms) = 0;

  virtual void cancelIdleTimer() = 0;
  virtual void closeFiles() = 0;
};

class ReadMode {
 public:
  ReadMode(state::GlobalState* state, ReadOps* ops);

  ModeRunResult main();
  void abort(const char* reason);

  bool abortRequested() const;
  const char* abortReason() const;

 private:
  bool shouldStop() const;
  ModeRunResult abortedResult() const;
  ModeRunResult renderEmptyUntilIdle();
  bool loadCurrentMessage(size_t index, std::string* message_out) const;
  bool clampAndPersistProgress(size_t count,
                               const std::string& message,
                               ReadProgress* progress);
  bool persistProgress(size_t count, const ReadProgress& progress);

  state::GlobalState* state_;
  ReadOps* ops_;
  bool abort_requested_ = false;
  bool abort_cleanup_done_ = false;
  const char* abort_reason_ = nullptr;
};

class RecordingOps {
 public:
  virtual ~RecordingOps() = default;

  virtual void screenOn() = 0;
  virtual void setCpuForRecording() = 0;
  virtual void cancelRtcWake() = 0;
  virtual bool clearAssistantMessages() = 0;
  virtual bool recordVoiceToFile() = 0;
  virtual bool ensureWifiConnected() = 0;
  virtual bool voiceToText(std::string* text_out) = 0;
  virtual void deleteVoiceFile() = 0;
  virtual bool sendTextMessage(const std::string& text, std::string* user_message_id_out) = 0;

  virtual void stopVoiceRecording() = 0;
  virtual void cancelVoiceToText() = 0;
  virtual void cancelTextMessageSend() = 0;
  virtual void closeFiles() = 0;
};

class RecordingMode {
 public:
  RecordingMode(state::GlobalState* state, RecordingOps* ops);

  ModeRunResult main();
  void abort(const char* reason);

  bool abortRequested() const;
  const char* abortReason() const;

 private:
  bool shouldStop() const;
  ModeRunResult abortedResult() const;
  void cleanupVoiceFile();

  state::GlobalState* state_;
  RecordingOps* ops_;
  bool abort_requested_ = false;
  bool abort_cleanup_done_ = false;
  bool voice_file_may_exist_ = false;
  const char* abort_reason_ = nullptr;
};

}  // namespace modes
}  // namespace zero_buddy
