import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import microphone, sensor
from esphome.const import (
    CONF_CHANNEL,
    CONF_ID,
    CONF_MICROPHONE,
    DEVICE_CLASS_SIGNAL_STRENGTH,
    STATE_CLASS_MEASUREMENT,
    UNIT_DECIBEL,
)

from .. import satellite1 as sat

DEPENDENCIES = ["satellite1"]

Satellite1MicRmsSensor = sat.namespace.class_("Satellite1MicRmsSensor", sensor.Sensor, cg.PollingComponent)
MicRmsSource = sat.namespace.enum("MicRmsSource")

CONF_SOURCE = "source"

RMS_SOURCE_ENUM = {
    "xmos": MicRmsSource.XMOS,
    "esp_local": MicRmsSource.ESP_LOCAL,
}

CONFIG_SCHEMA = (
    sensor.sensor_schema(
        unit_of_measurement=UNIT_DECIBEL,
        accuracy_decimals=1,
        device_class=DEVICE_CLASS_SIGNAL_STRENGTH,
        state_class=STATE_CLASS_MEASUREMENT,
    )
    .extend(
        {
            cv.GenerateID(CONF_ID): cv.declare_id(Satellite1MicRmsSensor),
            cv.GenerateID(sat.CONF_SATELLITE1): cv.use_id(sat.Satellite1),
            cv.Optional(CONF_SOURCE, default="xmos"): cv.enum(RMS_SOURCE_ENUM, lower=True),
            cv.Optional(CONF_MICROPHONE): microphone.microphone_source_schema(
                min_bits_per_sample=16,
                max_bits_per_sample=32,
                min_channels=1,
                max_channels=4,
            ),
            cv.Optional(CONF_CHANNEL, default=0): cv.int_range(min=0, max=3),
        }
    )
    .extend(cv.polling_component_schema("500ms"))
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await sensor.register_sensor(var, config)
    await cg.register_component(var, config)
    parent = await cg.get_variable(config[sat.CONF_SATELLITE1])

    cg.add(var.set_parent(parent))
    cg.add(var.set_source(config[CONF_SOURCE]))
    cg.add(var.set_channel(config[CONF_CHANNEL]))
    if CONF_MICROPHONE in config:
        mic_source = await microphone.microphone_source_to_code(config[CONF_MICROPHONE])
        cg.add(var.set_microphone_source(mic_source))
