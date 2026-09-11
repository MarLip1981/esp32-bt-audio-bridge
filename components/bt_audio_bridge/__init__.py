from pathlib import Path

import esphome.codegen as cg
import esphome.config_validation as cv

from esphome.components import sensor, text_sensor
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
CONF_RSSI = "rssi"
CONF_BATTERY = "battery"
CONF_AUDIO_URL = "audio_url"
CONF_DEVICES = "devices"
CONF_TEXT_SENSOR = "text_sensor"
CONF_DIAGNOSTICS = "diagnostics"
CONF_WIFI_RSSI = "wifi_rssi"
CONF_WIFI_SIGNAL = "wifi_signal"
CONF_WIFI_CHANNEL = "wifi_channel"
CONF_WIFI_TX_POWER = "wifi_tx_power"
CONF_WIFI_SSID = "wifi_ssid"
CONF_WIFI_PHY = "wifi_phy"
CONF_WIFI_STATUS = "wifi_status"
CONF_WIFI_UPTIME = "wifi_uptime"
CONF_CPU_LOAD = "cpu_load"
CONF_CPU_FREQUENCY = "cpu_frequency"
CONF_HEAP_FREE = "heap_free"
CONF_HEAP_USED = "heap_used"
CONF_HEAP_PERCENT = "heap_percent"
CONF_HEAP_MIN_FREE = "heap_min_free"
CONF_HEAP_LARGEST_BLOCK = "heap_largest_block"
CONF_BT_STATUS = "bt_status"
CONF_BT_CONTROLLER = "bt_controller"
CONF_BT_CONNECTIONS = "bt_connections"
CONF_BT_RECONNECTS = "bt_reconnects"
CONF_BT_UPTIME = "bt_uptime"

AUTO_LOAD = ["sensor", "text_sensor"]
DEPENDENCIES = ["wifi"]

bt_audio_bridge_ns = cg.esphome_ns.namespace("bt_audio_bridge")

BtAudioBridge = bt_audio_bridge_ns.class_(
    "BtAudioBridge",
    cg.Component,
)

BtAudioBridgeDiagnostics = bt_audio_bridge_ns.class_(
    "BtAudioBridgeDiagnostics",
    cg.Component,
)

DEVICE_SLOT_SCHEMA = cv.Schema({
    cv.Required(CONF_TEXT_SENSOR): text_sensor.text_sensor_schema(),
})

DIAGNOSTICS_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(BtAudioBridgeDiagnostics),
    cv.Optional(CONF_WIFI_RSSI): sensor.sensor_schema(unit_of_measurement="dBm", accuracy_decimals=0),
    cv.Optional(CONF_WIFI_SIGNAL): sensor.sensor_schema(unit_of_measurement="%", accuracy_decimals=0),
    cv.Optional(CONF_WIFI_CHANNEL): sensor.sensor_schema(unit_of_measurement="", accuracy_decimals=0),
    cv.Optional(CONF_WIFI_TX_POWER): sensor.sensor_schema(unit_of_measurement="dBm", accuracy_decimals=2),
    cv.Optional(CONF_WIFI_SSID): text_sensor.text_sensor_schema(),
    cv.Optional(CONF_WIFI_PHY): text_sensor.text_sensor_schema(),
    cv.Optional(CONF_WIFI_STATUS): text_sensor.text_sensor_schema(),
    cv.Optional(CONF_WIFI_UPTIME): sensor.sensor_schema(unit_of_measurement="s", accuracy_decimals=0),
    cv.Optional(CONF_CPU_LOAD): sensor.sensor_schema(unit_of_measurement="%", accuracy_decimals=0),
    cv.Optional(CONF_CPU_FREQUENCY): sensor.sensor_schema(unit_of_measurement="MHz", accuracy_decimals=0),
    cv.Optional(CONF_HEAP_FREE): sensor.sensor_schema(unit_of_measurement="B", accuracy_decimals=0),
    cv.Optional(CONF_HEAP_USED): sensor.sensor_schema(unit_of_measurement="B", accuracy_decimals=0),
    cv.Optional(CONF_HEAP_PERCENT): sensor.sensor_schema(unit_of_measurement="%", accuracy_decimals=1),
    cv.Optional(CONF_HEAP_MIN_FREE): sensor.sensor_schema(unit_of_measurement="B", accuracy_decimals=0),
    cv.Optional(CONF_HEAP_LARGEST_BLOCK): sensor.sensor_schema(unit_of_measurement="B", accuracy_decimals=0),
    cv.Optional(CONF_BT_STATUS): text_sensor.text_sensor_schema(),
    cv.Optional(CONF_BT_CONTROLLER): text_sensor.text_sensor_schema(),
    cv.Optional(CONF_BT_CONNECTIONS): sensor.sensor_schema(unit_of_measurement="", accuracy_decimals=0),
    cv.Optional(CONF_BT_RECONNECTS): sensor.sensor_schema(unit_of_measurement="", accuracy_decimals=0),
    cv.Optional(CONF_BT_UPTIME): sensor.sensor_schema(unit_of_measurement="s", accuracy_decimals=0),
})

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(BtAudioBridge),
    cv.Optional(CONF_STATUS): text_sensor.text_sensor_schema(),
    cv.Optional(CONF_EVENT): text_sensor.text_sensor_schema(),
    cv.Optional(CONF_RESET_REASON): text_sensor.text_sensor_schema(),
    cv.Optional(CONF_DEVICE): text_sensor.text_sensor_schema(),
    cv.Optional(CONF_RSSI): sensor.sensor_schema(unit_of_measurement="dBm", accuracy_decimals=0),
    cv.Optional(CONF_BATTERY): text_sensor.text_sensor_schema(),
    cv.Optional(CONF_AUDIO_URL): cv.string_strict,
    cv.Optional(CONF_DEVICES, default=[]): cv.All(
        cv.ensure_list(DEVICE_SLOT_SCHEMA),
        cv.Length(max=8),
    ),
    cv.Optional(CONF_DIAGNOSTICS): DIAGNOSTICS_SCHEMA,
}).extend(cv.COMPONENT_SCHEMA)


async def _new_sensor(config, key, setter, var):
    if sensor_config := config.get(key):
        value = await sensor.new_sensor(sensor_config)
        cg.add(getattr(var, setter)(value))


async def _new_text_sensor(config, key, setter, var):
    if text_config := config.get(key):
        value = await text_sensor.new_text_sensor(text_config)
        cg.add(getattr(var, setter)(value))


async def to_code(config):
    include_builtin_idf_component("bt")
    include_builtin_idf_component("esp_http_client")

    add_idf_sdkconfig_option("CONFIG_BT_ENABLED", True)
    add_idf_sdkconfig_option("CONFIG_BTDM_CTRL_MODE_BLE_ONLY", False)
    add_idf_sdkconfig_option("CONFIG_BTDM_CTRL_MODE_BR_EDR_ONLY", True)
    add_idf_sdkconfig_option("CONFIG_BTDM_CTRL_MODE_BTDM", False)
    add_idf_sdkconfig_option("CONFIG_BT_BLUEDROID_ENABLED", True)
    add_idf_sdkconfig_option("CONFIG_BT_CLASSIC_ENABLED", True)
    add_idf_sdkconfig_option("CONFIG_BT_A2DP_ENABLE", True)
    add_idf_sdkconfig_option("CONFIG_BT_BLE_ENABLED", False)
    add_idf_sdkconfig_option("CONFIG_BT_CLASSIC_ENABLE_POWER_CTRL_VSC", True)

    add_idf_component(
        name="ESP32-A2DP",
        path=str(Path(__file__).resolve().parent.parent / "ESP32-A2DP"),
    )

    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    if status_config := config.get(CONF_STATUS):
        status_sensor = await text_sensor.new_text_sensor(status_config)
        cg.add(var.set_status_sensor(status_sensor))

    if event_config := config.get(CONF_EVENT):
        event_sensor = await text_sensor.new_text_sensor(event_config)
        cg.add(var.set_event_sensor(event_sensor))

    if reset_config := config.get(CONF_RESET_REASON):
        reset_sensor = await text_sensor.new_text_sensor(reset_config)
        cg.add(var.set_reset_reason_sensor(reset_sensor))

    if device_config := config.get(CONF_DEVICE):
        device_sensor = await text_sensor.new_text_sensor(device_config)
        cg.add(var.set_device_sensor(device_sensor))

    if rssi_config := config.get(CONF_RSSI):
        rssi_sensor = await sensor.new_sensor(rssi_config)
        cg.add(var.set_rssi_sensor(rssi_sensor))

    if battery_config := config.get(CONF_BATTERY):
        battery_sensor = await text_sensor.new_text_sensor(battery_config)
        cg.add(var.set_battery_sensor(battery_sensor))

    if audio_url := config.get(CONF_AUDIO_URL):
        cg.add(var.set_audio_url(audio_url))

    for slot in config.get(CONF_DEVICES, []):
        slot_sensor = await text_sensor.new_text_sensor(slot[CONF_TEXT_SENSOR])
        cg.add(var.add_device_slot(slot_sensor))

    if diagnostics_config := config.get(CONF_DIAGNOSTICS):
        diagnostics_var = cg.new_Pvariable(diagnostics_config[CONF_ID])
        await cg.register_component(diagnostics_var, diagnostics_config)

        await _new_sensor(diagnostics_config, CONF_WIFI_RSSI, "set_wifi_rssi", diagnostics_var)
        await _new_sensor(diagnostics_config, CONF_WIFI_SIGNAL, "set_wifi_signal", diagnostics_var)
        await _new_sensor(diagnostics_config, CONF_WIFI_CHANNEL, "set_wifi_channel", diagnostics_var)
        await _new_sensor(diagnostics_config, CONF_WIFI_TX_POWER, "set_wifi_tx_power", diagnostics_var)
        await _new_text_sensor(diagnostics_config, CONF_WIFI_SSID, "set_wifi_ssid", diagnostics_var)
        await _new_text_sensor(diagnostics_config, CONF_WIFI_PHY, "set_wifi_phy", diagnostics_var)
        await _new_text_sensor(diagnostics_config, CONF_WIFI_STATUS, "set_wifi_status", diagnostics_var)
        await _new_sensor(diagnostics_config, CONF_WIFI_UPTIME, "set_wifi_uptime", diagnostics_var)

        await _new_sensor(diagnostics_config, CONF_CPU_LOAD, "set_cpu_load", diagnostics_var)
        await _new_sensor(diagnostics_config, CONF_CPU_FREQUENCY, "set_cpu_frequency", diagnostics_var)
        await _new_sensor(diagnostics_config, CONF_HEAP_FREE, "set_heap_free", diagnostics_var)
        await _new_sensor(diagnostics_config, CONF_HEAP_USED, "set_heap_used", diagnostics_var)
        await _new_sensor(diagnostics_config, CONF_HEAP_PERCENT, "set_heap_percent", diagnostics_var)
        await _new_sensor(diagnostics_config, CONF_HEAP_MIN_FREE, "set_heap_min_free", diagnostics_var)
        await _new_sensor(diagnostics_config, CONF_HEAP_LARGEST_BLOCK, "set_heap_largest_block", diagnostics_var)

        await _new_text_sensor(diagnostics_config, CONF_BT_STATUS, "set_bt_status", diagnostics_var)
        await _new_text_sensor(diagnostics_config, CONF_BT_CONTROLLER, "set_bt_controller", diagnostics_var)
        await _new_sensor(diagnostics_config, CONF_BT_CONNECTIONS, "set_bt_connections", diagnostics_var)
        await _new_sensor(diagnostics_config, CONF_BT_RECONNECTS, "set_bt_reconnects", diagnostics_var)
        await _new_sensor(diagnostics_config, CONF_BT_UPTIME, "set_bt_uptime", diagnostics_var)
