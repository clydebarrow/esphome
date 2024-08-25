#pragma once

#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/components/uart/uart_component.h"
#include "esphome/components/usb_host/usb_host.h"

namespace esphome {
namespace usb_uart {
class USBUartTypeCdcAcm;
class USBUartComponent;

static const char *TAG = "usb_uart";

static constexpr uint8_t USB_CDC_SUBCLASS_ACM = 0x02;
static constexpr uint8_t USB_SUBCLASS_COMMON = 0x02;
static constexpr uint8_t USB_SUBCLASS_NULL = 0x00;
static constexpr uint8_t USB_PROTOCOL_NULL = 0x00;
static constexpr uint8_t USB_DEVICE_PROTOCOL_IAD = 0x01;
static constexpr uint8_t USB_VENDOR_IFC = usb_host::USB_TYPE_VENDOR | usb_host::USB_RECIP_INTERFACE;

typedef struct {
  const usb_ep_desc_t *notify_ep;
  const usb_ep_desc_t *in_ep;
  const usb_ep_desc_t *out_ep;
  const usb_intf_desc_t *intf;
} cdc_eps_t;

enum UARTParityOptions {
  UART_CONFIG_PARITY_NONE = 0,
  UART_CONFIG_PARITY_ODD,
  UART_CONFIG_PARITY_EVEN,
  UART_CONFIG_PARITY_MARK,
  UART_CONFIG_PARITY_SPACE,
};

enum UARTStopBitsOptions {
  UART_CONFIG_STOP_BITS_1 = 0,
  UART_CONFIG_STOP_BITS_1_5,
  UART_CONFIG_STOP_BITS_2,
};

static const char *const parity_names[] = {"NONE", "ODD", "EVEN", "MARK", "SPACE"};

class RingBuffer {
 public:
  RingBuffer(uint16_t buffer_size) : buffer_size(buffer_size), buffer_(new uint8_t[buffer_size]) {}
  bool is_empty() const { return this->read_pos_ == this->insert_pos_; }
  size_t get_available() const {
    return (this->insert_pos_ + this->buffer_size - this->read_pos_) % this->buffer_size;
  };
  size_t get_free_space() const { return this->buffer_size - 1 - this->get_available(); }
  uint8_t peek() const { return this->buffer_[this->read_pos_]; }
  void push(uint8_t item);
  void push(const uint8_t *data, size_t len);
  uint8_t pop();
  size_t pop(uint8_t *data, size_t len);

 protected:
  uint16_t insert_pos_ = 0;
  uint16_t read_pos_ = 0;
  uint16_t buffer_size{256};
  uint8_t *buffer_{};
};

class USBUartChannel : public uart::UARTComponent, public Parented<USBUartComponent> {
  friend class USBUartComponent;
  friend class USBUartTypeCdcAcm;
  friend class USBUartTypeCP210X;

 public:
  USBUartChannel(uint8_t index, uint16_t buffer_size)
      : index_(index), input_buffer_(RingBuffer(buffer_size)), output_buffer_(RingBuffer(buffer_size)) {}
  void write_array(const uint8_t *data, size_t len) override;
  ;
  bool peek_byte(uint8_t *data) override;
  ;
  bool read_array(uint8_t *data, size_t len) override;
  int available() override { return static_cast<int>(this->input_buffer_.get_available()); }
  void flush() override {}
  void check_logger_conflict() override {}
  void set_parity(UARTParityOptions parity) { this->parity_ = parity; }

 protected:
  const uint8_t index_;
  RingBuffer input_buffer_;
  RingBuffer output_buffer_;
  UARTParityOptions parity_{UART_CONFIG_PARITY_NONE};
  bool input_started_{true};
  bool output_started_{true};
  cdc_eps_t cdc_dev_{};
  bool initialised_{};
};

class USBUartComponent : public usb_host::USBClient {
 public:
  USBUartComponent(uint16_t vid, uint16_t pid) : usb_host::USBClient(vid, pid) {}
  void setup() override;
  void loop() override;
  void dump_config() override;
  std::vector<USBUartChannel *> get_channels() { return this->channels_; }

  void add_channel(USBUartChannel *channel) { this->channels_.push_back(channel); }

  void start_input(USBUartChannel *channel);
  void start_output(USBUartChannel *channel);
  void set_debug(bool debug) { this->debug_ = debug; }
  void set_dummy_receiver(bool dummy_receiver) { this->dummy_receiver_ = dummy_receiver; }

 protected:
  std::vector<USBUartChannel *> channels_{};
  bool debug_{};
  bool dummy_receiver_{};
};

class USBUartTypeCdcAcm : public USBUartComponent {
 public:
  USBUartTypeCdcAcm(uint16_t vid, uint16_t pid) : USBUartComponent(vid, pid) {}

 protected:
  virtual std::vector<cdc_eps_t> parse_descriptors_(usb_device_handle_t dev_hdl);
  void on_connected_() override;
  virtual void enable_channels_();
  void on_disconnected_() override;
};

class USBUartTypeCP210X : public USBUartTypeCdcAcm {
 public:
  USBUartTypeCP210X(uint16_t vid, uint16_t pid) : USBUartTypeCdcAcm(vid, pid) {}

 protected:
  std::vector<cdc_eps_t> parse_descriptors_(usb_device_handle_t dev_hdl) override;
  void enable_channels_() override;
};

}  // namespace usb_uart
}  // namespace esphome
