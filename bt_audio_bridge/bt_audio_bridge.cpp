#include "bt_audio_bridge.h"

#include "esphome/core/log.h"

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
BtAudioBridge *global_bt_audio_bridge = nullptr;

static bool bt_ssid_callback(const char *ssid, esp_bd_addr_t address, int rrsi) {
  if (global_bt_audio_bridge == nullptr) return true;
  char mac[18];
  std::snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
                address[0], address[1], address[2], address[3], address[4], address[5]);
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

static bool valid_bt_mac(const char *mac) {
  if (mac == nullptr || std::strlen(mac) != 17) return false;
  for (int i = 0; i < 17; i++) {
    if ((i + 1) % 3 == 0) {
      if (mac[i] != ':') return false;
    } else if (!((mac[i] >= '0' && mac[i] <= '9') ||
                 (mac[i] >= 'A' && mac[i] <= 'F') ||
                 (mac[i] >= 'a' && mac[i] <= 'f'))) {
      return false;
    }
  }
  return true;
}

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
    if (this->device_sensors_[i] != nullptr) {
      this->device_sensors_[i]->publish_state("BRAK URZĄDZENIA");
    }
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
  std::snprintf(state, sizeof(state), "%s | %s | %d dBm",
                this->devices_[index].name,
                this->devices_[index].mac,
                this->devices_[index].rssi);
  this->device_sensors_[index]->publish_state(state);
}

void BtAudioBridge::load_saved_speaker_() {
  nvs_handle_t handle;
  esp_err_t err = nvs_open("bt_aud_bridge", NVS_READONLY, &handle);
  if (err != ESP_OK) return;

  size_t size = sizeof(this->selected_name_);
  if (nvs_get_str(handle, "speaker_name", this->selected_name_, &size) != ESP_OK) {
    this->selected_name_[0] = '\0';
  }

  size = sizeof(this->selected_mac_);
  if (nvs_get_str(handle, "speaker_mac", this->selected_mac_, &size) != ESP_OK ||
      !valid_bt_mac(this->selected_mac_)) {
    this->selected_mac_[0] = '\0';
  }

  nvs_close(handle);
}

void BtAudioBridge::save_speaker_(const char *name, const char *mac) {
  if (!valid_bt_mac(mac)) return;

  nvs_handle_t handle;
  esp_err_t err = nvs_open("bt_aud_bridge", NVS_READWRITE, &handle);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Cannot open speaker storage: %s", esp_err_to_name(err));
    return;
  }

  if (name != nullptr && std::strlen(name) > 0) {
    nvs_set_str(handle, "speaker_name", name);
  }
  nvs_set_str(handle, "speaker_mac", mac);
  nvs_commit(handle);
  nvs_close(handle);
}

void BtAudioBridge::sync_current_speaker_() {
  if (!this->a2dp_started_ || !this->connected_) return;

  esp_bd_addr_t *address = this->a2dp_source_.get_current_peer_address();
  if (address == nullptr) return;

  bool nonzero = false;
  for (int i = 0; i < ESP_BD_ADDR_LEN; i++) {
    if ((*address)[i] != 0) {
      nonzero = true;
      break;
    }
  }
  if (!nonzero) return;

  char mac[18];
  std::snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
                (*address)[0], (*address)[1], (*address)[2],
                (*address)[3], (*address)[4], (*address)[5]);

  const char *library_name = this->a2dp_source_.get_name();
  if (library_name != nullptr && std::strlen(library_name) > 0) {
    std::strncpy(this->selected_name_, library_name, sizeof(this->selected_name_) - 1);
    this->selected_name_[sizeof(this->selected_name_) - 1] = '\0';
  }

  const bool mac_changed = std::strncmp(this->selected_mac_, mac, sizeof(this->selected_mac_)) != 0;
  if (mac_changed) {
    std::strncpy(this->selected_mac_, mac, sizeof(this->selected_mac_) - 1);
    this->selected_mac_[sizeof(this->selected_mac_) - 1] = '\0';
    if (this->device_sensor_ != nullptr) this->device_sensor_->publish_state(this->selected_mac_);
  }

  this->save_speaker_(this->selected_name_, this->selected_mac_);

  if (this->current_speaker_sensor_ != nullptr) {
    char state[100];
    const char *name = this->selected_name_[0] != '\0' ? this->selected_name_ : "NIEZNANY GŁOŚNIK";
    std::snprintf(state, sizeof(state), "%s | %s", name, this->selected_mac_);
    this->current_speaker_sensor_->publish_state(state);
  }
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

void BtAudioBridge::setup() {
  global_bt_audio_bridge = this;
  ESP_LOGI(TAG, "Bluetooth A2DP Source ready");
  ESP_LOGI(TAG, "Reset reason: %d", static_cast<int>(esp_reset_reason()));
  this->connected_ = false;
  this->scanning_ = false;
  this->a2dp_started_ = false;
  this->scan_requested_ = false;
  this->scan_cycles_ = 0;
  this->device_count_ = 0;
  this->devices_dirty_ = false;
  std::strncpy(this->status_, "READY", sizeof(this->status_) - 1);
  this->publish_status_();
  this->publish_event_("BOOT: component ready");
  if (this->reset_reason_sensor_ != nullptr)
    this->reset_reason_sensor_->publish_state(this->reset_reason_());
  if (this->device_sensor_ != nullptr)
    this->device_sensor_->publish_state(this->selected_mac_[0] != '\0' ? this->selected_mac_ : "NONE");
  if (this->current_speaker_sensor_ != nullptr) {
    this->current_speaker_sensor_->publish_state("URUCHAMIANIE");
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
    if (!this->connected_) this->publish_event_("BT: speaker connected");
    this->connected_ = true;
    this->scanning_ = false;
    std::strncpy(this->status_, "CONNECTED", sizeof(this->status_) - 1);
    this->sync_current_speaker_();
  } else {
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
    this->publish_event_("CONNECT: invalid MAC address");
    return;
  }

  esp_bd_addr_t address = {static_cast<uint8_t>(b[0]), static_cast<uint8_t>(b[1]), static_cast<uint8_t>(b[2]),
                           static_cast<uint8_t>(b[3]), static_cast<uint8_t>(b[4]), static_cast<uint8_t>(b[5])};

  std::strncpy(this->selected_mac_, mac, sizeof(this->selected_mac_) - 1);
  this->selected_mac_[sizeof(this->selected_mac_) - 1] = '\0';
  this->save_speaker_(this->selected_name_, this->selected_mac_);
  std::strncpy(this->status_, "CONNECTING", sizeof(this->status_) - 1);
  this->publish_status_();
  this->publish_event_("CONNECT: selected Bluetooth device");
  if (this->device_sensor_ != nullptr) this->device_sensor_->publish_state(this->selected_mac_);
  if (this->current_speaker_sensor_ != nullptr) {
    char state[100];
    const char *name = this->selected_name_[0] != '\0' ? this->selected_name_ : "NIEZNANY GŁOŚNIK";
    std::snprintf(state, sizeof(state), "%s | %s", name, this->selected_mac_);
    this->current_speaker_sensor_->publish_state(state);
  }
  ESP_LOGI(TAG, "Connecting to Bluetooth device: %s", this->selected_mac_);

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
  std::strncpy(this->status_, "DISCONNECTED", sizeof(this->status_) - 1);
  this->publish_status_();
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

  // NVS is initialized now, so restore our human-readable speaker name and MAC.
  this->load_saved_speaker_();
  if (this->device_sensor_ != nullptr)
    this->device_sensor_->publish_state(this->selected_mac_[0] != '\0' ? this->selected_mac_ : "NONE");
  if (this->current_speaker_sensor_ != nullptr) {
    char state[100];
    if (this->selected_mac_[0] != '\0') {
      const char *name = this->selected_name_[0] != '\0' ? this->selected_name_ : "NIEZNANY GŁOŚNIK";
      std::snprintf(state, sizeof(state), "%s | %s", name, this->selected_mac_);
    } else {
      std::snprintf(state, sizeof(state), "BRAK ZAPISANEGO GŁOŚNIKA");
    }
    this->current_speaker_sensor_->publish_state(state);
  }

  this->a2dp_source_.set_local_name("ESP32 BT Audio Bridge");
  this->a2dp_source_.set_ssp_enabled(true);
  this->a2dp_source_.set_auto_reconnect(true, 10);
  this->a2dp_source_.set_ssid_callback(bt_ssid_callback);
  this->a2dp_source_.set_discovery_mode_callback(bt_discovery_callback);
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
    case ESP_RST_WDT: return "WDT";
    case ESP_RST_BROWNOUT: return "BROWNOUT";
    case ESP_RST_SDIO: return "SDIO";
    default: return "OTHER";
  }
}

}  // namespace bt_audio_bridge
}  // namespace esphome
