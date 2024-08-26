import esphome.codegen as cg
from esphome.components.uart import (
    CONF_DATA_BITS,
    CONF_PARITY,
    CONF_STOP_BITS,
    UARTComponent,
)
from esphome.components.usb_host import register_usb_client, usb_device_schema
import esphome.config_validation as cv
from esphome.const import (
    CONF_BAUD_RATE,
    CONF_BUFFER_SIZE,
    CONF_CHANNELS,
    CONF_DEBUG,
    CONF_DUMMY_RECEIVER,
    CONF_ID,
)
from esphome.cpp_types import Component

# DEPENDENCIES = ["usb_host"]

usb_uart_ns = cg.esphome_ns.namespace("usb_uart")
USBUartComponent = usb_uart_ns.class_("USBUartComponent", Component)
USBUartChannel = usb_uart_ns.class_("USBUartChannel", UARTComponent)

AUTO_LOAD = ["uart", "usb_host"]

UARTParityOptions = usb_uart_ns.enum("UARTParityOptions")
UART_PARITY_OPTIONS = {
    "NONE": UARTParityOptions.UART_CONFIG_PARITY_NONE,
    "EVEN": UARTParityOptions.UART_CONFIG_PARITY_EVEN,
    "ODD": UARTParityOptions.UART_CONFIG_PARITY_ODD,
    "MARK": UARTParityOptions.UART_CONFIG_PARITY_MARK,
    "SPACE": UARTParityOptions.UART_CONFIG_PARITY_SPACE,
}

UARTStopBitsOptions = usb_uart_ns.enum("UARTStopBitsOptions")
UART_STOP_BITS_OPTIONS = {
    "1": UARTStopBitsOptions.UART_CONFIG_STOP_BITS_1,
    "1.5": UARTStopBitsOptions.UART_CONFIG_STOP_BITS_1_5,
    "2": UARTStopBitsOptions.UART_CONFIG_STOP_BITS_2,
}


class Type:
    def __init__(self, name, vid, pid, max_channels=1, cls=None):
        self.name = name
        cls = cls or name
        self.vid = vid
        self.pid = pid
        self.max_channels = max_channels
        self.cls = usb_uart_ns.class_(f"USBUartType{cls}", USBUartComponent)


uart_types = (
    Type("CH34X", 0x1A86, 0x55D5, 3, "CH34X"),
    Type("CH340", 0x1A86, 0x7523, 1, "CH34X"),
    Type("ESP_JTAG", 0x303A, 0x1001, 1, "CdcAcm"),
    Type("CP210X", 0x10C4, 0xEA60, 3, "CP210X"),
)


def max_length(length):
    def validator(value):
        if len(value) > length:
            raise cv.Invalid(f"Too many list entries: {len(value)} > {length}")
        return value

    return validator


def channel_schema(channels):
    return cv.Schema(
        {
            cv.Required(CONF_CHANNELS): cv.All(
                cv.ensure_list(
                    cv.Schema(
                        {
                            cv.GenerateID(): cv.declare_id(USBUartChannel),
                            cv.Optional(CONF_BUFFER_SIZE, default=256): cv.int_range(
                                min=64, max=8192
                            ),
                            cv.Required(CONF_BAUD_RATE): cv.int_range(
                                min=300, max=1000000
                            ),
                            cv.Optional(CONF_STOP_BITS, default="1"): cv.enum(
                                UART_STOP_BITS_OPTIONS, upper=True
                            ),
                            cv.Optional(CONF_PARITY, default="NONE"): cv.enum(
                                UART_PARITY_OPTIONS, upper=True
                            ),
                            cv.Optional(CONF_DATA_BITS, default=8): cv.int_range(
                                min=5, max=8
                            ),
                            cv.Optional(CONF_DUMMY_RECEIVER, default=False): cv.boolean,
                            cv.Optional(CONF_DEBUG, default=False): cv.boolean,
                        }
                    )
                ),
                max_length(channels),
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
        for index, channel in enumerate(device[CONF_CHANNELS]):
            chvar = cg.new_Pvariable(channel[CONF_ID], index, channel[CONF_BUFFER_SIZE])
            await cg.register_parented(chvar, var)
            cg.add(chvar.set_rx_buffer_size(channel[CONF_BUFFER_SIZE]))
            cg.add(chvar.set_stop_bits(channel[CONF_STOP_BITS]))
            cg.add(chvar.set_data_bits(channel[CONF_DATA_BITS]))
            cg.add(chvar.set_parity(channel[CONF_PARITY]))
            cg.add(chvar.set_baud_rate(channel[CONF_BAUD_RATE]))
            cg.add(var.add_channel(chvar))
            cg.add(var.set_dummy_receiver(channel[CONF_DUMMY_RECEIVER]))
            cg.add(var.set_debug(channel[CONF_DEBUG]))
            if channel[CONF_DEBUG]:
                cg.add_define("USE_UART_DEBUGGER")
