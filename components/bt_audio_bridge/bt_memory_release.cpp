#include <esp_bt.h>

#ifdef USE_ARDUINO
#include "esp32-hal-bt.h"

// The bridge uses Classic Bluetooth only. Release BLE controller/host memory
// before the custom BT startup wrapper initializes Classic Bluetooth.
// Arduino's normal btStartMode() path does this automatically, but the bridge
// supplies its own wrapper and therefore must do it explicitly.
__attribute__((constructor)) static void bt_audio_bridge_release_ble_memory() {
  if (esp_bt_mem_release(ESP_BT_MODE_BLE) == ESP_OK) {
    btMarkMemReleased(BT_MODE_BLE);
  }
}
#endif
