#include "ble_serial.h"

#ifdef USE_ESP32

#include "esphome/core/application.h"
#include "esphome/core/log.h"

#include <cstring>

namespace esphome::ble_serial {

ESPHOME_LOG_TAG(TAG, "ble_serial");

// Chunks sent per loop, so a long write does not fill the stack's queue
static constexpr size_t MAX_CHUNKS_PER_LOOP = 4;
static constexpr uint16_t ATT_HEADER_SIZE = 3;

namespace {

struct SerialService {
  espbt::ESPBTUUID service;
  espbt::ESPBTUUID notify;  // data from the device
  espbt::ESPBTUUID write;   // data to the device
};

// The services XCSoar's Android BleSerialPort uses
const std::array<SerialService, 3> &serial_services() {
  static const std::array<SerialService, 3> SERVICES{{
      // HM-10: one characteristic both ways
      {espbt::ESPBTUUID::from_uint16(0xFFE0), espbt::ESPBTUUID::from_uint16(0xFFE1),
       espbt::ESPBTUUID::from_uint16(0xFFE1)},
      // Nordic UART
      {espbt::ESPBTUUID::from_raw("6E400001-B5A3-F393-E0A9-E50E24DCCA9E"),
       espbt::ESPBTUUID::from_raw("6E400003-B5A3-F393-E0A9-E50E24DCCA9E"),
       espbt::ESPBTUUID::from_raw("6E400002-B5A3-F393-E0A9-E50E24DCCA9E")},
      // ISSC (Microchip) transparent UART, as in BlueFly varios
      {espbt::ESPBTUUID::from_raw("49535343-FE7D-4AE5-8FA9-9FAFD205E455"),
       espbt::ESPBTUUID::from_raw("49535343-1E4D-4BD9-BA61-23C647249616"),
       espbt::ESPBTUUID::from_raw("49535343-8841-43F4-A8D4-ECBE34729BB3")},
  }};
  return SERVICES;
}

bool is_serial_device(const espbt::ESPBTDevice &device) {
  for (const auto &uuid : device.get_service_uuids()) {
    for (const auto &service : serial_services()) {
      if (uuid == service.service)
        return true;
    }
  }
  // BlueFly varios do not always advertise their service
  auto name = device.get_name();
  return name.size() >= 7 && strncasecmp(name.c_str(), "BlueFly", 7) == 0;
}

void format_address(uint64_t address, char *buf) {
  uint8_t mac[6];
  for (size_t i = 0; i != 6; i++)
    mac[i] = address >> (40 - 8 * i);
  format_mac_addr_upper(mac, buf);
}

// "AA:BB:CC:DD:EE:FF" to the form of ESPBTDevice::address_uint64(); 0 if malformed
uint64_t parse_address(const char *text) {
  if (strlen(text) != ADDRESS_SIZE - 1)
    return 0;
  uint64_t address = 0;
  for (size_t i = 0; i != 6; i++) {
    uint8_t byte;
    if ((i != 5 && text[i * 3 + 2] != ':') || !parse_hex(text + i * 3, &byte, 1))
      return 0;
    address = (address << 8) | byte;
  }
  return address;
}

}  // namespace

void BleSerialClient::loop() {
  BLEClientBase::loop();
  if (this->ready_ && !this->congested_)
    this->send_();
}

void BleSerialClient::open(uint64_t address) {
  this->open_address_ = address;
  this->set_address(address);
  this->set_auto_connect(true);
}

void BleSerialClient::close() {
  this->open_address_ = 0;
  this->set_auto_connect(false);
  this->set_ready_(false);
  {
    LockGuard guard(this->tx_lock_);
    this->tx_count_ = 0;
  }
  switch (this->state()) {
    case espbt::ClientState::DISCOVERED:
    case espbt::ClientState::CONNECTING:
    case espbt::ClientState::CONNECTED:
    case espbt::ClientState::ESTABLISHED:
      this->disconnect();
      break;
    default:
      this->set_address(0);
      break;
  }
}

size_t BleSerialClient::write(const uint8_t *data, size_t size) {
  if (!this->ready_)
    return size;
  LockGuard guard(this->tx_lock_);
  const size_t n = std::min(size, TX_BUFFER_SIZE - this->tx_count_);
  for (size_t i = 0; i != n; i++)
    this->tx_[(this->tx_start_ + this->tx_count_ + i) % TX_BUFFER_SIZE] = data[i];
  this->tx_count_ += n;
  return n;
}

void BleSerialClient::send_() {
  const size_t chunk_size = std::max<int>(this->mtu_ - ATT_HEADER_SIZE, 20);
  for (size_t chunks = 0; chunks != MAX_CHUNKS_PER_LOOP; chunks++) {
    uint8_t chunk[TX_BUFFER_SIZE];
    size_t n;
    {
      LockGuard guard(this->tx_lock_);
      n = std::min(this->tx_count_, chunk_size);
      if (n == 0)
        return;
      for (size_t i = 0; i != n; i++)
        chunk[i] = this->tx_[(this->tx_start_ + i) % TX_BUFFER_SIZE];
    }
    esp_err_t err = esp_ble_gattc_write_char(this->gattc_if_, this->conn_id_, this->write_handle_, n, chunk,
                                             ESP_GATT_WRITE_TYPE_NO_RSP, ESP_GATT_AUTH_REQ_NONE);
    if (err != ESP_OK) {
      // Kept for the next loop
      ESP_LOGV(TAG, "[%s] Write failed: %d", this->address_str(), err);
      return;
    }
    LockGuard guard(this->tx_lock_);
    this->tx_start_ = (this->tx_start_ + n) % TX_BUFFER_SIZE;
    this->tx_count_ -= n;
  }
}

bool BleSerialClient::start_serial_() {
  for (const auto &service : serial_services()) {
    auto *notify = this->get_characteristic(service.service, service.notify);
    auto *write = this->get_characteristic(service.service, service.write);
    if (notify == nullptr || write == nullptr)
      continue;
    this->notify_handle_ = notify->handle;
    this->write_handle_ = write->handle;
    esp_err_t err = this->register_for_notify(this->notify_handle_);
    if (err != ESP_OK) {
      ESP_LOGW(TAG, "[%s] Notification request failed: %d", this->address_str(), err);
      return false;
    }
    this->notify_pending_ = true;
    return true;
  }
  ESP_LOGW(TAG, "[%s] No serial service", this->address_str());
  return false;
}

void BleSerialClient::set_ready_(bool ready) {
  if (this->ready_.exchange(ready) != ready)
    this->parent_->client_ready(this, ready);
}

bool BleSerialClient::gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                                          esp_ble_gattc_cb_param_t *param) {
  if (!BLEClientBase::gattc_event_handler(event, gattc_if, param))
    return false;

  switch (event) {
    case ESP_GATTC_SEARCH_CMPL_EVT:
      if (!this->start_serial_())
        this->disconnect();
      break;
    case ESP_GATTC_REG_FOR_NOTIFY_EVT:
      // The event has no connection id: match it by the request this client has outstanding
      if (this->notify_pending_ && param->reg_for_notify.handle == this->notify_handle_) {
        this->notify_pending_ = false;
        if (param->reg_for_notify.status == ESP_GATT_OK) {
          this->congested_ = false;
          this->set_ready_(true);
        } else {
          ESP_LOGW(TAG, "[%s] Notification request failed: %d", this->address_str(), param->reg_for_notify.status);
          this->disconnect();
        }
      }
      break;
    case ESP_GATTC_NOTIFY_EVT:
      if (param->notify.handle == this->notify_handle_ && this->ready_)
        this->parent_->client_data(this, param->notify.value, param->notify.value_len);
      break;
    case ESP_GATTC_CONGEST_EVT:
      if (param->congest.conn_id == this->conn_id_)
        this->congested_ = param->congest.congested;
      break;
    case ESP_GATTC_DISCONNECT_EVT:
    case ESP_GATTC_CLOSE_EVT:
      this->notify_pending_ = false;
      this->set_ready_(false);
      break;
    default:
      break;
  }
  return true;
}

void BleSerial::setup() {
  // The scan runs only when wanted (see update_scan_())
  espbt::global_esp32_ble_tracker->set_scan_continuous(false);
}

void BleSerial::loop() {
  if (this->has_commands_) {
    StaticVector<Command, 8> commands;
    {
      LockGuard guard(this->lock_);
      commands = this->commands_;
      this->commands_.clear();
      this->has_commands_ = false;
    }
    for (const auto &command : commands)
      this->run_(command);
  }
  this->update_scan_();
}

void BleSerial::dump_config() {
  ESP_LOGCONFIG(TAG,
                "BLE serial:\n"
                "  Devices: %u",
                static_cast<unsigned>(this->clients_.size()));
}

void BleSerial::queue_(CommandType type, uint64_t address) {
  {
    LockGuard guard(this->lock_);
    if (this->commands_.size() == this->commands_.capacity()) {
      ESP_LOGW(TAG, "Request dropped");
      return;
    }
    this->commands_.push_back({type, address});
    this->has_commands_ = true;
  }
  App.wake_loop_threadsafe();
}

void BleSerial::open(const char *address) {
  const uint64_t value = parse_address(address);
  if (value == 0) {
    ESP_LOGW(TAG, "Not an address: %s", address);
    return;
  }
  this->queue_(CommandType::COMMAND_TYPE_OPEN, value);
}

void BleSerial::close(const char *address) {
  const uint64_t value = parse_address(address);
  if (value != 0)
    this->queue_(CommandType::COMMAND_TYPE_CLOSE, value);
}

size_t BleSerial::write(const char *address, const uint8_t *data, size_t size) {
  const uint64_t value = parse_address(address);
  for (auto *client : this->clients_) {
    if (value != 0 && client->get_open_address() == value)
      return client->write(data, size);
  }
  return size;
}

BleSerialClient *BleSerial::find_client_(uint64_t address) {
  for (auto *client : this->clients_) {
    if (client->get_open_address() == address)
      return client;
  }
  return nullptr;
}

void BleSerial::run_(const Command &command) {
  char text[ADDRESS_SIZE];
  format_address(command.address, text);
  switch (command.type) {
    case CommandType::COMMAND_TYPE_START_SCAN:
      this->scan_requested_ = true;
      this->reported_.clear();
      break;
    case CommandType::COMMAND_TYPE_STOP_SCAN:
      this->scan_requested_ = false;
      break;
    case CommandType::COMMAND_TYPE_OPEN: {
      if (this->find_client_(command.address) != nullptr)
        break;
      auto *client = this->find_client_(0);
      if (client == nullptr) {
        ESP_LOGW(TAG, "No free connection for %s", text);
        break;
      }
      ESP_LOGI(TAG, "Opening %s", text);
      client->open(command.address);
      break;
    }
    case CommandType::COMMAND_TYPE_CLOSE:
      if (auto *client = this->find_client_(command.address); client != nullptr) {
        ESP_LOGI(TAG, "Closing %s", text);
        client->close();
      }
      break;
  }
}

void BleSerial::update_scan_() {
  bool want = this->scan_requested_;
  for (auto *client : this->clients_) {
    if (client->get_open_address() != 0 && !client->is_ready())
      want = true;
  }
  if (want == this->scanning_)
    return;
  this->scanning_ = want;
  auto *tracker = espbt::global_esp32_ble_tracker;
  if (want) {
    ESP_LOGD(TAG, "Scan on");
    tracker->set_scan_continuous(true);
    tracker->start_scan_if_idle();
  } else {
    ESP_LOGD(TAG, "Scan off");
    tracker->stop_scan();
  }
}

bool BleSerial::parse_device(const espbt::ESPBTDevice &device) {
  if (!this->scan_requested_ || !is_serial_device(device))
    return false;
  const uint64_t address = device.address_uint64();
  const bool named = !device.get_name().empty();
  Reported *entry = nullptr;
  for (auto &reported : this->reported_) {
    if (reported.address == address)
      entry = &reported;
  }
  if (entry != nullptr && (entry->named || !named))
    return false;
  if (entry == nullptr) {
    if (this->reported_.size() == this->reported_.capacity())
      return false;
    this->reported_.push_back({address, named});
  } else {
    entry->named = true;
  }
  char text[ADDRESS_SIZE];
  device.address_str_to(text);
  ESP_LOGD(TAG, "Found %s '%s' RSSI %d", text, device.get_name().c_str(), device.get_rssi());
  this->device_callback_.call(text, device.get_name().c_str());
  return false;
}

void BleSerial::client_ready(BleSerialClient *client, bool ready) {
  char text[ADDRESS_SIZE];
  format_address(client->get_address(), text);
  ESP_LOGI(TAG, "%s %s", text, ready ? LOG_STR_LITERAL("connected") : LOG_STR_LITERAL("lost"));
  this->connection_callback_.call(text, ready);
}

void BleSerial::client_data(BleSerialClient *client, const uint8_t *data, size_t size) {
  char text[ADDRESS_SIZE];
  format_address(client->get_address(), text);
  this->data_callback_.call(text, data, size);
}

}  // namespace esphome::ble_serial

#endif  // USE_ESP32
