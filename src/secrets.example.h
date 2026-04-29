#pragma once

// Optional compile-time defaults. Leave these empty for normal runtime
// provisioning. Runtime Wi-Fi in NVS takes precedence over compile-time Wi-Fi.
// Compile-time PAT and thread id, when provided, take precedence over older NVS
// auth values.

#define ZERO_BUDDY_WIFI_SSID ""
#define ZERO_BUDDY_WIFI_PASSWORD ""
#define ZERO_BUDDY_PAT ""
#define ZERO_BUDDY_THREAD_ID ""
