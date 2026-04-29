#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace zero_buddy {

struct ZeroMessagesResult {
  bool found_any = false;
  std::string newest_message_id;
  std::vector<std::string> assistant_messages;
};

struct ZeroMessage {
  bool ok = false;
  std::string id;
  std::string role;
  std::string content;
};

struct AssistantQueueManifest {
  bool ok = false;
  size_t count = 0;
  size_t index = 0;
  size_t scroll_top = 0;
  bool waiting = false;
  uint8_t next_delay_index = 0;
  std::string last_seen_message_id;
};

enum class BootRepairEvent : uint8_t {
  WifiUnavailable,
  MessageReadFailed,
  ThreadCreateFailed,
};

struct BootRepairAction {
  bool reprovision_wifi = false;
  bool clear_auth_token = false;
  bool clear_thread_id = false;
};

enum class ProvisioningState : uint8_t {
  Setup = 0,
  WifiReceived = 1,
  WifiConnecting = 2,
  WifiConnected = 3,
  DeviceCodePending = 4,
  DeviceCodeReady = 5,
  Binding = 6,
  Provisioned = 7,
  Error = 8,
};

enum class ProvisioningError : uint8_t {
  None = 0,
  WifiFailed = 1,
  DeviceCodeFailed = 2,
  CodeExpired = 3,
  BleWriteFailed = 4,
  NetworkFailed = 5,
};

std::string trim(std::string value);
std::string extractJsonString(std::string body, std::string key);
std::string preprocessAssistantForDisplay(std::string text);
ZeroMessagesResult parseZeroMessagesResponse(std::string body);
std::string buildAssistantQueueManifest(size_t count,
                                        size_t index,
                                        bool waiting,
                                        uint8_t next_delay_index,
                                        std::string last_seen_message_id,
                                        size_t scroll_top = 0);
AssistantQueueManifest parseAssistantQueueManifest(std::string body, size_t max_count);
bool externalPowerPresent(int32_t vbus_mv,
                          bool battery_charging,
                          int32_t present_threshold_mv = 4300);
BootRepairAction repairActionForBootFailure(BootRepairEvent event);
const char* provisioningStateName(ProvisioningState state);
const char* provisioningErrorCodeName(ProvisioningError error);
std::vector<uint8_t> buildProvisioningServiceData(ProvisioningState state,
                                                  uint8_t flags,
                                                  uint16_t short_device_id,
                                                  ProvisioningError error);

}  // namespace zero_buddy
