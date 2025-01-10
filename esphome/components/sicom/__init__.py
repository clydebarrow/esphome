import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import uart
from esphome.const import CONF_ID, CONF_TYPE, CONF_INDEX, CONF_DEVICE, CONF_ADDRESS

sicom_ns = cg.esphome_ns.namespace("sicom")
SicomComponent = sicom_ns.class_(
    "SicomComponent", cg.PollingComponent, uart.UARTDevice
)

SicomDevice = sicom_ns.class_("SicomDevice")

CONF_SICOM_ID = "sicom_id"
CONF_DEVICES = "devices"

SI_DEVICES = {
    "st107": sicom_ns.class_("ST107", SicomDevice),
    "sc301": sicom_ns.class_("SC301", SicomDevice),
    "sc303": sicom_ns.class_("SC303", SicomDevice),
}

SICOM_SENSOR_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_DEVICE): cv.use_id(SicomDevice),
        cv.Required(CONF_INDEX): cv.uint8_t,
    }
)


def device_schema(type):


CONFIG_SCHEMA = (
    cv.Schema({
        cv.GenerateID(): cv.declare_id(SicomComponent),
        cv.Required(CONF_DEVICES): cv.ensure_list(
            cv.typed_schema(
                {
                    name: cv.Schema(
                        {
                            cv.Required(CONF_ID): cv.declare_id(type),
                            cv.Required(CONF_ADDRESS): cv.uint8_t,
                        }
                    )
                    for name, type in SI_DEVICES.items()
                }
            )
        )
    })
    .extend(cv.polling_component_schema("5s"))
    .extend(uart.UART_DEVICE_SCHEMA)
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)
