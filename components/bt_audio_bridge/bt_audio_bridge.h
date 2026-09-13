#pragma once

#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/speaker/speaker.h"
#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#include "bt_audio_engine.h"

#include <BluetoothA2DPSource.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace esphome {
namespace bt_audio_bridge {

class BtAudioBridge;
extern BtAudioBridge *global_bt_audio_bridge;

class BtAudioBridgeA2DPSource : public BluetoothA2DPSource {
 public:
  explicit BtAudioBridgeA2DPSource(BtAudioBridge *owner) : owner_(owner) {}

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
    return setup_priority::AFTER_WIFI;
  }

  size_t play(const uint8_t *data, size_t length) override {
    if (!this->a2dp_started_ || !this->a2dp_source_.is_active()) return 0;
    if (this->state_ == speaker::STATE_STOPPED) this->start();
    if (this->state_ != speaker::STATE_RUNNING) return 0;
    this->engine_test_active_ = true;
    this->media_finish_pending_ = false;
    return this->audio_engine_.write(data, length);
  }

  void start() override {
    if (!this->a2dp_started_ || !this->a2dp_source_.is_active()) {
      this->state_ = speaker::STATE_STOPPED;
      return;
    }
    if (!this->audio_engine_.begin()) {
      ESP_LOGE("bt_audio_bridge", "Audio engine allocation failed");
      this->state_ = speaker::STATE_STOPPED;
      return;
    }
    this->audio_engine_.clear();
    this->engine_test_active_ = true;
    this->media_finish_pending_ = false;
    this->state_ = speaker::STATE_RUNNING;
  }

  void stop() override {
    this->media_finish_pending_ = false;
    this->engine_test_active_ = false;
    this->audio_engine_.clear();
    this->audio_engine_.end();
    this->state_ = speaker::STATE_STOPPED;
  }

  void finish() override {
    this->media_finish_pending_ = true;
    this->state_ = speaker::STATE_STOPPED;
  }

  bool has_buffered_data() const override {
    return this->audio_engine_.available() > 0;
  }

  void set_volume(float volume) override { this->volume_ = volume; }
  float get_volume() override { return this->volume_; }
  void set_mute_state(bool mute_state) override { this->mute_state_ = mute_state; }
  bool get_mute_state() override { return this->mute_state_; }

  void set_status_sensor(text_sensor::TextSensor *sensor) { this->status_sensor_ = sensor; }
  void set_event_sensor(text_sensor::TextSensor *sensor) { this->event_sensor_ = sensor; }
  void set_reset_reason_sensor(text_sensor::TextSensor *sensor) { this->reset_reason_sensor_ = sensor; }
  void set_device_sensor(text_sensor::TextSensor *sensor) { this->device_sensor_ = sensor; }
  void set_rssi_sensor(sensor::Sensor *sensor) { this->rssi_sensor_ = sensor; }
  void set_battery_sensor(text_sensor::TextSensor *sensor) { this->battery_sensor_ = sensor; }
  void set_audio_url(const char *url);
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
  void start_test_tone();
  void stop_test_tone();
  void start_engine_test();
  void play_http_wav();
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

  static int32_t test_tone_callback_(uint8_t *data, int32_t len);
  static int32_t engine_audio_callback_(uint8_t *data, int32_t len);
  static void engine_test_task_(void *arg);
  static void http_wav_task_(void *arg);
  void http_wav_playback_();
  int32_t generate_test_tone_(uint8_t *data, int32_t len);
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
  bool test_tone_active_{false};
  volatile bool engine_test_active_{false};
  volatile bool http_wav_busy_{false};
  bool media_finish_pending_{false};
  TaskHandle_t http_wav_task_handle_{nullptr};
  uint32_t auto_connect_started_{0};
  uint32_t test_tone_until_{0};
  uint32_t test_tone_phase_{0};

  char selected_mac_[18]{};
  char selected_name_[64]{};
  char status_[32]{"STARTING"};
  char battery_status_[24]{"UNKNOWN"};
  char audio_url_[256]{};
  char http_playback_url_[256]{};
  uint8_t http_pcm_buffer_[1024]{};

  int scan_cycles_{0};
  unsigned long last_status_check_{0};
  unsigned long last_rssi_request_{0};
};

}  // namespace bt_audio_bridge
}  // namespace esphome
