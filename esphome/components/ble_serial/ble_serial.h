#pragma once

#ifdef USE_ESP32

#include "esphome/components/esp32_ble_client/ble_client_base.h"
#include "esphome/components/esp32_ble_tracker/esp32_ble_tracker.h"
#include "esphome/core/component.h"
#include "esphome/core/defines.h"
#include "esphome/core/helpers.h"

#include <array>
#include <atomic>

namespace esphome::ble_serial {

namespace espbt = esphome::esp32_ble_tracker;

// "AA:BB:CC:DD:EE:FF" and its terminator
static constexpr size_t ADDRESS_SIZE = MAC_ADDRESS_PRETTY_BUFFER_SIZE;
// Data for one device that the radio has not taken yet
static constexpr size_t TX_BUFFER_SIZE = 512;
// Devices reported in one scan
static constexpr size_t MAX_REPORTED = 16;

class BleSerial;

/// One connection to a device with a serial service: HM-10 (FFE0), Nordic UART or ISSC (BlueFly). The device's
/// address is set at run time; while it is open the client connects whenever the device advertises.
class BleSerialClient : public esp32_ble_client::BLEClientBase {
 public:
  explicit BleSerialClient(BleSerial *parent) : parent_(parent) {}

  void loop() override;
  bool gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                           esp_ble_gattc_cb_param_t *param) override;

  void open(uint64_t address);
  void close();
  uint64_t get_open_address() const { return this->open_address_; }
  bool is_ready() const { return this->ready_; }
  /// May be called from any task; returns the number of bytes taken. Data for a device that is not ready is
  /// dropped.
  size_t write(const uint8_t *data, size_t size);

 protected:
  void set_ready_(bool ready);
  /// Finds the serial service and asks for its notifications; false if the device has none.
  bool start_serial_();
  void send_();

  BleSerial *parent_;
  std::atomic<uint64_t> open_address_{0};
  uint16_t notify_handle_{0};
  uint16_t write_handle_{0};
  std::atomic<bool> ready_{false};
  bool notify_pending_{false};
  bool congested_{false};

  // Filled by write() on other tasks, drained by loop()
  Mutex tx_lock_;
  std::array<uint8_t, TX_BUFFER_SIZE> tx_{};
  size_t tx_start_{0};
  size_t tx_count_{0};
};

/// Bluetooth LE serial devices chosen at run time, for an application that keeps its own list of devices (XCSoar).
/// It reports the devices in range while a scan is requested, connects to the devices that are opened, and passes
/// their data on. The public methods may be called from any task: requests are carried out in the main loop.
class BleSerial : public Component, public espbt::ESPBTDeviceListener {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::AFTER_BLUETOOTH; }

  bool parse_device(const espbt::ESPBTDevice &device) override;

  void add_client(BleSerialClient *client) { this->clients_.push_back(client); }

  /// Reports the devices in range until stop_scan(), each once (again when its name becomes known).
  void start_scan() { this->queue_(CommandType::COMMAND_TYPE_START_SCAN, 0); }
  void stop_scan() { this->queue_(CommandType::COMMAND_TYPE_STOP_SCAN, 0); }
  /// Connects to the device at address ("AA:BB:CC:DD:EE:FF"), and again after it is lost, until close().
  void open(const char *address);
  void close(const char *address);
  size_t write(const char *address, const uint8_t *data, size_t size);

  /// Called with address and name (empty if unknown) for each device found by a scan.
  template<typename F> void add_on_device_callback(F &&callback) {
    this->device_callback_.add(std::forward<F>(callback));
  }
  /// Called with address and true once a device's serial service is ready, false when it is lost.
  template<typename F> void add_on_connection_callback(F &&callback) {
    this->connection_callback_.add(std::forward<F>(callback));
  }
  /// Called with address, data and size for the data a device sent.
  template<typename F> void add_on_data_callback(F &&callback) { this->data_callback_.add(std::forward<F>(callback)); }

  void client_ready(BleSerialClient *client, bool ready);
  void client_data(BleSerialClient *client, const uint8_t *data, size_t size);

 protected:
  enum class CommandType : uint8_t {
    COMMAND_TYPE_START_SCAN,
    COMMAND_TYPE_STOP_SCAN,
    COMMAND_TYPE_OPEN,
    COMMAND_TYPE_CLOSE,
  };
  struct Command {
    CommandType type;
    uint64_t address;
  };
  struct Reported {
    uint64_t address;
    bool named;
  };

  void queue_(CommandType type, uint64_t address);
  void run_(const Command &command);
  /// Scans while a scan is requested or an open device is not connected.
  void update_scan_();
  BleSerialClient *find_client_(uint64_t address);

  StaticVector<BleSerialClient *, BLE_SERIAL_MAX_DEVICES> clients_;
  StaticVector<Reported, MAX_REPORTED> reported_;
  bool scan_requested_{false};
  // The tracker may have started its own scan at boot; the first loop stops it if no scan is wanted
  bool scanning_{true};

  Mutex lock_;
  StaticVector<Command, 8> commands_;
  std::atomic<bool> has_commands_{false};

  CallbackManager<void(const char *, const char *)> device_callback_;
  CallbackManager<void(const char *, bool)> connection_callback_;
  CallbackManager<void(const char *, const uint8_t *, size_t)> data_callback_;
};

}  // namespace esphome::ble_serial

#endif  // USE_ESP32
