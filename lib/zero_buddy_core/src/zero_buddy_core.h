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

struct ZeroMessage {
  bool ok = false;
  std::string id;
  std::string role;
  std::string content;
};

enum class AsrCaptureStrategy : uint8_t {
  LiveBacklog,
  FlashReplay,
};

struct AsrCaptureMetrics {
  size_t recorded_bytes = 0;
  size_t flash_bytes = 0;
  size_t sent_bytes = 0;
  size_t dropped_bytes = 0;
  bool flash_failed = false;
};

struct AsrCaptureAssessment {
  bool input_complete = false;
  bool upload_complete = false;
  std::string reason;

  bool ok() const {
    return input_complete && upload_complete;
  }
};

class FixedByteQueue {
 public:
  FixedByteQueue(uint8_t* storage, size_t capacity);

  size_t pushNewest(const uint8_t* data, size_t data_len);
  size_t pop(uint8_t* out, size_t max_len);
  void clear();

  size_t size() const;
  size_t capacity() const;
  size_t droppedBytes() const;

 private:
  uint8_t* storage_;
  size_t capacity_;
  size_t head_;
  size_t size_;
  size_t dropped_bytes_;
};

std::string trim(std::string value);
std::string trimToWidth(std::string text, size_t max_len);
std::string extractJsonString(std::string body, std::string key);
std::string extractNestedResultText(std::string body);
std::string extractFirstResultArrayText(std::string body);
std::string extractFirstUtteranceText(std::string body);
std::string preprocessAssistantForDisplay(std::string text);
std::string decodeChunkedBody(std::string body);
ZeroMessage parseZeroMessageObject(std::string object);
ZeroMessagesResult parseZeroMessagesResponse(std::string body);
void appendU32(std::vector<uint8_t>& out, uint32_t value);
std::vector<uint8_t> buildStreamAudioPacket(const uint8_t* data, size_t data_len, bool is_final);
AsrPayloadResult parseAsrServerPayload(const uint8_t* frame_payload, size_t frame_payload_size);
AsrCaptureAssessment assessAsrCapture(AsrCaptureStrategy strategy,
                                      const AsrCaptureMetrics& metrics);

}  // namespace zero_buddy
