idf_component_register(
    SRC_DIRS src
    INCLUDE_DIRS src
    REQUIRES bt esp_common freertos hal log nvs_flash driver
)

target_compile_options(${COMPONENT_LIB} PUBLIC
    -DA2DP_LEGACY_I2S_SUPPORT=0
    -DA2DP_I2S_AUDIOTOOLS=0
)

# This component is used through the raw A2DP PCM callback.
# No Arduino compatibility layer is required when ESPHome runs on ESP-IDF.
