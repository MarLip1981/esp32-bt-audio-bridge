idf_component_register(
    SRCS
        "bt_audio_bridge.cpp"
    INCLUDE_DIRS
        "."
    REQUIRES
        bt
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE
    A2DP_FORCE_ARDUINO_LAYOUT=1
)
