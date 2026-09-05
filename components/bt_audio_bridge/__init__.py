import esphome.codegen as cg
import esphome.config_validation as cv

from esphome.const import CONF_ID
from esphome.components.esp32 import include_builtin_idf_component

DEPENDENCIES = ["wifi"]

bt_audio_bridge_ns = cg.esphome_ns.namespace("bt_audio_bridge")

BtAudioBridge = bt_audio_bridge_ns.class_(
    "BtAudioBridge",
    cg.Component,
)

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(BtAudioBridge),
}).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    # ESP32-A2DP uses ESP-IDF Bluetooth Classic APIs directly.
    # ESPHome 2026.2+ excludes unused IDF components by default,
    # so explicitly keep the Bluetooth component enabled.
    include_builtin_idf_component("bt")

    # Native ESPHome 2026.7 builds can fetch Arduino libraries directly
    # through codegen. The .git suffix is important for native ESP-IDF toolchain.
    cg.add_library(
        "ESP32-A2DP",
        "1.8.11",
        "https://github.com/pschatzmann/ESP32-A2DP.git#v1.8.11",
    )

    # We only need callback-based A2DP source operation for now.
    cg.add_build_flag("-DA2DP_LEGACY_I2S_SUPPORT=0")
    cg.add_build_flag("-DA2DP_I2S_AUDIOTOOLS=0")

    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
