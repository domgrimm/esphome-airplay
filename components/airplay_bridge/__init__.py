import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import media_player
from esphome.const import CONF_ID, CONF_NAME

DEPENDENCIES = ["network"]

CONF_TARGETS = "targets"
CONF_MEDIA_PLAYER = "media_player"
CONF_PORT_BASE = "port_base"
CONF_MEDIA_URL_TEMPLATE = "media_url_template"

airplay_bridge_ns = cg.esphome_ns.namespace("airplay_bridge")
AirPlayBridge = airplay_bridge_ns.class_("AirPlayBridge", cg.Component)

TARGET_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_MEDIA_PLAYER): cv.use_id(media_player.MediaPlayer),
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
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    cg.add(var.set_port_base(config[CONF_PORT_BASE]))
    cg.add(var.set_media_url_template(config[CONF_MEDIA_URL_TEMPLATE]))

    for target in config[CONF_TARGETS]:
        player = await cg.get_variable(target[CONF_MEDIA_PLAYER])
        target_name = target.get(CONF_NAME, "")
        cg.add(var.add_target(player, target_name))
