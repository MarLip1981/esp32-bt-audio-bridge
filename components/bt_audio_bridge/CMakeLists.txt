idf_component_register(
    SRCS
        "bt_audio_bridge.cpp"
        "bt_audio_engine.cpp"
        "bt_audio_engine_test.cpp"
        "bt_http_wav.cpp"
        "bt_classic_memory.cpp"
    INCLUDE_DIRS
        "."
    REQUIRES
        bt
        arduino-esp32
        esp_http_client
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE
    A2DP_FORCE_ARDUINO_LAYOUT=1
)

# bt_audio_bridge.cpp has a legacy strong btInUse() symbol. Wrap calls from
# Arduino's initArduino() so its BLE-memory release guard sees the actual
# Classic-BT-only configuration.
target_link_libraries(${COMPONENT_LIB} INTERFACE
    "-Wl,--wrap=btInUse"
)
