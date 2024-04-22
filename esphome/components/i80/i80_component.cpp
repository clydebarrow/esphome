#include "i80_component.h"
#include "i80_esp_idf.cpp"
#ifdef USE_I80

namespace esphome {
namespace i80 {
void I80Component::dump_config() {
  ESP_LOGCONFIG(TAG, "I80 bus:");
  LOG_PIN("  WR Pin: ", this->wr_pin_)
  LOG_PIN("  DC Pin: ", this->dc_pin_)
  for (unsigned i = 0; i != this->data_pins_.size(); i++) {
    ESP_LOGCONFIG(TAG, "  Data pin %u: GPIO%d", i, this->data_pins_[i]);
  }
}

void I80Component::setup() {
  auto *bus = new I80BusIdf(this->wr_pin_, this->dc_pin_, this->data_pins_);  // NOLINT
  // write pin is unused, but pulled high (inactive) if present
  if (this->rd_pin_ != nullptr) {
    this->rd_pin_->setup();
    this->rd_pin_->digital_write(true);
  }
  if (bus->is_failed())
    this->mark_failed();
  this->bus_ = bus;
}
I80Delegate *I80Component::register_device(I80ByteBus *device, GPIOPin *cs_pin, unsigned int data_rate) {
  if (this->devices_.count(device) != 0) {
    ESP_LOGE(TAG, "i80 device already registered");
    return this->devices_[device];
  }
  auto *delegate = this->bus_->get_delegate(cs_pin, data_rate);  // NOLINT
  this->devices_[device] = delegate;
  return delegate;
}
void I80Component::unregister_device(I80ByteBus *device) {
  if (this->devices_.count(device) == 0) {
    esph_log_e(TAG, "i80 device not registered");
    return;
  }
  delete this->devices_[device];  // NOLINT
  this->devices_.erase(device);
}

void I80ByteBus::dump_config() {
  ESP_LOGCONFIG(TAG, "  Data rate: %dMHz", (unsigned) (this->data_rate_ / 1000000));
  LOG_PIN("  CS Pin: ", this->cs_);
}

}  // namespace i80
}  // namespace esphome

#endif  // USE_I80
