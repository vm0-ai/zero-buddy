# Zero Buddy

M5StickC Plus voice companion firmware.

It records audio with the onboard microphone, streams ASR directly to Doubao,
sends recognized text to a Zero chat thread, and polls the thread for assistant
messages to show on the device screen.

## Setup

1. Install PlatformIO.
2. Copy `src/secrets.example.h` to `src/secrets.h`.
3. Fill in Doubao ASR credentials in `src/secrets.h`.
   Wi-Fi, Zero auth token, and Zero thread id are provisioned at runtime and
   stored in NVS.
4. Build and upload:

```sh
pio run -t upload
```

`src/secrets.h` is intentionally ignored by git.

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
