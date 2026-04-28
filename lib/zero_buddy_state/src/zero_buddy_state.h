#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace zero_buddy {
namespace state {

constexpr uint32_t kInitialCheckDelayMs = 30UL * 1000UL;
constexpr uint32_t kMaxCheckDelayMs = 60UL * 60UL * 1000UL;
constexpr size_t kLastMessageIdBytes = 160;

enum class Mode : uint8_t {
  DeepSleep,
  CheckAssistantMessage,
  Read,
  Recording,
};

enum class Event : uint8_t {
  RtcWake,
  BtnAShortPress,
  BtnALongPress,
  CheckComplete,
  ReadComplete,
  RecordingComplete,
};

struct GlobalState {
  Mode currentMode = Mode::DeepSleep;
  uint32_t checkDelayMs = kInitialCheckDelayMs;
  char lastMessageId[kLastMessageIdBytes] = {0};
  bool hasAssistantMessage = false;
};

struct AssistantCheckResult {
  bool hasNewAssistantMessages = false;
  std::string newestMessageId;
  std::vector<std::string> assistantMessages;
};

struct DeepSleepPlan {
  uint32_t rtcDelayMs = kInitialCheckDelayMs;
};

struct Transition {
  bool valid = false;
  bool requiresAbort = false;
  Mode nextMode = Mode::DeepSleep;
};

GlobalState makeDefaultGlobalState();

void setMode(GlobalState* state, Mode mode);

bool hasLastMessageId(const GlobalState& state);
std::string copyLastMessageId(const GlobalState& state);
bool setLastMessageId(GlobalState* state, const std::string& id);
void clearLastMessageId(GlobalState* state);

uint32_t nextCheckDelay(uint32_t currentDelayMs,
                        uint32_t initialDelayMs = kInitialCheckDelayMs,
                        uint32_t maxDelayMs = kMaxCheckDelayMs);
void resetCheckDelay(GlobalState* state, uint32_t initialDelayMs = kInitialCheckDelayMs);
void advanceCheckDelay(GlobalState* state,
                       uint32_t initialDelayMs = kInitialCheckDelayMs,
                       uint32_t maxDelayMs = kMaxCheckDelayMs);

bool hasAssistantMessage(const GlobalState& state);
void setHasAssistantMessage(GlobalState* state, bool hasAssistantMessage);

void beginRecordingTurn(GlobalState* state);
bool commitRecordingMessageSent(GlobalState* state, const std::string& userMessageId);
void abortRecording(GlobalState* state);

bool commitAssistantCheck(GlobalState* state, const AssistantCheckResult& result);
void abortAssistantCheck(GlobalState* state);

void abortRead(GlobalState* state);

DeepSleepPlan makeDeepSleepPlan(const GlobalState& state);
void abortDeepSleep(GlobalState* state);

Transition transitionForEvent(Mode mode, Event event);
bool applyTransition(GlobalState* state, const Transition& transition);

}  // namespace state
}  // namespace zero_buddy
