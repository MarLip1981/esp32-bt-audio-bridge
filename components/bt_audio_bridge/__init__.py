import esphome.codegen as cg
import esphome.config_validation as cv

from esphome.const import CONF_ID

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
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    # ESP32-A2DP korzysta z bibliotek Arduino.
    # W ESPHome 2026.2+ trzeba je jawnie włączyć.
    cg.add_library("ESP32-A2DP", None)
