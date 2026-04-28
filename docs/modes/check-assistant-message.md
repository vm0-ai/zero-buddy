# CheckAssistantMessage Mode

`CheckAssistantMessage` is a short-lived mode entered after an RTC wake. Its job is to check whether the assistant has produced new messages, persist any results, update the global message cursor and check backoff, then return control to the state machine.

This mode does not own display rendering or direct LED operations. It also does not decide the next mode directly. After `main` finishes or `abort` completes, the state machine performs the next transition.

## Owned Work

`CheckAssistantMessage` owns:

- CPU frequency selection for the check operation.
- Wi-Fi connection setup needed for HTTP communication.
- Assistant message polling.
- Appending fetched assistant messages through `append_assistant_message`.
- Updating `lastMessageId` after a successful poll.
- Updating the next check backoff.
- Keeping partial LittleFS writes and global state from becoming dirty if aborted.

It does not own:

- Rendering, screen on/off, or brightness changes.
- Direct LED on/off calls.
- User message sending.
- Reading stored assistant messages later.
- Rolling CPU frequency back after exit.
- Turning Wi-Fi off after exit.

## Main Process

`main(context)` is the entry process for this mode.

1. Set CPU frequency for network work.
   - Use the lowest CPU frequency that can reliably support Wi-Fi and HTTP communication.
   - This is a mode entry concern because Wi-Fi and TLS/HTTP need more resources than deep sleep bookkeeping.

2. Check and connect Wi-Fi.
   - If Wi-Fi is already connected, reuse it.
   - Otherwise connect using the configured credentials.
   - If Wi-Fi cannot be connected, finish the mode as a failed or empty check and let the state machine return to `DeepSleep`.

3. Query latest assistant messages.
   - If `GlobalState.lastMessageId` is empty, skip polling entirely.
   - Otherwise use `GlobalState.lastMessageId` as `sinceId`.
   - The query result should identify:
     - the newest message id observed,
     - the assistant messages that should be stored,
     - whether any new assistant message was found.

4. Append result to LittleFS and update global queue state.
   - Store assistant message bodies through `append_assistant_message`.
   - The helper updates `GlobalState.hasAssistantMessage`.
   - The helper owns the LED transition: if LittleFS had no unread assistant message before the append, it turns the LED on; otherwise it leaves the LED alone.
   - If new assistant messages are appended, existing unread messages remain in place and the new messages are added after them.
   - These writes should be committed in an order that avoids dirty state if the device is interrupted.

5. Update the exponential backoff state.
   - If new assistant messages were fetched, reset the check backoff to index `0`.
   - If no new assistant message was fetched, increment the backoff index by `1`.
   - The computed delay is capped at 1 hour.
   - The persisted global value is the next RTC check delay, represented by `GlobalState.checkDelayMs`.

6. Return to the state machine.
   - `main` returns a completion result.
   - The state machine switches to the next mode, currently `DeepSleep`.

## Abort Process

`abort(reason)` may be called during any step of `main`.

The main requirement is immediate termination of in-flight work, followed by cleanup that leaves LittleFS and global state consistent enough for another mode to start.

Typical trigger:

- BtnA short press while `CheckAssistantMessage` is running.
- The state machine calls `CheckAssistantMessage.abort("btn_a_short_press")`.
- After `abort` completes, the state machine switches to `Read`.
- BtnA long press while `CheckAssistantMessage` is running.
- The state machine calls `CheckAssistantMessage.abort("btn_a_long_press")`.
- After `abort` completes, the state machine switches to `Recording`.

## Abort Requirements

`abort(reason)` must:

- Signal any in-flight Wi-Fi, HTTP, TLS, or polling operation to stop as soon as possible.
- Stop writing assistant message files.
- Close any open LittleFS file handles owned by this mode.
- Delete or ignore temporary files created by an incomplete poll.
- Avoid publishing partially written assistant queue metadata.
- Avoid setting `hasAssistantMessage` unless the corresponding message files are fully written.
- Avoid advancing `lastMessageId` unless the poll result has been fully accepted.
- Avoid updating `checkDelayMs` from a partial or aborted check.
- Leave the mode in a state where another mode can be entered immediately.
- Be idempotent: repeated calls must be safe.

`abort(reason)` does not need to:

- Restore the previous CPU frequency.
- Disconnect Wi-Fi.
- Turn off Wi-Fi.
- Enter `DeepSleep`.
- Enter `Read`.
- Enter `Recording`.

Those are state machine or later mode responsibilities.

## Commit Boundary

The mode should treat LittleFS and global state updates as a small transaction.

Recommended order:

1. Append fetched assistant messages to temporary files.
2. Validate all files were written successfully.
3. Rename or promote temporary files into their final paths.
4. Update assistant queue metadata.
5. Update `hasAssistantMessage`.
6. Update `lastMessageId`.
7. Update `checkDelayMs`.

If `abort` is requested before the commit boundary finishes, cleanup should remove temporary files and keep the previous global state.

If `abort` is requested after the commit boundary finishes, the new global state is considered accepted and should not be rolled back.

## Backoff

Backoff is represented by `GlobalState.checkDelayMs`.

The policy is exponential backoff capped at 1 hour:

```cpp
nextDelay = min(currentDelay * 2, 60 * 60 * 1000);
```

The initial delay and exact sequence are implementation details, but the state machine should only need the final `checkDelayMs` value when scheduling the next RTC wake.

When new assistant messages are found:

```cpp
checkDelayMs = initialCheckDelayMs;
```

When no new assistant message is found:

```cpp
checkDelayMs = min(checkDelayMs * 2, oneHourMs);
```

If the check is aborted, `checkDelayMs` should remain unchanged.
