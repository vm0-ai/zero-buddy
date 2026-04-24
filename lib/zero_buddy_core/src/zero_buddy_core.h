#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace zero_buddy {

struct AsrPayloadResult {
  bool ok = false;
  bool final = false;
  std::string text;
  std::string error;
};

struct ZeroMessagesResult {
  bool found_any = false;
  std::string newest_message_id;
  std::vector<std::string> assistant_messages;
};

std::string trim(std::string value);
std::string trimToWidth(std::string text, size_t max_len);
std::string extractJsonString(std::string body, std::string key);
std::string extractNestedResultText(std::string body);
std::string extractFirstResultArrayText(std::string body);
std::string extractFirstUtteranceText(std::string body);
std::string preprocessAssistantForDisplay(std::string text);
std::string decodeChunkedBody(std::string body);
ZeroMessagesResult parseZeroMessagesResponse(std::string body);
void appendU32(std::vector<uint8_t>& out, uint32_t value);
std::vector<uint8_t> buildStreamAudioPacket(const uint8_t* data, size_t data_len, bool is_final);
AsrPayloadResult parseAsrServerPayload(const uint8_t* frame_payload, size_t frame_payload_size);

}  // namespace zero_buddy
