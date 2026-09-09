#include "bt_audio_bridge.h"

#include "esphome/core/log.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include <esp_system.h>
#include <nvs.h>
#include <nvs_flash.h>

#ifdef USE_ARDUINO
#include "esp32-hal-alloc-bt-classic-mem.h"
#include "esp32-hal-bt.h"
#include <esp_bt.h>
#include <esp_err.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
extern "C" bool btInUse() { return true; }

extern "C" bool __wrap_btStartMode(bt_mode mode) {
  esp_bt_mode_t esp_mode;
  switch (mode) {
    case BT_MODE_BLE: esp_mode = ESP_BT_MODE_BLE; break;
    case BT_MODE_CLASSIC_BT: esp_mode = ESP_BT_MODE_CLASSIC_BT; break;
    case BT_MODE_BTDM: esp_mode = ESP_BT_MODE_BTDM; break;
    case BT_MODE_DEFAULT:
    default: esp_mode = ESP_BT_MODE_CLASSIC_BT; break;
  }
  ESP_LOGI("bt_audio_bridge", "Direct BT start: mode=%d", static_cast<int>(esp_mode));
  if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_ENABLED) return true;
  esp_bt_controller_config_t cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
  cfg.mode = esp_mode;
  if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_IDLE) {
    esp_err_t err = esp_bt_controller_init(&cfg);
    if (err != ESP_OK) {
      ESP_LOGE("bt_audio_bridge", "Direct BT controller init failed: %s", esp_err_to_name(err));
      return false;
    }
    while (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_IDLE) vTaskDelay(1);
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
static const char *const NVS_NAMESPACE = "bt_aud_bridge";
static const char *const NVS_NAME_KEY = "speaker_name";
static const char *const NVS_MAC_KEY = "speaker_mac";
BtAudioBridge *global_bt_audio_bridge = nullptr;

void BtAudioBridgeA2DPSource::app_gap_callback(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param) {
  BluetoothA2DPSource::app_gap_callback(event, param);
  if (this->owner_ == nullptr || param == nullptr) return;

  switch (event) {
    case ESP_BT_GAP_READ_ACL_REAL_RSSI_EVT:
      this->owner_->on_real_rssi(static_cast<int>(param->read_acl_real_rssi.rssi));
      break;
    default:
      break;
  }
}

void BtAudioBridgeA2DPSource::bt_av_notify_evt_handler(uint8_t event, esp_avrc_rn_param_t *param) {
  BluetoothA2DPSource::bt_av_notify_evt_handler(event, param);
  if (this->owner_ == nullptr || param == nullptr) return;
  if (event == ESP_AVRC_RN_BATTERY_STATUS_CHANGE) {
    this->owner_->on_battery_status(param->batt);
  }
}

static bool bt_ssid_callback(const char *ssid, esp_bd_addr_t address, int rrsi) {
  if (global_bt_audio_bridge == nullptr) return true;
  char mac[18];
  std::snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X", address[0], address[1], address[2], address[3], address[4], address[5]);
  ESP_LOGI(TAG, "BT device found: %s  MAC: %s  RSSI: %d", ssid, mac, rrsi);
  global_bt_audio_bridge->on_device_found(ssid, mac, rrsi);
  return false;
}

static void bt_discovery_callback(esp_bt_gap_discovery_state_t discovery_mode) {
  if (global_bt_audio_bridge == nullptr) return;
  if (discovery_mode == ESP_BT_GAP_DISCOVERY_STARTED) {
    ESP_LOGI(TAG, "Bluetooth scan started");
  } else {
    ESP_LOGI(TAG, "Bluetooth scan finished");
    global_bt_audio_bridge->on_discovery_stopped();
  }
}

BtAudioBridge::BtAudioBridge() : a2dp_source_(this) {}

void BtAudioBridge::on_device_found(const char *name, const char *mac, int rssi) {
  if (name == nullptr || mac == nullptr) return;
  for (size_t i = 0; i < this->device_count_; i++) {
    if (std::strncmp(this->devices_[i].mac, mac, sizeof(this->devices_[i].mac)) == 0) {
      std::strncpy(this->devices_[i].name, name, sizeof(this->devices_[i].name) - 1);
      this->devices_[i].name[sizeof(this->devices_[i].name) - 1] = '\0';
      this->devices_[i].rssi = rssi;
      this->devices_dirty_ = true;
      return;
    }
  }
  if (this->device_count_ >= MAX_DEVICES) {
    ESP_LOGW(TAG, "Bluetooth device list full (%u entries), ignoring %s", static_cast<unsigned>(MAX_DEVICES), name);
    return;
  }
  const size_t index = this->device_count_++;
  this->devices_[index].used = true;
  std::strncpy(this->devices_[index].name, name, sizeof(this->devices_[index].name) - 1);
  this->devices_[index].name[sizeof(this->devices_[index].name) - 1] = '\0';
  std::strncpy(this->devices_[index].mac, mac, sizeof(this->devices_[index].mac) - 1);
  this->devices_[index].mac[sizeof(this->devices_[index].mac) - 1] = '\0';
  this->devices_[index].rssi = rssi;
  this->devices_dirty_ = true;
}

void BtAudioBridge::clear_devices_() {
  for (size_t i = 0; i < MAX_DEVICES; i++) {
    this->devices_[i] = DeviceInfo{};
    if (this->device_sensors_[i] != nullptr) this->device_sensors_[i]->publish_state("BRAK URZĄDZENIA");
  }
  this->device_count_ = 0;
  this->devices_dirty_ = false;
}

void BtAudioBridge::publish_device_(size_t index) {
  if (index >= this->device_slot_count_ || this->device_sensors_[index] == nullptr) return;
  if (index >= this->device_count_ || !this->devices_[index].used) {
    this->device_sensors_[index]->publish_state("BRAK URZĄDZENIA");
    return;
  }
  char state[120];
  std::snprintf(state, sizeof(state), "%s | %s | %d dBm", this->devices_[index].name, this->devices_[index].mac, this->devices_[index].rssi);
  this->device_sensors_[index]->publish_state(state);
}

void BtAudioBridge::on_discovery_stopped() {
  if (!this->scanning_) return;
  this->scan_cycles_++;
  ESP_LOGI(TAG, "Bluetooth scan cycle %d/3 finished", this->scan_cycles_);
  if (this->scan_cycles_ >= 3) {
    ESP_LOGI(TAG, "Bluetooth scan limit reached (3 cycles), stopping scan");
    this->a2dp_source_.cancel_discovery();
    this->scanning_ = false;
    std::strncpy(this->status_, "DISCONNECTED", sizeof(this->status_) - 1);
    this->publish_status_();
    this->publish_event_("SCAN: finished - results available in Home Assistant");
  }
}

void BtAudioBridge::load_saved_speaker_() {
  nvs_handle_t handle;
  esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
  if (err != ESP_OK) {
    ESP_LOGI(TAG, "No saved Bluetooth speaker in NVS");
    return;
  }
  size_t len = sizeof(this->selected_name_);
  if (nvs_get_str(handle, NVS_NAME_KEY, this->selected_name_, &len) != ESP_OK) this->selected_name_[0] = '\0';
  len = sizeof(this->selected_mac_);
  if (nvs_get_str(handle, NVS_MAC_KEY, this->selected_mac_, &len) != ESP_OK) this->selected_mac_[0] = '\0';
  nvs_close(handle);
  if (this->selected_mac_[0] != '\0') {
    ESP_LOGI(TAG, "Saved Bluetooth speaker: %s | %s", this->selected_name_[0] != '\0' ? this->selected_name_ : "UNKNOWN", this->selected_mac_);
  }
}

void BtAudioBridge::save_speaker_() {
  if (this->selected_mac_[0] == '\0') return;
  nvs_handle_t handle;
  esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Cannot open speaker NVS: %s", esp_err_to_name(err));
    return;
  }
  nvs_set_str(handle, NVS_MAC_KEY, this->selected_mac_);
  if (this->selected_name_[0] != '\0') nvs_set_str(handle, NVS_NAME_KEY, this->selected_name_);
  err = nvs_commit(handle);
  nvs_close(handle);
  if (err == ESP_OK) {
    ESP_LOGI(TAG, "Saved Bluetooth speaker: %s | %s", this->selected_name_[0] != '\0' ? this->selected_name_ : "UNKNOWN", this->selected_mac_);
  } else {
    ESP_LOGE(TAG, "Cannot commit speaker NVS: %s", esp_err_to_name(err));
  }
}

void BtAudioBridge::sync_current_speaker_() {
  if (!this->a2dp_started_ || !this->a2dp_source_.is_active()) return;

  auto is_zero_address = [](const esp_bd_addr_t *addr) {
    if (addr == nullptr) return true;
    for (size_t i = 0; i < ESP_BD_ADDR_LEN; i++) {
      if ((*addr)[i] != 0) return false;
    }
    return true;
  };

  esp_bd_addr_t *address = this->a2dp_source_.get_last_peer_address();
  if (is_zero_address(address)) return;

  char mac[18];
  std::snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X", (*address)[0], (*address)[1], (*address)[2], (*address)[3], (*address)[4], (*address)[5]);

  const char *library_name = this->a2dp_source_.get_name();
  ESP_LOGD(TAG, "A2DP name during sync: '%s' | saved name: '%s'", library_name != nullptr ? library_name : "<null>", this->selected_name_[0] != '\0' ? this->selected_name_ : "<empty>");

  bool changed = std::strncmp(this->selected_mac_, mac, sizeof(this->selected_mac_)) != 0;
  if (changed) {
    std::strncpy(this->selected_mac_, mac, sizeof(this->selected_mac_) - 1);
    this->selected_mac_[sizeof(this->selected_mac_) - 1] = '\0';
  }

  const bool library_name_valid = library_name != nullptr && library_name[0] != '\0' &&
                                  std::strcmp(library_name, "UNKNOWN") != 0 &&
                                  std::strcmp(library_name, "unknown") != 0 &&
                                  std::strcmp(library_name, "ESP32_A2DP_SRC") != 0;
  if (library_name_valid && std::strncmp(this->selected_name_, library_name, sizeof(this->selected_name_)) != 0) {
    std::strncpy(this->selected_name_, library_name, sizeof(this->selected_name_) - 1);
    this->selected_name_[sizeof(this->selected_name_) - 1] = '\0';
    changed = true;
  }

  char state[120];
  std::snprintf(state, sizeof(state), "%s | %s", this->selected_name_[0] != '\0' ? this->selected_name_ : "UNKNOWN", this->selected_mac_);
  if (this->device_sensor_ != nullptr) this->device_sensor_->publish_state(state);
  if (changed) this->save_speaker_();
}

void BtAudioBridge::on_real_rssi(int rssi) {
  ESP_LOGI(TAG, "Connected speaker real RSSI: %d dBm", rssi);
  if (this->rssi_sensor_ != nullptr) this->rssi_sensor_->publish_state(static_cast<float>(rssi));
}

void BtAudioBridge::on_battery_status(esp_avrc_batt_stat_t status) {
  const char *text = "UNKNOWN";
  switch (status) {
    case ESP_AVRC_BATT_NORMAL: text = "NORMAL"; break;
    case ESP_AVRC_BATT_WARNING: text = "LOW"; break;
    case ESP_AVRC_BATT_CRITICAL: text = "CRITICAL"; break;
    case ESP_AVRC_BATT_EXTERNAL: text = "EXTERNAL POWER"; break;
    case ESP_AVRC_BATT_FULL_CHARGE: text = "FULL"; break;
    default: break;
  }
  std::strncpy(this->battery_status_, text, sizeof(this->battery_status_) - 1);
  this->battery_status_[sizeof(this->battery_status_) - 1] = '\0';
  ESP_LOGI(TAG, "Speaker battery status: %s", this->battery_status_);
  if (this->battery_sensor_ != nullptr) this->battery_sensor_->publish_state(this->battery_status_);
}

int32_t BtAudioBridge::test_tone_callback_(uint8_t *data, int32_t len) {
  if (global_bt_audio_bridge == nullptr) {
    std::memset(data, 0, static_cast<size_t>(len));
    return len;
  }
  return global_bt_audio_bridge->generate_test_tone_(data, len);
}

int32_t BtAudioBridge::generate_test_tone_(uint8_t *data, int32_t len) {
  if (!this->test_tone_active_ || millis() >= this->test_tone_until_) {
    this->test_tone_active_ = false;
    std::memset(data, 0, static_cast<size_t>(len));
    return len;
  }

  constexpr double sample_rate = 44100.0;
  constexpr double frequency = 880.0;
  constexpr double two_pi = 6.28318530717958647692;
  constexpr int bytes_per_sample = 4;  // 16-bit stereo
  const int samples = len / bytes_per_sample;
  auto *out = reinterpret_cast<int16_t *>(data);

  for (int i = 0; i < samples; i++) {
    const double sample = std::sin(two_pi * frequency * static_cast<double>(this->test_tone_phase_) / sample_rate) * 0.18;
    const int16_t value = static_cast<int16_t>(sample * 32767.0);
    out[i * 2] = value;
    out[i * 2 + 1] = value;
    this->test_tone_phase_++;
  }

  const int used = samples * bytes_per_sample;
  if (used < len) std::memset(data + used, 0, static_cast<size_t>(len - used));
  return len;
}

void BtAudioBridge::start_test_tone() {
  if (!this->a2dp_started_ || !this->a2dp_source_.is_active()) {
    ESP_LOGW(TAG, "Test tone requested while Bluetooth speaker is not connected");
    return;
  }
  this->test_tone_phase_ = 0;
  this->test_tone_until_ = millis() + 700;
  this->test_tone_active_ = true;
  this->a2dp_source_.set_data_callback(&BtAudioBridge::test_tone_callback_);
  this->publish_event_("AUDIO: connection confirmation tone");
}

void BtAudioBridge::stop_test_tone() {
  this->test_tone_active_ = false;
  this->test_tone_until_ = 0;
}

void BtAudioBridge::setup() {
  global_bt_audio_bridge = this;
  ESP_LOGI(TAG, "Bluetooth A2DP Source ready");
  ESP_LOGI(TAG, "Reset reason: %d", static_cast<int>(esp_reset_reason()));
  esp_err_t nvs_err = nvs_flash_init();
  if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES || nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_LOGW(TAG, "NVS init requires erase; erasing NVS partition");
    nvs_flash_erase();
    nvs_err = nvs_flash_init();
  }
  if (nvs_err == ESP_OK) this->load_saved_speaker_();
  else ESP_LOGE(TAG, "NVS init failed during setup: %s", esp_err_to_name(nvs_err));
  this->connected_ = false;
  this->scanning_ = false;
  this->a2dp_started_ = false;
  this->scan_requested_ = false;
  this->scan_cycles_ = 0;
  this->device_count_ = 0;
  this->devices_dirty_ = false;
  this->last_rssi_request_ = 0;
  this->test_tone_active_ = false;
  std::strncpy(this->status_, "READY", sizeof(this->status_) - 1);
  if (this->battery_sensor_ != nullptr) this->battery_sensor_->publish_state("UNKNOWN");
  this->publish_status_();
  this->publish_event_("BOOT: component ready");
  if (this->reset_reason_sensor_ != nullptr) this->reset_reason_sensor_->publish_state(this->reset_reason_());
  if (this->device_sensor_ != nullptr) {
    char state[120];
    std::snprintf(state, sizeof(state), "%s | %s", this->selected_name_[0] != '\0' ? this->selected_name_ : "UNKNOWN", this->selected_mac_[0] != '\0' ? this->selected_mac_ : "NONE");
    this->device_sensor_->publish_state(state);
  }
  for (size_t i = 0; i < this->device_slot_count_; i++) this->publish_device_(i);
}

void BtAudioBridge::loop() {
  if (this->scan_requested_ && !this->a2dp_started_) {
    this->scan_requested_ = false;
    ESP_LOGI(TAG, "Starting Bluetooth A2DP stack from ESPHome loop");
    this->publish_event_("SCAN: starting A2DP stack");
    this->start_a2dp_();
  }
  if (this->devices_dirty_) {
    for (size_t i = 0; i < this->device_slot_count_; i++) this->publish_device_(i);
    this->devices_dirty_ = false;
  }
  const unsigned long now = millis();
  if (now - this->last_status_check_ < 1000) return;
  this->last_status_check_ = now;
  if (!this->a2dp_started_) return;
  const bool active = this->a2dp_source_.is_active();
  if (active) {
    if (!this->connected_) {
      this->publish_event_("BT: speaker connected");
      this->start_test_tone();
      this->last_rssi_request_ = 0;
    }
    this->connected_ = true;
    this->scanning_ = false;
    std::strncpy(this->status_, "CONNECTED", sizeof(this->status_) - 1);
    this->sync_current_speaker_();

    if (now - this->last_rssi_request_ >= 5000) {
      esp_bd_addr_t *address = this->a2dp_source_.get_last_peer_address();
      if (address != nullptr) {
        esp_err_t err = esp_bt_gap_read_acl_real_rssi(*address);
        if (err != ESP_OK) ESP_LOGW(TAG, "Real RSSI request failed: %s", esp_err_to_name(err));
      }
      this->last_rssi_request_ = now;
    }
  } else {
    this->test_tone_active_ = false;
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
  ESP_LOGCONFIG(TAG, "  HA scan slots: %u", static_cast<unsigned>(this->device_slot_count_));
  ESP_LOGCONFIG(TAG, "  Connected RSSI: enabled");
  ESP_LOGCONFIG(TAG, "  AVRCP battery status: enabled");
  ESP_LOGCONFIG(TAG, "  Connection chime: 700 ms");
}

void BtAudioBridge::start_scan() {
  ESP_LOGI(TAG, "Bluetooth scan requested");
  if (this->a2dp_started_) {
    ESP_LOGW(TAG, "A2DP stack already started; scan is already running or connected");
    this->publish_event_("SCAN: rejected, A2DP already started");
    return;
  }
  this->clear_devices_();
  this->scan_cycles_ = 0;
  this->scanning_ = true;
  std::strncpy(this->status_, "SCANNING", sizeof(this->status_) - 1);
  this->publish_status_();
  this->publish_event_("SCAN: requested from Home Assistant");
  this->scan_requested_ = true;
}

void BtAudioBridge::stop_scan() {
  ESP_LOGI(TAG, "Bluetooth scan stop requested");
  this->scan_requested_ = false;
  if (this->a2dp_started_ && this->a2dp_source_.is_discovery_active()) {
    ESP_LOGI(TAG, "Cancelling active Bluetooth discovery");
    this->a2dp_source_.cancel_discovery();
  }
  if (this->a2dp_started_) {
    this->a2dp_source_.end();
    this->a2dp_started_ = false;
  }
  this->connected_ = false;
  this->scanning_ = false;
  this->scan_cycles_ = 0;
  this->test_tone_active_ = false;
  std::strncpy(this->status_, "DISCONNECTED", sizeof(this->status_) - 1);
  this->publish_status_();
  this->publish_event_("SCAN: stopped manually - results kept in Home Assistant");
}

void BtAudioBridge::connect_slot(size_t index) {
  if (index >= this->device_count_ || !this->devices_[index].used) {
    ESP_LOGW(TAG, "Connect requested for empty Bluetooth slot %u", static_cast<unsigned>(index + 1));
    this->publish_event_("CONNECT: selected slot is empty");
    return;
  }
  std::strncpy(this->selected_name_, this->devices_[index].name, sizeof(this->selected_name_) - 1);
  this->selected_name_[sizeof(this->selected_name_) - 1] = '\0';
  this->connect_to(this->devices_[index].mac);
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
    this->publish_event_("CONNECT: invalid Bluetooth MAC address");
    return;
  }
  esp_bd_addr_t address = {static_cast<uint8_t>(b[0]), static_cast<uint8_t>(b[1]), static_cast<uint8_t>(b[2]), static_cast<uint8_t>(b[3]), static_cast<uint8_t>(b[4]), static_cast<uint8_t>(b[5])};
  std::strncpy(this->selected_mac_, mac, sizeof(this->selected_mac_) - 1);
  this->selected_mac_[sizeof(this->selected_mac_) - 1] = '\0';
  std::strncpy(this->status_, "CONNECTING", sizeof(this->status_) - 1);
  this->publish_status_();
  this->publish_event_("CONNECT: selected Bluetooth device");
  if (this->device_sensor_ != nullptr) {
    char state[120];
    std::snprintf(state, sizeof(state), "%s | %s", this->selected_name_[0] != '\0' ? this->selected_name_ : "UNKNOWN", this->selected_mac_);
    this->device_sensor_->publish_state(state);
  }
  ESP_LOGI(TAG, "Connecting to Bluetooth device: %s | %s", this->selected_name_[0] != '\0' ? this->selected_name_ : "UNKNOWN", this->selected_mac_);
  this->save_speaker_();
  if (!this->a2dp_started_) {
    this->scan_requested_ = false;
    this->start_a2dp_();
  } else if (this->a2dp_source_.is_discovery_active()) {
    ESP_LOGI(TAG, "Cancelling discovery before direct connection");
    this->a2dp_source_.cancel_discovery();
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
  this->scan_cycles_ = 0;
  this->test_tone_active_ = false;
  std::strncpy(this->status_, "DISCONNECTED", sizeof(this->status_) - 1);
  this->publish_status_();
}

void BtAudioBridge::forget_speaker() {
  ESP_LOGI(TAG, "Forget speaker requested");
  this->publish_event_("BT: forgetting saved speaker");
  this->scan_requested_ = false;
  this->a2dp_source_.set_auto_reconnect(true, 10);
  this->a2dp_source_.clean_last_connection();
  if (this->a2dp_started_) {
    this->a2dp_source_.end();
    this->a2dp_started_ = false;
  }
  nvs_handle_t handle;
  if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) == ESP_OK) {
    nvs_erase_key(handle, NVS_NAME_KEY);
    nvs_erase_key(handle, NVS_MAC_KEY);
    nvs_commit(handle);
    nvs_close(handle);
  }
  this->selected_name_[0] = '\0';
  this->selected_mac_[0] = '\0';
  this->connected_ = false;
  this->scanning_ = false;
  this->scan_cycles_ = 0;
  this->test_tone_active_ = false;
  std::strncpy(this->status_, "DISCONNECTED", sizeof(this->status_) - 1);
  if (this->device_sensor_ != nullptr) this->device_sensor_->publish_state("NONE");
  if (this->rssi_sensor_ != nullptr) this->rssi_sensor_->publish_state(NAN);
  if (this->battery_sensor_ != nullptr) this->battery_sensor_->publish_state("UNKNOWN");
  this->publish_status_();
  this->publish_event_("BT: saved speaker forgotten");
}

bool BtAudioBridge::is_connected() { return this->connected_; }
const char *BtAudioBridge::get_status() { return this->status_; }

void BtAudioBridge::start_a2dp_() {
  esp_err_t nvs_err = nvs_flash_init();
  if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES || nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_LOGW(TAG, "NVS init requires erase; erasing NVS partition");
    nvs_flash_erase();
    nvs_err = nvs_flash_init();
  }
  if (nvs_err != ESP_OK) {
    ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(nvs_err));
    this->publish_event_("BT: NVS init failed");
    this->scanning_ = false;
    return;
  }
  this->a2dp_source_.set_local_name("ESP32 BT Audio Bridge");
  this->a2dp_source_.set_ssp_enabled(true);
  this->a2dp_source_.set_auto_reconnect(true, 10);
  this->a2dp_source_.set_ssid_callback(bt_ssid_callback);
  this->a2dp_source_.set_discovery_mode_callback(bt_discovery_callback);
  this->a2dp_source_.set_avrc_rn_events({ESP_AVRC_RN_VOLUME_CHANGE, ESP_AVRC_RN_BATTERY_STATUS_CHANGE});
  this->a2dp_source_.set_data_callback(&BtAudioBridge::test_tone_callback_);
  this->a2dp_source_.start();
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
    case ESP_RST_POWERON: return "POWERON";
    case ESP_RST_EXT: return "EXTERNAL";
    case ESP_RST_SW: return "SOFTWARE";
    case ESP_RST_PANIC: return "PANIC";
    case ESP_RST_INT_WDT: return "INT_WDT";
    case ESP_RST_TASK_WDT: return "TASK_WDT";
    case ESP_RST_WDT: return "WDT";
    case ESP_RST_BROWNOUT: return "BROWNOUT";
    case ESP_RST_SDIO: return "SDIO";
    default: return "OTHER";
  }
}

}  // namespace bt_audio_bridge
}  // namespace esphome
