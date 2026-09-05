#include "bt_audio_bridge.h"

#include "esphome/core/log.h"

#include <cstdio>
#include <cstring>

#ifdef USE_ARDUINO
extern "C" bool btInUse() { return true; }
#endif

namespace esphome {
namespace bt_audio_bridge {

static const char *const TAG = "bt_audio_bridge";

BtAudioBridge *global_bt_audio_bridge = nullptr;

static bool bt_ssid_callback(const char *ssid, esp_bd_addr_t address, int rrsi) {
  if (global_bt_audio_bridge == nullptr) {
    return true;
  }

  ESP_LOGI(TAG, "BT device found: %s  RSSI: %d", ssid, rrsi);

  return true;
}

static void bt_discovery_callback(
    esp_bt_gap_discovery_state_t discovery_mode) {

  if (global_bt_audio_bridge == nullptr) {
    return;
  }

  if (discovery_mode == ESP_BT_GAP_DISCOVERY_STARTED) {
    ESP_LOGI(TAG, "Bluetooth scan started");
  } else {
    ESP_LOGI(TAG, "Bluetooth scan finished");
  }
}

void BtAudioBridge::setup() {
  global_bt_audio_bridge = this;

  ESP_LOGI(TAG, "Starting Bluetooth A2DP Source");

  // Do not start Classic Bluetooth during ESPHome setup. The previous
  // immediate start crashed before the native API came online. A2DP is
  // initialized only when an explicit scan or connection is requested.
  this->connected_ = false;
  this->scanning_ = false;
  this->a2dp_started_ = false;
  std::strncpy(this->status_, "READY", sizeof(this->status_) - 1);
  this->publish_status_();

  ESP_LOGI(TAG, "Bluetooth A2DP Source started");
}

void BtAudioBridge::loop() {
  const unsigned long now = millis();

  if (now - this->last_status_check_ < 1000) {
    return;
  }

  this->last_status_check_ = now;

  if (!this->a2dp_started_) {
    return;
  }

  const bool active = this->a2dp_source_.is_active();

  if (active) {
    if (!this->connected_) {
      ESP_LOGI(TAG, "Bluetooth speaker connected");
    }

    this->connected_ = true;
    std::strncpy(this->status_, "CONNECTED", sizeof(this->status_) - 1);

  } else {
    if (this->connected_) {
      ESP_LOGW(TAG, "Bluetooth speaker disconnected");
    }

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
  ESP_LOGI(TAG, "Starting Bluetooth discovery");

  if (this->a2dp_started_ && this->a2dp_source_.is_discovery_active()) {
    ESP_LOGI(TAG, "Bluetooth discovery already active");
    return;
  }

  this->scanning_ = true;
  std::strncpy(this->status_, "SCANNING", sizeof(this->status_) - 1);
  this->publish_status_();

  this->start_a2dp_();
}

void BtAudioBridge::connect_to(const char *mac) {
  if (mac == nullptr || std::strlen(mac) == 0) {
    ESP_LOGW(TAG, "No Bluetooth MAC address specified");
    return;
  }

  unsigned int b[6];
  if (std::sscanf(mac, "%02x:%02x:%02x:%02x:%02x:%02x",
                  &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) != 6) {
    ESP_LOGE(TAG, "Invalid Bluetooth MAC address: %s", mac);
    return;
  }

  esp_bd_addr_t address = {
      static_cast<uint8_t>(b[0]), static_cast<uint8_t>(b[1]),
      static_cast<uint8_t>(b[2]), static_cast<uint8_t>(b[3]),
      static_cast<uint8_t>(b[4]), static_cast<uint8_t>(b[5])};

  std::strncpy(this->selected_mac_, mac, sizeof(this->selected_mac_) - 1);
  this->selected_mac_[sizeof(this->selected_mac_) - 1] = '\0';
  std::strncpy(this->status_, "CONNECTING", sizeof(this->status_) - 1);
  this->publish_status_();

  ESP_LOGI(TAG, "Connecting to Bluetooth device: %s", this->selected_mac_);
  if (!this->a2dp_started_) {
    this->start_a2dp_();
  }
  this->a2dp_source_.connect_to(address);
}

void BtAudioBridge::disconnect() {
  ESP_LOGI(TAG, "Disconnect requested");

  if (this->a2dp_started_) {
    this->a2dp_source_.end();
    this->a2dp_started_ = false;
  }

  this->connected_ = false;
  this->scanning_ = false;

  std::strncpy(this->status_, "DISCONNECTED", sizeof(this->status_) - 1);
  this->publish_status_();
}

bool BtAudioBridge::is_connected() {
  return this->connected_;
}

const char *BtAudioBridge::get_status() {
  return this->status_;
}

void BtAudioBridge::start_a2dp_() {
  this->a2dp_source_.set_local_name("ESP32 BT Audio Bridge");
  this->a2dp_source_.set_ssp_enabled(true);
  this->a2dp_source_.set_auto_reconnect(true, 10);
  this->a2dp_source_.set_ssid_callback(bt_ssid_callback);
  this->a2dp_source_.set_discovery_mode_callback(bt_discovery_callback);
  this->a2dp_source_.start();
  this->a2dp_started_ = true;
}

void BtAudioBridge::publish_status_() {
  if (this->status_sensor_ != nullptr) {
    this->status_sensor_->publish_state(this->status_);
  }
}

}  // namespace bt_audio_bridge
}  // namespace esphome
