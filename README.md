# Zero Buddy

Firmware for a small AI voice companion on M5StickC Plus / M5StickC-compatible
ESP32 hardware.

Zero Buddy records raw microphone audio, sends PCM bytes to Zero's
transcription API, sends the recognized text to a Zero chat thread, polls that
thread for assistant replies, stores unread assistant messages in LittleFS, and
renders them on the device screen.

## Hardware

- Target board: `m5stick-c`
- Display: M5StickC Plus style screen
- Input:
  - BtnA short press: open `Read`
  - BtnA long press: open `Recording`
  - BtnB press: restart
  - Hold BtnA while pressing BtnB: clear runtime provisioning config and restart
- Status LED reflects whether LittleFS currently has unread assistant messages.

## Core Modes

The firmware has four business modes:

- `DeepSleep`
  - Unplugged idle state.
  - Checks charging before sleep preparation.
  - If unplugged, configures RTC wake, BtnA wake, turns the screen off,
    disconnects Wi-Fi, and enters ESP32 deep sleep.
  - If plugged, returns to `Read` without configuring RTC wake.
- `CheckAssistantMessage`
  - Connects Wi-Fi if needed, polls the Zero thread from `lastMessageId`,
    appends new assistant messages to LittleFS, and updates polling backoff.
- `Read`
  - Turns on the screen and lets the user read stored assistant messages.
  - BtnA short press scrolls and advances messages.
  - When all messages are read, it clears assistant message storage and LED.
  - A 15 second idle timeout completes the mode.
- `Recording`
  - Turns on the screen, records up to the configured capture window, transcribes
    raw PCM, sends the user message, updates `lastMessageId`, resets check delay,
    shows success/failure for 5 seconds, then returns to the state machine.

Unplugged flow:

```mermaid
stateDiagram-v2
    [*] --> DeepSleep
    DeepSleep --> CheckAssistantMessage: RTC wake
    CheckAssistantMessage --> DeepSleep: check complete / failed
    DeepSleep --> Read: BtnA short press
    Read --> DeepSleep: idle timeout / read complete
    DeepSleep --> Recording: BtnA long press
    CheckAssistantMessage --> Read: BtnA short press / abort check
    CheckAssistantMessage --> Recording: BtnA long press / abort check
    Read --> Recording: BtnA long press / abort read
    Recording --> DeepSleep: success / failed / aborted
```

Plugged flow:

```mermaid
stateDiagram-v2
    [*] --> Read
    Read --> CheckAssistantMessage: idle timeout / read complete
    CheckAssistantMessage --> Read: check complete / failed
    Read --> Recording: BtnA long press / abort read
    CheckAssistantMessage --> Read: BtnA short press / abort check
    CheckAssistantMessage --> Recording: BtnA long press / abort check
    Recording --> Read: success / failed / aborted
```

Plugged operation does not schedule RTC wake. The device stays in the foreground
`Read -> CheckAssistantMessage -> Read` loop until external power is removed.

## Provisioning

Normal builds do not require secrets at compile time. Runtime configuration is
stored in NVS namespace `zero_runtime`:

- `wifi_ssid`
- `wifi_password`
- `auth_token`
- `thread_id`

Setup is BLE-first:

1. Open `https://bb0.ai` in Google Chrome.
2. Connect to the advertised `Zero-Buddy-xxxx` device through Web Bluetooth.
3. The page writes Wi-Fi credentials to the device over BLE.
4. BLE stops after Wi-Fi is configured.
5. The device requests a device code over HTTPS and shows it on screen.
6. Approve the device code on `bb0.ai`.
7. The device polls for the approved token/thread and persists them to NVS.

BLE discovery uses service UUID:

```text
bb000001-8f16-4b2a-9bb0-000000000001
```

The info characteristic is:

```text
bb000002-8f16-4b2a-9bb0-000000000001
```

Advertising only exposes coarse setup state and non-sensitive flags. Tokens,
thread ids, poll tokens, and device codes are not advertised.

## Optional Compile-Time Defaults

For a pre-provisioned build, copy the template:

```sh
cp src/secrets.example.h src/secrets.h
```

Then fill any optional macros:

```cpp
#define ZERO_BUDDY_WIFI_SSID ""
#define ZERO_BUDDY_WIFI_PASSWORD ""
#define ZERO_BUDDY_PAT ""
#define ZERO_BUDDY_THREAD_ID ""
```

Runtime Wi-Fi in NVS takes precedence over compile-time Wi-Fi. Compile-time PAT
and thread id, when provided, take precedence over older NVS auth/thread values.

`src/secrets.h` is ignored by git. Do not commit real credentials.

## Build, Test, Upload

Install PlatformIO, then run:

```sh
pio test -e native
pio run -e m5stick-c
pio run -e m5stick-c -t upload
```

The default serial port is configured in `platformio.ini`:

```text
/dev/cu.usbserial-9552026538
```

If your device appears on a different port, update `platformio.ini` or pass the
port through PlatformIO.

Serial monitor:

```sh
pio device monitor -e m5stick-c
```

## State Storage

- RTC memory:
  - `currentMode`
  - `checkDelayMs`
  - `lastMessageId`
  - `hasAssistantMessage`
  - `lastRenderScreenState`
- LittleFS:
  - assistant message files
  - assistant queue/read progress manifest
  - temporary raw PCM recording file
- NVS:
  - runtime Wi-Fi, auth token, and thread id

`lastRenderScreenState` is only a screen renderer cache. Business modes must not
read it for state transitions.

## Project Layout

- `src/main.cpp`: hardware orchestration, provisioning, mode wiring
- `src/screen_renderer.*`: semantic screen rendering and render cache
- `lib/zero_buddy_state`: hardware-independent shared state helpers
- `lib/zero_buddy_modes`: mode lifecycle implementations
- `lib/zero_buddy_core`: parsing and pure helper functions
- `docs/state.md`: full state model
- `docs/boot-preflight.md`: startup/provisioning flow
- `docs/modes/*.md`: mode-specific lifecycle notes
- `test/*`: native Unity tests
