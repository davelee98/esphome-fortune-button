"""Fortune button with optional coordinated sharing of its addressable light."""

from esphome import automation
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor, light
from esphome.components.ledc.output import LEDCOutput
from esphome.const import CONF_BRIGHTNESS, CONF_ID, CONF_OUTPUT

DEPENDENCIES = ["esp32", "light", "output", "binary_sensor"]

fortune_button_ns = cg.esphome_ns.namespace("fortune_button")
FortuneButton = fortune_button_ns.class_("FortuneButton", cg.Component)

CONF_LIGHT = "light"
CONF_BUTTON = "button"
CONF_YES_PERCENT = "yes_percent"
CONF_SHARE_LIGHT = "share_light"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(FortuneButton),
        cv.Required(CONF_LIGHT): cv.use_id(light.AddressableLightState),
        cv.Required(CONF_OUTPUT): cv.use_id(LEDCOutput),
        cv.Required(CONF_BUTTON): cv.use_id(binary_sensor.BinarySensor),
        cv.Optional(CONF_YES_PERCENT, default=50): cv.int_range(min=0, max=100),
        cv.Optional(CONF_BRIGHTNESS, default="100%"): cv.All(cv.percentage, cv.Range(min=0.01)),
        cv.Optional(CONF_SHARE_LIGHT, default=False): cv.boolean,
        cv.Optional("on_start"): automation.validate_automation({}, single=True),
        cv.Optional("on_finish"): automation.validate_automation({}, single=True),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_light(await cg.get_variable(config[CONF_LIGHT])))
    cg.add(var.set_output(await cg.get_variable(config[CONF_OUTPUT])))
    cg.add(var.set_button(await cg.get_variable(config[CONF_BUTTON])))
    cg.add(var.set_yes_percent(config[CONF_YES_PERCENT]))
    cg.add(var.set_brightness(config[CONF_BRIGHTNESS]))
    cg.add(var.set_share_light(config[CONF_SHARE_LIGHT]))
    if "on_start" in config:
        await automation.build_automation(var.get_start_trigger(), [], config["on_start"])
    if "on_finish" in config:
        await automation.build_automation(var.get_finish_trigger(), [], config["on_finish"])
