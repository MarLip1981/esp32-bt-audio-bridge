#pragma once

#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/core/component.h"
#include "esphome/core/hal.h"

#include <BluetoothA2DPSource.h>

namespace esphome {
namespace bt_audio_bridge {

class BtAudioBridge : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;

  float get_setup_priority() const override {
    return setup_priority::AFTER_WIFI;
  }

  void set_status_sensor(text_sensor::TextSensor *status_sensor) {
    this->status_sensor_ = status_sensor;
  }

  void start_scan();
  void connect_to(const char *mac);
  void disconnect();

  bool is_connected();
  const char *get_status();

 protected:
  void publish_status_();
  void start_a2dp_();

  BluetoothA2DPSource a2dp_source_;
  text_sensor::TextSensor *status_sensor_{nullptr};

  bool connected_{false};
  bool scanning_{false};
  bool a2dp_started_{false};
  bool scan_requested_{false};

  char selected_mac_[18]{};
  char selected_name_[64]{};
  char status_[32]{"STARTING"};

  unsigned long last_status_check_{0};
};

}  // namespace bt_audio_bridge
}  // namespace esphome
