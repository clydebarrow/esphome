import esphome.codegen as cg
import esphome.config_validation as cv

from esphome.components import byte_bus
from esphome import pins

from esphome.const import (
    CONF_DATA_PINS,
    CONF_ID,
    CONF_DATA_RATE,
    CONF_CS_PIN,
    CONF_DC_PIN,
)

CODEOWNERS = ["@clydebarrow"]
AUTO_LOAD = ["byte_bus"]

i80_ns = cg.esphome_ns.namespace("i80")
I80Component = i80_ns.class_("I80Component", cg.Component)
I80ByteBus = i80_ns.class_("I80ByteBus", byte_bus.ByteBus)

CONF_RD_PIN = "rd_pin"
CONF_WR_PIN = "wr_pin"
CONF_I80_ID = "i80_id"


def validate_data_pins():
    pass


CONFIG_SCHEMA = cv.All(
    cv.ensure_list(
        cv.Schema(
            {
                cv.GenerateID(): cv.declare_id(I80Component),
                cv.Required(CONF_DATA_PINS): cv.All(
                    cv.ensure_list(pins.internal_gpio_output_pin_number),
                    cv.Length(min=8, max=16),
                ),
                cv.Required(CONF_WR_PIN): pins.internal_gpio_output_pin_schema,
                cv.Optional(CONF_RD_PIN): pins.internal_gpio_output_pin_schema,
                cv.Required(CONF_DC_PIN): pins.internal_gpio_output_pin_schema,
            },
            validate_data_pins,
        )
    ),
)


async def to_code(configs):
    cg.add_define("USE_I80")
    for conf in configs:
        wr = await cg.gpio_pin_expression(conf[CONF_WR_PIN])
        dc = await cg.gpio_pin_expression(conf[CONF_DC_PIN])

        var = cg.new_Pvariable(conf[CONF_ID], wr, dc, conf[CONF_DATA_PINS])
        await cg.register_component(var, conf)
        if rd := conf.get(CONF_RD_PIN):
            rd = await cg.gpio_pin_expression(rd)
            cg.add(var.set_rd_pin(rd))


def i80_client_schema(
    cs_pin_required=False,
    default_data_rate="2MHz",
):
    """Create a schema for an i80 device
    :param cs_pin_required: If true, make the CS_PIN required in the config.
    :param default_data_rate: Optional data_rate to use as default
    :return: The i80 client schema, `extend` this in your config schema.
    """
    schema = {
        cv.GenerateID(CONF_I80_ID): cv.use_id(I80Component),
        cv.Optional(CONF_DATA_RATE, default=default_data_rate): cv.frequency,
    }
    if cs_pin_required:
        schema[cv.Required(CONF_CS_PIN)] = pins.gpio_output_pin_schema
    else:
        schema[cv.Optional(CONF_CS_PIN)] = pins.gpio_output_pin_schema
    return cv.Schema(schema)


def final_validate_databus_schema(name: str, pin_count, config=None):
    data_pins = byte_bus.get_config_from_id(config[CONF_I80_ID])[CONF_DATA_PINS]
    if len(data_pins) != pin_count:
        i80_id = config[CONF_I80_ID].id
        bus_type = config["bus_type"]
        raise cv.Invalid(
            f"The {bus_type} bus type requires that the {i80_id} component has {pin_count} pins declared."
        )
    return config


@byte_bus.include_databus(
    "i80",
    bus_class=I80ByteBus,
    schema=i80_client_schema(),
    final_validate=final_validate_databus_schema,
    final_args={"pin_count": 8},
)
@byte_bus.include_databus(
    "par8",
    bus_class=I80ByteBus,
    schema=i80_client_schema(),
    final_validate=final_validate_databus_schema,
    final_args={"pin_count": 8},
)
@byte_bus.include_databus(
    "par16",
    bus_class=I80ByteBus,
    schema=i80_client_schema(),
    final_validate=final_validate_databus_schema,
    final_args={"pin_count": 16},
)
async def create_i80_databus(config, var, bustype):
    cg.add(var.set_parent(await cg.get_variable(config[CONF_I80_ID])))
    cg.add(var.set_data_rate(config[CONF_DATA_RATE]))
    if pin := config.get(CONF_CS_PIN):
        cg.add(var.set_cs_pin(await cg.gpio_pin_expression(pin)))

    return var
