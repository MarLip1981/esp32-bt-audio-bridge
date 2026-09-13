#include "bt_audio_bridge.h"

#include <cstdio>
#include <cstring>

namespace esphome {
namespace bt_audio_bridge {

void BtAudioBridge::connect_slot(size_t index) {
  if (index >= this->device_count_ || !this->devices_[index].used) return;
  std::strncpy(this->selected_name_, this->devices_[index].name, sizeof(this->selected_name_) - 1);
  this->selected_name_[sizeof(this->selected_name_) - 1] = '\0';
  this->connect_to(this->devices_[index].mac);
}

void BtAudioBridge::connect_to(const char *mac) {
  if (mac == nullptr || std::strlen(mac) == 0) return;
  unsigned int b[6];
  if (std::sscanf(mac, "%02x:%02x:%02x:%02x:%02x:%02x", &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) != 6) return;
  esp_bd_addr_t address = {
      static_cast<uint8_t>(b[0]), static_cast<uint8_t>(b[1]), static_cast<uint8_t>(b[2]),
      static_cast<uint8_t>(b[3]), static_cast<uint8_t>(b[4]), static_cast<uint8_t>(b[5])};
  this->auto_connect_pending_ = false;
  std::strncpy(this->selected_mac_, mac, sizeof(this->selected_mac_) - 1);
  this->selected_mac_[sizeof(this->selected_mac_) - 1] = '\0';
  std::strncpy(this->status_, "CONNECTING", sizeof(this->status_) - 1);
  this->publish_status_();
  this->save_speaker_();
  if (!this->a2dp_started_) {
    this->scan_requested_ = false;
    this->start_a2dp_();
  } else if (this->a2dp_source_.is_discovery_active()) {
    this->a2dp_source_.cancel_discovery();
  }
  this->a2dp_source_.connect_to(address);
}

void BtAudioBridge::disconnect() {
  this->auto_connect_pending_ = false;
  this->scan_requested_ = false;
  this->connected_ = false;
  this->engine_test_active_ = false;
  this->speaker_started_ = false;
  this->finish_requested_ = false;
  this->audio_engine_.clear();
  this->audio_engine_.end();

  if (this->a2dp_started_) {
    this->a2dp_source_.disconnect();
  }

  std::strncpy(this->status_, "DISCONNECTED", sizeof(this->status_) - 1);
  this->status_[sizeof(this->status_) - 1] = '\0';
  this->publish_status_();
  this->publish_event_("BT: disconnected");
}

void BtAudioBridge::forget_speaker() {
  this->auto_connect_pending_ = false;
  this->scan_requested_ = false;
  if (this->a2dp_started_) {
    this->a2dp_source_.end();
    this->a2dp_started_ = false;
  }
  this->a2dp_source_.clean_last_connection();
  this->selected_name_[0] = '\0';
  this->selected_mac_[0] = '\0';
  this->last_published_device_[0] = '\0';
  this->connected_ = false;
  this->scanning_ = false;
  this->engine_test_active_ = false;
  this->speaker_started_ = false;
  this->finish_requested_ = false;
  this->audio_engine_.clear();
  this->audio_engine_.end();
  if (this->device_sensor_ != nullptr) this->device_sensor_->publish_state("NONE");
  if (this->rssi_sensor_ != nullptr) this->rssi_sensor_->publish_state(NAN);
  if (this->battery_sensor_ != nullptr) this->battery_sensor_->publish_state("UNKNOWN");
  std::strncpy(this->status_, "DISCONNECTED", sizeof(this->status_) - 1);
  this->status_[sizeof(this->status_) - 1] = '\0';
  this->publish_status_();
  this->publish_event_("BT: saved speaker forgotten");
}

}  // namespace bt_audio_bridge
}  // namespace esphome
