"""ESPHome code-generation wrapper for the IP5306 battery management IC.

Registers the component, wires the optional battery-level sensor and charging
binary_sensor to the C++ driver, and sets the polling interval.
"""

import esphome.codegen as cg
from esphome.components import binary_sensor, i2c, sensor
import esphome.config_validation as cv
from esphome.const import (
    CONF_BINARY_SENSORS,
    CONF_ID,
    CONF_UPDATE_INTERVAL,
    DEVICE_CLASS_BATTERY,
    DEVICE_CLASS_BATTERY_CHARGING,
    STATE_CLASS_MEASUREMENT,
    UNIT_PERCENT,
)

CODEOWNERS = ["@robinlankhorst"]
DEPENDENCIES = ["i2c"]

CONF_BATTERY_LEVEL = "battery_level"
CONF_CHARGING = "charging"

ns_ = cg.esphome_ns.namespace("ip5306")
cls_ = ns_.class_("IP5306", cg.PollingComponent, i2c.I2CDevice)

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(cls_),
            cv.Optional(CONF_BATTERY_LEVEL): sensor.sensor_schema(
                unit_of_measurement=UNIT_PERCENT,
                accuracy_decimals=0,
                device_class=DEVICE_CLASS_BATTERY,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
            cv.Optional(CONF_CHARGING): binary_sensor.binary_sensor_schema(
                device_class=DEVICE_CLASS_BATTERY_CHARGING,
            ),
        }
    )
    .extend(cv.polling_component_schema("30s"))
    .extend(i2c.i2c_device_schema(0x75))
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await i2c.register_i2c_device(var, config)

    if CONF_BATTERY_LEVEL in config:
        sens = await sensor.new_sensor(config[CONF_BATTERY_LEVEL])
        cg.add(var.set_battery_level_sensor(sens))

    if CONF_CHARGING in config:
        bs = await binary_sensor.new_binary_sensor(config[CONF_CHARGING])
        cg.add(var.set_charging_sensor(bs))
