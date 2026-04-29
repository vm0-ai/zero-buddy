#include "zero_buddy_core.h"

#include <algorithm>

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

std::string escapeJsonString(std::string value) {
  std::string out;
  out.reserve(value.size() + 8);
  for (char ch : value) {
    switch (ch) {
      case '"':
      case '\\':
        out += '\\';
        out += ch;
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        out += ch;
        break;
    }
  }
  return out;
}

bool extractJsonUnsigned(std::string body, std::string key, size_t* value_out) {
  const std::string key_needle = "\"" + key + "\"";
  const size_t key_pos = body.find(key_needle);
  if (key_pos == std::string::npos) {
    return false;
  }
  size_t cursor = key_pos + key_needle.size();
  while (cursor < body.size() && isSpace(body[cursor])) {
    ++cursor;
  }
  if (cursor >= body.size() || body[cursor] != ':') {
    return false;
  }
  ++cursor;
  while (cursor < body.size() && isSpace(body[cursor])) {
    ++cursor;
  }
  if (cursor >= body.size() || body[cursor] < '0' || body[cursor] > '9') {
    return false;
  }
  size_t value = 0;
  while (cursor < body.size() && body[cursor] >= '0' && body[cursor] <= '9') {
    value = value * 10 + static_cast<size_t>(body[cursor] - '0');
    ++cursor;
  }
  *value_out = value;
  return true;
}

bool extractJsonBool(std::string body, std::string key, bool* value_out) {
  const std::string key_needle = "\"" + key + "\"";
  const size_t key_pos = body.find(key_needle);
  if (key_pos == std::string::npos) {
    return false;
  }
  size_t cursor = key_pos + key_needle.size();
  while (cursor < body.size() && isSpace(body[cursor])) {
    ++cursor;
  }
  if (cursor >= body.size() || body[cursor] != ':') {
    return false;
  }
  ++cursor;
  while (cursor < body.size() && isSpace(body[cursor])) {
    ++cursor;
  }
  if (body.compare(cursor, 4, "true") == 0) {
    *value_out = true;
    return true;
  }
  if (body.compare(cursor, 5, "false") == 0) {
    *value_out = false;
    return true;
  }
  return false;
}

}  // namespace

bool externalPowerPresent(int32_t vbus_mv,
                          bool battery_charging,
                          int32_t present_threshold_mv) {
  if (battery_charging) {
    return true;
  }
  if (present_threshold_mv <= 0) {
    present_threshold_mv = 4300;
  }
  return vbus_mv >= present_threshold_mv;
}

BootRepairAction repairActionForBootFailure(BootRepairEvent event) {
  BootRepairAction action;
  switch (event) {
    case BootRepairEvent::WifiUnavailable:
      action.reprovision_wifi = true;
      break;
    case BootRepairEvent::MessageReadFailed:
      action.clear_thread_id = true;
      break;
    case BootRepairEvent::ThreadCreateFailed:
      action.clear_auth_token = true;
      action.clear_thread_id = true;
      break;
  }
  return action;
}

const char* provisioningStateName(ProvisioningState state) {
  switch (state) {
    case ProvisioningState::Setup:
      return "setup";
    case ProvisioningState::WifiReceived:
      return "wifi_received";
    case ProvisioningState::WifiConnecting:
      return "wifi_connecting";
    case ProvisioningState::WifiConnected:
      return "wifi_connected";
    case ProvisioningState::DeviceCodePending:
      return "device_code_pending";
    case ProvisioningState::DeviceCodeReady:
      return "device_code_ready";
    case ProvisioningState::Binding:
      return "binding";
    case ProvisioningState::Provisioned:
      return "provisioned";
    case ProvisioningState::Error:
      return "error";
  }
  return "error";
}

const char* provisioningErrorCodeName(ProvisioningError error) {
  switch (error) {
    case ProvisioningError::None:
      return "";
    case ProvisioningError::WifiFailed:
      return "wifi_failed";
    case ProvisioningError::DeviceCodeFailed:
      return "device_code_failed";
    case ProvisioningError::CodeExpired:
      return "code_expired";
    case ProvisioningError::BleWriteFailed:
      return "ble_write_failed";
    case ProvisioningError::NetworkFailed:
      return "network_failed";
  }
  return "unknown";
}

std::vector<uint8_t> buildProvisioningServiceData(ProvisioningState state,
                                                  uint8_t flags,
                                                  uint16_t short_device_id,
                                                  ProvisioningError error) {
  return {
      1,
      static_cast<uint8_t>(state),
      flags,
      static_cast<uint8_t>(short_device_id & 0xFF),
      static_cast<uint8_t>((short_device_id >> 8) & 0xFF),
      static_cast<uint8_t>(error),
  };
}

std::string trim(std::string value) {
  const auto begin = std::find_if_not(value.begin(), value.end(), isSpace);
  const auto end = std::find_if_not(value.rbegin(), value.rend(), isSpace).base();
  if (begin >= end) {
    return "";
  }
  return std::string(begin, end);
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

static ZeroMessage parseZeroMessageObject(std::string object) {
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

std::string buildAssistantQueueManifest(size_t count,
                                        size_t index,
                                        bool waiting,
                                        uint8_t next_delay_index,
                                        std::string last_seen_message_id,
                                        size_t scroll_top) {
  if (index > count) {
    index = count;
  }
  return std::string("{\"count\":") + std::to_string(count) +
         ",\"index\":" + std::to_string(index) +
         ",\"scrollTop\":" + std::to_string(scroll_top) +
         ",\"waiting\":" + (waiting ? "true" : "false") +
         ",\"next_delay_index\":" + std::to_string(next_delay_index) +
         ",\"last_seen_message_id\":\"" + escapeJsonString(last_seen_message_id) + "\"}";
}

AssistantQueueManifest parseAssistantQueueManifest(std::string body, size_t max_count) {
  AssistantQueueManifest manifest;
  size_t count = 0;
  size_t index = 0;
  size_t scroll_top = 0;
  size_t next_delay_index = 0;
  bool waiting = false;
  if (!extractJsonUnsigned(body, "count", &count) ||
      !extractJsonUnsigned(body, "index", &index)) {
    return manifest;
  }
  if (count > max_count || index > count) {
    return manifest;
  }
  extractJsonBool(body, "waiting", &waiting);
  if (!extractJsonUnsigned(body, "scrollTop", &scroll_top)) {
    extractJsonUnsigned(body, "scroll_top", &scroll_top);
  }
  extractJsonUnsigned(body, "next_delay_index", &next_delay_index);

  manifest.ok = true;
  manifest.count = count;
  manifest.index = index;
  manifest.scroll_top = scroll_top;
  manifest.waiting = waiting;
  manifest.next_delay_index = static_cast<uint8_t>(std::min<size_t>(next_delay_index, 255));
  manifest.last_seen_message_id = extractJsonString(body, "last_seen_message_id");
  return manifest;
}

}  // namespace zero_buddy
