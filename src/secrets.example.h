#pragma once

#include <Arduino.h>

struct WifiCred {
  const char* ssid;
  const char* password;
};

constexpr WifiCred kWifiCreds[] = {
    {"your-ssid", "your-password"},
};

constexpr char kAsrWsAppKey[] = "your-doubao-app-id";
constexpr char kAsrWsAccessKey[] = "your-doubao-access-token";
constexpr char kAsrWsResourceId[] = "volc.bigasr.sauc.duration";

constexpr char kZeroApiKey[] = "your-zero-api-key";
constexpr char kZeroThreadId[] = "your-zero-thread-id";
