# Zero Buddy

M5StickC Plus voice companion firmware.

It records raw PCM audio with the onboard microphone, sends it to Zero's
transcription API, sends recognized text to a Zero chat thread, and polls the
thread for assistant messages to show on the device screen.

## Setup

1. Install PlatformIO.
2. Build and upload:

```sh
pio run -t upload
```

Wi-Fi, Zero auth token, Zero thread id, and transcription auth are provisioned
at runtime and stored in NVS. `src/secrets.h` is still ignored by git for local
experiments, but no secret is required for the normal firmware build.

For a pre-provisioned build, copy `src/secrets.example.h` to `src/secrets.h`
and fill the optional `ZERO_BUDDY_WIFI_SSID`, `ZERO_BUDDY_WIFI_PASSWORD`,
`ZERO_BUDDY_PAT`, and `ZERO_BUDDY_THREAD_ID` macros. Runtime Wi-Fi in NVS still
takes precedence over compile-time Wi-Fi. A compile-time PAT or thread id, when
provided, is treated as the firmware source of truth and is not overridden by
older NVS auth or thread values.

To reset runtime provisioning, hold BtnA and press BtnB. This clears the runtime
NVS config and starts setup again. Holding BtnA + BtnB through boot performs the
same reset.

Runtime Wi-Fi setup is BLE-only. Open `https://bb0.ai` in Chrome, connect to
`Zero-Buddy-xxxx` with Web Bluetooth, and the page writes Wi-Fi credentials back
to the device. After Wi-Fi is configured, BLE stops and device auth continues
over HTTPS with an on-device device-code countdown.

Provisioning discovery uses BLE service UUID
`bb000001-8f16-4b2a-9bb0-000000000001`. Advertising exposes only coarse Wi-Fi
setup status; the info characteristic `bb000002-8f16-4b2a-9bb0-000000000001`
notifies JSON state changes while BLE setup is active.
