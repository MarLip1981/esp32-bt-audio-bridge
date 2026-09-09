#include "bt_audio_bridge_diagnostics.h"

#include "bt_audio_bridge.h"
#include "esphome/core/log.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include <esp_bt.h>
#include <esp_heap_caps.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#ifdef USE_ARDUINO
#include <Arduino.h>
#endif

namespace esphome {
namespace bt_audio_bridge {

static const char *const TAG = "bt_audio_diag";
static constexpr uint32_t UPDATE_INTERVAL_MS = 5000;

static const char *bt_controller_status_text(esp_bt_controller_status_t status) {
  switch (status) {
    case ESP_BT_CONTROLLER_STATUS_IDLE: return "IDLE";
    case ESP_BT_CONTROLLER_STATUS_INITED: return "INITED";
    case ESP_BT_CONTROLLER_STATUS_ENABLED: return "ENABLED";
    default: return "DISABLED/UNKNOWN";
  }
}

void BtAudioBridgeDiagnostics::setup() {
  this->last_update_ms_ = 0;
  this->bt_was_connected_ = false;
  this->bt_connections_ = 0;
  this->bt_reconnects_ = 0;
  this->last_wifi_connected_ms_ = 0;
  this->last_bt_connected_ms_ = 0;
  this->update_();
}

void BtAudioBridgeDiagnostics::loop() {
  const uint32_t now = millis();
  if (now - this->last_update_ms_ < UPDATE_INTERVAL_MS) return;
  this->last_update_ms_ = now;
  this->update_();
}

void BtAudioBridgeDiagnostics::update_() {
  this->update_wifi_();
  this->update_system_();
  this->update_bluetooth_();
}

void BtAudioBridgeDiagnostics::update_wifi_() {
  wifi_ap_record_t ap_info{};
  const esp_err_t err = esp_wifi_sta_get_ap_info(&ap_info);
  if (err != ESP_OK) {
    if (this->wifi_status_sensor_ != nullptr) this->wifi_status_sensor_->publish_state("DISCONNECTED");
    if (this->wifi_ssid_sensor_ != nullptr) this->wifi_ssid_sensor_->publish_state("BRAK");
    if (this->wifi_phy_sensor_ != nullptr) this->wifi_phy_sensor_->publish_state("BRAK");
    if (this->wifi_rssi_sensor_ != nullptr) this->wifi_rssi_sensor_->publish_state(NAN);
    if (this->wifi_signal_sensor_ != nullptr) this->wifi_signal_sensor_->publish_state(NAN);
    if (this->wifi_channel_sensor_ != nullptr) this->wifi_channel_sensor_->publish_state(NAN);
    if (this->wifi_tx_power_sensor_ != nullptr) this->wifi_tx_power_sensor_->publish_state(NAN);
    if (this->wifi_uptime_sensor_ != nullptr) this->wifi_uptime_sensor_->publish_state(0);
    this->last_wifi_connected_ms_ = 0;
    return;
  }

  if (this->wifi_status_sensor_ != nullptr) this->wifi_status_sensor_->publish_state("CONNECTED");
  if (this->wifi_ssid_sensor_ != nullptr) {
    char ssid[33];
    std::memcpy(ssid, ap_info.ssid, sizeof(ap_info.ssid));
    ssid[sizeof(ssid) - 1] = '\0';
    this->wifi_ssid_sensor_->publish_state(ssid);
  }

  const int rssi = static_cast<int>(ap_info.rssi);
  const float signal = std::max(0.0f, std::min(100.0f, 2.0f * (static_cast<float>(rssi) + 100.0f)));
  if (this->wifi_rssi_sensor_ != nullptr) this->wifi_rssi_sensor_->publish_state(static_cast<float>(rssi));
  if (this->wifi_signal_sensor_ != nullptr) this->wifi_signal_sensor_->publish_state(signal);
  if (this->wifi_channel_sensor_ != nullptr) this->wifi_channel_sensor_->publish_state(static_cast<float>(ap_info.primary));

  int8_t tx_power = 0;
  if (this->wifi_tx_power_sensor_ != nullptr) {
    if (esp_wifi_get_max_tx_power(&tx_power) == ESP_OK) {
      this->wifi_tx_power_sensor_->publish_state(static_cast<float>(tx_power) / 4.0f);
    } else {
      this->wifi_tx_power_sensor_->publish_state(NAN);
    }
  }

  if (this->wifi_phy_sensor_ != nullptr) {
    if (ap_info.phy_11n) this->wifi_phy_sensor_->publish_state("802.11n");
    else if (ap_info.phy_11g) this->wifi_phy_sensor_->publish_state("802.11g");
    else if (ap_info.phy_11b) this->wifi_phy_sensor_->publish_state("802.11b");
    else if (ap_info.phy_lr) this->wifi_phy_sensor_->publish_state("802.11 LR");
    else this->wifi_phy_sensor_->publish_state("UNKNOWN");
  }

  if (this->last_wifi_connected_ms_ == 0) this->last_wifi_connected_ms_ = millis();
  if (this->wifi_uptime_sensor_ != nullptr) {
    this->wifi_uptime_sensor_->publish_state(static_cast<float>((millis() - this->last_wifi_connected_ms_) / 1000UL));
  }
}

void BtAudioBridgeDiagnostics::update_system_() {
  constexpr uint32_t caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
  const size_t total_heap = heap_caps_get_total_size(caps);
  const size_t free_heap = heap_caps_get_free_size(caps);
  const size_t minimum_free = heap_caps_get_minimum_free_size(caps);
  const size_t largest_block = heap_caps_get_largest_free_block(caps);
  const size_t used_heap = total_heap > free_heap ? total_heap - free_heap : 0;
  const float used_percent = total_heap > 0 ? (100.0f * static_cast<float>(used_heap) / static_cast<float>(total_heap)) : NAN;

  if (this->heap_free_sensor_ != nullptr) this->heap_free_sensor_->publish_state(static_cast<float>(free_heap));
  if (this->heap_used_sensor_ != nullptr) this->heap_used_sensor_->publish_state(static_cast<float>(used_heap));
  if (this->heap_percent_sensor_ != nullptr) this->heap_percent_sensor_->publish_state(used_percent);
  if (this->heap_min_free_sensor_ != nullptr) this->heap_min_free_sensor_->publish_state(static_cast<float>(minimum_free));
  if (this->heap_largest_block_sensor_ != nullptr) this->heap_largest_block_sensor_->publish_state(static_cast<float>(largest_block));

  if (this->cpu_frequency_sensor_ != nullptr) {
#ifdef USE_ARDUINO
    this->cpu_frequency_sensor_->publish_state(static_cast<float>(ESP.getCpuFreqMHz()));
#else
    this->cpu_frequency_sensor_->publish_state(NAN);
#endif
  }

  if (this->cpu_load_sensor_ != nullptr) {
#if defined(CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS) && defined(CONFIG_FREERTOS_USE_STATS_FORMATTING_FUNCTIONS) && defined(INCLUDE_xTaskGetIdleTaskHandle)
    const uint32_t idle_percent = static_cast<uint32_t>(ulTaskGetIdleRunTimePercent());
    const float busy_percent = idle_percent > 100 ? 0.0f : 100.0f - static_cast<float>(idle_percent);
    this->cpu_load_sensor_->publish_state(busy_percent);
#else
    this->cpu_load_sensor_->publish_state(NAN);
#endif
  }
}

void BtAudioBridgeDiagnostics::update_bluetooth_() {
  const esp_bt_controller_status_t controller_status = esp_bt_controller_get_status();
  if (this->bt_controller_sensor_ != nullptr) {
    this->bt_controller_sensor_->publish_state(bt_controller_status_text(controller_status));
  }

  bool connected = false;
  const char *bridge_status = "UNKNOWN";
  if (global_bt_audio_bridge != nullptr) {
    connected = global_bt_audio_bridge->is_connected();
    bridge_status = global_bt_audio_bridge->get_status();
  }

  if (this->bt_status_sensor_ != nullptr) {
    this->bt_status_sensor_->publish_state(bridge_status);
  }

  const uint32_t now = millis();
  if (connected && !this->bt_was_connected_) {
    this->bt_connections_++;
    if (this->bt_connections_ > 1) this->bt_reconnects_++;
    this->last_bt_connected_ms_ = now;
  } else if (!connected) {
    this->last_bt_connected_ms_ = 0;
  }
  this->bt_was_connected_ = connected;

  if (this->bt_connections_sensor_ != nullptr) this->bt_connections_sensor_->publish_state(static_cast<float>(this->bt_connections_));
  if (this->bt_reconnects_sensor_ != nullptr) this->bt_reconnects_sensor_->publish_state(static_cast<float>(this->bt_reconnects_));
  if (this->bt_uptime_sensor_ != nullptr) {
    const float uptime = connected && this->last_bt_connected_ms_ != 0 ? static_cast<float>((now - this->last_bt_connected_ms_) / 1000UL) : 0.0f;
    this->bt_uptime_sensor_->publish_state(uptime);
  }
}

void BtAudioBridgeDiagnostics::dump_config() {
  ESP_LOGCONFIG(TAG, "ESP32 diagnostics:");
  ESP_LOGCONFIG(TAG, "  Update interval: %u ms", static_cast<unsigned>(UPDATE_INTERVAL_MS));
  ESP_LOGCONFIG(TAG, "  WiFi: RSSI, signal, channel, PHY, TX power, SSID, uptime");
  ESP_LOGCONFIG(TAG, "  System: CPU load, CPU frequency, internal heap, minimum heap, largest block");
  ESP_LOGCONFIG(TAG, "  Bluetooth: controller state, bridge state, connections, reconnects, uptime");
}

}  // namespace bt_audio_bridge
}  // namespace esphome
