#include "tracker_identity.h"

#include <cstring>

#include "esphome/components/logger/logger.h"
#include "esphome/core/application.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

#ifdef USE_ZEPHYR
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/storage/flash_map.h>

#include "identity_page.h"
#elif defined(USE_LOGGER_UART_SELECTION_USB_SERIAL_JTAG)
#include <driver/usb_serial_jtag.h>
#elif defined(USE_ESP32)
#include <driver/uart.h>
#endif

namespace esphome::tracker_identity {

ESPHOME_LOG_TAG(TAG, "tracker_identity");
static constexpr size_t MAX_LINE = 160;

#ifdef USE_ZEPHYR
// The logger's USB port, where the tracker page asks
static const device *const CDC_UART = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(cdc_acm_uart0));

// Written by the set-up UF2 from the tracker page, which an update UF2 leaves alone
static bool load_identity_page(Identity &identity) {
  const flash_area *area;
  if (flash_area_open(FIXED_PARTITION_ID(identity_partition), &area) != 0)
    return false;
  uint8_t page[IDENTITY_PAGE_SIZE];
  const bool read = flash_area_read(area, 0, page, sizeof(page)) == 0;
  flash_area_close(area);
  return read && parse_identity_page(page, identity);
}
#endif

void TrackerIdentity::setup() {
#ifdef USE_ZEPHYR
  this->set_ = load_identity_page(this->identity_);
#else
  this->pref_ = global_preferences->make_preference<Stored>(fnv1_hash("tracker_identity"), true);
  Stored stored{};
  this->set_ = this->pref_.load(&stored) && stored.magic == MAGIC && stored.identity.address != 0;
  if (this->set_)
    this->identity_ = stored.identity;
#endif
  this->apply_();
}

// Gives the identity to the layers and components that use it; without one the tracker sends nothing
void TrackerIdentity::apply_() {
  if (this->set_) {
    this->addressing_->set_address(this->identity_.address);
    this->encryption_->set_key(this->identity_.key);
  }
#ifdef USE_TRACKER_IDENTITY_GLIDER_TRACKER
  if (this->tracker_ != nullptr)
    this->tracker_->set_identified(this->set_);
#endif
#ifdef USE_TRACKER_IDENTITY_FLARM
  if (this->flarm_ != nullptr && this->set_)
    this->flarm_->set_registration(this->identity_.registration);
#endif
#ifdef USE_TRACKER_IDENTITY_BLE
  if (this->ble_ != nullptr && this->set_) {
    // A Bluetooth name is at most 20 characters
    snprintf(this->ble_name_, sizeof(this->ble_name_), "%.14s FLARM", this->identity_.registration);
    this->ble_->set_name(this->ble_name_);
  }
#endif
}

int TrackerIdentity::read_(uint8_t *buffer, size_t size) {
#ifdef USE_ZEPHYR
  if (!device_is_ready(CDC_UART))
    return 0;
  int count = 0;
  while (size_t(count) < size && uart_poll_in(CDC_UART, &buffer[count]) == 0)
    count++;
  return count;
#elif defined(USE_LOGGER_UART_SELECTION_USB_SERIAL_JTAG)
  return usb_serial_jtag_read_bytes(buffer, size, 0);
#elif defined(USE_ESP32)
  return uart_read_bytes(logger::global_logger->get_uart_num(), buffer, size, 0);
#else
  return 0;
#endif
}

void TrackerIdentity::answer_(const std::string &text) {
#ifdef USE_ZEPHYR
  if (!device_is_ready(CDC_UART))
    return;
  for (char c : text)
    uart_poll_out(CDC_UART, static_cast<unsigned char>(c));
#elif defined(USE_LOGGER_UART_SELECTION_USB_SERIAL_JTAG)
  usb_serial_jtag_write_bytes(text.data(), text.size(), pdMS_TO_TICKS(50));
#elif defined(USE_ESP32)
  uart_write_bytes(logger::global_logger->get_uart_num(), text.data(), text.size());
#endif
}

void TrackerIdentity::loop() {
  uint8_t buffer[64];
  const int count = this->read_(buffer, sizeof(buffer));
  for (int i = 0; i < count; i++) {
    const char c = char(buffer[i]);
    if (c == '\r' || c == '\n') {
      if (!this->line_.empty())
        this->handle_line_(this->line_);
      this->line_.clear();
    } else if (this->line_.size() < MAX_LINE) {
      this->line_ += c;
    }
  }
}

void TrackerIdentity::handle_line_(const std::string &line) {
  if (line == "ID") {
    char mac[MAC_ADDRESS_PRETTY_BUFFER_SIZE];
    get_mac_address_pretty_into_buffer(mac);
    this->answer_(
        id_line(App.get_name().c_str(), uint32_t(App.get_build_time()), this->set_ ? &this->identity_ : nullptr, mac));
    return;
  }
  if (line.rfind("SET ", 0) != 0)
    return;
#ifdef USE_ZEPHYR
  this->answer_("SET ERR set up from the tracker page, which writes the identity into the firmware file\r\n");
#else
  Identity identity;
  if (const char *error = parse_set(line.substr(4), identity)) {
    this->answer_(std::string("SET ERR ") + error + "\r\n");
    return;
  }
  Stored stored{MAGIC, identity};
  if (!this->pref_.save(&stored) || !global_preferences->sync()) {
    this->answer_("SET ERR the identity could not be saved\r\n");
    return;
  }
  ESP_LOGI(TAG, "Identity set: %s at 0x%04X; restarting to use it", identity.registration, identity.address);
  this->answer_("SET OK\r\n");
  // Bluetooth takes its name only as it starts, so the whole identity is applied by a restart
  this->set_timeout(500, []() { App.safe_reboot(); });
#endif
}

void TrackerIdentity::dump_config() {
  if (this->set_) {
    ESP_LOGCONFIG(TAG, "Tracker identity: %s at 0x%04X", this->identity_.registration, this->identity_.address);
  } else {
    ESP_LOGCONFIG(TAG, "Tracker identity: not set, so nothing is sent on LoRa");
  }
}

}  // namespace esphome::tracker_identity
