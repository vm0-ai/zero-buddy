#include "zero_buddy_core.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace zero_buddy {
namespace {

bool isSpace(char ch) {
  return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

size_t decodeUtf8Codepoint(std::string text, size_t index, uint32_t* codepoint_out) {
  const uint8_t b0 = static_cast<uint8_t>(text[index]);
  if (b0 < 0x80) {
    *codepoint_out = b0;
    return 1;
  }
  if ((b0 & 0xE0) == 0xC0 && index + 1 < text.size()) {
    *codepoint_out = ((b0 & 0x1F) << 6) |
                     (static_cast<uint8_t>(text[index + 1]) & 0x3F);
    return 2;
  }
  if ((b0 & 0xF0) == 0xE0 && index + 2 < text.size()) {
    *codepoint_out = ((b0 & 0x0F) << 12) |
                     ((static_cast<uint8_t>(text[index + 1]) & 0x3F) << 6) |
                     (static_cast<uint8_t>(text[index + 2]) & 0x3F);
    return 3;
  }
  if ((b0 & 0xF8) == 0xF0 && index + 3 < text.size()) {
    *codepoint_out = ((b0 & 0x07) << 18) |
                     ((static_cast<uint8_t>(text[index + 1]) & 0x3F) << 12) |
                     ((static_cast<uint8_t>(text[index + 2]) & 0x3F) << 6) |
                     (static_cast<uint8_t>(text[index + 3]) & 0x3F);
    return 4;
  }
  *codepoint_out = '?';
  return 1;
}

bool isSupportedDisplayCodepoint(uint32_t cp) {
  if (cp == '\n') {
    return true;
  }
  if (cp >= 0x20 && cp <= 0x7E) {
    return true;
  }
  if (cp >= 0x3000 && cp <= 0x303F) {
    return true;
  }
  if (cp >= 0x3400 && cp <= 0x4DBF) {
    return true;
  }
  if (cp >= 0x4E00 && cp <= 0x9FFF) {
    return true;
  }
  if (cp >= 0xFF00 && cp <= 0xFFEF) {
    return true;
  }
  return false;
}

void replaceAll(std::string& text, std::string from, std::string to) {
  if (from.empty()) {
    return;
  }
  size_t pos = 0;
  while ((pos = text.find(from, pos)) != std::string::npos) {
    text.replace(pos, from.size(), to);
    pos += to.size();
  }
}

std::string collapseWhitespace(std::string text) {
  std::string out;
  out.reserve(text.size());
  bool last_was_space = false;
  for (char ch : text) {
    if (ch == ' ' || ch == '\t') {
      if (!last_was_space) {
        out += ' ';
        last_was_space = true;
      }
      continue;
    }
    out += ch;
    last_was_space = false;
  }
  return out;
}

std::string removeMarkdownMarkers(std::string text) {
  replaceAll(text, "\r\n", "\n");
  replaceAll(text, "\r", "\n");
  replaceAll(text, "```", "\n[code]\n");
  replaceAll(text, "**", "");
  replaceAll(text, "__", "");
  replaceAll(text, "#### ", "");
  replaceAll(text, "### ", "");
  replaceAll(text, "## ", "");
  replaceAll(text, "# ", "");
  replaceAll(text, "`", "");
  replaceAll(text, "- [ ] ", "- ");
  replaceAll(text, "- [x] ", "- ");
  replaceAll(text, "* ", "- ");
  replaceAll(text, "• ", "- ");
  while (text.find("\n\n\n") != std::string::npos) {
    replaceAll(text, "\n\n\n", "\n\n");
  }
  return text;
}

std::string stripMarkdownLinks(std::string text) {
  std::string out;
  out.reserve(text.size());
  for (size_t i = 0; i < text.size(); ++i) {
    if (text[i] == '[') {
      const size_t mid = text.find("](", i);
      const size_t end = text.find(')', i);
      if (mid != std::string::npos && end != std::string::npos && mid > i && end > mid) {
        out.append(text.substr(i + 1, mid - i - 1));
        i = end;
        continue;
      }
    }
    out += text[i];
  }
  return out;
}

uint32_t readU32(const uint8_t* p) {
  return (static_cast<uint32_t>(p[0]) << 24) |
         (static_cast<uint32_t>(p[1]) << 16) |
         (static_cast<uint32_t>(p[2]) << 8) |
         static_cast<uint32_t>(p[3]);
}

bool timeReached(uint32_t now_ms, uint32_t deadline_ms) {
  return static_cast<int32_t>(now_ms - deadline_ms) >= 0;
}

}  // namespace

FixedByteQueue::FixedByteQueue(uint8_t* storage, size_t capacity)
    : storage_(storage), capacity_(capacity), head_(0), size_(0), dropped_bytes_(0) {}

size_t FixedByteQueue::pushNewest(const uint8_t* data, size_t data_len) {
  if (storage_ == nullptr || capacity_ == 0 || data == nullptr || data_len == 0) {
    return 0;
  }

  if (data_len >= capacity_) {
    dropped_bytes_ += size_ + (data_len - capacity_);
    memcpy(storage_, data + data_len - capacity_, capacity_);
    head_ = 0;
    size_ = capacity_;
    return capacity_;
  }

  if (size_ + data_len > capacity_) {
    const size_t to_drop = size_ + data_len - capacity_;
    head_ = (head_ + to_drop) % capacity_;
    size_ -= to_drop;
    dropped_bytes_ += to_drop;
  }

  size_t tail = (head_ + size_) % capacity_;
  size_t written = 0;
  while (written < data_len) {
    const size_t contiguous = std::min(data_len - written, capacity_ - tail);
    memcpy(storage_ + tail, data + written, contiguous);
    tail = (tail + contiguous) % capacity_;
    written += contiguous;
  }
  size_ += data_len;
  return data_len;
}

size_t FixedByteQueue::pop(uint8_t* out, size_t max_len) {
  if (storage_ == nullptr || capacity_ == 0 || out == nullptr || max_len == 0 || size_ == 0) {
    return 0;
  }
  const size_t to_read = std::min(max_len, size_);
  size_t read = 0;
  while (read < to_read) {
    const size_t contiguous = std::min(to_read - read, capacity_ - head_);
    memcpy(out + read, storage_ + head_, contiguous);
    head_ = (head_ + contiguous) % capacity_;
    read += contiguous;
  }
  size_ -= to_read;
  if (size_ == 0) {
    head_ = 0;
  }
  return to_read;
}

void FixedByteQueue::clear() {
  head_ = 0;
  size_ = 0;
  dropped_bytes_ = 0;
}

size_t FixedByteQueue::size() const {
  return size_;
}

size_t FixedByteQueue::capacity() const {
  return capacity_;
}

size_t FixedByteQueue::droppedBytes() const {
  return dropped_bytes_;
}

AsrCaptureAssessment assessAsrCapture(AsrCaptureStrategy strategy,
                                      const AsrCaptureMetrics& metrics) {
  AsrCaptureAssessment assessment;
  if (metrics.recorded_bytes == 0) {
    assessment.reason = "empty audio";
    return assessment;
  }

  if (strategy == AsrCaptureStrategy::FlashReplay) {
    if (metrics.flash_failed) {
      assessment.reason = "flash failed";
      return assessment;
    }
    if (metrics.flash_bytes != metrics.recorded_bytes) {
      assessment.reason = "flash mismatch";
      return assessment;
    }
    assessment.input_complete = true;
  } else {
    if (metrics.dropped_bytes != 0) {
      assessment.reason = "backlog dropped";
      return assessment;
    }
    assessment.input_complete = true;
  }

  if (metrics.sent_bytes != metrics.recorded_bytes) {
    assessment.reason = "sent mismatch";
    return assessment;
  }
  assessment.upload_complete = true;
  assessment.reason = "complete";
  return assessment;
}

NotificationBlinkResult updateNotificationBlink(NotificationBlinkState* state,
                                                NotificationBlinkEvent event,
                                                uint32_t now_ms,
                                                uint32_t interval_ms,
                                                uint32_t pulse_ms) {
  NotificationBlinkResult result;
  if (state == nullptr) {
    return result;
  }

  const bool previous_led = state->led_on;
  if (event == NotificationBlinkEvent::AssistantArrived) {
    state->pending = true;
    state->led_on = true;
    state->last_pulse_ms = now_ms;
    state->led_off_at_ms = now_ms + pulse_ms;
  } else if (event == NotificationBlinkEvent::UserAction) {
    state->pending = false;
    state->led_on = false;
    state->last_pulse_ms = now_ms;
    state->led_off_at_ms = now_ms;
  } else if (!state->pending) {
    state->led_on = false;
  } else {
    if (state->led_on && timeReached(now_ms, state->led_off_at_ms)) {
      state->led_on = false;
    }
    if (!state->led_on && timeReached(now_ms, state->last_pulse_ms + interval_ms)) {
      state->led_on = true;
      state->last_pulse_ms = now_ms;
      state->led_off_at_ms = now_ms + pulse_ms;
    }
  }

  result.pending = state->pending;
  result.led_on = state->led_on;
  result.led_changed = previous_led != state->led_on;
  return result;
}

uint8_t nextBrightnessLevel(uint8_t current_level, uint8_t level_count) {
  if (level_count == 0 || current_level >= level_count) {
    return 0;
  }
  return static_cast<uint8_t>((current_level + 1) % level_count);
}

std::string trim(std::string value) {
  const auto begin = std::find_if_not(value.begin(), value.end(), isSpace);
  const auto end = std::find_if_not(value.rbegin(), value.rend(), isSpace).base();
  if (begin >= end) {
    return "";
  }
  return std::string(begin, end);
}

std::string trimToWidth(std::string text, size_t max_len) {
  if (text.size() <= max_len) {
    return std::string(text);
  }
  if (max_len <= 3) {
    return std::string(text.substr(0, max_len));
  }
  std::string out(text.substr(0, max_len - 3));
  out += "...";
  return out;
}

std::string extractJsonString(std::string body, std::string key) {
  const std::string key_needle = "\"" + std::string(key) + "\"";
  const size_t key_pos = body.find(key_needle);
  if (key_pos == std::string::npos) {
    return "";
  }
  size_t cursor = key_pos + key_needle.size();
  while (cursor < body.size() && isSpace(body[cursor])) {
    ++cursor;
  }
  if (cursor >= body.size() || body[cursor] != ':') {
    return "";
  }
  ++cursor;
  while (cursor < body.size() && isSpace(body[cursor])) {
    ++cursor;
  }
  if (cursor >= body.size() || body[cursor] != '"') {
    return "";
  }
  ++cursor;

  std::string value;
  bool escaped = false;
  for (size_t i = cursor; i < body.size(); ++i) {
    const char ch = body[i];
    if (escaped) {
      switch (ch) {
        case 'n':
          value += '\n';
          break;
        case 'r':
          value += '\r';
          break;
        case 't':
          value += '\t';
          break;
        default:
          value += ch;
          break;
      }
      escaped = false;
      continue;
    }
    if (ch == '\\') {
      escaped = true;
      continue;
    }
    if (ch == '"') {
      return value;
    }
    value += ch;
  }
  return "";
}

std::string extractNestedResultText(std::string body) {
  const size_t result_pos = body.find("\"result\"");
  if (result_pos == std::string::npos) {
    return "";
  }
  return extractJsonString(body.substr(result_pos), "text");
}

std::string extractFirstResultArrayText(std::string body) {
  const size_t result_pos = body.find("\"result\"");
  if (result_pos == std::string::npos) {
    return "";
  }
  const size_t bracket_pos = body.find('[', result_pos);
  if (bracket_pos == std::string::npos) {
    return "";
  }
  const size_t first_obj_pos = body.find('{', bracket_pos);
  if (first_obj_pos == std::string::npos) {
    return "";
  }
  return extractJsonString(body.substr(first_obj_pos), "text");
}

std::string extractFirstUtteranceText(std::string body) {
  const size_t utter_pos = body.find("\"utterances\"");
  if (utter_pos == std::string::npos) {
    return "";
  }
  return extractJsonString(body.substr(utter_pos), "text");
}

std::string preprocessAssistantForDisplay(std::string text) {
  text = stripMarkdownLinks(text);
  text = removeMarkdownMarkers(text);

  std::string out;
  out.reserve(text.size());
  bool last_was_space = false;
  bool last_was_newline = false;
  for (size_t i = 0; i < text.size();) {
    uint32_t cp = 0;
    const size_t n = decodeUtf8Codepoint(text, i, &cp);
    if (cp == '\r') {
      i += n;
      continue;
    }
    if (cp == '\n') {
      if (!last_was_newline) {
        out += '\n';
        last_was_newline = true;
      }
      last_was_space = false;
      i += n;
      continue;
    }
    if (cp == '\t') {
      cp = ' ';
    }
    if (cp == ' ') {
      if (!last_was_space) {
        out += ' ';
        last_was_space = true;
      }
      last_was_newline = false;
      i += n;
      continue;
    }
    if (isSupportedDisplayCodepoint(cp)) {
      out.append(text.substr(i, n));
      last_was_space = false;
      last_was_newline = false;
    } else if (!last_was_space) {
      out += ' ';
      last_was_space = true;
      last_was_newline = false;
    }
    i += n;
  }

  out = collapseWhitespace(out);
  replaceAll(out, "\n ", "\n");
  replaceAll(out, " \n", "\n");
  out = trim(out);
  return out.empty() ? "[empty reply]" : out;
}

std::string decodeChunkedBody(std::string body) {
  std::string out;
  size_t cursor = 0;
  while (cursor < body.size()) {
    const size_t line_end = body.find("\r\n", cursor);
    if (line_end == std::string::npos) {
      return std::string(body);
    }
    const std::string size_hex(body.substr(cursor, line_end - cursor));
    const int chunk_size = static_cast<int>(std::strtol(size_hex.c_str(), nullptr, 16));
    cursor = line_end + 2;
    if (chunk_size <= 0) {
      break;
    }
    if (cursor + static_cast<size_t>(chunk_size) > body.size()) {
      return std::string(body);
    }
    out.append(body.substr(cursor, chunk_size));
    cursor += static_cast<size_t>(chunk_size) + 2;
  }
  return out;
}

ZeroMessage parseZeroMessageObject(std::string object) {
  ZeroMessage message;
  message.id = extractJsonString(object, "id");
  message.role = extractJsonString(object, "role");
  message.content = extractJsonString(object, "content");
  message.ok = !message.id.empty();
  return message;
}

ZeroMessagesResult parseZeroMessagesResponse(std::string body) {
  ZeroMessagesResult result;
  size_t cursor = 0;
  while (true) {
    const size_t id_pos = body.find("{\"id\":\"", cursor);
    if (id_pos == std::string::npos) {
      break;
    }
    const size_t created_pos = body.find("\"createdAt\":\"", id_pos);
    if (created_pos == std::string::npos) {
      break;
    }
    const size_t created_end = body.find('"', created_pos + 13);
    if (created_end == std::string::npos) {
      break;
    }
    const size_t obj_end = body.find('}', created_end);
    if (obj_end == std::string::npos) {
      break;
    }
    const std::string item = body.substr(id_pos, obj_end - id_pos + 1);
    const ZeroMessage message = parseZeroMessageObject(item);
    if (!message.id.empty()) {
      result.newest_message_id = message.id;
      result.found_any = true;
    }
    if (message.role == "assistant" && !message.content.empty()) {
      result.assistant_messages.push_back(preprocessAssistantForDisplay(message.content));
    }
    cursor = obj_end + 1;
  }
  return result;
}

void appendU32(std::vector<uint8_t>& out, uint32_t value) {
  out.push_back((value >> 24) & 0xff);
  out.push_back((value >> 16) & 0xff);
  out.push_back((value >> 8) & 0xff);
  out.push_back(value & 0xff);
}

std::vector<uint8_t> buildStreamAudioPacket(const uint8_t* data, size_t data_len, bool is_final) {
  std::vector<uint8_t> packet;
  packet.reserve(data_len + 8);
  packet.push_back(0x11);
  packet.push_back(is_final ? 0x22 : 0x20);
  packet.push_back(0x00);
  packet.push_back(0x00);
  appendU32(packet, static_cast<uint32_t>(data_len));
  if (data != nullptr && data_len > 0) {
    packet.insert(packet.end(), data, data + data_len);
  }
  return packet;
}

AsrPayloadResult parseAsrServerPayload(const uint8_t* frame_payload, size_t frame_payload_size) {
  AsrPayloadResult result;
  if (frame_payload_size < 4) {
    result.error = "short asr frame";
    return result;
  }
  const uint8_t byte1 = frame_payload[1];
  const uint8_t msg_type = (byte1 >> 4) & 0x0f;
  const uint8_t msg_flags = byte1 & 0x0f;
  const uint8_t byte2 = frame_payload[2];
  const uint8_t compression = byte2 & 0x0f;
  if (compression != 0) {
    result.error = "gzip not supported";
    return result;
  }

  if (msg_type == 0x0f) {
    if (frame_payload_size < 12) {
      result.error = "short asr err";
      return result;
    }
    const uint32_t code = readU32(frame_payload + 4);
    const uint32_t size = readU32(frame_payload + 8);
    if (12 + size > frame_payload_size) {
      result.error = "bad asr err";
      return result;
    }
    result.error = "asr err " + std::to_string(code) + " " +
                   std::string(reinterpret_cast<const char*>(frame_payload + 12), size);
    return result;
  }

  if (msg_type != 0x09 || frame_payload_size < 12) {
    result.error = "unexpected asr msg";
    return result;
  }

  const uint32_t payload_size = readU32(frame_payload + 8);
  if (12 + payload_size > frame_payload_size) {
    result.error = "bad asr payload";
    return result;
  }

  const std::string json(reinterpret_cast<const char*>(frame_payload + 12), payload_size);
  std::string text = extractNestedResultText(json);
  if (text.empty()) {
    text = extractFirstResultArrayText(json);
  }
  if (text.empty()) {
    text = extractFirstUtteranceText(json);
  }
  if (text.empty()) {
    text = extractJsonString(json, "text");
  }

  result.ok = true;
  result.final = (msg_flags == 0x03);
  result.text = text;
  return result;
}

}  // namespace zero_buddy
