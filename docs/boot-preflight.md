# Boot Preflight

Every startup runs a preflight chain before entering the normal mode flow. The
chain derives its next step from persisted runtime configuration rather than a
saved "current setup step" pointer.

## Runtime Configuration

Runtime configuration is stored in NVS namespace `zero_runtime`.

- `wifi_ssid`
- `wifi_password`
- `auth_token`
- `thread_id`

ASR credentials stay in firmware secrets. Wi-Fi, Zero auth, and Zero thread are
not compile-time values.

## Reset

If BtnA is held while BtnB is pressed, the firmware clears the full
`zero_runtime` NVS namespace and restarts. Holding BtnA + BtnB through boot
performs the same reset before continuing through the same preflight checks.

## Network Stage

Goal: obtain usable Wi-Fi.

1. If `wifi_ssid` exists, try to connect.
2. If Wi-Fi is missing or connection fails, start BLE provisioning.
3. The device advertises as `Zero-Buddy-xxxx`, where `xxxx` is derived from the chip id.
   The advertised service UUID remains
   `bb000001-8f16-4b2a-9bb0-000000000001`.
4. The user opens `https://bb0.ai` in Chrome and uses Web Bluetooth to connect
   to the device.
5. The web page writes Wi-Fi credentials to BLE config characteristic
   `bb000003-8f16-4b2a-9bb0-000000000001`:
   - `{"wifi_ssid":"...","wifi_password":"..."}`
6. Submitted Wi-Fi credentials are written to NVS immediately.
7. BLE provisioning is stopped before the device connects to Wi-Fi.

Wi-Fi failure does not clear `auth_token` or `thread_id`.

## Identity Stage

Goal: obtain usable Zero auth.

1. If `auth_token` is missing, request a bb0 device code over HTTPS:
   - `POST https://vm0-api.vm6.ai/api/device-token`
   - body:
     `{"device_type":"bb0","device_id":"Zero-Buddy-F1B4","firmware_version":"0.1.0"}`
2. The response must include `device_code`, `poll_token`, `expires_in`, and
   `interval`.
3. Display the returned `device_code` with a 5 minute countdown.
4. Poll `POST https://vm0-api.vm6.ai/api/device-token/poll` with
   `device_code` and `poll_token` until the server returns `api_token` and
   `thread_id`.
5. Persist both values to NVS as soon as they are received.

The bb0 device polls for token approval itself. The web page only confirms the
human-visible device code with the server; it does not receive or write the
device API token over BLE.

If the device-code API or poll API is reachable but rejects the request or
returns an unexpected response, the firmware shows "temporarily unavailable" and
does not continuously retry. A plain network failure still returns to Wi-Fi
repair or retries polling until the device code expires.

## BLE Provisioning Status

BLE advertising and scan-response data expose only coarse, non-sensitive Wi-Fi
setup status. They must never include `api_token`, `thread_id`, `poll_token`, or
`device_code`.

- Device name: `Zero-Buddy-xxxx`
- GATT service UUID: `bb000001-8f16-4b2a-9bb0-000000000001`
- 32-bit service data UUID: `0xbb000001`
- Service data payload, 6 bytes:
  - byte 0: protocol version, currently `1`
  - byte 1: provisioning state enum
  - byte 2: flags
  - bytes 3-4: little-endian short device id
  - byte 5: error code, `0` when absent

Provisioning state enum:

- `0` setup
- `1` wifi_received
- `2` wifi_connecting
- `3` wifi_connected
- `4` device_code_pending
- `5` device_code_ready
- `6` binding
- `7` provisioned
- `8` error

Flags:

- `0x01` Wi-Fi configured
- `0x02` auth token configured
- `0x04` thread configured
- `0x80` error present

The info characteristic returns JSON and notifies during Wi-Fi provisioning:

```json
{
  "protocol": "bb0.provisioning.v1",
  "device_id": "Zero-Buddy-F1B4",
  "firmware_version": "0.1.0",
  "provisioning_state": "setup",
  "device_code": null,
  "ble_session_nonce": null,
  "error_code": null
}
```

## Session Stage

Goal: obtain a usable `thread_id`.

1. If `thread_id` exists, validate it by reading messages from that thread.
2. If message read is rejected by the service, clear only `thread_id`.
3. If `auth_token` exists but `thread_id` is missing, send the init message
   `hello world from BB0` without `threadId` to create a new thread.
4. If thread creation is rejected, clear both `auth_token` and `thread_id`, then
   return to the identity stage.
5. If network transport fails, retry Wi-Fi without clearing token or thread.

When a new thread is accepted, the firmware clears the old message cursor and
stored assistant messages because they are scoped to the previous thread.
