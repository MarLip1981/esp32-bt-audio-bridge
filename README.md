# esp32-bt-audio-bridge

ESPHome Bluetooth A2DP Audio Bridge for ESP32-WROOM / ESP32 DevKit V1.

## Current architecture

```text
Home Assistant / ESPHome
        |
        v
   BtAudioBridge
        |
        +---- Bluetooth discovery / selection
        |
        +---- NVS saved speaker (name + MAC)
        |
        +---- A2DP Source
        |         |
        |         v
        |    permanent audio callback
        |         |
        v         v
   BtAudioEngine -> PCM 44.1 kHz / 16-bit / stereo
        |
        v
      speaker
```

### Bluetooth state

The bridge currently supports:

- Bluetooth Classic A2DP Source.
- Device discovery with up to 8 Home Assistant slots.
- Manual connection by discovery slot.
- Saved speaker name and MAC address in NVS.
- Startup reconnect to the saved speaker.
- A2DP library auto-reconnect to the last Bluetooth address.
- Synchronisation of the currently active peer back to the `Bluetooth Selected Device` entity.
- Real RSSI and AVRCP battery status when the speaker provides them.
- Connection confirmation tone.

The selected speaker is persisted independently from the discovery list. After an automatic reconnect, the active A2DP peer is used as the source of truth and the HA device entity is synchronised with its MAC address and, when available, its Bluetooth name.

## Audio Engine

`BtAudioEngine` is a non-blocking PCM producer/consumer ring buffer implemented with a FreeRTOS stream buffer.

- Buffer: 24 KiB.
- Format target: 44.1 kHz, 16-bit, stereo PCM.
- Producer: future network/audio input writes PCM with `BtAudioEngine::write()`.
- Consumer: the permanent A2DP data callback reads PCM with `BtAudioEngine::read()`.
- Underruns and overruns are counted.
- The existing Audio Engine Test exercises the PCM path without changing the Bluetooth callback architecture.

The 24 KiB buffer and the existing callback/task architecture are intentionally kept unchanged while the network input is added.

## HTTP WAV stage

The next functional stage is:

```text
HTTP WAV
   |
   v
WAV header parser
   |
   v
PCM 44.1 kHz / 16-bit / stereo
   |
   v
BtAudioEngine::write()
   |
   v
permanent A2DP callback
   |
   v
Bluetooth speaker
```

At this stage the project does **not** yet contain a general MP3 decoder or Home Assistant TTS integration. Those are later stages built on top of the same PCM engine.

## Test order

1. Confirm Bluetooth discovery and manual connection.
2. Confirm saved-speaker startup reconnect.
3. Confirm current-speaker synchronisation after reconnect.
4. Confirm `Audio Engine Test` and PCM ring-buffer counters.
5. Add HTTP WAV input without modifying the established Bluetooth callback.
6. Only after WAV/PCM is stable, add MP3/TTS support.
