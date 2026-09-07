#pragma once

#include "esphome/components/button/button.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/core/component.h"
#include "esphome/core/hal.h"

#include <BluetoothA2DPSource.h>

namespace esphome {
namespace bt_audio_bridge {

class BtAudioBridge : public Component {
 public:
  static constexpr size_t MAX_DEVICES = 8;

  void setup() override;
  void loop() override;
  void dump_config() override;

  float get_setup_priority() const override {
    return setup_priority::AFTER_WIFI;
  }

  void set_status_sensor(text_sensor::TextSensor *sensor) { this->status_sensor_ = sensor; }
  void set_event_sensor(text_sensor::TextSensor *sensor) { this->event_sensor_ = sensor; }
  void set_reset_reason_sensor(text_sensor::TextSensor *sensor) { this->reset_reason_sensor_ = sensor; }
  void set_device_sensor(text_sensor::TextSensor *sensor) { this->device_sensor_ = sensor; }
  void add_device_slot(text_sensor::TextSensor *sensor, button::Button *button) {
    if (this->device_slot_count_ >= MAX_DEVICES) return;
    this->device_sensors_[this->device_slot_count_] = sensor;
    this->connect_buttons_[this->device_slot_count_] = button;
    this->device_slot_count_++;
  }

  void start_scan();
  void stop_scan();
  void connect_to(const char *mac);
  void connect_slot(size_t index);
  void disconnect();
  void on_discovery_stopped();
  void on_device_found(const char *name, const char *mac, int rssi);

  bool is_connected();
  const char *get_status();

 protected:
  struct DeviceInfo {
    bool used{false};
    char name[64]{};
    char mac[18]{};
    int rssi{-127};
  };

  void publish_status_();
  void publish_event_(const char *event);
  void publish_device_(size_t index);
  void clear_devices_();
  void start_a2dp_();
  const char *reset_reason_();

  BluetoothA2DPSource a2dp_source_;
  text_sensor::TextSensor *status_sensor_{nullptr};
  text_sensor::TextSensor *event_sensor_{nullptr};
  text_sensor::TextSensor *reset_reason_sensor_{nullptr};
  text_sensor::TextSensor *device_sensor_{nullptr};
  text_sensor::TextSensor *device_sensors_[MAX_DEVICES]{};
  button::Button *connect_buttons_[MAX_DEVICES]{};
  DeviceInfo devices_[MAX_DEVICES]{};
  size_t device_slot_count_{0};
  size_t device_count_{0};

  bool connected_{false};
  bool scanning_{false};
  bool a2dp_started_{false};
  bool scan_requested_{false};

  char selected_mac_[18]{};
  char selected_name_[64]{};
  char status_[32]{"STARTING"};

  int scan_cycles_{0};
  unsigned long last_status_check_{0};
};

}  // namespace bt_audio_bridge
}  // namespace esphome
