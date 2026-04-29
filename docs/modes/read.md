# Read Mode

`Read` lets the user inspect assistant messages stored in LittleFS. It owns screen-on rendering for this reading session and owns persistence of reading progress.

## Owned Resources

- Screen-on display state while reading.
- CPU frequency suitable for screen rendering and button polling.
- Assistant message file reads.
- Assistant queue metadata updates:
  - current assistant message index
  - `scrollTop` for that message
- Clearing assistant messages after the final stored message has been fully read.
- The 15 second idle timeout while the user is in `Read`.
- The 4-bar battery icon overlay in the top-right corner while reading.
- Receiving a runtime assistant-check timer event when `Read` is being used as the foreground charging mode.

## Not Owned

- Assistant message creation.
- Direct mutation of `GlobalState.hasAssistantMessage`.
- Direct assistant LED on/off control.
- Wi-Fi state.
- RTC scheduling.
- Starting or clearing the runtime assistant-check timer.
- Screen-off policy after the mode exits.

## Main Flow

1. Turn the screen on.
2. Set CPU frequency to a display-safe frequency.
3. Load assistant message count and persisted read progress.
4. If there are no assistant messages:
   - Render the empty state.
   - Wait up to 15 seconds for input.
   - BtnA short press keeps the empty state visible and restarts the 15 second idle timeout.
   - BtnA long press aborts to `Recording`.
   - Timeout completes to `DeepSleep`.
5. If there are assistant messages:
   - Clamp the persisted message index and `scrollTop` to valid values.
   - Load the current assistant message from LittleFS.
   - Render the message at the persisted `scrollTop`.
6. While reading:
   - BtnA short press scrolls the current message by one viewport.
   - If the current message is already fully scrolled, BtnA short press advances to the next assistant message.
   - Each scroll or message advance persists the new message index and `scrollTop`.
   - Each BtnA short press restarts the 15 second idle timeout.
   - Each BtnA short press immediately refreshes the top-right battery icon and schedules one more battery refresh 5 seconds later.
   - If the final assistant message is already fully scrolled, BtnA short press calls `clear_assistant_message`, then renders the empty state.
   - Idle timeout completes the mode and enters `DeepSleep`.
   - Runtime assistant-check timer expiry aborts the mode with `check_due`, then enters `CheckAssistantMessage`.
   - BtnA long press aborts the mode before entering `Recording`.

## Rendering

- Assistant message rendering uses a Chinese-capable M5GFX efont so Chinese text is visible.
- The top-left header shows the current message position, such as `1/1`.
- The top-right header shows the current 4-bar battery icon.
- The message body is clipped to the viewport and rendered from the persisted `scrollTop`.
- Full-screen render work is skipped inside `ScreenRenderer` when its cached render target already matches the same read screen.
- The 80x45 Zero dot-matrix avatar is rendered in:
  - `Recording` status screens.
  - The `Read` empty-message screen.

## Abort Flow

`abort(reason)` may be called while `Read.main()` is waiting for input or between file/render operations.

Abort must:

- Mark the mode as aborted.
- Cancel the 15 second idle timeout.
- Close any file handles owned by the mode.
- Leave persisted read progress at the last cleanly saved value.
- Return the firmware to a state where another mode can enter.

Abort must not:

- Change `GlobalState.hasAssistantMessage`.
- Turn the assistant LED on or off.
- Turn the screen off.
- Change Wi-Fi state.
- Restore CPU frequency.

This keeps `Read` interruption cheap: a long press can abort reading and then enter `Recording`, whose own entry flow clears assistant messages through `clear_assistant_message`. A runtime assistant-check timer can also abort reading with `check_due` while the device is charging; this does not clear assistant messages.
