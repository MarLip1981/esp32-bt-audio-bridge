#include <esp32-hal-bt.h>

#ifdef USE_ARDUINO
// bt_audio_bridge.cpp historically provides a strong btInUse() override so
// Arduino does not release Classic BT memory. That override also makes the
// Arduino startup guard think BLE is in use and therefore skips releasing
// the unused BLE side. The linker wrapper below makes the startup query false
// while leaving the existing bridge symbol untouched.
extern "C" bool __wrap_btInUse() {
  return false;
}
#endif
