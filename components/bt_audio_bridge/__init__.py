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
CONF_EVENT = "event"
CONF_RESET_REASON = "reset_reason"
CONF_DEVICE = "device"

AUTO_LOAD = ["text_sensor"]
DEPENDENCIES = ["wifi"]

bt_audio_bridge_ns = cg.esphome_ns.namespace("bt_audio_bridge")

BtAudioBridge = bt_audio_bridge_ns.class_(
    "BtAudioBridge",
    cg.Component,
)

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(BtAudioBridge),
    cv.Optional(CONF_STATUS): text_sensor.text_sensor_schema(),
    cv.Optional(CONF_EVENT): text_sensor.text_sensor_schema(),
    cv.Optional(CONF_RESET_REASON): text_sensor.text_sensor_schema(),
    cv.Optional(CONF_DEVICE): text_sensor.text_sensor_schema(),
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

    add_idf_component(
        name="ESP32-A2DP",
        path=str(Path(__file__).resolve().parent.parent / "ESP32-A2DP"),
    )

    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    if status_config := config.get(CONF_STATUS):
        sensor = await text_sensor.new_text_sensor(status_config)
        cg.add(var.set_status_sensor(sensor))

    if event_config := config.get(CONF_EVENT):
        sensor = await text_sensor.new_text_sensor(event_config)
        cg.add(var.set_event_sensor(sensor))

    if reset_config := config.get(CONF_RESET_REASON):
        sensor = await text_sensor.new_text_sensor(reset_config)
        cg.add(var.set_reset_reason_sensor(sensor))

    if device_config := config.get(CONF_DEVICE):
        sensor = await text_sensor.new_text_sensor(device_config)
        cg.add(var.set_device_sensor(sensor))
