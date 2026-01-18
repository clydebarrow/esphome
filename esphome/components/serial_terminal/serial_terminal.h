#pragma once

#include "esphome/core/component.h"
#include "esphome/core/defines.h"
#include "esphome/components/uart/uart.h"

#ifdef USE_ESP32
#include <esp_http_server.h>
#include "esphome/components/web_server_idf/web_server_idf.h"
#elif defined(USE_ESP8266)
#include "esphome/components/web_server_base/web_server_base.h"
#endif

#include <string>
#include <vector>
#include <mutex>

namespace esphome {
namespace serial_terminal {

#ifdef USE_ESP32
/**
 * SerialTerminal provides WebSocket-based serial communication for ESP32 platforms.
 * 
 * This component bridges UART serial ports with WebSocket endpoints, enabling
 * web-based serial terminal access. It supports:
 * - Bidirectional data flow (UART ↔ WebSocket)
 * - Multiple concurrent WebSocket clients
 * - Configurable serial port parameters
 * - Thread-safe operation
 */
class SerialTerminal : public Component, public web_server_idf::AsyncWebHandler {
 public:
  SerialTerminal() = default;

  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::LATE; }

  void set_uart(uart::UARTComponent *uart) { this->uart_ = uart; }
  void set_path(const std::string &path) { this->path_ = path; }
  void set_server(httpd_handle_t server) { this->server_ = server; }

  // AsyncWebHandler interface
  bool canHandle(web_server_idf::AsyncWebServerRequest *request) const override;
  void handleRequest(web_server_idf::AsyncWebServerRequest *request) override;

 protected:
  /**
   * WebSocket frame handler - called by ESP-IDF when WebSocket frames arrive.
   * This is invoked from the HTTP server task, not the main loop task.
   * 
   * @param req ESP-IDF HTTP request handle
   * @return ESP_OK on success, error code otherwise
   */
  static esp_err_t ws_handler(httpd_req_t *req);

  /**
   * Sends data from UART to all connected WebSocket clients.
   * Must be called from the main loop task for thread safety.
   */
  void send_uart_to_websocket_();

  /**
   * Processes queued WebSocket messages and writes them to UART.
   * Must be called from the main loop task for thread safety.
   */
  void process_websocket_to_uart_();

  /**
   * Sends data to a specific WebSocket client.
   * 
   * @param fd Socket file descriptor
   * @param data Data to send
   * @param len Length of data
   */
  void send_to_client_(int fd, const uint8_t *data, size_t len);

  /**
   * Removes a client from the active clients list.
   * 
   * @param fd Socket file descriptor to remove
   */
  void remove_client_(int fd);

  uart::UARTComponent *uart_{nullptr};
  httpd_handle_t server_{nullptr};
  std::string path_{"/serial"};

  // Thread-safe client management
  std::mutex clients_mutex_;
  std::vector<int> clients_;  // Active WebSocket client file descriptors

  // Thread-safe message queue for WebSocket -> UART
  std::mutex tx_queue_mutex_;
  std::vector<uint8_t> tx_queue_;  // Data received from WebSocket, waiting to be sent to UART

  // Buffer for UART -> WebSocket
  static constexpr size_t RX_BUFFER_SIZE = 512;
  uint8_t rx_buffer_[RX_BUFFER_SIZE];
};

#elif defined(USE_ESP8266)
/**
 * SerialTerminal stub for ESP8266 platforms.
 * 
 * WebSocket support on ESP8266 is limited due to memory constraints.
 * This stub provides the component interface but does not implement functionality.
 */
class SerialTerminal : public Component {
 public:
  SerialTerminal() = default;

  void setup() override {}
  void loop() override {}
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::LATE; }

  void set_uart(uart::UARTComponent *uart) { this->uart_ = uart; }
  void set_path(const std::string &path) { this->path_ = path; }
  void set_server(void *server) {}  // Not used on ESP8266

 protected:
  uart::UARTComponent *uart_{nullptr};
  std::string path_{"/serial"};
};
#endif

}  // namespace serial_terminal
}  // namespace esphome
