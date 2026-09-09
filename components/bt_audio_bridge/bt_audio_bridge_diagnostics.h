#pragma once

#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/core/component.h"

namespace esphome {
namespace bt_audio_bridge {

class BtAudioBridgeDiagnostics : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;

  float get_setup_priority() const override {
    return setup_priority::AFTER_WIFI;
  }

  void set_wifi_rssi(sensor::Sensor *sensor) { this->wifi_rssi_sensor_ = sensor; }
  void set_wifi_signal(sensor::Sensor *sensor) { this->wifi_signal_sensor_ = sensor; }
  void set_wifi_channel(sensor::Sensor *sensor) { this->wifi_channel_sensor_ = sensor; }
  void set_wifi_tx_power(sensor::Sensor *sensor) { this->wifi_tx_power_sensor_ = sensor; }
  void set_wifi_ssid(text_sensor::TextSensor *sensor) { this->wifi_ssid_sensor_ = sensor; }
  void set_wifi_phy(text_sensor::TextSensor *sensor) { this->wifi_phy_sensor_ = sensor; }
  void set_wifi_status(text_sensor::TextSensor *sensor) { this->wifi_status_sensor_ = sensor; }
  void set_wifi_uptime(sensor::Sensor *sensor) { this->wifi_uptime_sensor_ = sensor; }

  void set_cpu_load(sensor::Sensor *sensor) { this->cpu_load_sensor_ = sensor; }
  void set_cpu_frequency(sensor::Sensor *sensor) { this->cpu_frequency_sensor_ = sensor; }
  void set_heap_free(sensor::Sensor *sensor) { this->heap_free_sensor_ = sensor; }
  void set_heap_used(sensor::Sensor *sensor) { this->heap_used_sensor_ = sensor; }
  void set_heap_percent(sensor::Sensor *sensor) { this->heap_percent_sensor_ = sensor; }
  void set_heap_min_free(sensor::Sensor *sensor) { this->heap_min_free_sensor_ = sensor; }
  void set_heap_largest_block(sensor::Sensor *sensor) { this->heap_largest_block_sensor_ = sensor; }

  void set_bt_status(text_sensor::TextSensor *sensor) { this->bt_status_sensor_ = sensor; }
  void set_bt_controller(text_sensor::TextSensor *sensor) { this->bt_controller_sensor_ = sensor; }
  void set_bt_connections(sensor::Sensor *sensor) { this->bt_connections_sensor_ = sensor; }
  void set_bt_reconnects(sensor::Sensor *sensor) { this->bt_reconnects_sensor_ = sensor; }
  void set_bt_uptime(sensor::Sensor *sensor) { this->bt_uptime_sensor_ = sensor; }

 protected:
  void update_();
  void update_wifi_();
  void update_system_();
  void update_bluetooth_();

  sensor::Sensor *wifi_rssi_sensor_{nullptr};
  sensor::Sensor *wifi_signal_sensor_{nullptr};
  sensor::Sensor *wifi_channel_sensor_{nullptr};
  sensor::Sensor *wifi_tx_power_sensor_{nullptr};
  text_sensor::TextSensor *wifi_ssid_sensor_{nullptr};
  text_sensor::TextSensor *wifi_phy_sensor_{nullptr};
  text_sensor::TextSensor *wifi_status_sensor_{nullptr};
  sensor::Sensor *wifi_uptime_sensor_{nullptr};

  sensor::Sensor *cpu_load_sensor_{nullptr};
  sensor::Sensor *cpu_frequency_sensor_{nullptr};
  sensor::Sensor *heap_free_sensor_{nullptr};
  sensor::Sensor *heap_used_sensor_{nullptr};
  sensor::Sensor *heap_percent_sensor_{nullptr};
  sensor::Sensor *heap_min_free_sensor_{nullptr};
  sensor::Sensor *heap_largest_block_sensor_{nullptr};

  text_sensor::TextSensor *bt_status_sensor_{nullptr};
  text_sensor::TextSensor *bt_controller_sensor_{nullptr};
  sensor::Sensor *bt_connections_sensor_{nullptr};
  sensor::Sensor *bt_reconnects_sensor_{nullptr};
  sensor::Sensor *bt_uptime_sensor_{nullptr};

  bool bt_was_connected_{false};
  uint32_t bt_connections_{0};
  uint32_t bt_reconnects_{0};
  uint32_t last_wifi_connected_ms_{0};
  uint32_t last_bt_connected_ms_{0};
  uint32_t last_update_ms_{0};
};

}  // namespace bt_audio_bridge
}  // namespace esphome
