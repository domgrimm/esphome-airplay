import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import media_player
from esphome.const import CONF_ID, CONF_NAME

DEPENDENCIES = ["network"]

CONF_TARGETS = "targets"
CONF_MEDIA_PLAYER = "media_player"
CONF_SPEAKER = "speaker"
CONF_PORT_BASE = "port_base"
CONF_MEDIA_URL_TEMPLATE = "media_url_template"
CONF_OUTPUT_SAMPLE_RATE = "output_sample_rate"

airplay_bridge_ns = cg.esphome_ns.namespace("airplay_bridge")
AirPlayBridge = airplay_bridge_ns.class_("AirPlayBridge", cg.Component)

TARGET_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_MEDIA_PLAYER): cv.use_id(media_player.MediaPlayer),
        cv.Optional(CONF_SPEAKER): cv.use_id(cg.Component),
        cv.Optional(CONF_NAME): cv.string_strict,
    }
)

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(AirPlayBridge),
            cv.Required(CONF_TARGETS): cv.All(cv.ensure_list(TARGET_SCHEMA), cv.Length(min=1)),
            cv.Optional(CONF_PORT_BASE, default=7000): cv.port,
            cv.Optional(CONF_MEDIA_URL_TEMPLATE, default=""): cv.string_strict,
            cv.Optional(CONF_OUTPUT_SAMPLE_RATE, default=16000): cv.positive_int,
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    cg.add(var.set_port_base(config[CONF_PORT_BASE]))
    cg.add(var.set_media_url_template(config[CONF_MEDIA_URL_TEMPLATE]))
    cg.add(var.set_output_sample_rate(config[CONF_OUTPUT_SAMPLE_RATE]))

    for target in config[CONF_TARGETS]:
        player = await cg.get_variable(target[CONF_MEDIA_PLAYER])
        target_name = target.get(CONF_NAME, "")
        if CONF_SPEAKER in target:
            speaker = await cg.get_variable(target[CONF_SPEAKER])
            cg.add(var.add_target(player, target_name, speaker))
        else:
            cg.add(var.add_target(player, target_name, cg.RawExpression("nullptr")))
