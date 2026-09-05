#include "bt_audio_bridge.h"

#include "esphome/core/log.h"

#include <cstring>

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

  this->a2dp_source_.set_local_name("ESP32 BT Audio Bridge");

  // Secure Simple Pairing
  this->a2dp_source_.set_ssp_enabled(true);

  // Automatic reconnect
  this->a2dp_source_.set_auto_reconnect(true, 10);

  // Bluetooth device discovery callbacks
  this->a2dp_source_.set_ssid_callback(bt_ssid_callback);
  this->a2dp_source_.set_discovery_mode_callback(bt_discovery_callback);

  // Start Bluetooth A2DP Source.
  // No device name is specified yet, so the bridge can perform discovery.
  this->a2dp_source_.start();

  this->connected_ = false;
  this->scanning_ = false;
  std::strncpy(this->status_, "SCANNING", sizeof(this->status_) - 1);

  ESP_LOGI(TAG, "Bluetooth A2DP Source started");
}

void BtAudioBridge::loop() {
  const unsigned long now = millis();

  if (now - this->last_status_check_ < 1000) {
    return;
  }

  this->last_status_check_ = now;

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
      std::strncpy(
          this->status_, "SCANNING", sizeof(this->status_) - 1);
    } else {
      this->scanning_ = false;
      std::strncpy(
          this->status_, "DISCONNECTED", sizeof(this->status_) - 1);
    }
  }
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

  if (this->a2dp_source_.is_discovery_active()) {
    ESP_LOGI(TAG, "Bluetooth discovery already active");
    return;
  }

  this->scanning_ = true;
  std::strncpy(this->status_, "SCANNING", sizeof(this->status_) - 1);

  this->a2dp_source_.start();
}

void BtAudioBridge::connect_to(const char *mac) {
  if (mac == nullptr || strlen(mac) == 0) {
    ESP_LOGW(TAG, "No Bluetooth MAC address specified");
    return;
  }

  ESP_LOGI(TAG, "Requested connection to Bluetooth device: %s", mac);

  std::strncpy(
      this->selected_mac_, mac, sizeof(this->selected_mac_) - 1);

  // Connection by MAC will be implemented in the next stage.
  std::strncpy(
      this->status_, "CONNECTING", sizeof(this->status_) - 1);
}

void BtAudioBridge::disconnect() {
  ESP_LOGI(TAG, "Disconnect requested");

  this->a2dp_source_.end();

  this->connected_ = false;
  this->scanning_ = false;

  std::strncpy(
      this->status_, "DISCONNECTED", sizeof(this->status_) - 1);
}

bool BtAudioBridge::is_connected() {
  return this->connected_;
}

const char *BtAudioBridge::get_status() {
  return this->status_;
}

}  // namespace bt_audio_bridge
}  // namespace esphome
