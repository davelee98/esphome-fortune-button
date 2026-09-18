"""PWM speech - tone contours (the sad trombone) out of one buzzer pin.

The name is historical: it also spoke words as 8-bit PCM until those were
removed because they did not work through the buzzer.
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import automation
from esphome.components import output
from esphome.const import CONF_ID, CONF_OUTPUT

CODEOWNERS = ["@fortune-button"]
DEPENDENCIES = ["output"]

pwm_speech_ns = cg.esphome_ns.namespace("pwm_speech")
PwmSpeech = pwm_speech_ns.class_("PwmSpeech", cg.Component)
PlayAction = pwm_speech_ns.class_("PlayAction", automation.Action)
StopAction = pwm_speech_ns.class_("StopAction", automation.Action)
IsPlayingCondition = pwm_speech_ns.class_("IsPlayingCondition", automation.Condition)

CONF_CONTOUR_LEVEL = "contour_level"
CONF_SOUND = "sound"

# Order must match CONTOURS[] in contour_data.h
CONTOURS = {
    "trombone": 0,
}

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(PwmSpeech),
        cv.Required(CONF_OUTPUT): cv.use_id(output.FloatOutput),
        # PWM duty at a contour's loudest step. 50% is the loudest square wave.
        cv.Optional(CONF_CONTOUR_LEVEL, default="50%"): cv.percentage,
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    out = await cg.get_variable(config[CONF_OUTPUT])
    cg.add(var.set_output(out))
    cg.add(var.set_contour_level(config[CONF_CONTOUR_LEVEL]))


@automation.register_action(
    "pwm_speech.play",
    PlayAction,
    cv.Schema(
        {
            cv.GenerateID(): cv.use_id(PwmSpeech),
            cv.Required(CONF_SOUND): cv.one_of(*CONTOURS, lower=True),
        }
    ),
    # Contours return at once and play from loop(), so play() is done when
    # it returns.
    synchronous=True,
)
async def play_action_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    cg.add(var.set_contour(CONTOURS[config[CONF_SOUND]]))
    return var


@automation.register_action(
    "pwm_speech.stop",
    StopAction,
    automation.maybe_simple_id({cv.GenerateID(): cv.use_id(PwmSpeech)}),
    synchronous=True,
)
async def stop_action_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    return var


@automation.register_condition(
    "pwm_speech.is_playing",
    IsPlayingCondition,
    automation.maybe_simple_id({cv.GenerateID(): cv.use_id(PwmSpeech)}),
)
async def is_playing_to_code(config, condition_id, template_arg, args):
    var = cg.new_Pvariable(condition_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    return var
