idf_component_register(
    SRC_DIRS src
    INCLUDE_DIRS src
    REQUIRES bt esp_common freertos hal log nvs_flash driver
)

target_compile_options(${COMPONENT_LIB} PUBLIC
    -DA2DP_LEGACY_I2S_SUPPORT=0
    -DA2DP_I2S_AUDIOTOOLS=0
)

target_compile_definitions(${COMPONENT_LIB} PUBLIC
    A2DP_FORCE_ARDUINO_LAYOUT=1
)

# ESP32-A2DP calls Arduino's btStartMode() on IDF 5.x.
# Arduino 3.3.x has a Classic-BT startup regression there, so intercept
# that symbol and start the controller directly through ESP-IDF instead.
target_link_options(${COMPONENT_LIB} INTERFACE
    "-Wl,--wrap=btStartMode"
)
