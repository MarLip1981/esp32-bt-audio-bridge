#include "bt_audio_bridge.h"

#include "esphome/core/log.h"

#include <cstdio>
#include <cstring>

#include <esp_system.h>

#ifdef USE_ARDUINO
#include "esp32-hal-alloc-bt-classic-mem.h"
#include "esp32-hal-bt.h"
#include <esp_bt.h>
#include <esp_err.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
extern "C" bool btInUse() { return true; }

// Arduino-ESP32 3.3.x routes Classic-BT startup through btStartMode().
// In our ESPHome/A2DP build that path can fail before Bluedroid starts.
// The A2DP library already performs the Bluedroid initialization itself, so
// here we only replace the controller-start part with the equivalent direct
// ESP-IDF sequence. The linker --wrap in CMakeLists.txt redirects the library
// call without modifying the vendored ESP32-A2DP source.
extern "C" bool __wrap_btStartMode(bt_mode mode) {
  esp_bt_mode_t esp_mode;
  switch (mode) {
    case BT_MODE_BLE:
      esp_mode = ESP_BT_MODE_BLE;
      break;
    case BT_MODE_CLASSIC_BT:
      esp_mode = ESP_BT_MODE_CLASSIC_BT;
      break;
    case BT_MODE_BTDM:
      esp_mode = ESP_BT_MODE_BTDM;
      break;
    case BT_MODE_DEFAULT:
    default:
      esp_mode = ESP_BT_MODE_CLASSIC_BT;
      break;
  }

  ESP_LOGI("bt_audio_bridge", "Direct BT start: mode=%d", static_cast<int>(esp_mode));

  if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_ENABLED) {
    return true;
  }

  esp_bt_controller_config_t cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
  cfg.mode = esp_mode;

  if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_IDLE) {
    esp_err_t err = esp_bt_controller_init(&cfg);
    if (err != ESP_OK) {
      ESP_LOGE("bt_audio_bridge", "Direct BT controller init failed: %s", esp_err_to_name(err));
      return false;
    }

    while (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_IDLE) {
      vTaskDelay(1);
    }
  }

  if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_INITED) {
    esp_err_t err = esp_bt_controller_enable(esp_mode);
    if (err != ESP_OK) {
      ESP_LOGE("bt_audio_bridge", "Direct BT controller enable failed: %s", esp_err_to_name(err));
      return false;
    }
  }

  const bool started = esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_ENABLED;
  ESP_LOGI("bt_audio_bridge", "Direct BT start result: %s", started ? "OK" : "FAILED");
  return started;
}
#endif

namespace esphome {
namespace bt_audio_bridge {

static const char *const TAG = "bt_audio_bridge";
BtAudioBridge *global_bt_audio_bridge = nullptr;

static bool bt_ssid_callback(const char *ssid, esp_bd_addr_t address, int rrsi) {
  if (global_bt_audio_bridge == nullptr) return true;
  char mac[18];
  std::snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
                address[0], address[1], address[2], address[3], address[4], address[5]);
  ESP_LOGI(TAG, "BT device found: %s  MAC: %s  RSSI: %d", ssid, mac, rrsi);
  return false;
}

static void bt_discovery_callback(esp_bt_gap_discovery_state_t discovery_mode) {
  if (discovery_mode == ESP_BT_GAP_DISCOVERY_STARTED)
    ESP_LOGI(TAG, "Bluetooth scan started");
  else
    ESP_LOGI(TAG, "Bluetooth scan finished");
}

void BtAudioBridge::setup() {
  global_bt_audio_bridge = this;
  ESP_LOGI(TAG, "Bluetooth A2DP Source ready");
  ESP_LOGI(TAG, "Reset reason: %d", static_cast<int>(esp_reset_reason()));
  this->connected_ = false;
  this->scanning_ = false;
  this->a2dp_started_ = false;
  this->scan_requested_ = false;
  std::strncpy(this->status_, "READY", sizeof(this->status_) - 1);
  this->publish_status_();
  this->publish_event_("BOOT: component ready");
  if (this->reset_reason_sensor_ != nullptr)
    this->reset_reason_sensor_->publish_state(this->reset_reason_());
  if (this->device_sensor_ != nullptr)
    this->device_sensor_->publish_state(this->selected_mac_[0] != '\0' ? this->selected_mac_ : "NONE");
}

void BtAudioBridge::loop() {
  if (this->scan_requested_ && !this->a2dp_started_) {
    this->scan_requested_ = false;
    ESP_LOGI(TAG, "Starting Bluetooth A2DP stack from ESPHome loop");
    this->publish_event_("SCAN: starting A2DP stack");
    this->start_a2dp_();
  }

  const unsigned long now = millis();
  if (now - this->last_status_check_ < 1000) return;
  this->last_status_check_ = now;
  if (!this->a2dp_started_) return;

  const bool active = this->a2dp_source_.is_active();
  if (active) {
    if (!this->connected_) this->publish_event_("BT: speaker connected");
    this->connected_ = true;
    std::strncpy(this->status_, "CONNECTED", sizeof(this->status_) - 1);
  } else {
    if (this->connected_) this->publish_event_("BT: speaker disconnected");
    this->connected_ = false;
    if (this->a2dp_source_.is_discovery_active()) {
      this->scanning_ = true;
      std::strncpy(this->status_, "SCANNING", sizeof(this->status_) - 1);
    } else {
      this->scanning_ = false;
      std::strncpy(this->status_, "DISCONNECTED", sizeof(this->status_) - 1);
    }
  }
  this->publish_status_();
}

void BtAudioBridge::dump_config() {
  ESP_LOGCONFIG(TAG, "Bluetooth Audio Bridge:");
  ESP_LOGCONFIG(TAG, "  Mode: A2DP Source");
  ESP_LOGCONFIG(TAG, "  Local name: ESP32 BT Audio Bridge");
  ESP_LOGCONFIG(TAG, "  SSP: enabled");
  ESP_LOGCONFIG(TAG, "  Auto reconnect: enabled");
}

void BtAudioBridge::start_scan() {
  ESP_LOGI(TAG, "Bluetooth scan requested");
  if (this->a2dp_started_) {
    ESP_LOGW(TAG, "A2DP stack already started; scan is already running or connected");
    this->publish_event_("SCAN: rejected, A2DP already started");
    return;
  }
  this->scanning_ = true;
  std::strncpy(this->status_, "SCANNING", sizeof(this->status_) - 1);
  this->publish_status_();
  this->publish_event_("SCAN: requested from Home Assistant");
  this->scan_requested_ = true;
}

void BtAudioBridge::connect_to(const char *mac) {
  if (mac == nullptr || std::strlen(mac) == 0) {
    ESP_LOGW(TAG, "No Bluetooth MAC address specified");
    this->publish_event_("CONNECT: no MAC address");
    return;
  }
  unsigned int b[6];
  if (std::sscanf(mac, "%02x:%02x:%02x:%02x:%02x:%02x", &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) != 6) {
    ESP_LOGE(TAG, "Invalid Bluetooth MAC address: %s", mac);
    this->publish_event_("CONNECT: invalid MAC address");
    return;
  }
  esp_bd_addr_t address = {static_cast<uint8_t>(b[0]), static_cast<uint8_t>(b[1]), static_cast<uint8_t>(b[2]),
                           static_cast<uint8_t>(b[3]), static_cast<uint8_t>(b[4]), static_cast<uint8_t>(b[5])};
  std::strncpy(this->selected_mac_, mac, sizeof(this->selected_mac_) - 1);
  this->selected_mac_[sizeof(this->selected_mac_) - 1] = '\0';
  std::strncpy(this->status_, "CONNECTING", sizeof(this->status_) - 1);
  this->publish_status_();
  this->publish_event_("CONNECT: starting");
  if (this->device_sensor_ != nullptr) this->device_sensor_->publish_state(this->selected_mac_);
  ESP_LOGI(TAG, "Connecting to Bluetooth device: %s", this->selected_mac_);
  if (!this->a2dp_started_) {
    this->scan_requested_ = false;
    this->start_a2dp_();
  }
  this->a2dp_source_.connect_to(address);
}

void BtAudioBridge::disconnect() {
  ESP_LOGI(TAG, "Disconnect requested");
  this->publish_event_("BT: disconnect requested");
  this->scan_requested_ = false;
  if (this->a2dp_started_) {
    this->a2dp_source_.end();
    this->a2dp_started_ = false;
  }
  this->connected_ = false;
  this->scanning_ = false;
  std::strncpy(this->status_, "DISCONNECTED", sizeof(this->status_) - 1);
  this->publish_status_();
}

bool BtAudioBridge::is_connected() { return this->connected_; }
const char *BtAudioBridge::get_status() { return this->status_; }

void BtAudioBridge::start_a2dp_() {
  ESP_LOGI(TAG, "A2DP init 1/6: set_local_name");
  this->publish_event_("A2DP 1/6: set_local_name");
  this->a2dp_source_.set_local_name("ESP32 BT Audio Bridge");
  ESP_LOGI(TAG, "A2DP init 2/6: set_ssp_enabled");
  this->publish_event_("A2DP 2/6: set_ssp_enabled");
  this->a2dp_source_.set_ssp_enabled(true);
  ESP_LOGI(TAG, "A2DP init 3/6: set_auto_reconnect");
  this->publish_event_("A2DP 3/6: set_auto_reconnect");
  this->a2dp_source_.set_auto_reconnect(true, 10);
  ESP_LOGI(TAG, "A2DP init 4/6: set_ssid_callback");
  this->publish_event_("A2DP 4/6: set_ssid_callback");
  this->a2dp_source_.set_ssid_callback(bt_ssid_callback);
  ESP_LOGI(TAG, "A2DP init 5/6: set_discovery_mode_callback");
  this->publish_event_("A2DP 5/6: set_discovery_callback");
  this->a2dp_source_.set_discovery_mode_callback(bt_discovery_callback);
  ESP_LOGI(TAG, "A2DP init 6/6: start");
  this->publish_event_("A2DP 6/6: start()");
  this->a2dp_source_.start();
  ESP_LOGI(TAG, "A2DP init: start() returned successfully");
  this->publish_event_("A2DP: start() returned");
  this->a2dp_started_ = true;
}

void BtAudioBridge::publish_status_() {
  if (this->status_sensor_ != nullptr) this->status_sensor_->publish_state(this->status_);
}

void BtAudioBridge::publish_event_(const char *event) {
  if (this->event_sensor_ != nullptr && event != nullptr) this->event_sensor_->publish_state(event);
}

const char *BtAudioBridge::reset_reason_() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "POWER ON";
    case ESP_RST_EXT: return "EXTERNAL RESET";
    case ESP_RST_SW: return "SOFTWARE RESET";
    case ESP_RST_PANIC: return "PANIC / CRASH";
    case ESP_RST_INT_WDT: return "INTERRUPT WATCHDOG";
    case ESP_RST_TASK_WDT: return "TASK WATCHDOG";
    case ESP_RST_WDT: return "OTHER WATCHDOG";
    case ESP_RST_DEEPSLEEP: return "DEEP SLEEP";
    case ESP_RST_BROWNOUT: return "BROWNOUT / LOW VOLTAGE";
    case ESP_RST_SDIO: return "SDIO RESET";
    default: return "UNKNOWN";
  }
}

}  // namespace bt_audio_bridge
}  // namespace esphome
