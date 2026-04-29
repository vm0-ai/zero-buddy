# Recording Mode

`Recording` is entered when the user long-presses BtnA from `DeepSleep`, `CheckAssistantMessage`, or `Read`. It records the user's voice, transcribes it, sends the recognized text as a user message, updates the global message cursor with the returned user `messageId`, resets assistant-check backoff, then returns control to the state machine.

This mode turns the screen on and owns the recording status/result rendering. It also does not decide the next mode directly. After `main` finishes or `abort` completes, the state machine performs the next transition.

## Owned Work

`Recording` owns:

- Turning the screen on for the recording session.
- Rendering the Zero dot-matrix avatar and dialogue bubble on recording status screens.
- Rendering the final recording result and keeping it visible for 5 seconds before the state machine enters `DeepSleep`.
- Selecting a CPU frequency that can reliably support audio capture.
- Canceling pending assistant-check timer state, including both RTC wake and the runtime timer used while charging.
- Clearing stored assistant messages through `clear_assistant_message` before starting a new user turn.
- Capturing microphone audio into LittleFS.
- Connecting Wi-Fi if needed.
- Running voice-to-text.
- Removing the temporary voice file after transcription.
- Sending the recognized text as a user message.
- Updating `lastMessageId` with the returned user `messageId`.
- Resetting `checkDelayMs` to the initial assistant-check delay.
- Cleaning up temporary voice data if aborted.

It does not own:

- Polling for assistant messages after sending the user message.
- Entering `CheckAssistantMessage`.
- Entering `DeepSleep`.
- Restoring CPU frequency after exit.
- Turning Wi-Fi off after exit.

## Main Process

`main(context)` is the entry process for this mode.

1. Turn the screen on.
   - Render the Zero avatar and dialogue bubble for recording status.

2. Set CPU frequency for recording.
   - Use `240MHz`, or the lowest frequency proven to reliably support microphone capture and file writes.
   - If later testing proves a lower frequency is stable, use that lower value.

3. Clear pending assistant-check timer state.
   - Cancel any pending RTC timer configured for `CheckAssistantMessage`.
   - Cancel any runtime assistant-check timer configured while the device was charging.
   - Clear mode-owned RTC wake metadata that would otherwise cause a stale check after this user turn starts.
   - Do not clear `lastMessageId` at this step; it remains the current message cursor until a new user message is successfully sent.

4. Clear stored assistant messages.
   - Remove assistant message files and assistant queue metadata from LittleFS.
   - Set `GlobalState.hasAssistantMessage = false`.
   - Turn the assistant-message LED off through `clear_assistant_message`.
   - This starts a fresh user turn and discards previously unread assistant messages.

5. Record voice to LittleFS.
   - Open a temporary voice file.
   - Capture microphone audio and append it to the file, up to 60 seconds.
   - Close or flush the file before transcription begins.
   - The file is temporary and must not be treated as persistent user data.

6. Check and connect Wi-Fi.
   - If Wi-Fi is already connected, reuse it.
   - Otherwise connect using the configured credentials.
   - If Wi-Fi cannot be connected, finish with a failure result and let the state machine transition, currently to `DeepSleep`.

7. Convert voice to text.
   - Send the recorded raw PCM file to Zero's transcription API:
     `POST https://api.vm0.ai/api/v1/audio/transcriptions`.
   - Use `Authorization: Bearer <runtime auth_token>` and
     `Content-Type: application/octet-stream`.
   - The request body is raw PCM bytes, not a WAV container.
   - Produce recognized text.
   - If transcription succeeds, delete the temporary voice file from LittleFS.
   - If transcription fails, delete the temporary voice file before returning.

8. Send the text message.
   - Send the recognized text as a user message.
   - The response must include the latest user `messageId`.
   - Do not update `lastMessageId` until the send is accepted.

9. Update global message cursor.
   - Write the returned user `messageId` to `GlobalState.lastMessageId`.
   - This becomes the `sinceId` for the next `CheckAssistantMessage` poll.

10. Reset assistant-check delay.
    - Set `GlobalState.checkDelayMs = 30s`.
    - This schedules the first assistant check soon after the user message is sent.

11. Return to the state machine.
    - `main` returns a completion result.
    - The runtime renders the success or failure result for 5 seconds.
    - The state machine switches to the next mode, currently `DeepSleep`.

## Abort Process

`abort(reason)` may be called during any step of `main`.

The main requirement is to stop recording or network work as soon as possible, remove temporary voice data, and leave global state consistent enough for another mode to start.

Typical trigger:

- A future event or state-machine decision interrupts the recording flow.
- The state machine calls `Recording.abort(reason)`.
- After `abort` completes, the state machine switches to the next mode.

## Abort Requirements

`abort(reason)` must:

- Stop microphone capture if it is active.
- Stop or signal cancellation for in-flight ASR or message-send work.
- Close any open LittleFS file handle owned by this mode.
- Delete the temporary voice file from LittleFS.
- Avoid leaving a partially written voice file.
- Avoid updating `lastMessageId` unless the user message send has completed and returned a valid `messageId`.
- Avoid resetting `checkDelayMs` unless the user message send has completed and `lastMessageId` has been accepted.
- Leave assistant message files and queue metadata in a consistent state.
- Leave the mode in a state where another mode can be entered immediately.
- Be idempotent: repeated calls must be safe.

`abort(reason)` does not need to:

- Restore CPU frequency.
- Disconnect Wi-Fi.
- Turn Wi-Fi off.
- Restore the previous screen state.
- Recreate cleared assistant messages.
- Enter `DeepSleep`.
- Enter `CheckAssistantMessage`.

Those are state machine or later mode responsibilities.

## Cleanup Notes

In addition to deleting the voice file, abort should clean up mode-owned runtime resources:

- microphone/I2S state,
- ASR session handles,
- HTTP request handles,
- temporary buffers owned by the mode,
- open LittleFS file handles.

The important rule is that abort cleans up owned work but does not perform global power restoration. CPU, Wi-Fi, and screen state are intentionally left for the next mode or system-level power policy.

## Commit Boundary

`Recording` should treat the user-message send as the commit boundary for global cursor updates.

Before the send is accepted:

- `lastMessageId` must remain unchanged.
- `checkDelayMs` must remain unchanged.
- temporary voice data may exist and must be removable by `abort`.

After the send is accepted:

- `lastMessageId` is updated to the returned user `messageId`.
- `checkDelayMs` is reset to 30 seconds.
- the voice file must already be deleted or scheduled for deletion.

If `abort` is requested before the commit boundary finishes, cleanup should remove temporary voice files and keep the previous `lastMessageId` and `checkDelayMs`.

If `abort` is requested after the commit boundary finishes, the updated `lastMessageId` and `checkDelayMs` are considered accepted and should not be rolled back.
