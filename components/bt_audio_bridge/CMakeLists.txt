idf_component_register(
    SRCS
        "bt_audio_bridge.cpp"
        "bt_audio_engine.cpp"
        "bt_audio_engine_test.cpp"
        "bt_http_wav.cpp"
    INCLUDE_DIRS
        "."
    REQUIRES
        bt
        arduino-esp32
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE
    A2DP_FORCE_ARDUINO_LAYOUT=1
)
