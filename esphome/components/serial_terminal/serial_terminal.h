#pragma once

#include "esphome/core/component.h"
#include "esphome/core/defines.h"
#include "esphome/components/uart/uart.h"

#ifdef USE_ESP32
#include <esp_http_server.h>
#include "esphome/components/web_server_idf/web_server_idf.h"
#include "esphome/core/helpers.h"
#elif defined(USE_ESP8266)
#include "esphome/components/web_server_base/web_server_base.h"
#endif

#include <string>
#include <vector>
#include <atomic>
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
  SerialTerminal(uart::UARTComponent *uart, const std::string &path, httpd_handle_t server, size_t buffer_size)
      : uart_(uart), path_(path), server_(server), tx_buffer_size_(buffer_size) {}

  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::LATE; }

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

  uart::UARTComponent *uart_;
  httpd_handle_t server_;
  std::string path_;
  size_t tx_buffer_size_;

  // Thread-safe client management
  // Using atomic count to quickly check if we have clients without locking
  std::atomic<size_t> clients_count_{0};
  std::mutex clients_mutex_;
  std::vector<int> clients_;  // Active WebSocket client file descriptors

  // Message queue for WebSocket -> UART (single producer from WebSocket handler, single consumer in loop)
  std::mutex tx_queue_mutex_;
  FixedVector<uint8_t> tx_queue_;

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
  SerialTerminal(uart::UARTComponent *uart, const std::string &path) : uart_(uart), path_(path) {}

  void setup() override {}
  void loop() override {}
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::LATE; }

 protected:
  uart::UARTComponent *uart_;
  std::string path_;
};
#endif

}  // namespace serial_terminal
}  // namespace esphome
