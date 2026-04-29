#include <unity.h>

#include <string>

#include "zero_buddy_core.h"

namespace {

void test_zero_messages_response_parsing() {
  const std::string zero_json =
      R"({"messages":[)"
      R"({"id":"user-1","role":"user","content":"把线上 release 合进去","createdAt":"2026-04-24T00:00:00Z"},)"
      R"({"id":"assistant-1","role":"assistant","content":"# 结果\n**已处理** [PR](https://x.test) 🚀","createdAt":"2026-04-24T00:00:01Z"}]})";

  const auto messages = zero_buddy::parseZeroMessagesResponse(zero_json);
  TEST_ASSERT_TRUE(messages.found_any);
  TEST_ASSERT_EQUAL_STRING("assistant-1", messages.newest_message_id.c_str());
  TEST_ASSERT_EQUAL_UINT(1, messages.assistant_messages.size());
  TEST_ASSERT_TRUE(messages.assistant_messages[0].find("结果") != std::string::npos);
  TEST_ASSERT_TRUE(messages.assistant_messages[0].find("已处理") != std::string::npos);
  TEST_ASSERT_TRUE(messages.assistant_messages[0].find("🚀") == std::string::npos);
}

void test_external_power_present_prefers_vbus_over_charge_current() {
  TEST_ASSERT_TRUE(zero_buddy::externalPowerPresent(5000, false));
  TEST_ASSERT_TRUE(zero_buddy::externalPowerPresent(-1, true));
  TEST_ASSERT_FALSE(zero_buddy::externalPowerPresent(-1, false));
  TEST_ASSERT_FALSE(zero_buddy::externalPowerPresent(4200, false));
  TEST_ASSERT_TRUE(zero_buddy::externalPowerPresent(4200, false, 4000));
}

void test_boot_repair_actions() {
  auto action =
      zero_buddy::repairActionForBootFailure(zero_buddy::BootRepairEvent::WifiUnavailable);
  TEST_ASSERT_TRUE(action.reprovision_wifi);
  TEST_ASSERT_FALSE(action.clear_auth_token);
  TEST_ASSERT_FALSE(action.clear_thread_id);

  action = zero_buddy::repairActionForBootFailure(zero_buddy::BootRepairEvent::MessageReadFailed);
  TEST_ASSERT_FALSE(action.reprovision_wifi);
  TEST_ASSERT_FALSE(action.clear_auth_token);
  TEST_ASSERT_TRUE(action.clear_thread_id);

  action =
      zero_buddy::repairActionForBootFailure(zero_buddy::BootRepairEvent::ThreadCreateFailed);
  TEST_ASSERT_FALSE(action.reprovision_wifi);
  TEST_ASSERT_TRUE(action.clear_auth_token);
  TEST_ASSERT_TRUE(action.clear_thread_id);
}

void test_provisioning_service_data_encoding() {
  const auto data = zero_buddy::buildProvisioningServiceData(
      zero_buddy::ProvisioningState::DeviceCodeReady,
      0x05,
      0xF1B4,
      zero_buddy::ProvisioningError::None);
  const uint8_t expected[] = {
      1,
      static_cast<uint8_t>(zero_buddy::ProvisioningState::DeviceCodeReady),
      0x05,
      0xB4,
      0xF1,
      static_cast<uint8_t>(zero_buddy::ProvisioningError::None),
  };
  TEST_ASSERT_EQUAL_UINT(sizeof(expected), data.size());
  TEST_ASSERT_EQUAL_MEMORY(expected, data.data(), sizeof(expected));
  TEST_ASSERT_EQUAL_STRING(
      "device_code_ready",
      zero_buddy::provisioningStateName(zero_buddy::ProvisioningState::DeviceCodeReady));
  TEST_ASSERT_EQUAL_STRING(
      "network_failed",
      zero_buddy::provisioningErrorCodeName(zero_buddy::ProvisioningError::NetworkFailed));
  TEST_ASSERT_EQUAL_STRING(
      "",
      zero_buddy::provisioningErrorCodeName(zero_buddy::ProvisioningError::None));
}

void test_assistant_queue_manifest_round_trip() {
  const auto json =
      zero_buddy::buildAssistantQueueManifest(3, 1, true, 2, "msg-1\\quoted", 120);
  const auto manifest = zero_buddy::parseAssistantQueueManifest(json, 5);
  TEST_ASSERT_TRUE(manifest.ok);
  TEST_ASSERT_EQUAL_UINT(3, manifest.count);
  TEST_ASSERT_EQUAL_UINT(1, manifest.index);
  TEST_ASSERT_EQUAL_UINT(120, manifest.scroll_top);
  TEST_ASSERT_TRUE(manifest.waiting);
  TEST_ASSERT_EQUAL_UINT8(2, manifest.next_delay_index);
  TEST_ASSERT_EQUAL_STRING("msg-1\\quoted", manifest.last_seen_message_id.c_str());

  TEST_ASSERT_FALSE(zero_buddy::parseAssistantQueueManifest(
                        R"({"count":6,"index":0,"waiting":true})", 5)
                        .ok);
  TEST_ASSERT_FALSE(zero_buddy::parseAssistantQueueManifest(
                        R"({"count":2,"index":3,"waiting":true})", 5)
                        .ok);
}

}  // namespace

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_zero_messages_response_parsing);
  RUN_TEST(test_external_power_present_prefers_vbus_over_charge_current);
  RUN_TEST(test_boot_repair_actions);
  RUN_TEST(test_provisioning_service_data_encoding);
  RUN_TEST(test_assistant_queue_manifest_round_trip);
  return UNITY_END();
}
