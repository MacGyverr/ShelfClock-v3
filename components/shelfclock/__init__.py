import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor, time
from esphome.const import CONF_ID

CONF_TIME_ID = "time_id"
CONF_TEMPERATURE_SENSOR = "temperature_sensor"
CONF_HUMIDITY_SENSOR = "humidity_sensor"

DEPENDENCIES = ["esp32", "time", "wifi"]
AUTO_LOAD = ["sensor"]

shelfclock_ns = cg.esphome_ns.namespace("shelfclock_component")
ShelfClockComponent = shelfclock_ns.class_("ShelfClockComponent", cg.Component)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(ShelfClockComponent),
        cv.Required(CONF_TIME_ID): cv.use_id(time.RealTimeClock),
        cv.Optional(CONF_TEMPERATURE_SENSOR): cv.use_id(sensor.Sensor),
        cv.Optional(CONF_HUMIDITY_SENSOR): cv.use_id(sensor.Sensor),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    time_source = await cg.get_variable(config[CONF_TIME_ID])
    cg.add(var.set_time_source(time_source))
    if temperature_id := config.get(CONF_TEMPERATURE_SENSOR):
        temperature = await cg.get_variable(temperature_id)
        cg.add(var.set_temperature_sensor(temperature))
    if humidity_id := config.get(CONF_HUMIDITY_SENSOR):
        humidity = await cg.get_variable(humidity_id)
        cg.add(var.set_humidity_sensor(humidity))
