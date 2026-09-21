"""ESPHome component enabling ESP-IDF automatic light sleep (COM-214 option 3)."""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

CODEOWNERS = ["@rcompton78"]
DEPENDENCIES = ["esp32"]

light_sleep_ns = cg.esphome_ns.namespace("light_sleep")
LightSleepComponent = light_sleep_ns.class_("LightSleepComponent", cg.Component)

CONF_WAKEUP_PIN = "wakeup_pin"

# A plain pin number rather than a full GPIO pin schema: this talks to the
# ESP-IDF GPIO/sleep APIs directly rather than through ESPHome's own GPIOPin
# abstraction, so it deliberately sits outside ESPHome's pin-reuse tracking --
# the same physical pin (GPIO17, the FT6336U's INT line) is also declared as
# an ext1 wakeup source under boards/freenove-s3.yaml's dormant deep_sleep_1
# (COM-214 option 1), and a full pin schema here would collide with that.
CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(LightSleepComponent),
        cv.Required(CONF_WAKEUP_PIN): cv.int_range(min=0, max=48),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_wakeup_pin(config[CONF_WAKEUP_PIN]))
    cg.add_global(cg.RawStatement('#include "esphome/components/light_sleep/light_sleep_component.h"'))
