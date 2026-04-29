#include <Arduino.h>
#include <HTTPClient.h>
#include <LittleFS.h>
#include <M5Unified.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <NimBLEDevice.h>
#include <driver/gpio.h>
#include <esp_sleep.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <esp_wifi.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>
#include <vector>

#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
#endif

#ifndef ZERO_BUDDY_WIFI_SSID
#define ZERO_BUDDY_WIFI_SSID ""
#endif

#ifndef ZERO_BUDDY_WIFI_PASSWORD
#define ZERO_BUDDY_WIFI_PASSWORD ""
#endif

#ifndef ZERO_BUDDY_PAT
#define ZERO_BUDDY_PAT ""
#endif

#ifndef ZERO_BUDDY_THREAD_ID
#define ZERO_BUDDY_THREAD_ID ""
#endif

#include "screen_renderer.h"
#include "zero_buddy_core.h"
#include "zero_buddy_modes.h"
#include "zero_buddy_state.h"

namespace {

using zero_buddy::modes::CheckAssistantMessageMode;
using zero_buddy::modes::CheckAssistantMessageOps;
using zero_buddy::modes::DeepSleepMode;
using zero_buddy::modes::DeepSleepOps;
using zero_buddy::modes::ModeRunError;
using zero_buddy::modes::ModeRunResult;
using zero_buddy::modes::ModeRunStatus;
using zero_buddy::modes::ReadInput;
using zero_buddy::modes::ReadMode;
using zero_buddy::modes::ReadOps;
using zero_buddy::modes::ReadProgress;
using zero_buddy::modes::RecordingMode;
using zero_buddy::modes::RecordingOps;
using zero_buddy::state::AssistantCheckResult;
using zero_buddy::state::GlobalState;
using zero_buddy::state::RenderScreenKind;
using zero_buddy::screen::ScreenRenderer;

constexpr uint32_t kRtcMagic = 0x5A0B2028UL;
constexpr gpio_num_t kBtnAWakePin = GPIO_NUM_37;
constexpr gpio_num_t kBtnBPin = GPIO_NUM_39;
constexpr gpio_num_t kPowerIrqWakePin = GPIO_NUM_35;
constexpr gpio_num_t kLedGpio = GPIO_NUM_10;
constexpr int kLedPin = static_cast<int>(kLedGpio);
constexpr int kBuzzerPin = 2;
constexpr int kSampleRate = 16000;
constexpr size_t kMicSamplesPerRead = 256;
constexpr size_t kPcmBufferBytes = kMicSamplesPerRead * sizeof(int16_t);
constexpr uint32_t kLongPressMs = 550;
constexpr uint32_t kMaxRecordingMs = 60000;
constexpr uint32_t kBootSplashMs = 3000;
constexpr uint32_t kWifiConnectAttemptMs = 12000;
constexpr uint8_t kRecordingCpuMhz = 240;
constexpr uint8_t kNetworkCpuMhz = 80;
constexpr uint8_t kReadCpuMhz = 80;
constexpr size_t kMaxAssistantMessages = 5;
constexpr size_t kMaxAssistantMessageBytes = 4096;

constexpr char kVoicePcmPath[] = "/voice.pcm";
constexpr char kAssistantQueueManifestPath[] = "/assistant_queue.txt";
constexpr char kZeroHost[] = "api.vm0.ai";
constexpr char kZeroPostUrl[] = "https://api.vm0.ai/api/v1/chat-threads/messages";
constexpr char kZeroTranscriptionUrl[] =
    "https://api.vm0.ai/api/v1/audio/transcriptions";
constexpr char kZeroDeviceTokenUrl[] = "https://api.vm0.ai/api/device-token";
constexpr char kZeroDeviceTokenPollUrl[] = "https://api.vm0.ai/api/device-token/poll";
constexpr char kZeroUserAgent[] = "zero-buddy/0.1.0";
constexpr char kBb0VerificationUrl[] = "https://bb0.ai";
constexpr char kFirmwareVersion[] = "0.1.0";
constexpr char kRuntimeConfigNamespace[] = "zero_runtime";
constexpr char kRuntimeWifiSsidKey[] = "wifi_ssid";
constexpr char kRuntimeWifiPasswordKey[] = "wifi_password";
constexpr char kRuntimeAuthTokenKey[] = "auth_token";
constexpr char kRuntimeThreadIdKey[] = "thread_id";
constexpr size_t kMaxWifiSsidChars = 64;
constexpr size_t kMaxWifiPasswordChars = 128;
constexpr size_t kMaxAuthTokenChars = 768;
constexpr size_t kMaxThreadIdChars = 96;
constexpr size_t kMaxPollTokenChars = 512;
constexpr uint32_t kProvisioningConnectAttemptMs = 20000;
constexpr char kBb0BleServiceUuid[] = "bb000001-8f16-4b2a-9bb0-000000000001";
constexpr char kBb0BleInfoUuid[] = "bb000002-8f16-4b2a-9bb0-000000000001";
constexpr char kBb0BleConfigUuid[] = "bb000003-8f16-4b2a-9bb0-000000000001";
constexpr uint32_t kBb0BleServiceDataUuid32 = 0xBB000001UL;
constexpr uint8_t kBleFlagWifiConfigured = 1U << 0;
constexpr uint8_t kBleFlagAuthConfigured = 1U << 1;
constexpr uint8_t kBleFlagThreadConfigured = 1U << 2;
constexpr uint8_t kBleFlagError = 1U << 7;
constexpr uint8_t kAxp192IrqEnable1Reg = 0x40;
constexpr uint8_t kAxp192IrqStatus1Reg = 0x44;
constexpr uint8_t kAxp192IrqStatus2Reg = 0x45;
constexpr uint8_t kAxp192IrqStatus3Reg = 0x46;
constexpr uint8_t kAxp192IrqStatus4Reg = 0x47;
constexpr uint8_t kAxp192IrqStatus5Reg = 0x4D;
constexpr uint8_t kAxp192VbusInsertIrqBit = 1U << 3;

RTC_DATA_ATTR uint32_t g_rtc_magic = 0;
RTC_DATA_ATTR GlobalState g_state;

ScreenRenderer g_screen(&g_state);
uint8_t g_pcm_buffer[kPcmBufferBytes] = {0};
bool g_btn_b_was_down = false;
volatile bool g_btn_b_interrupt_pending = false;
uint32_t g_last_btn_b_restart_ms = 0;
esp_timer_handle_t g_runtime_check_timer = nullptr;
volatile bool g_runtime_check_due = false;
volatile bool g_runtime_check_timer_active = false;
bool g_ble_wifi_received = false;
String g_ble_wifi_ssid;
String g_ble_wifi_password;
NimBLECharacteristic* g_ble_info_characteristic = nullptr;
NimBLEAdvertising* g_ble_advertising = nullptr;
bool g_ble_started = false;

void beepAssistantLedOn();
bool writeTextFile(const char* path, const std::string& value);
bool writeAssistantQueueManifest(size_t count, size_t index, size_t scroll_top);
void clearRuntimeConfig();

bool stateLooksValid(const GlobalState& state) {
  if (state.checkDelayMs == 0 ||
      state.checkDelayMs > zero_buddy::state::kMaxCheckDelayMs) {
    return false;
  }
  const auto mode = static_cast<uint8_t>(state.currentMode);
  if (mode > static_cast<uint8_t>(zero_buddy::state::Mode::Recording)) {
    return false;
  }
  const auto render_kind = static_cast<uint8_t>(state.lastRenderScreenState.kind);
  return render_kind <= static_cast<uint8_t>(RenderScreenKind::SetupStatus);
}

void ensureGlobalStateInitialized() {
  if (g_rtc_magic != kRtcMagic || !stateLooksValid(g_state)) {
    g_state = zero_buddy::state::makeDefaultGlobalState();
    g_rtc_magic = kRtcMagic;
  }
}

void setCpu(uint8_t mhz) {
  setCpuFrequencyMhz(mhz);
}

constexpr uint64_t gpioWakeMask(gpio_num_t pin) {
  return 1ULL << static_cast<uint8_t>(pin);
}

void setStatusLed(bool on) {
  pinMode(kLedPin, OUTPUT);
  digitalWrite(kLedPin, on ? LOW : HIGH);
}

void releaseLedHold() {
  gpio_deep_sleep_hold_dis();
  gpio_hold_dis(kLedGpio);
}

void holdLedThroughDeepSleep() {
  gpio_hold_en(kLedGpio);
  gpio_deep_sleep_hold_en();
}

void setAssistantLedOn() {
  releaseLedHold();
  beepAssistantLedOn();
  setStatusLed(true);
  holdLedThroughDeepSleep();
}

void setAssistantLedOff() {
  releaseLedHold();
  setStatusLed(false);
}

void beepTone(int freq_hz, int duration_ms) {
  ledcAttachPin(kBuzzerPin, 0);
  ledcWriteTone(0, freq_hz);
  delay(duration_ms);
  ledcWriteTone(0, 0);
}

void beepMicOn() {
  beepTone(1760, 35);
}

void beepMicOff() {
  beepTone(880, 70);
}

void beepAssistantLedOn() {
  beepTone(1568, 45);
}

bool buttonADown() {
  return digitalRead(static_cast<int>(kBtnAWakePin)) == LOW;
}

bool buttonBDown() {
  return digitalRead(static_cast<int>(kBtnBPin)) == LOW;
}

void IRAM_ATTR handleBtnBFallingInterrupt() {
  g_btn_b_interrupt_pending = true;
}

void restartIfBtnBPressed() {
  const bool down = buttonBDown();
  const bool interrupt_pending = g_btn_b_interrupt_pending;
  if (interrupt_pending) {
    g_btn_b_interrupt_pending = false;
  }
  const bool pressed = interrupt_pending || (down && !g_btn_b_was_down);
  g_btn_b_was_down = down;
  if (pressed) {
    const uint32_t now = millis();
    if (now - g_last_btn_b_restart_ms < 250) {
      return;
    }
    g_last_btn_b_restart_ms = now;
    if (buttonADown()) {
      Serial.println("BtnA+BtnB clear runtime config");
      clearRuntimeConfig();
      g_screen.render_screen_setup_status("runtime config", "cleared");
      Serial.flush();
      delay(800);
      esp_restart();
    }
    Serial.println("BtnB restart");
    Serial.flush();
    delay(20);
    esp_restart();
  }
}

void pollControls() {
  M5.update();
  restartIfBtnBPressed();
}

void runtimeCheckTimerCallback(void*) {
  g_runtime_check_due = true;
  g_runtime_check_timer_active = false;
}

bool ensureRuntimeCheckTimer() {
  if (g_runtime_check_timer != nullptr) {
    return true;
  }
  esp_timer_create_args_t args = {};
  args.callback = &runtimeCheckTimerCallback;
  args.name = "assistant-check";
  return esp_timer_create(&args, &g_runtime_check_timer) == ESP_OK;
}

void stopRuntimeCheckTimer() {
  if (g_runtime_check_timer != nullptr) {
    esp_timer_stop(g_runtime_check_timer);
  }
  g_runtime_check_due = false;
  g_runtime_check_timer_active = false;
}

void cancelCheckTimers() {
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
  stopRuntimeCheckTimer();
}

void scheduleRuntimeCheckTimerIfNeeded(uint32_t delay_ms) {
  if (g_runtime_check_due || g_runtime_check_timer_active) {
    return;
  }
  if (!ensureRuntimeCheckTimer()) {
    g_runtime_check_due = true;
    return;
  }
  const uint32_t effective_delay =
      delay_ms == 0 ? zero_buddy::state::kInitialCheckDelayMs : delay_ms;
  if (esp_timer_start_once(g_runtime_check_timer,
                           static_cast<uint64_t>(effective_delay) * 1000ULL) == ESP_OK) {
    g_runtime_check_timer_active = true;
  } else {
    g_runtime_check_due = true;
  }
}

bool runtimeCheckDue() {
  return g_runtime_check_due;
}

void restartAwareDelay(uint32_t duration_ms) {
  const uint32_t start = millis();
  while (millis() - start < duration_ms) {
    pollControls();
    const uint32_t elapsed = millis() - start;
    if (elapsed >= duration_ms) {
      break;
    }
    const uint32_t remaining = duration_ms - elapsed;
    delay(remaining > 10 ? 10 : remaining);
  }
}

bool waitForBtnALongPress(uint32_t hold_ms) {
  if (!buttonADown()) {
    return false;
  }
  const uint32_t start = millis();
  while (buttonADown()) {
    pollControls();
    if (millis() - start >= hold_ms) {
      return true;
    }
    delay(10);
  }
  return false;
}

bool externalPowerConnected() {
  const int16_t vbus_mv = M5.Power.getVBUSVoltage();
  const auto charge_state = M5.Power.isCharging();
  const bool connected = zero_buddy::externalPowerPresent(
      vbus_mv, charge_state == m5::Power_Class::is_charging);
  Serial.print("power external=");
  Serial.print(connected ? 1 : 0);
  Serial.print(" vbus=");
  Serial.print(static_cast<int>(vbus_mv));
  Serial.print(" charging=");
  Serial.println(static_cast<int>(charge_state));
  return connected;
}

void clearAxp192IrqStatuses() {
  M5.Power.Axp192.writeRegister8(kAxp192IrqStatus1Reg, 0xFF);
  M5.Power.Axp192.writeRegister8(kAxp192IrqStatus2Reg, 0xFF);
  M5.Power.Axp192.writeRegister8(kAxp192IrqStatus3Reg, 0xFF);
  M5.Power.Axp192.writeRegister8(kAxp192IrqStatus4Reg, 0xFF);
  M5.Power.Axp192.writeRegister8(kAxp192IrqStatus5Reg, 0xFF);
}

void enableExternalPowerWakeIrq() {
  clearAxp192IrqStatuses();
  M5.Power.Axp192.bitOn(kAxp192IrqEnable1Reg, kAxp192VbusInsertIrqBit);
}

bool axp192VbusInsertIrqActive() {
  return (M5.Power.Axp192.readRegister8(kAxp192IrqStatus1Reg) &
          kAxp192VbusInsertIrqBit) != 0;
}

const char* modeRunErrorName(ModeRunError error) {
  switch (error) {
    case ModeRunError::None:
      return "none";
    case ModeRunError::WifiUnavailable:
      return "wifi-unavailable";
    case ModeRunError::AssistantPollFailed:
      return "assistant-poll";
    case ModeRunError::AssistantStorageFailed:
      return "assistant-storage";
    case ModeRunError::InvalidStateCommit:
      return "invalid-state";
    case ModeRunError::AssistantClearFailed:
      return "assistant-clear";
    case ModeRunError::VoiceCaptureFailed:
      return "voice-capture";
    case ModeRunError::VoiceToTextFailed:
      return "voice-to-text";
    case ModeRunError::MessageSendFailed:
      return "message-send";
  }
  return "unknown";
}

String assistantMessagePath(size_t index) {
  return "/assistant_" + String(static_cast<unsigned>(index)) + ".txt";
}

String assistantMessageTmpPath(size_t index) {
  return "/assistant_" + String(static_cast<unsigned>(index)) + ".tmp";
}

void removeIfExists(const char* path) {
  if (path != nullptr && LittleFS.exists(path)) {
    LittleFS.remove(path);
  }
}

void clearAssistantFiles() {
  for (size_t i = 0; i < kMaxAssistantMessages; ++i) {
    const String final_path = assistantMessagePath(i);
    const String tmp_path = assistantMessageTmpPath(i);
    removeIfExists(final_path.c_str());
    removeIfExists(tmp_path.c_str());
  }
  removeIfExists(kAssistantQueueManifestPath);
}

bool readTextFile(const char* path, std::string* value_out) {
  if (path == nullptr || value_out == nullptr) {
    return false;
  }
  File file = LittleFS.open(path, "r");
  if (!file) {
    return false;
  }
  value_out->clear();
  while (file.available()) {
    value_out->push_back(static_cast<char>(file.read()));
  }
  file.close();
  return true;
}

zero_buddy::AssistantQueueManifest readAssistantQueueManifest() {
  std::string manifest_body;
  if (!readTextFile(kAssistantQueueManifestPath, &manifest_body)) {
    return zero_buddy::AssistantQueueManifest();
  }
  return zero_buddy::parseAssistantQueueManifest(manifest_body, kMaxAssistantMessages);
}

size_t assistant_message_file_count() {
  size_t count = 0;
  for (size_t i = 0; i < kMaxAssistantMessages; ++i) {
    const String final_path = assistantMessagePath(i);
    if (LittleFS.exists(final_path.c_str())) {
      ++count;
    }
  }
  return count;
}

size_t assistant_message_count() {
  const auto manifest = readAssistantQueueManifest();
  if (manifest.ok) {
    return std::min(manifest.count, kMaxAssistantMessages);
  }
  return assistant_message_file_count();
}

bool assistant_message_exists() {
  return assistant_message_count() > 0;
}

bool read_assistant_message(size_t index, std::string* value_out) {
  if (index >= kMaxAssistantMessages || value_out == nullptr) {
    return false;
  }
  const String final_path = assistantMessagePath(index);
  return readTextFile(final_path.c_str(), value_out);
}

bool load_read_progress(ReadProgress* progress_out) {
  if (progress_out == nullptr) {
    return false;
  }
  progress_out->messageIndex = 0;
  progress_out->scrollTop = 0;

  const auto manifest = readAssistantQueueManifest();
  if (!manifest.ok) {
    return true;
  }
  progress_out->messageIndex = manifest.index;
  progress_out->scrollTop = manifest.scroll_top;
  return true;
}

bool append_assistant_message(const std::string& value) {
  const size_t index = assistant_message_count();
  if (index >= kMaxAssistantMessages) {
    return false;
  }
  const bool should_turn_led_on = !assistant_message_exists();
  const auto previous_manifest = readAssistantQueueManifest();
  const String tmp_path = assistantMessageTmpPath(index);
  const String final_path = assistantMessagePath(index);
  const size_t next_count = index + 1;
  const std::string trimmed = value.substr(0, kMaxAssistantMessageBytes);
  removeIfExists(tmp_path.c_str());
  if (!writeTextFile(tmp_path.c_str(), trimmed)) {
    removeIfExists(tmp_path.c_str());
    return false;
  }
  removeIfExists(final_path.c_str());
  if (!LittleFS.rename(tmp_path.c_str(), final_path.c_str())) {
    removeIfExists(tmp_path.c_str());
    return false;
  }
  size_t next_read_index = 0;
  size_t next_scroll_top = 0;
  if (previous_manifest.ok && index > 0) {
    next_read_index = previous_manifest.index;
    next_scroll_top = previous_manifest.scroll_top;
    if (next_read_index > next_count) {
      next_read_index = next_count - 1;
      next_scroll_top = 0;
    }
  }
  if (!writeAssistantQueueManifest(next_count, next_read_index, next_scroll_top)) {
    removeIfExists(final_path.c_str());
    return false;
  }
  zero_buddy::state::setHasAssistantMessage(&g_state, true);
  if (should_turn_led_on) {
    setAssistantLedOn();
  }
  return true;
}

bool clear_assistant_message() {
  clearAssistantFiles();
  zero_buddy::state::setHasAssistantMessage(&g_state, false);
  setAssistantLedOff();
  return true;
}

bool writeTextFile(const char* path, const std::string& value) {
  File file = LittleFS.open(path, "w");
  if (!file) {
    return false;
  }
  const size_t written =
      file.write(reinterpret_cast<const uint8_t*>(value.data()), value.size());
  file.close();
  return written == value.size();
}

bool writeAssistantQueueManifest(size_t count, size_t index, size_t scroll_top) {
  const std::string body =
      std::string("{\"count\":") + std::to_string(count) +
      ",\"index\":" + std::to_string(index) +
      ",\"scrollTop\":" + std::to_string(scroll_top) + "}";
  return writeTextFile(kAssistantQueueManifestPath, body);
}

String jsonEscape(const String& text) {
  String out;
  out.reserve(text.length() + 16);
  for (size_t i = 0; i < text.length(); ++i) {
    const char ch = text[i];
    const uint8_t byte = static_cast<uint8_t>(ch);
    if (ch == '"' || ch == '\\') {
      out += '\\';
      out += ch;
    } else if (ch == '\n') {
      out += "\\n";
    } else if (ch == '\r') {
      out += "\\r";
    } else if (ch == '\t') {
      out += "\\t";
    } else if (byte < 0x20) {
      char escaped[7] = {0};
      snprintf(escaped, sizeof(escaped), "\\u%04x", byte);
      out += escaped;
    } else {
      out += ch;
    }
  }
  return out;
}

struct RuntimeConfig {
  String wifi_ssid;
  String wifi_password;
  String auth_token;
  String thread_id;
  bool wifi_from_nvs = false;
  bool auth_token_from_nvs = false;
  bool thread_id_from_nvs = false;
  bool auth_token_from_compile = false;
  bool thread_id_from_compile = false;

  bool hasWifi() const {
    return wifi_ssid.length() > 0;
  }

  bool hasAuthToken() const {
    return auth_token.length() > 0;
  }

  bool hasThreadId() const {
    return thread_id.length() > 0;
  }
};

String readRuntimeConfigString(const char* key, size_t max_chars) {
  Preferences prefs;
  if (!prefs.begin(kRuntimeConfigNamespace, true)) {
    return "";
  }
  String value = prefs.getString(key, "");
  prefs.end();
  if (value.length() > max_chars) {
    return "";
  }
  return value;
}

String compileTimeConfigString(const char* value, size_t max_chars) {
  if (value == nullptr || value[0] == '\0') {
    return "";
  }
  String compiled(value);
  if (compiled.length() > max_chars) {
    return "";
  }
  return compiled;
}

bool writeRuntimeConfigString(const char* key, const String& value, size_t max_chars) {
  if (value.length() == 0 || value.length() > max_chars) {
    return false;
  }
  Preferences prefs;
  if (!prefs.begin(kRuntimeConfigNamespace, false)) {
    return false;
  }
  const size_t written = prefs.putString(key, value);
  prefs.end();
  return written == value.length();
}

void removeRuntimeConfigKey(const char* key) {
  Preferences prefs;
  if (!prefs.begin(kRuntimeConfigNamespace, false)) {
    return;
  }
  prefs.remove(key);
  prefs.end();
}

RuntimeConfig loadRuntimeConfig() {
  RuntimeConfig config;
  const String nvs_wifi_ssid =
      readRuntimeConfigString(kRuntimeWifiSsidKey, kMaxWifiSsidChars);
  if (nvs_wifi_ssid.length() > 0) {
    config.wifi_ssid = nvs_wifi_ssid;
    config.wifi_from_nvs = true;
    config.wifi_password =
        readRuntimeConfigString(kRuntimeWifiPasswordKey, kMaxWifiPasswordChars);
  } else {
    config.wifi_ssid =
        compileTimeConfigString(ZERO_BUDDY_WIFI_SSID, kMaxWifiSsidChars);
    if (config.wifi_ssid.length() > 0) {
      config.wifi_password =
          compileTimeConfigString(ZERO_BUDDY_WIFI_PASSWORD, kMaxWifiPasswordChars);
    }
  }

  config.auth_token = compileTimeConfigString(ZERO_BUDDY_PAT, kMaxAuthTokenChars);
  config.auth_token_from_compile = config.auth_token.length() > 0;
  if (config.auth_token.length() == 0) {
    config.auth_token = readRuntimeConfigString(kRuntimeAuthTokenKey, kMaxAuthTokenChars);
    config.auth_token_from_nvs = config.auth_token.length() > 0;
  }

  config.thread_id = compileTimeConfigString(ZERO_BUDDY_THREAD_ID, kMaxThreadIdChars);
  config.thread_id_from_compile = config.thread_id.length() > 0;
  if (config.thread_id.length() == 0) {
    config.thread_id = readRuntimeConfigString(kRuntimeThreadIdKey, kMaxThreadIdChars);
    config.thread_id_from_nvs = config.thread_id.length() > 0;
  }
  return config;
}

bool saveRuntimeWifiConfig(const String& ssid, const String& password) {
  if (!writeRuntimeConfigString(kRuntimeWifiSsidKey, ssid, kMaxWifiSsidChars)) {
    return false;
  }
  if (password.length() == 0) {
    removeRuntimeConfigKey(kRuntimeWifiPasswordKey);
    return true;
  }
  return writeRuntimeConfigString(kRuntimeWifiPasswordKey, password, kMaxWifiPasswordChars);
}

bool saveRuntimeAuthToken(const String& auth_token) {
  return writeRuntimeConfigString(kRuntimeAuthTokenKey, auth_token, kMaxAuthTokenChars);
}

bool saveRuntimeThreadId(const String& thread_id) {
  return writeRuntimeConfigString(kRuntimeThreadIdKey, thread_id, kMaxThreadIdChars);
}

void clearRuntimeThreadId() {
  removeRuntimeConfigKey(kRuntimeThreadIdKey);
  zero_buddy::state::clearLastMessageId(&g_state);
  clear_assistant_message();
}

void clearRuntimeAuthAndThread() {
  removeRuntimeConfigKey(kRuntimeAuthTokenKey);
  clearRuntimeThreadId();
}

void clearRuntimeConfig() {
  Preferences prefs;
  if (prefs.begin(kRuntimeConfigNamespace, false)) {
    prefs.clear();
    prefs.end();
  }
  zero_buddy::state::clearLastMessageId(&g_state);
  zero_buddy::state::resetCheckDelay(&g_state);
  clear_assistant_message();
}

enum class ButtonPressIntent {
  None,
  ShortPress,
  LongPress,
};

class ButtonAbortDetector {
 public:
  bool shouldAbort() {
    return intent() != ButtonPressIntent::None;
  }

  ButtonPressIntent intent() {
    if (latched_intent_ != ButtonPressIntent::None) {
      return latched_intent_;
    }

    const uint32_t now = millis();
    if (buttonADown()) {
      if (down_since_ms_ == 0) {
        down_since_ms_ = now == 0 ? 1 : now;
      }
      if (now - down_since_ms_ >= kLongPressMs) {
        latched_intent_ = ButtonPressIntent::LongPress;
      }
    } else if (down_since_ms_ != 0) {
      latched_intent_ = ButtonPressIntent::ShortPress;
      down_since_ms_ = 0;
    }
    return latched_intent_;
  }

  const char* abortReason() {
    switch (intent()) {
      case ButtonPressIntent::ShortPress:
        return "btn_a_short_press";
      case ButtonPressIntent::LongPress:
        return "btn_a_long_press";
      case ButtonPressIntent::None:
        break;
    }
    return nullptr;
  }

 private:
  uint32_t down_since_ms_ = 0;
  ButtonPressIntent latched_intent_ = ButtonPressIntent::None;
};

bool connectWifiCredentials(const String& ssid,
                            const String& password,
                            uint32_t timeout_ms,
                            ButtonAbortDetector* abort_detector) {
  if (ssid.length() == 0) {
    return false;
  }
  if (WiFi.status() == WL_CONNECTED) {
    return true;
  }
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true, true);
  restartAwareDelay(100);
  WiFi.mode(WIFI_STA);
  if (g_ble_started) {
    WiFi.setSleep(true);
    esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
  } else {
    WiFi.setSleep(false);
    esp_wifi_set_ps(WIFI_PS_NONE);
  }
  WiFi.begin(ssid.c_str(), password.c_str());
  const uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < timeout_ms) {
    pollControls();
    if (abort_detector != nullptr && abort_detector->shouldAbort()) {
      return false;
    }
    delay(20);
  }
  return WiFi.status() == WL_CONNECTED;
}

bool connectWifi(ButtonAbortDetector* abort_detector) {
  if (WiFi.status() == WL_CONNECTED) {
    return true;
  }
  const RuntimeConfig config = loadRuntimeConfig();
  return connectWifiCredentials(config.wifi_ssid,
                                config.wifi_password,
                                kWifiConnectAttemptMs,
                                abort_detector);
}

struct HttpResponse {
  int status_code = -1;
  String body;
};

void configureZeroHttpClient(HTTPClient* http) {
  if (http == nullptr) {
    return;
  }
  http->setUserAgent(kZeroUserAgent);
  http->setReuse(false);
}

bool httpGetWithToken(const String& url,
                      const String& auth_token,
                      HttpResponse* response_out,
                      ButtonAbortDetector* abort_detector) {
  if (response_out == nullptr || auth_token.length() == 0) {
    return false;
  }
  response_out->status_code = -1;
  response_out->body.remove(0);
  restartIfBtnBPressed();
  if (abort_detector != nullptr && abort_detector->shouldAbort()) {
    return false;
  }
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  configureZeroHttpClient(&http);
  http.setTimeout(12000);
  if (!http.begin(client, url)) {
    return false;
  }
  http.addHeader("Authorization", String("Bearer ") + auth_token);
  http.addHeader("Accept", "application/json");
  const int code = http.GET();
  response_out->status_code = code;
  restartIfBtnBPressed();
  if (abort_detector != nullptr && abort_detector->shouldAbort()) {
    http.end();
    return false;
  }
  if (code != 200) {
    http.end();
    return false;
  }
  response_out->body = http.getString();
  http.end();
  return true;
}

bool httpPostJsonRequest(const String& url,
                         const String& body,
                         const String& auth_token,
                         HttpResponse* response_out,
                         ButtonAbortDetector* abort_detector) {
  if (response_out == nullptr) {
    return false;
  }
  response_out->status_code = -1;
  response_out->body.remove(0);
  restartIfBtnBPressed();
  if (abort_detector != nullptr && abort_detector->shouldAbort()) {
    return false;
  }
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  configureZeroHttpClient(&http);
  http.setTimeout(20000);
  if (!http.begin(client, url)) {
    return false;
  }
  if (auth_token.length() > 0) {
    http.addHeader("Authorization", String("Bearer ") + auth_token);
  }
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Accept", "application/json");
  const int code = http.POST(body);
  response_out->status_code = code;
  if (code > 0) {
    response_out->body = http.getString();
  }
  restartIfBtnBPressed();
  if (abort_detector != nullptr && abort_detector->shouldAbort()) {
    http.end();
    return false;
  }
  if (code != 200 && code != 201) {
    http.end();
    return false;
  }
  http.end();
  return true;
}

bool httpPostJsonAnyStatus(const String& url,
                           const String& body,
                           const String& auth_token,
                           HttpResponse* response_out,
                           ButtonAbortDetector* abort_detector) {
  if (response_out == nullptr) {
    return false;
  }
  response_out->status_code = -1;
  response_out->body.remove(0);
  restartIfBtnBPressed();
  if (abort_detector != nullptr && abort_detector->shouldAbort()) {
    return false;
  }
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  configureZeroHttpClient(&http);
  http.setTimeout(5000);
  if (!http.begin(client, url)) {
    return false;
  }
  if (auth_token.length() > 0) {
    http.addHeader("Authorization", String("Bearer ") + auth_token);
  }
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Accept", "application/json");
  const int code = http.POST(body);
  response_out->status_code = code;
  restartIfBtnBPressed();
  if (abort_detector != nullptr && abort_detector->shouldAbort()) {
    http.end();
    return false;
  }
  if (code > 0) {
    response_out->body = http.getString();
  }
  http.end();
  return code > 0;
}

bool httpGet(const String& url, String* body_out, ButtonAbortDetector* abort_detector) {
  if (body_out == nullptr) {
    return false;
  }
  const RuntimeConfig config = loadRuntimeConfig();
  HttpResponse response;
  if (!httpGetWithToken(url, config.auth_token, &response, abort_detector)) {
    return false;
  }
  *body_out = response.body;
  return true;
}

bool httpPostJson(const String& url,
                  const String& body,
                  String* response_out,
                  ButtonAbortDetector* abort_detector) {
  if (response_out == nullptr) {
    return false;
  }
  const RuntimeConfig config = loadRuntimeConfig();
  HttpResponse response;
  if (!httpPostJsonRequest(url, body, config.auth_token, &response, abort_detector)) {
    return false;
  }
  *response_out = response.body;
  return true;
}

bool transcribePcmFile(const char* path, std::string* text_out) {
  if (text_out == nullptr) {
    return false;
  }
  text_out->clear();
  const RuntimeConfig config = loadRuntimeConfig();
  if (!config.hasAuthToken()) {
    Serial.println("transcription failed: missing auth token");
    return false;
  }

  File file = LittleFS.open(path, FILE_READ);
  if (!file || file.size() == 0) {
    Serial.println("transcription failed: missing or empty pcm file");
    if (file) {
      file.close();
    }
    return false;
  }
  const size_t pcm_size = static_cast<size_t>(file.size());
  Serial.printf("transcription pcm bytes=%u\n", static_cast<unsigned>(pcm_size));

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  configureZeroHttpClient(&http);
  http.setTimeout(60000);
  if (!http.begin(client, kZeroTranscriptionUrl)) {
    file.close();
    Serial.println("transcription failed: http begin");
    return false;
  }
  http.addHeader("Authorization", String("Bearer ") + config.auth_token);
  http.addHeader("Content-Type", "application/octet-stream");
  http.addHeader("Accept", "application/json");
  const int code = http.sendRequest("POST", &file, pcm_size);
  file.close();
  restartIfBtnBPressed();
  String response_body;
  if (code > 0) {
    response_body = http.getString();
  }
  Serial.printf("transcription http code=%d\n", code);
  if (code != 200) {
    if (response_body.length() > 0) {
      Serial.print("transcription response: ");
      Serial.println(response_body.substring(0, 240));
    }
    http.end();
    return false;
  }
  http.end();

  *text_out =
      zero_buddy::extractJsonString(std::string(response_body.c_str()), "text");
  if (text_out->empty()) {
    Serial.print("transcription empty text response: ");
    Serial.println(response_body.substring(0, 240));
  }
  return !text_out->empty();
}

enum class BootHttpOutcome {
  Ok,
  NetworkFailed,
  Rejected,
};

struct DeviceCodeSession {
  String device_code;
  String poll_token;
  uint32_t expires_in_seconds = 300;
  uint32_t interval_seconds = 3;
};

enum class DeviceTokenPollResult {
  Approved,
  Pending,
  Expired,
  NetworkFailed,
  Rejected,
};

String chipSuffix() {
  char suffix[5] = {0};
  snprintf(suffix, sizeof(suffix), "%04X",
           static_cast<unsigned>(ESP.getEfuseMac() & 0xFFFF));
  return String(suffix);
}

bool extractJsonUnsigned(const String& body, const char* key, uint32_t* value_out) {
  if (key == nullptr || value_out == nullptr) {
    return false;
  }
  const String needle = String("\"") + key + "\":";
  int pos = body.indexOf(needle);
  if (pos < 0) {
    return false;
  }
  pos += needle.length();
  while (pos < static_cast<int>(body.length()) && isspace(body[pos])) {
    ++pos;
  }
  uint32_t value = 0;
  bool found = false;
  while (pos < static_cast<int>(body.length()) && isdigit(body[pos])) {
    found = true;
    value = value * 10 + static_cast<uint32_t>(body[pos] - '0');
    ++pos;
  }
  if (!found) {
    return false;
  }
  *value_out = value;
  return true;
}

BootHttpOutcome requestDeviceCode(DeviceCodeSession* session_out) {
  if (session_out == nullptr) {
    return BootHttpOutcome::Rejected;
  }
  session_out->device_code = "";
  session_out->poll_token = "";
  session_out->expires_in_seconds = 300;
  session_out->interval_seconds = 3;
  const String body = String("{\"device_type\":\"bb0\",\"device_id\":\"Zero-Buddy-") +
                      chipSuffix() + "\",\"firmware_version\":\"" +
                      kFirmwareVersion + "\"}";
  HttpResponse response;
  if (!httpPostJsonRequest(kZeroDeviceTokenUrl, body, "", &response, nullptr)) {
    return response.status_code < 0 ? BootHttpOutcome::NetworkFailed
                                    : BootHttpOutcome::Rejected;
  }
  const std::string response_body(response.body.c_str());
  session_out->device_code =
      String(zero_buddy::extractJsonString(response_body, "device_code").c_str());
  session_out->poll_token =
      String(zero_buddy::extractJsonString(response_body, "poll_token").c_str());
  uint32_t expires = 300;
  extractJsonUnsigned(response.body, "expires_in", &expires);
  session_out->expires_in_seconds = expires == 0 ? 300 : expires;
  uint32_t interval = 3;
  extractJsonUnsigned(response.body, "interval", &interval);
  session_out->interval_seconds = interval == 0 ? 3 : interval;
  if (session_out->device_code.length() == 0 ||
      session_out->poll_token.length() == 0 ||
      session_out->poll_token.length() > kMaxPollTokenChars) {
    return BootHttpOutcome::Rejected;
  }
  return BootHttpOutcome::Ok;
}

class Bb0ConfigWriteCallbacks : public NimBLECharacteristicCallbacks {
 public:
  void onWrite(NimBLECharacteristic* characteristic, NimBLEConnInfo&) override {
    if (characteristic == nullptr) {
      return;
    }
    const std::string payload = characteristic->getValue();
    std::string ssid = zero_buddy::extractJsonString(payload, "wifi_ssid");
    std::string password = zero_buddy::extractJsonString(payload, "wifi_password");
    if (ssid.empty()) {
      ssid = zero_buddy::extractJsonString(payload, "ssid");
      password = zero_buddy::extractJsonString(payload, "password");
    }
    if (!ssid.empty() &&
        ssid.size() <= kMaxWifiSsidChars &&
        password.size() <= kMaxWifiPasswordChars) {
      g_ble_wifi_ssid = String(ssid.c_str());
      g_ble_wifi_password = String(password.c_str());
      g_ble_wifi_received = true;
      return;
    }

  }
};

String bleDeviceName() {
  return String("Zero-Buddy-") + chipSuffix();
}

uint16_t bleShortDeviceId() {
  return static_cast<uint16_t>(ESP.getEfuseMac() & 0xFFFF);
}

uint8_t bleProvisioningFlags(zero_buddy::ProvisioningError error) {
  uint8_t flags = error == zero_buddy::ProvisioningError::None ? 0 : kBleFlagError;
  const RuntimeConfig config = loadRuntimeConfig();
  if (config.hasWifi()) {
    flags |= kBleFlagWifiConfigured;
  }
  if (config.hasAuthToken()) {
    flags |= kBleFlagAuthConfigured;
  }
  if (config.hasThreadId()) {
    flags |= kBleFlagThreadConfigured;
  }
  return flags;
}

void updateBleAdvertisingState(zero_buddy::ProvisioningState state,
                               zero_buddy::ProvisioningError error) {
  if (g_ble_advertising == nullptr) {
    return;
  }
  const auto data = zero_buddy::buildProvisioningServiceData(
      state, bleProvisioningFlags(error), bleShortDeviceId(), error);
  NimBLEAdvertisementData scan_data;
  scan_data.setName(bleDeviceName().c_str());
  if (!scan_data.setServiceData(
          NimBLEUUID(kBb0BleServiceDataUuid32), data.data(), data.size()) ||
      !g_ble_advertising->setScanResponseData(scan_data)) {
    Serial.println("BLE service data update failed");
  }
}

void appendNullableJsonString(String* json, const char* key, const String& value) {
  if (json == nullptr || key == nullptr) {
    return;
  }
  *json += "\"";
  *json += key;
  *json += "\":";
  if (value.length() == 0) {
    *json += "null";
    return;
  }
  *json += "\"";
  *json += jsonEscape(value);
  *json += "\"";
}

String buildBleProvisioningInfoJson(zero_buddy::ProvisioningState state,
                                    zero_buddy::ProvisioningError error,
                                    const String& device_code,
                                    const String& nonce) {
  String json = "{";
  json += "\"protocol\":\"bb0.provisioning.v1\",";
  json += "\"device_id\":\"";
  json += bleDeviceName();
  json += "\",";
  json += "\"firmware_version\":\"";
  json += kFirmwareVersion;
  json += "\",";
  json += "\"provisioning_state\":\"";
  json += zero_buddy::provisioningStateName(state);
  json += "\",";
  appendNullableJsonString(&json, "device_code", device_code);
  json += ",";
  appendNullableJsonString(&json, "ble_session_nonce", nonce);
  json += ",\"error_code\":";
  const char* error_code = zero_buddy::provisioningErrorCodeName(error);
  if (error_code == nullptr || error_code[0] == '\0') {
    json += "null";
  } else {
    json += "\"";
    json += error_code;
    json += "\"";
  }
  json += "}";
  return json;
}

void publishBleProvisioningState(
    zero_buddy::ProvisioningState state,
    zero_buddy::ProvisioningError error = zero_buddy::ProvisioningError::None,
    const String& device_code = "",
    const String& nonce = "") {
  updateBleAdvertisingState(state, error);
  if (g_ble_info_characteristic == nullptr) {
    return;
  }
  const String json = buildBleProvisioningInfoJson(state, error, device_code, nonce);
  g_ble_info_characteristic->setValue(json.c_str());
  g_ble_info_characteristic->notify();
}

bool ensureBleProvisioningStarted() {
  if (g_ble_started) {
    return true;
  }
  g_ble_wifi_received = false;
  g_ble_wifi_ssid = "";
  g_ble_wifi_password = "";

  const String device_name = bleDeviceName();
  if (WiFi.getMode() != WIFI_OFF || WiFi.status() == WL_CONNECTED) {
    Serial.println("Stopping WiFi before BLE provisioning start");
    WiFi.disconnect(false, false);
    WiFi.mode(WIFI_OFF);
    delay(120);
  }
  NimBLEDevice::init(device_name.c_str());
  NimBLEServer* server = NimBLEDevice::createServer();
  NimBLEService* service = server->createService(kBb0BleServiceUuid);
  NimBLECharacteristic* info = service->createCharacteristic(
      kBb0BleInfoUuid,
      NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  NimBLECharacteristic* config = service->createCharacteristic(
      kBb0BleConfigUuid,
      NIMBLE_PROPERTY::WRITE);
  static Bb0ConfigWriteCallbacks callbacks;
  config->setCallbacks(&callbacks);
  g_ble_info_characteristic = info;
  g_ble_advertising = NimBLEDevice::getAdvertising();
  g_ble_advertising->enableScanResponse(true);
  g_ble_advertising->addServiceUUID(kBb0BleServiceUuid);
  publishBleProvisioningState(zero_buddy::ProvisioningState::Setup);
  g_ble_advertising->start();
  g_ble_started = true;
  return true;
}

void stopBleProvisioning() {
  if (!g_ble_started) {
    return;
  }
  if (g_ble_advertising != nullptr) {
    g_ble_advertising->stop();
  }
  NimBLEDevice::deinit(true);
  g_ble_info_characteristic = nullptr;
  g_ble_advertising = nullptr;
  g_ble_started = false;
  g_ble_wifi_received = false;
}

bool waitForBleWifiCredentials() {
  if (!ensureBleProvisioningStarted()) {
    return false;
  }
  const String device_name = bleDeviceName();
  g_ble_wifi_received = false;
  publishBleProvisioningState(zero_buddy::ProvisioningState::Setup);
  g_screen.render_screen_setup_wifi(device_name.c_str(), kBb0VerificationUrl);
  while (!g_ble_wifi_received) {
    pollControls();
    delay(20);
  }
  publishBleProvisioningState(zero_buddy::ProvisioningState::WifiReceived);
  return saveRuntimeWifiConfig(g_ble_wifi_ssid, g_ble_wifi_password);
}

bool provisionWifiOverBle() {
  while (true) {
    if (!waitForBleWifiCredentials()) {
      publishBleProvisioningState(zero_buddy::ProvisioningState::Error,
                                  zero_buddy::ProvisioningError::BleWriteFailed);
      g_screen.render_screen_setup_status("wifi setup failed", "ble write");
      restartAwareDelay(1200);
      continue;
    }
    const String ssid = g_ble_wifi_ssid;
    const String password = g_ble_wifi_password;
    stopBleProvisioning();
    g_screen.render_screen_setup_status("connecting wifi", ssid.c_str());
    const bool connected = connectWifiCredentials(ssid,
                                                  password,
                                                  kProvisioningConnectAttemptMs,
                                                  nullptr);
    if (connected) {
      return true;
    }
    const auto action =
        zero_buddy::repairActionForBootFailure(zero_buddy::BootRepairEvent::WifiUnavailable);
    if (action.reprovision_wifi) {
      g_screen.render_screen_setup_status("wifi failed", "send again");
      restartAwareDelay(1500);
    }
  }
}

bool ensureWifiReady() {
  RuntimeConfig config = loadRuntimeConfig();
  if (config.hasWifi() &&
      connectWifiCredentials(config.wifi_ssid, config.wifi_password, kWifiConnectAttemptMs, nullptr)) {
    return true;
  }
  return provisionWifiOverBle();
}

DeviceTokenPollResult pollDeviceTokenOnce(const DeviceCodeSession& session,
                                          uint32_t* next_interval_seconds) {
  const String body = String("{\"device_code\":\"") + jsonEscape(session.device_code) +
                      "\",\"poll_token\":\"" + jsonEscape(session.poll_token) + "\"}";
  HttpResponse response;
  if (!httpPostJsonAnyStatus(kZeroDeviceTokenPollUrl, body, "", &response, nullptr)) {
    return DeviceTokenPollResult::NetworkFailed;
  }
  if (response.status_code == 202) {
    if (next_interval_seconds != nullptr) {
      uint32_t interval = *next_interval_seconds;
      extractJsonUnsigned(response.body, "interval", &interval);
      *next_interval_seconds = interval == 0 ? *next_interval_seconds : interval;
    }
    return DeviceTokenPollResult::Pending;
  }
  if (response.status_code == 410) {
    return DeviceTokenPollResult::Expired;
  }
  if (response.status_code != 200) {
    return DeviceTokenPollResult::Rejected;
  }

  const std::string response_body(response.body.c_str());
  const std::string status = zero_buddy::extractJsonString(response_body, "status");
  if (status == "pending") {
    if (next_interval_seconds != nullptr) {
      uint32_t interval = *next_interval_seconds;
      extractJsonUnsigned(response.body, "interval", &interval);
      *next_interval_seconds = interval == 0 ? *next_interval_seconds : interval;
    }
    return DeviceTokenPollResult::Pending;
  }
  if (status == "expired") {
    return DeviceTokenPollResult::Expired;
  }
  if (!status.empty() && status != "approved") {
    return DeviceTokenPollResult::Rejected;
  }

  const String auth_token =
      String(zero_buddy::extractJsonString(response_body, "api_token").c_str());
  std::string thread = zero_buddy::extractJsonString(response_body, "thread_id");
  if (thread.empty()) {
    thread = zero_buddy::extractJsonString(response_body, "threadId");
  }
  const String thread_id = String(thread.c_str());
  if (auth_token.length() == 0 || auth_token.length() > kMaxAuthTokenChars ||
      thread_id.length() == 0 || thread_id.length() > kMaxThreadIdChars) {
    return DeviceTokenPollResult::Rejected;
  }
  if (!saveRuntimeAuthToken(auth_token) || !saveRuntimeThreadId(thread_id)) {
    return DeviceTokenPollResult::Rejected;
  }
  zero_buddy::state::clearLastMessageId(&g_state);
  zero_buddy::state::resetCheckDelay(&g_state);
  clear_assistant_message();
  return DeviceTokenPollResult::Approved;
}

DeviceTokenPollResult pollDeviceTokenUntilApproved(const DeviceCodeSession& session) {
  const uint32_t expires_seconds =
      session.expires_in_seconds == 0 ? 300 : session.expires_in_seconds;
  uint32_t interval_seconds =
      session.interval_seconds == 0 ? 3 : session.interval_seconds;
  const uint32_t expires_ms = expires_seconds * 1000UL;
  const uint32_t start = millis();
  uint32_t next_poll_at = start;
  uint32_t last_rendered_seconds = UINT32_MAX;

  while (static_cast<int32_t>(millis() - (start + expires_ms)) < 0) {
    pollControls();
    const uint32_t now = millis();
    const uint32_t elapsed = now - start;
    const uint32_t remaining_ms = elapsed >= expires_ms ? 0 : expires_ms - elapsed;
    const uint32_t remaining_seconds = (remaining_ms + 999UL) / 1000UL;
    if (remaining_seconds != last_rendered_seconds) {
      g_screen.render_screen_setup_device_code(session.device_code.c_str(), remaining_seconds);
      last_rendered_seconds = remaining_seconds;
    }

    if (static_cast<int32_t>(now - next_poll_at) >= 0) {
      const DeviceTokenPollResult result =
          pollDeviceTokenOnce(session, &interval_seconds);
      if (result == DeviceTokenPollResult::Approved ||
          result == DeviceTokenPollResult::Expired ||
          result == DeviceTokenPollResult::Rejected) {
        return result;
      }
      const uint32_t bounded_interval = std::max<uint32_t>(1, std::min<uint32_t>(30, interval_seconds));
      next_poll_at = millis() + bounded_interval * 1000UL;
    }
    delay(20);
  }
  return DeviceTokenPollResult::Expired;
}

bool runDeviceCodeProvisioning() {
  while (true) {
    stopBleProvisioning();
    if (!ensureWifiReady()) {
      continue;
    }
    DeviceCodeSession session;
    const BootHttpOutcome requested = requestDeviceCode(&session);
    if (requested == BootHttpOutcome::NetworkFailed) {
      g_screen.render_screen_setup_status("network issue", "retrying");
      restartAwareDelay(1500);
      ensureWifiReady();
      continue;
    }
    if (requested != BootHttpOutcome::Ok) {
      g_screen.render_screen_setup_status("temporarily", "unavailable");
      while (true) {
        pollControls();
        delay(50);
      }
    }
    const DeviceTokenPollResult poll_result = pollDeviceTokenUntilApproved(session);
    if (poll_result == DeviceTokenPollResult::Approved) {
      g_screen.render_screen_setup_status("device bound", "ready");
      restartAwareDelay(800);
      return true;
    }
    if (poll_result == DeviceTokenPollResult::Rejected) {
      g_screen.render_screen_setup_status("temporarily", "unavailable");
      while (true) {
        pollControls();
        delay(50);
      }
    }
    g_screen.render_screen_setup_status("code expired", "retrying");
    restartAwareDelay(1500);
  }
}

BootHttpOutcome validateRuntimeThread(const RuntimeConfig& config) {
  if (!config.hasAuthToken() || !config.hasThreadId()) {
    return BootHttpOutcome::Rejected;
  }
  const String url = String("https://") + kZeroHost + "/api/v1/chat-threads/" +
                     config.thread_id + "/messages?limit=1";
  HttpResponse response;
  if (!httpGetWithToken(url, config.auth_token, &response, nullptr)) {
    return response.status_code < 0 ? BootHttpOutcome::NetworkFailed
                                    : BootHttpOutcome::Rejected;
  }
  return BootHttpOutcome::Ok;
}

BootHttpOutcome createRuntimeThreadFromInitMessage(const RuntimeConfig& config) {
  if (!config.hasAuthToken()) {
    return BootHttpOutcome::Rejected;
  }
  const String body = "{\"prompt\":\"hello world from BB0\"}";
  HttpResponse response;
  if (!httpPostJsonRequest(kZeroPostUrl, body, config.auth_token, &response, nullptr)) {
    return response.status_code < 0 ? BootHttpOutcome::NetworkFailed
                                    : BootHttpOutcome::Rejected;
  }
  const std::string response_body(response.body.c_str());
  const String thread_id =
      String(zero_buddy::extractJsonString(response_body, "threadId").c_str());
  const String message_id =
      String(zero_buddy::extractJsonString(response_body, "messageId").c_str());
  if (thread_id.length() == 0 || thread_id.length() > kMaxThreadIdChars) {
    return BootHttpOutcome::Rejected;
  }
  if (!saveRuntimeThreadId(thread_id)) {
    return BootHttpOutcome::Rejected;
  }
  if (message_id.length() > 0) {
    zero_buddy::state::setLastMessageId(&g_state, std::string(message_id.c_str()));
  }
  zero_buddy::state::resetCheckDelay(&g_state);
  clear_assistant_message();
  return BootHttpOutcome::Ok;
}

void haltBootPreflight(const char* line1, const char* line2) {
  g_screen.render_screen_setup_status(line1, line2);
  while (true) {
    pollControls();
    delay(50);
  }
}

bool ensureIdentityReady() {
  while (true) {
    RuntimeConfig config = loadRuntimeConfig();
    if (!config.hasAuthToken()) {
      runDeviceCodeProvisioning();
      continue;
    }
    if (!config.hasThreadId()) {
      const BootHttpOutcome created = createRuntimeThreadFromInitMessage(config);
      if (created == BootHttpOutcome::Ok) {
        continue;
      }
      if (created == BootHttpOutcome::NetworkFailed) {
        ensureWifiReady();
        continue;
      }
      const auto action =
          zero_buddy::repairActionForBootFailure(zero_buddy::BootRepairEvent::ThreadCreateFailed);
      if (action.clear_auth_token) {
        clearRuntimeAuthAndThread();
        if (config.auth_token_from_compile) {
          haltBootPreflight("token rejected", "check firmware");
        }
        if (!config.auth_token_from_nvs) {
          runDeviceCodeProvisioning();
        }
      }
      continue;
    }

    const BootHttpOutcome validated = validateRuntimeThread(config);
    if (validated == BootHttpOutcome::Ok) {
      return true;
    }
    if (validated == BootHttpOutcome::NetworkFailed) {
      ensureWifiReady();
      continue;
    }
    const auto action =
        zero_buddy::repairActionForBootFailure(zero_buddy::BootRepairEvent::MessageReadFailed);
    if (action.clear_thread_id) {
      if (config.thread_id_from_compile) {
        haltBootPreflight("thread rejected", "check firmware");
      }
      clearRuntimeThreadId();
    }
  }
}

bool resetRuntimeConfigRequested() {
  return buttonADown() && buttonBDown();
}

void runBootPreflight() {
  if (resetRuntimeConfigRequested()) {
    clearRuntimeConfig();
    g_screen.render_screen_setup_status("runtime config", "cleared");
    restartAwareDelay(1200);
  }
  ensureWifiReady();
  ensureIdentityReady();
  stopBleProvisioning();
}

class HardwareCheckOps : public CheckAssistantMessageOps {
 public:
  void setMode(CheckAssistantMessageMode* mode) {
    mode_ = mode;
  }

  void setCpuForNetwork() override {
    setCpu(kNetworkCpuMhz);
  }

  bool ensureWifiConnected() override {
    const bool ok = connectWifi(&abort_detector_);
    abortForButtonIntent();
    return ok && !isAborted();
  }

  bool pollAssistantMessages(const std::string& since_id,
                             AssistantCheckResult* result_out) override {
    const RuntimeConfig config = loadRuntimeConfig();
    if (result_out == nullptr || !config.hasThreadId()) {
      return false;
    }
    String url = String("https://") + kZeroHost + "/api/v1/chat-threads/" +
                 config.thread_id + "/messages?limit=10";
    if (!since_id.empty()) {
      url += "&sinceId=" + String(since_id.c_str());
    }
    String body;
    if (!httpGet(url, &body, &abort_detector_)) {
      abortForButtonIntent();
      return false;
    }
    abortForButtonIntent();
    if (isAborted()) {
      return false;
    }

    const auto parsed = zero_buddy::parseZeroMessagesResponse(body.c_str());
    if (parsed.newest_message_id.size() >= zero_buddy::state::kLastMessageIdBytes) {
      return false;
    }
    result_out->newestMessageId = parsed.newest_message_id;
    result_out->assistantMessages.clear();
    const size_t capacity = kMaxAssistantMessages - assistant_message_count();
    for (const auto& message : parsed.assistant_messages) {
      if (result_out->assistantMessages.size() >= capacity) {
        break;
      }
      result_out->assistantMessages.push_back(message);
    }
    result_out->hasNewAssistantMessages = !result_out->assistantMessages.empty();
    return true;
  }

  bool appendAssistantMessages(const AssistantCheckResult& result) override {
    cleanupTempFiles();
    const size_t capacity = kMaxAssistantMessages - assistant_message_count();
    if (result.assistantMessages.size() > capacity) {
      return false;
    }
    for (const auto& message : result.assistantMessages) {
      if (!append_assistant_message(message)) {
        return false;
      }
    }
    return true;
  }

  void cancelNetwork() override {
    // Current HTTP calls are synchronous and own their client locally.
    // There is no persistent network handle to close here.
  }

  void closeFiles() override {}

  void cleanupTempFiles() override {
    for (size_t i = 0; i < kMaxAssistantMessages; ++i) {
      const String tmp_path = assistantMessageTmpPath(i);
      removeIfExists(tmp_path.c_str());
    }
  }

 private:
  bool isAborted() const {
    return mode_ != nullptr && mode_->abortRequested();
  }

  void abortForButtonIntent() {
    if (mode_ != nullptr && abort_detector_.shouldAbort()) {
      mode_->abort(abort_detector_.abortReason());
    }
  }

  CheckAssistantMessageMode* mode_ = nullptr;
  ButtonAbortDetector abort_detector_;
};

class HardwareDeepSleepOps : public DeepSleepOps {
 public:
  void configureRtcWake(uint32_t delay_ms) override {
    stopRuntimeCheckTimer();
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(delay_ms) * 1000ULL);
  }

  void configureBtnAWake() override {
    enableExternalPowerWakeIrq();
    esp_sleep_enable_ext0_wakeup(kPowerIrqWakePin, 0);
    esp_sleep_enable_ext1_wakeup(gpioWakeMask(kBtnAWakePin), ESP_EXT1_WAKEUP_ALL_LOW);
  }

  void screenOff() override {
    g_screen.screenOff();
  }

  void disconnectWifi() override {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
  }

  bool isCharging() override {
    charging_detected_ = externalPowerConnected();
    return charging_detected_;
  }

  bool chargingDetected() const {
    return charging_detected_;
  }

  void enterCpuHibernate() override {
    delay(20);
    esp_deep_sleep_start();
  }

  void cancelRtcWake() override {
    cancelCheckTimers();
  }

 private:
  bool charging_detected_ = false;
};

class HardwareReadOps : public ReadOps {
 public:
  void screenOn() override {
    g_screen.screenOn();
  }

  void setCpuForReading() override {
    setCpu(kReadCpuMhz);
  }

  size_t storedAssistantMessageCount() override {
    return assistant_message_count();
  }

  bool loadReadProgress(ReadProgress* progress_out) override {
    return load_read_progress(progress_out);
  }

  bool loadAssistantMessage(size_t index, std::string* message_out) override {
    return read_assistant_message(index, message_out);
  }

  size_t maxScrollTopForMessage(const std::string& message) override {
    return g_screen.maxScrollTopForMessage(message);
  }

  size_t scrollStep() override {
    return g_screen.scrollStep();
  }

  bool saveReadProgress(size_t count, size_t index, size_t scroll_top) override {
    if (count == 0) {
      return writeAssistantQueueManifest(0, 0, 0);
    }
    if (index > count) {
      index = count - 1;
      scroll_top = 0;
    }
    return writeAssistantQueueManifest(count, index, scroll_top);
  }

  bool clearAssistantMessages() override {
    return clear_assistant_message();
  }

  void renderNoAssistantMessage() override {
    g_screen.render_screen_read_empty();
    refreshBatteryLevel();
  }

  void renderAssistantMessage(size_t index,
                              size_t count,
                              const std::string& message,
                              size_t scroll_top) override {
    g_screen.render_screen_read_message(index, count, message, scroll_top);
  }

  ReadInput waitForInput(uint32_t timeout_ms) override {
    const uint32_t start = millis();
    uint32_t down_since_ms = 0;
    while (millis() - start < timeout_ms) {
      pollControls();
      refreshBatteryLevelIfDue();
      if (runtimeCheckDue()) {
        return ReadInput::CheckDue;
      }
      if (buttonADown()) {
        const uint32_t now = millis();
        if (down_since_ms == 0) {
          down_since_ms = now == 0 ? 1 : now;
        }
        if (now - down_since_ms >= kLongPressMs) {
          return ReadInput::LongPress;
        }
      } else if (down_since_ms != 0) {
        refreshBatteryLevel();
        scheduleBatteryFollowupRefresh();
        return ReadInput::ShortPress;
      }
      restartAwareDelay(10);
    }
    return ReadInput::Timeout;
  }

  void cancelIdleTimer() override {}

  void closeFiles() override {}

 private:
  void refreshBatteryLevel() {
    g_screen.render_element_battery_level();
  }

  void scheduleBatteryFollowupRefresh() {
    g_screen.scheduleBatteryFollowupRefresh(millis());
  }

  void refreshBatteryLevelIfDue() {
    g_screen.renderBatteryFollowupIfDue(millis());
  }
};

class HardwareRecordingOps : public RecordingOps {
 public:
  void screenOn() override {
    g_screen.screenOn();
    g_screen.render_screen_recording_prompt();
  }

  void showResult(const ModeRunResult& result) {
    if (result.status == ModeRunStatus::Completed) {
      g_screen.render_screen_recording_sent();
      Serial.println("recording completed");
      Serial.flush();
      restartAwareDelay(5000);
      return;
    }
    if (result.status == ModeRunStatus::Aborted) {
      g_screen.render_screen_recording_aborted();
      Serial.print("recording aborted: ");
      Serial.println("abort");
      Serial.flush();
      restartAwareDelay(5000);
      return;
    }

    const char* error = modeRunErrorName(result.error);
    String detail(error);
    if (recording_failure_detail_ != nullptr && recording_failure_detail_[0] != '\0') {
      detail += ":";
      detail += recording_failure_detail_;
    }
    g_screen.render_screen_recording_failed(detail.c_str());
    Serial.print("recording failed: ");
    Serial.println(error);
    if (recording_failure_detail_ != nullptr && recording_failure_detail_[0] != '\0') {
      Serial.print("recording detail: ");
      Serial.println(recording_failure_detail_);
    }
    Serial.flush();
    restartAwareDelay(5000);
  }

  void setCpuForRecording() override {
    setCpu(kRecordingCpuMhz);
  }

  void cancelRtcWake() override {
    cancelCheckTimers();
  }

  bool clearAssistantMessages() override {
    return clear_assistant_message();
  }

  bool recordVoiceToFile() override {
    setRecordingFailureDetail("");
    g_screen.render_screen_recording_active();
    auto mic_cfg = M5.Mic.config();
    mic_cfg.pin_data_in = GPIO_NUM_34;
    mic_cfg.pin_ws = GPIO_NUM_0;
    mic_cfg.pin_mck = I2S_PIN_NO_CHANGE;
    mic_cfg.sample_rate = kSampleRate;
    mic_cfg.over_sampling = 1;
    mic_cfg.magnification = 16;
    mic_cfg.noise_filter_level = 0;
    mic_cfg.dma_buf_len = 128;
    mic_cfg.dma_buf_count = 8;
    mic_cfg.stereo = false;
    mic_cfg.left_channel = false;
    mic_cfg.use_adc = false;
    M5.Mic.config(mic_cfg);
    if (!M5.Mic.begin()) {
      setRecordingFailureDetail("mic-begin");
      return false;
    }

    File file = LittleFS.open(kVoicePcmPath, "w");
    if (!file) {
      M5.Mic.end();
      setRecordingFailureDetail("file-open");
      return false;
    }

    beepMicOn();
    size_t bytes_written = 0;
    const uint32_t start = millis();
    while (buttonADown() && millis() - start < kMaxRecordingMs) {
      pollControls();
      const bool got =
          M5.Mic.record(reinterpret_cast<int16_t*>(g_pcm_buffer),
                        kMicSamplesPerRead,
                        kSampleRate);
      if (!got) {
        restartAwareDelay(2);
        continue;
      }
      const size_t written = file.write(g_pcm_buffer, sizeof(g_pcm_buffer));
      if (written != sizeof(g_pcm_buffer)) {
        file.close();
        M5.Mic.end();
        beepMicOff();
        setRecordingFailureDetail("file-write");
        return false;
      }
      bytes_written += written;
    }
    file.flush();
    file.close();
    M5.Mic.end();
    beepMicOff();
    if (bytes_written == 0) {
      setRecordingFailureDetail("too-short");
    }
    return bytes_written > 0;
  }

  bool ensureWifiConnected() override {
    setRecordingFailureDetail("");
    g_screen.render_screen_recording_wifi();
    const bool ok = connectWifi(nullptr);
    if (!ok) {
      setRecordingFailureDetail("connect");
    }
    return ok;
  }

  bool voiceToText(std::string* text_out) override {
    setRecordingFailureDetail("");
    g_screen.render_screen_recording_transcribing();
    const bool ok = transcribePcmFile(kVoicePcmPath, text_out);
    if (!ok) {
      setRecordingFailureDetail("asr");
    }
    return ok;
  }

  void deleteVoiceFile() override {
    removeIfExists(kVoicePcmPath);
  }

  bool sendTextMessage(const std::string& text, std::string* user_message_id_out) override {
    const RuntimeConfig config = loadRuntimeConfig();
    if (user_message_id_out == nullptr || text.empty() || !config.hasThreadId()) {
      setRecordingFailureDetail("bad-message");
      return false;
    }
    setRecordingFailureDetail("");
    const std::string prompt_text = zero_buddy::preprocessAssistantForDisplay(text);
    g_screen.render_screen_recording_sending(prompt_text);
    const String body =
        String("{\"prompt\":\"") + jsonEscape(String(prompt_text.c_str())) +
        "\",\"threadId\":\"" + config.thread_id + "\"}";
    HttpResponse http_response;
    Serial.printf("message send raw prompt bytes=%u\n", static_cast<unsigned>(text.size()));
    Serial.printf("message send prompt bytes=%u\n",
                  static_cast<unsigned>(prompt_text.size()));
    Serial.printf("message send auth chars=%u source=%s\n",
                  static_cast<unsigned>(config.auth_token.length()),
                  config.auth_token_from_compile
                      ? "compiled"
                      : (config.auth_token_from_nvs ? "nvs" : "none"));
    Serial.printf("message send thread chars=%u source=%s\n",
                  static_cast<unsigned>(config.thread_id.length()),
                  config.thread_id_from_compile
                      ? "compiled"
                      : (config.thread_id_from_nvs ? "nvs" : "none"));
    if (!httpPostJsonRequest(kZeroPostUrl,
                             body,
                             config.auth_token,
                             &http_response,
                             nullptr)) {
      Serial.printf("message send http code=%d\n", http_response.status_code);
      if (http_response.body.length() > 0) {
        Serial.print("message send response: ");
        Serial.println(http_response.body.substring(0, 240));
      }
      if (http_response.status_code == 404) {
        Serial.println("message send thread invalid, retrying without threadId");
        const String repair_body =
            String("{\"prompt\":\"") + jsonEscape(String(prompt_text.c_str())) + "\"}";
        HttpResponse repair_response;
        if (httpPostJsonRequest(kZeroPostUrl,
                                repair_body,
                                config.auth_token,
                                &repair_response,
                                nullptr)) {
          Serial.printf("message send repair http code=%d\n", repair_response.status_code);
          const std::string repair_message_id =
              zero_buddy::extractJsonString(repair_response.body.c_str(), "messageId");
          const String repair_thread_id =
              String(zero_buddy::extractJsonString(repair_response.body.c_str(),
                                                   "threadId")
                         .c_str());
          if (!repair_message_id.empty() && repair_thread_id.length() > 0 &&
              repair_thread_id.length() <= kMaxThreadIdChars &&
              saveRuntimeThreadId(repair_thread_id)) {
            *user_message_id_out = repair_message_id;
            return true;
          }
          Serial.print("message send repair invalid response: ");
          Serial.println(repair_response.body.substring(0, 240));
        } else {
          Serial.printf("message send repair http code=%d\n",
                        repair_response.status_code);
          if (repair_response.body.length() > 0) {
            Serial.print("message send repair response: ");
            Serial.println(repair_response.body.substring(0, 240));
          }
        }
      }
      String detail("http:");
      detail += http_response.status_code;
      setRecordingFailureDetail(detail);
      return false;
    }
    Serial.printf("message send http code=%d\n", http_response.status_code);
    const std::string id =
        zero_buddy::extractJsonString(http_response.body.c_str(), "messageId");
    if (id.empty()) {
      Serial.print("message send missing messageId response: ");
      Serial.println(http_response.body.substring(0, 240));
      setRecordingFailureDetail("message-id");
      return false;
    }
    *user_message_id_out = id;
    return true;
  }

  void stopVoiceRecording() override {
    M5.Mic.end();
  }

  void cancelVoiceToText() override {
    // Current ASR calls are synchronous and own their client locally.
    // There is no persistent ASR handle to close here.
  }

  void cancelTextMessageSend() override {
    // Current HTTP calls are synchronous and own their client locally.
    // There is no persistent send handle to close here.
  }

  void closeFiles() override {}

 private:
  void setRecordingFailureDetail(const char* detail) {
    recording_failure_detail_storage_.remove(0);
    recording_failure_detail_ = detail;
  }

  void setRecordingFailureDetail(const String& detail) {
    recording_failure_detail_storage_ = detail;
    recording_failure_detail_ = recording_failure_detail_storage_.c_str();
  }

  const char* recording_failure_detail_ = "";
  String recording_failure_detail_storage_;
};

void enterDeepSleep();
enum class ReadRunResult {
  Completed,
  CheckDue,
  RecordingCompleted,
};

enum class CheckRunResult {
  Completed,
  ReadRequested,
  RecordingRequested,
};

CheckRunResult runCheckAssistantOnce();
ReadRunResult runReadOnce();
void runPluggedReadCheckLoop();
void runReadThenSleep();
void runRecordingOnce();
void runRecordingThenSleep();

void runRecordingOnce() {
  zero_buddy::state::setMode(&g_state, zero_buddy::state::Mode::Recording);
  HardwareRecordingOps ops;
  RecordingMode mode(&g_state, &ops);
  const auto result = mode.main();
  ops.showResult(result);
}

void enterDeepSleep() {
  while (true) {
    zero_buddy::state::setMode(&g_state, zero_buddy::state::Mode::DeepSleep);
    HardwareDeepSleepOps ops;
    DeepSleepMode mode(&g_state, &ops);
    const auto result = mode.main();
    if (result.status == ModeRunStatus::Completed && ops.chargingDetected()) {
      cancelCheckTimers();
      runPluggedReadCheckLoop();
      continue;
    }
    return;
  }
}

CheckRunResult runCheckAssistantOnce() {
  zero_buddy::state::setMode(&g_state, zero_buddy::state::Mode::CheckAssistantMessage);
  HardwareCheckOps ops;
  CheckAssistantMessageMode mode(&g_state, &ops);
  ops.setMode(&mode);
  const auto result = mode.main();
  if (result.status == ModeRunStatus::Aborted && mode.abortRequested()) {
    const char* reason = mode.abortReason();
    if (reason != nullptr && strcmp(reason, "btn_a_short_press") == 0) {
      return CheckRunResult::ReadRequested;
    }
    return CheckRunResult::RecordingRequested;
  }
  return CheckRunResult::Completed;
}

void runCheckAssistantThenSleep() {
  const CheckRunResult result = runCheckAssistantOnce();
  if (result == CheckRunResult::ReadRequested) {
    runReadThenSleep();
    return;
  }
  if (result == CheckRunResult::RecordingRequested) {
    runRecordingOnce();
  }
  enterDeepSleep();
}

ReadRunResult runReadOnce() {
  zero_buddy::state::setMode(&g_state, zero_buddy::state::Mode::Read);
  HardwareReadOps ops;
  ReadMode mode(&g_state, &ops);
  const auto result = mode.main();
  if (result.status == ModeRunStatus::Aborted && mode.abortRequested()) {
    const char* reason = mode.abortReason();
    if (reason != nullptr && strcmp(reason, "check_due") == 0) {
      return ReadRunResult::CheckDue;
    }
    runRecordingOnce();
    return ReadRunResult::RecordingCompleted;
  } else {
    mode.abort("read_exit");
  }
  return ReadRunResult::Completed;
}

void runReadThenSleep() {
  const ReadRunResult result = runReadOnce();
  if (result != ReadRunResult::RecordingCompleted &&
      (result == ReadRunResult::CheckDue || externalPowerConnected())) {
    cancelCheckTimers();
    const CheckRunResult check_result = runCheckAssistantOnce();
    if (check_result == CheckRunResult::RecordingRequested) {
      runRecordingOnce();
    }
  }
  enterDeepSleep();
}

void runPluggedReadCheckLoop() {
  while (externalPowerConnected()) {
    const ReadRunResult read_result = runReadOnce();
    if (read_result != ReadRunResult::RecordingCompleted) {
      cancelCheckTimers();
      const CheckRunResult check_result = runCheckAssistantOnce();
      if (check_result == CheckRunResult::RecordingRequested) {
        runRecordingOnce();
      }
    }
  }
}

void runRecordingThenSleep() {
  runRecordingOnce();
  enterDeepSleep();
}

}  // namespace

void setup() {
  Serial.begin(115200);
  auto cfg = M5.config();
  cfg.clear_display = false;
  M5.begin(cfg);

  pinMode(static_cast<int>(kBtnAWakePin), INPUT);
  pinMode(static_cast<int>(kBtnBPin), INPUT);
  pinMode(static_cast<int>(kPowerIrqWakePin), INPUT);
  g_btn_b_was_down = buttonBDown();
  g_btn_b_interrupt_pending = false;
  attachInterrupt(digitalPinToInterrupt(static_cast<int>(kBtnBPin)),
                  handleBtnBFallingInterrupt,
                  FALLING);
  ledcSetup(0, 2000, 8);
  ledcAttachPin(kBuzzerPin, 0);
  ledcWriteTone(0, 0);

  const bool had_rtc_state = g_rtc_magic == kRtcMagic && stateLooksValid(g_state);
  const auto previous_mode =
      had_rtc_state ? g_state.currentMode : zero_buddy::state::Mode::DeepSleep;

  setCpu(kRecordingCpuMhz);
  LittleFS.begin(true);
  ensureGlobalStateInitialized();
  zero_buddy::state::setHasAssistantMessage(&g_state, assistant_message_exists());

  const esp_sleep_wakeup_cause_t wake_cause = esp_sleep_get_wakeup_cause();
  const uint64_t ext1_wake_mask =
      wake_cause == ESP_SLEEP_WAKEUP_EXT1 ? esp_sleep_get_ext1_wakeup_status() : 0;
  const bool btn_a_wake =
      (wake_cause == ESP_SLEEP_WAKEUP_EXT1 &&
       (ext1_wake_mask & gpioWakeMask(kBtnAWakePin)) != 0) ||
      buttonADown();
  const bool power_irq_wake = wake_cause == ESP_SLEEP_WAKEUP_EXT0;
  const bool axp_vbus_insert = power_irq_wake && axp192VbusInsertIrqActive();
  const bool external_power_wake =
      (power_irq_wake && externalPowerConnected()) ||
      (wake_cause == ESP_SLEEP_WAKEUP_UNDEFINED && had_rtc_state &&
       previous_mode == zero_buddy::state::Mode::DeepSleep &&
       externalPowerConnected());
  if (power_irq_wake) {
    clearAxp192IrqStatuses();
  }

  if (wake_cause == ESP_SLEEP_WAKEUP_UNDEFINED && !external_power_wake) {
    g_screen.render_screen_boot();
    restartAwareDelay(kBootSplashMs);
  }

  if (btn_a_wake) {
    g_screen.screenOn();
    if (waitForBtnALongPress(kLongPressMs)) {
      runRecordingThenSleep();
      return;
    }
    runReadThenSleep();
    return;
  }

  if (wake_cause == ESP_SLEEP_WAKEUP_TIMER) {
    runCheckAssistantThenSleep();
    return;
  }

  if (external_power_wake || (power_irq_wake && axp_vbus_insert)) {
    g_screen.screenOn();
    runReadThenSleep();
    return;
  }

  runBootPreflight();
  runReadThenSleep();
}

void loop() {}
