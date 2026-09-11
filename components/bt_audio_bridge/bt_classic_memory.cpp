#include <esp32-hal-bt.h>

// This component uses Classic Bluetooth A2DP only. Arduino's normal
// btStartMode(BT_MODE_CLASSIC_BT) releases the unused BLE memory before
// initializing the controller. Our custom __wrap_btStartMode bypasses that
// Arduino helper, so do the same release once during static initialization.
//
// The Arduino btMemRelease() path also tracks the release and prevents a
// later double-release by the core's own memory management.
#ifdef USE_ARDUINO
namespace {
struct ClassicBluetoothMemorySetup {
  ClassicBluetoothMemorySetup() {
    btMemRelease(BT_MODE_BLE);
  }
};

ClassicBluetoothMemorySetup classic_bluetooth_memory_setup;
}  // namespace
#endif
