# Zero Buddy State Atomic Methods

This directory contains hardware-independent state primitives for the firmware rewrite.

The methods here are intentionally small and deterministic. They do not touch Wi-Fi, RTC hardware, LittleFS, microphone input, screen state, or HTTP clients. Mode implementations can call these methods around hardware work.

## Global State

- `makeDefaultGlobalState()`
  - Creates the initial global state.
  - Starts in `DeepSleep`.
  - Sets `checkDelayMs` to 30 seconds.
  - Clears `lastMessageId`.
  - Clears assistant message queue counters.

- `setMode(state, mode)`
  - Updates `currentMode`.

- `hasUnreadAssistantMessages(state)`
  - Returns whether `assistantMessageIndex < assistantMessageCount`.

## Message Cursor

- `hasLastMessageId(state)`
  - Returns whether `lastMessageId` is non-empty.

- `copyLastMessageId(state)`
  - Copies `lastMessageId` into a `std::string`.

- `setLastMessageId(state, id)`
  - Writes the shared message cursor.
  - Used by `Recording` after sending a user message.
  - Used by `CheckAssistantMessage` after accepting a poll result.
  - Always null-terminates the fixed-size buffer.
  - Returns whether the full id fit without truncation.

- `clearLastMessageId(state)`
  - Clears the cursor.

## Check Backoff

- `resetCheckDelay(state)`
  - Sets `checkDelayMs` to the initial delay, currently 30 seconds.

- `advanceCheckDelay(state)`
  - Applies exponential backoff.
  - Caps at 1 hour.
  - Treats `0` as the initial delay.

- `nextCheckDelay(currentDelayMs)`
  - Pure backoff calculation helper.

## Assistant Queue

- `clearAssistantMessages(state)`
  - Sets `assistantMessageCount = 0`.
  - Sets `assistantMessageIndex = 0`.

- `setAssistantMessages(state, count, index)`
  - Sets queue counters after assistant files have been committed.
  - Rejects `index > count`.

- `advanceAssistantMessageIndex(state)`
  - Moves to the next assistant message if one is available.
  - Keeps `assistantMessageIndex <= assistantMessageCount`.

## Recording Commit And Abort

- `beginRecordingTurn(state)`
  - Clears assistant message queue counters for a fresh user turn.
  - Does not clear `lastMessageId`.
  - Does not change `checkDelayMs`.

- `commitRecordingMessageSent(state, userMessageId)`
  - Writes `lastMessageId = userMessageId`.
  - Resets `checkDelayMs` to 30 seconds.
  - Rejects an empty `userMessageId`.

- `abortRecording(state)`
  - No-op for global state.
  - Recording abort cleanup is mode-owned: stop mic, close files, delete voice temp files, cancel ASR/HTTP handles.

## CheckAssistantMessage Commit And Abort

- `commitAssistantCheck(state, result)`
  - Accepts a completed poll.
  - Updates `lastMessageId` when `result.newestMessageId` is present.
  - If new assistant messages were fetched, appends to queue counters and resets backoff.
  - If no new assistant messages were fetched, advances backoff.
  - Rejects invalid queue metadata.

- `abortAssistantCheck(state)`
  - No-op for global state.
  - Check abort cleanup is mode-owned: cancel network work, close files, delete temp poll files, avoid committing metadata.

## DeepSleep Plan And Abort

- `makeDeepSleepPlan(state)`
  - Returns the RTC delay that `DeepSleep.main()` should apply.

- `abortDeepSleep(state)`
  - No-op for global state.
  - DeepSleep abort cleanup is mode-owned: cancel RTC timer only.

## State Transitions

- `transitionForEvent(mode, event)`
  - Pure transition reducer for the three-mode state machine.
  - Indicates whether the current mode must be aborted before entering the next mode.

- `applyTransition(state, transition)`
  - Updates `currentMode` if the transition is valid.
