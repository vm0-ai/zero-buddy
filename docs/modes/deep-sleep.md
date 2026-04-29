# DeepSleep Mode

`DeepSleep` is the low-power resting mode. Its job is to configure the next assistant-check wake and prepare the device for ESP32 deep sleep.

This mode owns only the screen-off power action, not display rendering or screen-on behavior. If the RTC wakes the device, the state machine enters `CheckAssistantMessage`. If BtnA wakes the device, the boot path measures the press: a short press enters `Read`, and a long press enters `Recording`. Plugged foreground behavior is handled before entering `DeepSleep`, so charging devices return to `Read` without scheduling RTC wake.

## Owned Work

`DeepSleep` owns:

- Scheduling the RTC timer using `GlobalState.checkDelayMs`.
- Checking whether the device is charging before RTC scheduling.
- Turning the screen off.
- Disconnecting Wi-Fi before sleep.
- Entering ESP32 deep sleep.
- Keeping the RTC timer from being left active if the mode is aborted before actual deep sleep starts.

It does not own:

- Restoring screen state after abort.
- Restoring CPU frequency after abort.
- Restoring Wi-Fi state after abort.
- Reading assistant messages.
- Checking assistant messages.
- Recording audio.
- Creating, clearing, or reflecting assistant-message LED state.
- Turning the screen on or rendering status/UI.

## Main Process

`main(context)` is the entry process for this mode.

1. Check charging state.
   - If charging is active, return to the state machine without configuring RTC wake or entering CPU hibernation.
   - The state machine enters `Read`.
   - Do not turn the screen off or disconnect Wi-Fi on this path; `Read` owns its own screen setup.

2. Configure the RTC wake timer.
   - Use `GlobalState.checkDelayMs` as the current delay.
   - This timer is the next scheduled `CheckAssistantMessage` wake.

3. Configure BtnA as a wake source.
   - BtnA must be able to wake the device from ESP32 deep sleep.
   - The wake itself does not prove whether the user intended a short press or long press; the firmware must validate the hold after boot.

4. Turn the screen off.
   - Blank the display and set brightness to `0`.
   - Do not render any status text or UI.
   - Clear the renderer's cached screen state.

5. Disconnect Wi-Fi.
   - Disconnect before sleep to reduce power and avoid leaving the radio active.

6. Enter CPU hibernation.
   - Call the ESP32 deep sleep entry point.
   - This must be the final operation in `main`.
   - This is a non-returning step. After this point, the CPU cannot run mode code until a wake source restarts the firmware.

## Abort Process

`abort(reason)` may be called while `main` is preparing for sleep, before the final deep sleep entry call.

Typical trigger:

- BtnA long press is detected while `DeepSleep.main()` is still running.
- The state machine calls `DeepSleep.abort("btn_a_long_press")`.
- After `abort` completes, the state machine switches to `Recording`.

For a BtnA wake that is released before the long-press threshold, the restarted firmware enters `Read` instead of aborting a still-running `DeepSleep.main()`.

## Abort Requirements

`abort(reason)` must:

- Cancel the pending RTC timer wake configured by this mode.
- Avoid entering ESP32 deep sleep after abort has been requested.
- Leave the mode in a state where another mode can be entered immediately.
- Be idempotent: repeated calls must be safe.

`abort(reason)` does not need to:

- Restore CPU frequency.
- Reconnect Wi-Fi.
- Turn Wi-Fi back on.
- Restore the screen.
- Change the assistant-message LED.
- Enter `Recording`.
- Enter `CheckAssistantMessage`.

Those are state machine or later mode responsibilities.

## Long Press Handling

There are two distinct phases:

- Before the final deep sleep call
  - The CPU is still running.
  - A long press can be detected if `DeepSleep.main()` is written with abort checkpoints or a GPIO/button interrupt.
  - If detected, call `abort("btn_a_long_press")` and let the state machine enter `Recording`.

- After the final deep sleep call
  - The CPU is stopped.
  - The firmware cannot run code or measure a long press while asleep.
  - BtnA can only act as a wake source.
  - After wake, firmware starts again and must check whether BtnA is still held long enough to count as a long press.
  - If it is not held long enough, the wake is treated as a short press and enters `Read`.

Because of this, `DeepSleep.main()` should keep the pre-sleep preparation short and abortable, and the boot path should validate BtnA hold duration after a button wake.
