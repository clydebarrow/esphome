# Serial Terminal Component

WebSocket-based serial terminal component for ESPHome web server.

## Overview

The `serial_terminal` component provides WebSocket endpoints for accessing UART serial ports through the web interface. This enables real-time bidirectional communication between web clients and hardware serial ports on ESP32 devices.

## Features

- **WebSocket Communication**: Real-time data exchange using WebSocket protocol
- **Multiple Serial Ports**: Support for configuring multiple UART ports with different WebSocket endpoints
- **Bidirectional Data Flow**: Send and receive data between web clients and serial hardware
- **Multi-Client Support**: Multiple WebSocket clients can connect simultaneously
- **Thread-Safe**: Uses mutexes to ensure safe concurrent access
- **Configurable Parameters**: Customize baud rate, data bits, parity, and stop bits via UART configuration

## Platform Support

- **ESP32**: Full WebSocket support using ESP-IDF framework
- **ESP8266**: Stub implementation (WebSocket not supported due to memory constraints)

## Configuration

### Dependencies

This component requires:
- `web_server_base`: Web server infrastructure
- `uart`: UART component for serial communication

### Basic Example

```yaml
# Enable web server base
web_server_base:
  id: my_web_server

# Configure UART
uart:
  - id: uart_1
    tx_pin: GPIO1
    rx_pin: GPIO3
    baud_rate: 115200

# Configure serial terminal
serial_terminal:
  web_server_base_id: my_web_server
  serial_terminals:
    - id: serial_term_1
      uart_id: uart_1
      path: "/serial"
```

### Multiple Serial Ports

```yaml
uart:
  - id: uart_1
    tx_pin: GPIO1
    rx_pin: GPIO3
    baud_rate: 115200
  
  - id: uart_2
    tx_pin: GPIO17
    rx_pin: GPIO16
    baud_rate: 9600

serial_terminal:
  web_server_base_id: my_web_server
  serial_terminals:
    - id: serial_term_1
      uart_id: uart_1
      path: "/serial1"
    
    - id: serial_term_2
      uart_id: uart_2
      path: "/serial2"
```

## Configuration Variables

### Main Component

- **web_server_base_id** (**Required**, ID): The ID of the web_server_base component
- **serial_terminals** (*Optional*, list): List of serial terminal configurations

### Serial Terminal Configuration

- **id** (**Required**, ID): Unique identifier for this serial terminal
- **uart_id** (**Required**, ID): The ID of the UART component to use
- **path** (*Optional*, string): WebSocket endpoint path. Defaults to `/serial`

## WebSocket Protocol

### Connecting

Connect to the WebSocket endpoint using the configured path:

```javascript
const ws = new WebSocket('ws://device-ip:port/serial');

ws.onopen = () => {
  console.log('Connected to serial terminal');
};

ws.onmessage = (event) => {
  console.log('Received:', event.data);
};

ws.onerror = (error) => {
  console.error('WebSocket error:', error);
};
```

### Sending Data

Send text data to the serial port:

```javascript
ws.send('Hello, serial port!');
```

### Receiving Data

Data received from the serial port is sent as WebSocket text frames:

```javascript
ws.onmessage = (event) => {
  const data = event.data;
  // Process serial data
};
```

## Implementation Details

### ESP32 (ESP-IDF)

- Uses ESP-IDF's native WebSocket support (`httpd_ws_*` APIs)
- Registers WebSocket handler during `setup()`
- Processes UART data in the main `loop()`
- Thread-safe with mutex-protected client list and message queue

### Data Flow

1. **UART → WebSocket**: 
   - `loop()` reads available UART data
   - Data is broadcast to all connected WebSocket clients
   - Failed sends automatically remove disconnected clients

2. **WebSocket → UART**:
   - WebSocket frames are queued in a thread-safe buffer
   - `loop()` processes the queue and writes to UART
   - Supports both TEXT and BINARY WebSocket frames

### Thread Safety

- **Client Management**: Mutex-protected client list
- **Message Queue**: Mutex-protected queue for WebSocket → UART data
- **UART Access**: All UART operations occur in the main loop task

## Limitations

- **ESP8266**: WebSocket not implemented due to memory and library constraints
- **Buffer Size**: UART → WebSocket buffer is 512 bytes per iteration
- **Frame Size**: Limited by ESP-IDF's httpd configuration (typically 4KB)

## See Also

- [Web Server Component](https://esphome.io/components/web_server.html)
- [UART Bus](https://esphome.io/components/uart.html)
- [ESP32](https://esphome.io/components/esp32.html)
