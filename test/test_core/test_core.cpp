#include <unity.h>

#include <cstring>
#include <string>
#include <vector>

#include "zero_buddy_core.h"

namespace {

std::vector<uint8_t> makeAsrServerFrame(const std::string& json, uint8_t flags = 0x03) {
  std::vector<uint8_t> frame = {0x11, static_cast<uint8_t>(0x90 | flags), 0x10, 0x00,
                                0x00, 0x00, 0x00, 0x01};
  zero_buddy::appendU32(frame, static_cast<uint32_t>(json.size()));
  frame.insert(frame.end(), json.begin(), json.end());
  return frame;
}

std::vector<uint8_t> makeAsrErrorFrame(uint32_t code, const std::string& message) {
  std::vector<uint8_t> frame = {0x11, 0xF0, 0x10, 0x00};
  zero_buddy::appendU32(frame, code);
  zero_buddy::appendU32(frame, static_cast<uint32_t>(message.size()));
  frame.insert(frame.end(), message.begin(), message.end());
  return frame;
}

void test_json_string_allows_spaces_and_escapes() {
  const std::string json = R"({"result": {"text" : "hello\nworld", "ignored": true}})";
  TEST_ASSERT_EQUAL_STRING("hello\nworld", zero_buddy::extractNestedResultText(json).c_str());
}

void test_asr_result_object_text() {
  const auto frame = makeAsrServerFrame(R"({"result":{"text":"你好世界"}})");
  const auto parsed = zero_buddy::parseAsrServerPayload(frame.data(), frame.size());
  TEST_ASSERT_TRUE(parsed.ok);
  TEST_ASSERT_TRUE(parsed.final);
  TEST_ASSERT_EQUAL_STRING("你好世界", parsed.text.c_str());
}

void test_asr_result_array_text() {
  const auto frame = makeAsrServerFrame(R"({"result":[{"text":"数组格式"}]})");
  const auto parsed = zero_buddy::parseAsrServerPayload(frame.data(), frame.size());
  TEST_ASSERT_TRUE(parsed.ok);
  TEST_ASSERT_TRUE(parsed.final);
  TEST_ASSERT_EQUAL_STRING("数组格式", parsed.text.c_str());
}

void test_asr_utterance_fallback_text() {
  const auto frame = makeAsrServerFrame(R"({"result":{"utterances":[{"text":"分句格式"}]}})");
  const auto parsed = zero_buddy::parseAsrServerPayload(frame.data(), frame.size());
  TEST_ASSERT_TRUE(parsed.ok);
  TEST_ASSERT_EQUAL_STRING("分句格式", parsed.text.c_str());
}

void test_asr_error_frame() {
  const auto frame = makeAsrErrorFrame(45000002, "empty audio");
  const auto parsed = zero_buddy::parseAsrServerPayload(frame.data(), frame.size());
  TEST_ASSERT_FALSE(parsed.ok);
  TEST_ASSERT_TRUE(parsed.error.find("45000002") != std::string::npos);
  TEST_ASSERT_TRUE(parsed.error.find("empty audio") != std::string::npos);
}

void test_final_empty_audio_packet_is_safe() {
  const auto packet = zero_buddy::buildStreamAudioPacket(nullptr, 0, true);
  const uint8_t expected[] = {0x11, 0x22, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  TEST_ASSERT_EQUAL_UINT(sizeof(expected), packet.size());
  TEST_ASSERT_EQUAL_MEMORY(expected, packet.data(), sizeof(expected));
}

void test_non_final_audio_packet_header() {
  const uint8_t pcm[] = {1, 2, 3};
  const auto packet = zero_buddy::buildStreamAudioPacket(pcm, sizeof(pcm), false);
  const uint8_t expected[] = {0x11, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 1, 2, 3};
  TEST_ASSERT_EQUAL_UINT(sizeof(expected), packet.size());
  TEST_ASSERT_EQUAL_MEMORY(expected, packet.data(), sizeof(expected));
}

void test_chunked_response_decodes_and_bad_chunk_falls_back() {
  TEST_ASSERT_EQUAL_STRING("Wikipedia",
                           zero_buddy::decodeChunkedBody("4\r\nWiki\r\n5\r\npedia\r\n0\r\n\r\n").c_str());
  TEST_ASSERT_EQUAL_STRING("not chunked", zero_buddy::decodeChunkedBody("not chunked").c_str());
}

void test_zero_messages_extracts_assistant_and_newest_id() {
  const std::string body =
      R"({"messages":[)"
      R"({"id":"user-1","role":"user","content":"hi","createdAt":"2026-04-24T00:00:00Z"},)"
      R"({"id":"assistant-1","role":"assistant","content":"**你好** [link](https://x.test) 🚀","createdAt":"2026-04-24T00:00:01Z"}]})";
  const auto parsed = zero_buddy::parseZeroMessagesResponse(body);
  TEST_ASSERT_TRUE(parsed.found_any);
  TEST_ASSERT_EQUAL_STRING("assistant-1", parsed.newest_message_id.c_str());
  TEST_ASSERT_EQUAL_UINT(1, parsed.assistant_messages.size());
  TEST_ASSERT_TRUE(parsed.assistant_messages[0].find("你好") != std::string::npos);
  TEST_ASSERT_TRUE(parsed.assistant_messages[0].find("🚀") == std::string::npos);
}

void test_display_preprocess_keeps_chinese_and_removes_emoji() {
  const auto text = zero_buddy::preprocessAssistantForDisplay("# 标题\n**你好** `code` 😊");
  TEST_ASSERT_TRUE(text.find("标题") != std::string::npos);
  TEST_ASSERT_TRUE(text.find("你好") != std::string::npos);
  TEST_ASSERT_TRUE(text.find("😊") == std::string::npos);
  TEST_ASSERT_TRUE(text.find("**") == std::string::npos);
}

}  // namespace

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_json_string_allows_spaces_and_escapes);
  RUN_TEST(test_asr_result_object_text);
  RUN_TEST(test_asr_result_array_text);
  RUN_TEST(test_asr_utterance_fallback_text);
  RUN_TEST(test_asr_error_frame);
  RUN_TEST(test_final_empty_audio_packet_is_safe);
  RUN_TEST(test_non_final_audio_packet_header);
  RUN_TEST(test_chunked_response_decodes_and_bad_chunk_falls_back);
  RUN_TEST(test_zero_messages_extracts_assistant_and_newest_id);
  RUN_TEST(test_display_preprocess_keeps_chinese_and_removes_emoji);
  return UNITY_END();
}
