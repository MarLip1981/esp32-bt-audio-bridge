idf_component_register(
    SRCS
        "bt_audio_bridge.cpp"
    INCLUDE_DIRS
        "."
    REQUIRES
        bt
)

# ESP32-A2DP calls Arduino's btStartMode() on IDF 5.x.
# Arduino 3.3.x has a Classic-BT startup regression there, so intercept
# that symbol and start the controller directly through ESP-IDF instead.
target_link_options(${COMPONENT_LIB} PRIVATE
    "-Wl,--wrap=btStartMode"
)
