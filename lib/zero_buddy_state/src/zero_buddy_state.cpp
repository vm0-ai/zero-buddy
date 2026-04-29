#include "zero_buddy_state.h"

#include <algorithm>
#include <cstring>

namespace zero_buddy {
namespace state {
namespace {

bool hasState(const GlobalState* state) {
  return state != nullptr;
}

bool messageIdFits(const std::string& id) {
  return id.size() < kLastMessageIdBytes;
}

}  // namespace

GlobalState makeDefaultGlobalState() {
  return GlobalState();
}

void setMode(GlobalState* state, Mode mode) {
  if (!hasState(state)) {
    return;
  }
  state->currentMode = mode;
}

bool hasLastMessageId(const GlobalState& state) {
  return state.lastMessageId[0] != '\0';
}

std::string copyLastMessageId(const GlobalState& state) {
  return std::string(state.lastMessageId);
}

bool setLastMessageId(GlobalState* state, const std::string& id) {
  if (!hasState(state)) {
    return false;
  }

  const size_t copy_len = std::min(id.size(), kLastMessageIdBytes - 1);
  std::memset(state->lastMessageId, 0, kLastMessageIdBytes);
  if (copy_len > 0) {
    std::memcpy(state->lastMessageId, id.data(), copy_len);
  }
  return id.size() < kLastMessageIdBytes;
}

void clearLastMessageId(GlobalState* state) {
  if (!hasState(state)) {
    return;
  }
  std::memset(state->lastMessageId, 0, kLastMessageIdBytes);
}

uint32_t nextCheckDelay(uint32_t currentDelayMs,
                        uint32_t initialDelayMs,
                        uint32_t maxDelayMs) {
  if (initialDelayMs == 0) {
    initialDelayMs = kInitialCheckDelayMs;
  }
  if (maxDelayMs == 0) {
    maxDelayMs = initialDelayMs;
  }
  if (maxDelayMs < initialDelayMs) {
    maxDelayMs = initialDelayMs;
  }
  if (currentDelayMs == 0) {
    return initialDelayMs;
  }
  if (currentDelayMs >= maxDelayMs / 2) {
    return maxDelayMs;
  }
  return std::max(initialDelayMs, currentDelayMs * 2);
}

void resetCheckDelay(GlobalState* state, uint32_t initialDelayMs) {
  if (!hasState(state)) {
    return;
  }
  state->checkDelayMs = initialDelayMs == 0 ? kInitialCheckDelayMs : initialDelayMs;
}

void advanceCheckDelay(GlobalState* state,
                       uint32_t initialDelayMs,
                       uint32_t maxDelayMs) {
  if (!hasState(state)) {
    return;
  }
  state->checkDelayMs = nextCheckDelay(state->checkDelayMs, initialDelayMs, maxDelayMs);
}

bool hasAssistantMessage(const GlobalState& state) {
  return state.hasAssistantMessage;
}

void setHasAssistantMessage(GlobalState* state, bool hasAssistantMessage) {
  if (!hasState(state)) {
    return;
  }
  state->hasAssistantMessage = hasAssistantMessage;
}

bool commitRecordingMessageSent(GlobalState* state, const std::string& userMessageId) {
  if (!hasState(state) || userMessageId.empty() || !messageIdFits(userMessageId)) {
    return false;
  }
  setLastMessageId(state, userMessageId);
  resetCheckDelay(state);
  return true;
}

void abortRecording(GlobalState*) {}

bool commitAssistantCheck(GlobalState* state, const AssistantCheckResult& result) {
  if (!hasState(state)) {
    return false;
  }
  if (!result.hasNewAssistantMessages && !result.assistantMessages.empty()) {
    return false;
  }
  if (!result.newestMessageId.empty() && !messageIdFits(result.newestMessageId)) {
    return false;
  }

  if (result.hasNewAssistantMessages) {
    if (result.assistantMessages.empty()) {
      return false;
    }
    resetCheckDelay(state);
  } else {
    advanceCheckDelay(state);
  }

  if (!result.newestMessageId.empty()) {
    setLastMessageId(state, result.newestMessageId);
  }
  return true;
}

void abortAssistantCheck(GlobalState*) {}

void abortRead(GlobalState*) {}

DeepSleepPlan makeDeepSleepPlan(const GlobalState& state) {
  DeepSleepPlan plan;
  plan.rtcDelayMs = state.checkDelayMs == 0 ? kInitialCheckDelayMs : state.checkDelayMs;
  return plan;
}

void abortDeepSleep(GlobalState*) {}

}  // namespace state
}  // namespace zero_buddy
