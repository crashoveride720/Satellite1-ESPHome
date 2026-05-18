import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

import esphome.components.text as text_comp
from esphome.components.http_request import (
    CONF_HTTP_REQUEST_ID,
    HttpRequestComponent,
)

CODEOWNERS = ["@FutureProofHomes"]
DEPENDENCIES = ["network", "http_request", "micro_wake_word"]
AUTO_LOAD = ["text"]

CONF_MICRO_WAKE_WORD_ID = "micro_wake_word_id"
CONF_MANIFEST_URL_ENTITY = "manifest_url_entity"

cww_loader_ns = cg.esphome_ns.namespace("custom_wake_word_loader")
CustomWakeWordLoader = cww_loader_ns.class_("CustomWakeWordLoader", cg.Component)
ManifestUrlText = cww_loader_ns.class_("ManifestUrlText", text_comp.Text)

mww_ns = cg.esphome_ns.namespace("micro_wake_word")
MicroWakeWord = mww_ns.class_("MicroWakeWord", cg.Component)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(CustomWakeWordLoader),
        cv.Required(CONF_MICRO_WAKE_WORD_ID): cv.use_id(MicroWakeWord),
        cv.GenerateID(CONF_HTTP_REQUEST_ID): cv.use_id(HttpRequestComponent),
        cv.Required(CONF_MANIFEST_URL_ENTITY): text_comp.text_schema(
            ManifestUrlText,
            entity_category="config",
            mode="text",
        ),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    mww = await cg.get_variable(config[CONF_MICRO_WAKE_WORD_ID])
    cg.add(var.set_micro_wake_word(mww))

    http = await cg.get_variable(config[CONF_HTTP_REQUEST_ID])
    cg.add(var.set_http_request(http))

    entity_conf = config[CONF_MANIFEST_URL_ENTITY]
    text_var = await text_comp.new_text(entity_conf)
    cg.add(var.set_text_entity(text_var))
