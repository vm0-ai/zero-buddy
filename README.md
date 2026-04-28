# Zero Buddy

M5StickC Plus voice companion firmware.

It records audio with the onboard microphone, streams ASR directly to Doubao,
sends recognized text to a Zero chat thread, and polls the thread for assistant
messages to show on the device screen.

## Setup

1. Install PlatformIO.
2. Copy `src/secrets.example.h` to `src/secrets.h`.
3. Fill in Wi-Fi credentials, Doubao ASR credentials, and Zero API settings in
   `src/secrets.h`.
4. Build and upload:

```sh
pio run -t upload
```

`src/secrets.h` is intentionally ignored by git.
