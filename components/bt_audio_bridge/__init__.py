from pathlib import Path

import esphome.codegen as cg
import esphome.config_validation as cv

from esphome.components import text_sensor
from esphome.const import CONF_ID

from esphome.components.esp32 import (
    add_idf_component,
    add_idf_sdkconfig_option,
    include_builtin_idf_component,
)

CONF_STATUS = "status"

DEPENDENCIES = ["wifi"]

bt_audio_bridge_ns = cg.esphome_ns.namespace("bt_audio_bridge")

BtAudioBridge = bt_audio_bridge_ns.class_(
    "BtAudioBridge",
    cg.Component,
)

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(BtAudioBridge),
    cv.Optional(CONF_STATUS): text_sensor.text_sensor_schema(),
}).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    # ESP32-A2DP uses Bluetooth Classic from ESP-IDF.
    # ESPHome 2026.2+ excludes unused IDF components by default.
    include_builtin_idf_component("bt")

    # ESP-IDF disables Classic Bluetooth and A2DP by default. These options
    # are required for the esp_a2dp_api.h API used by ESP32-A2DP.
    add_idf_sdkconfig_option("CONFIG_BT_ENABLED", True)
    add_idf_sdkconfig_option("CONFIG_BTDM_CTRL_MODE_BLE_ONLY", False)
    add_idf_sdkconfig_option("CONFIG_BTDM_CTRL_MODE_BR_EDR_ONLY", True)
    add_idf_sdkconfig_option("CONFIG_BTDM_CTRL_MODE_BTDM", False)
    add_idf_sdkconfig_option("CONFIG_BT_BLUEDROID_ENABLED", True)
    add_idf_sdkconfig_option("CONFIG_BT_CLASSIC_ENABLED", True)
    add_idf_sdkconfig_option("CONFIG_BT_A2DP_ENABLE", True)
    add_idf_sdkconfig_option("CONFIG_BT_BLE_ENABLED", False)

    # ESPHome copies external component C++ files into its src component.
    # Register the sibling vendored library explicitly with native ESP-IDF.
    add_idf_component(
        name="ESP32-A2DP",
        path=str(Path(__file__).resolve().parent.parent / "ESP32-A2DP"),
    )

    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    if status_config := config.get(CONF_STATUS):
        status = await text_sensor.new_text_sensor(status_config)
        cg.add(var.set_status_sensor(status))
