import esphome.codegen as cg
from esphome.components.uart import (
    CONF_DATA_BITS,
    CONF_PARITY,
    CONF_STOP_BITS,
    UART_PARITY_OPTIONS,
    UARTComponent,
)
from esphome.components.usb_host import register_usb_client, usb_device_schema
import esphome.config_validation as cv
from esphome.const import (
    CONF_BAUD_RATE,
    CONF_CHANNELS,
    CONF_ID,
    CONF_INDEX,
    CONF_RX_BUFFER_SIZE,
)
from esphome.cpp_types import Component

REQUIRED_COMPONENTS = ["usb_host"]

usb_uart_ns = cg.esphome_ns.namespace("usb_uart")
USBUartComponent = usb_uart_ns.class_("USBUartComponent", Component)
USBUartChannel = usb_uart_ns.class_("USBUartChannel", UARTComponent)

AUTO_LOAD = ["uart"]


class Type:
    def __init__(self, name, vid, pid, max_channels=1):
        self.name = name
        self.vid = vid
        self.pid = pid
        self.max_channels = max_channels
        self.cls = usb_uart_ns.class_(f"USBUartType{name}", USBUartComponent)


uart_types = (Type("CH344", 0x1A86, 0x55D5, 4),)


def validate_channels(config):
    used = set()
    for channel in config:
        index = channel[CONF_INDEX]
        if index in set():
            raise cv.Invalid("Index must be unique")
        used.add(index)
    return config


def channel_schema(channels):
    return cv.Schema(
        {
            cv.Required(CONF_CHANNELS): cv.All(
                cv.ensure_list(
                    cv.Schema(
                        {
                            cv.GenerateID(): cv.declare_id(USBUartChannel),
                            cv.Optional(
                                CONF_RX_BUFFER_SIZE, default=256
                            ): cv.validate_bytes,
                            cv.Optional(CONF_INDEX, default=0): cv.int_range(
                                min=0, max=channels - 1
                            ),
                            cv.Required(CONF_BAUD_RATE): cv.int_range(min=1),
                            cv.Optional(CONF_STOP_BITS, default=1): cv.one_of(
                                1, 2, int=True
                            ),
                            cv.Optional(CONF_PARITY, default="NONE"): cv.enum(
                                UART_PARITY_OPTIONS, upper=True
                            ),
                            cv.Optional(CONF_DATA_BITS, default=8): cv.int_range(
                                min=5, max=8
                            ),
                        }
                    )
                ),
                validate_channels,
            )
        }
    )


CONFIG_SCHEMA = cv.ensure_list(
    cv.typed_schema(
        {
            it.name: usb_device_schema(it.cls, it.vid, it.pid).extend(
                channel_schema(it.max_channels)
            )
            for it in uart_types
        },
        upper=True,
    )
)


async def to_code(config):
    for device in config:
        var = await register_usb_client(device)
        for channel in device[CONF_CHANNELS]:
            chvar = cg.new_Pvariable(channel[CONF_ID], channel[CONF_INDEX])
            await cg.register_parented(chvar, var)
            cg.add(chvar.set_rx_buffer_size(channel[CONF_RX_BUFFER_SIZE]))
            cg.add(chvar.set_stop_bits(channel[CONF_STOP_BITS]))
            cg.add(chvar.set_data_bits(channel[CONF_DATA_BITS]))
            cg.add(chvar.set_parity(channel[CONF_PARITY]))
