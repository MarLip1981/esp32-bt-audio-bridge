# Project Baseline

## Current stable base

**Baseline commit:** `17b01dc2345c032d0e357832ca90f5fb7ebbefc8`

**Date:** 2026-09-13

This is the new working base for the ESP32 Bluetooth Audio Bridge project.

### Confirmed working
- ESP32 DevKit V1 / ESP32-WROOM-32, rev 3.1
- Arduino framework / ESPHome 2026.7.4
- A2DP Source connection to TRACER Quasar (`58:88:0F:D1:6B:11`)
- Saved-speaker reconnect
- Bluetooth RSSI reporting
- Connection confirmation/test audio works
- PCM audio engine test works correctly after timing/prefill fix
- A2DP 4112-byte allocation failures observed previously are no longer present in the latest long runtime log
- Repeated unchanged text-sensor publication was reduced/cached to limit heap churn and fragmentation

### Known remaining issue
The current runtime log still contains intermittent:

`BT_L2CAP: l2cab is_cong_cback_context`

This is now the next investigation target. Do not revert the memory/startup fixes above unless new evidence requires it.

## Next phase
Move from low-level Bluetooth/ESP32 diagnostics to **Home Assistant integration and user-facing control**.

The current firmware baseline should be treated as the starting point for all HA work.
