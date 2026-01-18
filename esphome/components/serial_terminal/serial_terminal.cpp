#include "serial_terminal.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"

#ifdef USE_ESP32
#include <esp_http_server.h>
#include <cstring>
#endif

namespace esphome {
namespace serial_terminal {

static const char *const TAG = "serial_terminal";

#ifdef USE_ESP32

void SerialTerminal::setup() {
  if (this->server_ == nullptr) {
    ESP_LOGE(TAG, "HTTP server not initialized");
    this->mark_failed();
    return;
  }

  if (this->uart_ == nullptr) {
    ESP_LOGE(TAG, "UART not configured");
    this->mark_failed();
    return;
  }

  // Initialize tx_queue_ with configured buffer size
  this->tx_queue_.reserve(this->tx_buffer_size_);

  // Register WebSocket handler with ESP-IDF HTTP server
  const httpd_uri_t ws_handler_config = {
      .uri = this->path_.c_str(),
      .method = HTTP_GET,
      .handler = SerialTerminal::ws_handler,
      .user_ctx = this,
      .is_websocket = true,
      .handle_ws_control_frames = true,
  };

  esp_err_t err = httpd_register_uri_handler(this->server_, &ws_handler_config);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to register WebSocket handler: %s", esp_err_to_name(err));
    this->mark_failed();
    return;
  }

  ESP_LOGI(TAG, "Serial terminal WebSocket handler registered at %s", this->path_.c_str());
}

void SerialTerminal::loop() {
  // Send any available UART data to WebSocket clients
  this->send_uart_to_websocket_();

  // Process queued WebSocket data to UART
  this->process_websocket_to_uart_();
}

void SerialTerminal::dump_config() {
  ESP_LOGCONFIG(TAG, "Serial Terminal:");
  ESP_LOGCONFIG(TAG, "  Path: %s", this->path_.c_str());
  ESP_LOGCONFIG(TAG, "  Buffer Size: %zu", this->tx_buffer_size_);
  ESP_LOGCONFIG(TAG, "  UART: %p", (void *) this->uart_);
  if (this->uart_ != nullptr) {
    ESP_LOGCONFIG(TAG, "  Baud Rate: %" PRIu32, this->uart_->get_baud_rate());
    ESP_LOGCONFIG(TAG, "  Data Bits: %u", this->uart_->get_data_bits());
    ESP_LOGCONFIG(TAG, "  Stop Bits: %u", this->uart_->get_stop_bits());
    ESP_LOGCONFIG(TAG, "  Parity: %s", LOG_STR_ARG(uart::parity_to_str(this->uart_->get_parity())));
  }
}

bool SerialTerminal::canHandle(web_server_idf::AsyncWebServerRequest *request) const {
  // WebSocket upgrade requests are handled by ESP-IDF directly,
  // so we don't need to handle them through the AsyncWebHandler interface
  return false;
}

void SerialTerminal::handleRequest(web_server_idf::AsyncWebServerRequest *request) {
  // Not used - WebSocket requests are handled by ESP-IDF's ws_handler
}

esp_err_t SerialTerminal::ws_handler(httpd_req_t *req) {
  auto *self = static_cast<SerialTerminal *>(req->user_ctx);
  
  if (req->method == HTTP_GET) {
    // This is the initial WebSocket handshake
    ESP_LOGI(TAG, "WebSocket handshake on fd %d", httpd_req_to_sockfd(req));
    return ESP_OK;
  }

  // Handle WebSocket frames
  httpd_ws_frame_t ws_pkt;
  std::memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
  
  // First call to get frame length
  ws_pkt.type = HTTPD_WS_TYPE_TEXT;
  esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "httpd_ws_recv_frame failed to get frame len with %d", ret);
    return ret;
  }

  int fd = httpd_req_to_sockfd(req);

  // Handle different frame types
  if (ws_pkt.type == HTTPD_WS_TYPE_CLOSE) {
    ESP_LOGI(TAG, "WebSocket close frame received on fd %d", fd);
    self->remove_client_(fd);
    return ESP_OK;
  }

  if (ws_pkt.type == HTTPD_WS_TYPE_PING) {
    ESP_LOGD(TAG, "WebSocket ping frame received on fd %d", fd);
    // ESP-IDF handles PING/PONG automatically when handle_ws_control_frames is true
    return ESP_OK;
  }

  if (ws_pkt.type == HTTPD_WS_TYPE_PONG) {
    ESP_LOGD(TAG, "WebSocket pong frame received on fd %d", fd);
    return ESP_OK;
  }

  // Handle data frames (TEXT or BINARY)
  if (ws_pkt.len > 0) {
    // Use stack buffer for small frames, heap for large ones
    constexpr size_t STACK_BUF_SIZE = 256;
    uint8_t stack_buf[STACK_BUF_SIZE];
    std::vector<uint8_t> heap_buf;
    uint8_t *buf;
    
    if (ws_pkt.len <= STACK_BUF_SIZE) {
      buf = stack_buf;
    } else {
      heap_buf.resize(ws_pkt.len);
      buf = heap_buf.data();
    }
    
    ws_pkt.payload = buf;
    
    ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
    if (ret != ESP_OK) {
      ESP_LOGE(TAG, "httpd_ws_recv_frame failed with %d", ret);
      return ret;
    }

    ESP_LOGD(TAG, "Received %d bytes from WebSocket on fd %d", ws_pkt.len, fd);

    // Add client to active list if not already there
    {
      std::lock_guard<std::mutex> lock(self->clients_mutex_);
      if (std::find(self->clients_.begin(), self->clients_.end(), fd) == self->clients_.end()) {
        self->clients_.push_back(fd);
        self->clients_count_.store(self->clients_.size(), std::memory_order_release);
        ESP_LOGI(TAG, "Added WebSocket client fd %d, total clients: %d", fd, self->clients_.size());
      }
    }

    // Queue data to be sent to UART in the main loop
    {
      std::lock_guard<std::mutex> lock(self->tx_queue_mutex_);
      // Check if we have space
      if (self->tx_queue_.size() + ws_pkt.len <= self->tx_buffer_size_) {
        self->tx_queue_.insert(self->tx_queue_.end(), buf, buf + ws_pkt.len);
      } else {
        ESP_LOGW(TAG, "TX queue full, dropping %d bytes", ws_pkt.len);
      }
    }
  }

  return ESP_OK;
}

void SerialTerminal::send_uart_to_websocket_() {
  if (this->uart_ == nullptr) {
    return;
  }

  // Quick check without lock if we have any clients
  if (this->clients_count_.load(std::memory_order_acquire) == 0) {
    return;
  }

  // Read available data from UART
  int available = this->uart_->available();
  if (available <= 0) {
    return;
  }

  size_t to_read = std::min(static_cast<size_t>(available), RX_BUFFER_SIZE);
  if (!this->uart_->read_array(this->rx_buffer_, to_read)) {
    return;
  }

  ESP_LOGD(TAG, "Read %d bytes from UART, sending to WebSocket clients", to_read);

  // Send to all connected clients
  std::lock_guard<std::mutex> lock(this->clients_mutex_);
  auto it = this->clients_.begin();
  while (it != this->clients_.end()) {
    int fd = *it;
    
    httpd_ws_frame_t ws_pkt;
    std::memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;
    ws_pkt.payload = this->rx_buffer_;
    ws_pkt.len = to_read;

    esp_err_t ret = httpd_ws_send_frame_async(this->server_, fd, &ws_pkt);
    if (ret != ESP_OK) {
      ESP_LOGW(TAG, "Failed to send to WebSocket client fd %d: %s, removing client", fd, esp_err_to_name(ret));
      it = this->clients_.erase(it);
      this->clients_count_.store(this->clients_.size(), std::memory_order_release);
    } else {
      ++it;
    }
  }
}

void SerialTerminal::process_websocket_to_uart_() {
  if (this->uart_ == nullptr) {
    return;
  }

  FixedVector<uint8_t> data_to_send;
  
  // Get queued data
  {
    std::lock_guard<std::mutex> lock(this->tx_queue_mutex_);
    if (this->tx_queue_.empty()) {
      return;
    }
    data_to_send = std::move(this->tx_queue_);
    this->tx_queue_.clear();
  }

  // Send to UART
  ESP_LOGD(TAG, "Sending %d bytes from WebSocket to UART", data_to_send.size());
  this->uart_->write_array(data_to_send.data(), data_to_send.size());
  this->uart_->flush();
}

void SerialTerminal::send_to_client_(int fd, const uint8_t *data, size_t len) {
  httpd_ws_frame_t ws_pkt;
  std::memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
  ws_pkt.type = HTTPD_WS_TYPE_TEXT;
  ws_pkt.payload = const_cast<uint8_t *>(data);
  ws_pkt.len = len;

  esp_err_t ret = httpd_ws_send_frame_async(this->server_, fd, &ws_pkt);
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "Failed to send to WebSocket client fd %d: %s", fd, esp_err_to_name(ret));
  }
}

void SerialTerminal::remove_client_(int fd) {
  std::lock_guard<std::mutex> lock(this->clients_mutex_);
  auto it = std::find(this->clients_.begin(), this->clients_.end(), fd);
  if (it != this->clients_.end()) {
    this->clients_.erase(it);
    this->clients_count_.store(this->clients_.size(), std::memory_order_release);
    ESP_LOGI(TAG, "Removed WebSocket client fd %d, remaining clients: %d", fd, this->clients_.size());
  }
}

#elif defined(USE_ESP8266)

void SerialTerminal::dump_config() {
  ESP_LOGCONFIG(TAG, "Serial Terminal:");
  ESP_LOGCONFIG(TAG, "  Platform: ESP8266 (WebSocket not supported)");
  ESP_LOGCONFIG(TAG, "  Path: %s", this->path_.c_str());
}

#endif

}  // namespace serial_terminal
}  // namespace esphome
