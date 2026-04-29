# Zero Buddy State Atomic Methods

This directory contains hardware-independent state primitives for the firmware rewrite.

The methods here are intentionally small and deterministic. They do not touch Wi-Fi, RTC hardware, LittleFS, microphone input, screen state, or HTTP clients. Mode implementations can call these methods around hardware work.

## Global State

- `makeDefaultGlobalState()`
  - Creates the initial global state.
  - Starts in `DeepSleep`.
  - Sets `checkDelayMs` to 30 seconds.
  - Clears `lastMessageId`.
  - Sets `hasAssistantMessage = false`.
  - Sets the renderer cache field to `None`.

- `setMode(state, mode)`
  - Updates `currentMode`.

- `hasAssistantMessage(state)`
  - Returns whether assistant message storage currently has any message.

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

## Assistant Message Presence

- `setHasAssistantMessage(state, hasAssistantMessage)`
  - Sets the boolean assistant message presence flag.
  - Firmware should call this from `append_assistant_message` and `clear_assistant_message`.

## Render Cache

Render cache updates are owned by `ScreenRenderer`. Mode lifecycle code should
call semantic render methods such as `render_screen_read_message`; it should not
read or branch on `lastRenderScreenState` directly.

## Recording Commit And Abort

- `commitRecordingMessageSent(state, userMessageId)`
  - Writes `lastMessageId = userMessageId`.
  - Resets `checkDelayMs` to 30 seconds.
  - Rejects an empty `userMessageId`.

- `abortRecording(state)`
  - Does not mutate shared state.
  - Recording abort cleanup is mode-owned: stop mic, close files, delete voice temp files, cancel transcription/message-send HTTP handles.

## CheckAssistantMessage Commit And Abort

- `commitAssistantCheck(state, result)`
  - Accepts a completed poll.
  - Updates `lastMessageId` when `result.newestMessageId` is present.
  - If new assistant messages were fetched, resets backoff.
  - If no new assistant messages were fetched, advances backoff.
  - Does not update `hasAssistantMessage`; `append_assistant_message` owns that.

- `abortAssistantCheck(state)`
  - Does not mutate shared state.
  - Check abort cleanup is mode-owned: cancel network work, close files, delete temp poll files, avoid committing metadata.

## Read Abort

- `abortRead(state)`
  - Does not mutate shared state.
  - Read progress is persisted by the Read mode storage helper after each completed scroll or message advance.
  - Read abort cleanup is mode-owned: close any message file handle and leave the last cleanly saved progress intact.

## DeepSleep Plan And Abort

- `makeDeepSleepPlan(state)`
  - Returns the RTC delay that `DeepSleep.main()` should apply.

- `abortDeepSleep(state)`
  - Does not mutate shared state.
  - DeepSleep abort cleanup is mode-owned: cancel RTC timer only.
