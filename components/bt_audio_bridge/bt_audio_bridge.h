#pragma once

#include "esphome/components/sensor/sensor.h"
#include "esphome/components/speaker/speaker.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/core/component.h"
#include "esphome/core/hal.h"

#include "bt_audio_engine.h"

#include <BluetoothA2DPSource.h>

namespace esphome {
namespace bt_audio_bridge {

class BtAudioBridge;
extern BtAudioBridge *global_bt_audio_bridge;

class BtAudioBridgeA2DPSource : public BluetoothA2DPSource {
 public:
  explicit BtAudioBridgeA2DPSource(BtAudioBridge *owner) : owner_(owner) {
    this->set_event_stack_size(2048);
    this->set_event_queue_size(10);
  }

 protected:
  void app_gap_callback(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param) override;
  void bt_av_notify_evt_handler(uint8_t event, esp_avrc_rn_param_t *param) override;

 private:
  BtAudioBridge *owner_;
};

class BtAudioBridge : public Component, public speaker::Speaker {
 public:
  static constexpr size_t MAX_DEVICES = 8;

  BtAudioBridge();
  void setup() override;
  void loop() override;
  void dump_config() override;

  float get_setup_priority() const override {
    return setup_priority::BLUETOOTH;
  }

  size_t play(const uint8_t *data, size_t length) override;
  void start() override;
  void stop() override;
  void finish() override;
  bool has_buffered_data() const override;

  void set_status_sensor(text_sensor::TextSensor *sensor) { this->status_sensor_ = sensor; }
  void set_event_sensor(text_sensor::TextSensor *sensor) { this->event_sensor_ = sensor; }
  void set_reset_reason_sensor(text_sensor::TextSensor *sensor) { this->reset_reason_sensor_ = sensor; }
  void set_device_sensor(text_sensor::TextSensor *sensor) { this->device_sensor_ = sensor; }
  void set_rssi_sensor(sensor::Sensor *sensor) { this->rssi_sensor_ = sensor; }
  void set_battery_sensor(text_sensor::TextSensor *sensor) { this->battery_sensor_ = sensor; }
  void add_device_slot(text_sensor::TextSensor *sensor) {
    if (this->device_slot_count_ >= MAX_DEVICES) return;
    this->device_sensors_[this->device_slot_count_] = sensor;
    this->device_slot_count_++;
  }

  void start_scan();
  void stop_scan();
  void connect_to(const char *mac);
  void connect_slot(size_t index);
  void disconnect();
  void forget_speaker();

  // Kept as no-op compatibility shims for old YAML buttons.
  void start_test_tone() {}
  void stop_test_tone() {}
  void start_engine_test() {}

  void on_discovery_stopped();
  void on_device_found(const char *name, const char *mac, int rssi);
  void on_real_rssi(int rssi);
  void on_battery_status(esp_avrc_batt_stat_t status);

  bool is_connected();
  const char *get_status();

 protected:
  struct DeviceInfo {
    bool used{false};
    char name[64]{};
    char mac[18]{};
    int rssi{-127};
  };

  static int32_t engine_audio_callback_(uint8_t *data, int32_t len);
  void publish_status_();
  void publish_event_(const char *event);
  void publish_device_(size_t index);
  void clear_devices_();
  void start_a2dp_();
  void load_saved_speaker_();
  void save_speaker_();
  void sync_current_speaker_();
  const char *reset_reason_();

  BtAudioBridgeA2DPSource a2dp_source_;
  BtAudioEngine audio_engine_;
  text_sensor::TextSensor *status_sensor_{nullptr};
  text_sensor::TextSensor *event_sensor_{nullptr};
  text_sensor::TextSensor *reset_reason_sensor_{nullptr};
  text_sensor::TextSensor *device_sensor_{nullptr};
  sensor::Sensor *rssi_sensor_{nullptr};
  text_sensor::TextSensor *battery_sensor_{nullptr};
  text_sensor::TextSensor *device_sensors_[MAX_DEVICES]{};
  DeviceInfo devices_[MAX_DEVICES]{};
  size_t device_slot_count_{0};
  size_t device_count_{0};
  bool devices_dirty_{false};

  bool connected_{false};
  bool scanning_{false};
  bool a2dp_started_{false};
  bool scan_requested_{false};
  bool auto_connect_pending_{false};
  bool engine_test_active_{false};
  bool speaker_started_{false};
  bool finish_requested_{false};
  uint32_t auto_connect_started_{0};

  char selected_mac_[18]{};
  char selected_name_[64]{};
  char status_[32]{"STARTING"};
  char battery_status_[24]{"UNKNOWN"};
  char last_published_status_[32]{};
  char last_published_device_[120]{};

  int scan_cycles_{0};
  unsigned long last_status_check_{0};
  unsigned long last_rssi_request_{0};
};

}  // namespace bt_audio_bridge
}  // namespace esphome
