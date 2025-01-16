#pragma once

#include "esphome/core/component.h"

#include "esphome/components/usb_host/usb_host.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/bytebuffer/bytebuffer.h"
#ifdef USE_SWITCH
#include "esphome/components/switch/switch.h"
#endif

namespace esphome {
namespace sicom {

using namespace bytebuffer;

typedef struct {
  const usb_ep_desc_t *in_ep;
  const usb_ep_desc_t *out_ep;
  uint8_t interface_number;
} sicom_eps_t;

class SicomComponent;

class SicomDevice : public PollingComponent, Parented<SicomComponent> {
 public:
  SicomDevice(size_t voltage_cnt, size_t resistance_cnt, size_t current_cnt, size_t relay_cnt);

  void add_voltage_sensor(sensor::Sensor *sensor, size_t index) {
    if (index < this->voltage_sensors_.size()) {
      this->voltage_sensors_[index] = sensor;
    }
  }

  void add_resistance_sensor(sensor::Sensor *sensor, size_t index) {
    if (index < this->resistance_sensors_.size()) {
      this->resistance_sensors_[index] = sensor;
    }
  }
  void add_current_sensor(sensor::Sensor *sensor, size_t index) {
    if (index < this->current_sensors_.size()) {
      this->current_sensors_[index] = sensor;
    }
  }

  void update() override;
  virtual bool decode(ByteBuffer &data) = 0;

  float get_voltage(size_t index) const;
  float get_resistance(size_t index) const;
  float get_current(size_t index) const;
  bool get_relay(size_t index);

  size_t get_relay_count() { return relays_.size(); }
  size_t get_voltage_count() { return voltages_.size(); }
  size_t get_resistance_count() { return resistances_.size(); }
  size_t get_current_count() { return currents_.size(); }

  void set_address(uint8_t address) { address_ = address; }

 protected:
  uint8_t address_{};
  std::vector<float> voltages_{};
  std::vector<float> resistances_{};
  std::vector<float> currents_{};
  std::vector<bool> relays_{};
  std::vector<sensor::Sensor *> voltage_sensors_{};
  std::vector<sensor::Sensor *> resistance_sensors_{};
  std::vector<sensor::Sensor *> current_sensors_{};
#ifdef USE_SWITCH
  std::vector<switch_::Switch *> relay_sensors_{};
#endif  // USE_SWITCH
};

class SicomST107Device : public SicomDevice {
 public:
  SicomST107Device() : SicomDevice(3, 4, 0, 1){};
  bool decode(ByteBuffer &data) override;
};

class SicomComponent : public usb_host::USBClient {
 public:
  SicomComponent(uint16_t vid, uint16_t pid) : USBClient(vid, pid){};
  void setup() override;
  void loop() override;
  void process_data_(ByteBuffer &buffer);
  void process_byte_(uint8_t byte);
  void dump_config() override;

  void add_device(SicomDevice *device) { this->devices_.push_back(device); }

 protected:
  void on_connected_() override;
  void on_disconnected_() override;
  static optional<sicom_eps_t> parse_descriptors_(usb_device_handle_t dev_hdl);
  void start_input_();
  bool input_started_{};
  sicom_eps_t eps_{};
  std::vector<SicomDevice *> devices_{};
};

}  // namespace sicom
}  // namespace esphome
