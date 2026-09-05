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
    # ESP32-A2DP korzysta bezpośrednio z ESP-IDF Bluetooth Classic.
    # Od ESPHome 2026.2 nieużywane komponenty IDF są domyślnie wyłączane.
    include_builtin_idf_component("bt")

    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
