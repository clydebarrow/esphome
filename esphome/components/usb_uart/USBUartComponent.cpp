#include "USBUartComponent.h"
#include "usb/usb_host.h"
#include "esphome/core/log.h"
#include "esphome/components/uart/uart_debugger.h"

#include <cinttypes>

namespace esphome {
namespace usb_uart {

static optional<cdc_eps_t> get_cdc(const usb_config_desc_t *config_desc, uint8_t intf_idx) {
  int conf_offset, ep_offset;
  auto intf_desc = usb_parse_interface_descriptor(config_desc, intf_idx, 0, &conf_offset);
  if (!intf_desc) {
    ESP_LOGE(TAG, "usb_parse_interface_descriptor failed");
    return nullopt;
  }
  if (intf_desc->bNumEndpoints != 1 || intf_desc->bInterfaceClass != USB_CLASS_COMM) {
    ESP_LOGE(TAG, "num endpoints == %u, bInterfaceClass == %u", intf_desc->bNumEndpoints, intf_desc->bInterfaceClass);
    return nullopt;
  }
  ep_offset = conf_offset;
  auto notify_ep = usb_parse_endpoint_descriptor_by_index(intf_desc, 0, config_desc->wTotalLength, &ep_offset);
  if (!notify_ep) {
    ESP_LOGE(TAG, "notify_ep: usb_parse_endpoint_descriptor_by_index failed");
    return nullopt;
  }
  auto data_desc = usb_parse_interface_descriptor(config_desc, intf_idx + 1, 0, &conf_offset);
  if (!data_desc) {
    ESP_LOGE(TAG, "data_desc: usb_parse_interface_descriptor failed");
    return nullopt;
  }
  if (data_desc->bInterfaceClass != USB_CLASS_CDC_DATA || data_desc->bNumEndpoints != 2) {
    ESP_LOGE(TAG, "data_desc: bInterfaceClass == %u, bInterfaceSubClass == %u, bNumEndpoints == %u",
             data_desc->bInterfaceClass, data_desc->bInterfaceSubClass, data_desc->bNumEndpoints);
    return nullopt;
  }
  ep_offset = conf_offset;
  auto out_ep = usb_parse_endpoint_descriptor_by_index(data_desc, 0, config_desc->wTotalLength, &ep_offset);
  if (!out_ep) {
    ESP_LOGE(TAG, "out_ep: usb_parse_endpoint_descriptor_by_index failed");
    return nullopt;
  }
  ep_offset = conf_offset;
  auto in_ep = usb_parse_endpoint_descriptor_by_index(data_desc, 1, config_desc->wTotalLength, &ep_offset);
  if (!in_ep) {
    ESP_LOGE(TAG, "in_ep: usb_parse_endpoint_descriptor_by_index failed");
    return nullopt;
  }
  if (in_ep->bEndpointAddress & usb_host::USB_DIR_IN)
    return cdc_eps_t{notify_ep, in_ep, out_ep, data_desc};
  return cdc_eps_t{notify_ep, out_ep, in_ep, data_desc};
}

std::vector<cdc_eps_t> USBUartTypeCdcAcm::parse_descriptors_(usb_device_handle_t dev_hdl) {
  const usb_config_desc_t *config_desc;
  const usb_device_desc_t *device_desc;
  int desc_offset = 0;
  std::vector<cdc_eps_t> cdc_devs{};

  // Get required descriptors
  if (usb_host_get_device_descriptor(dev_hdl, &device_desc) != ESP_OK) {
    ESP_LOGE(TAG, "get_device_descriptor failed");
    return {};
  }
  if (usb_host_get_active_config_descriptor(dev_hdl, &config_desc) != ESP_OK) {
    ESP_LOGE(TAG, "get_active_config_descriptor failed");
    return {};
  }
  if (device_desc->bDeviceClass == USB_CLASS_COMM && device_desc->bDeviceSubClass == USB_CDC_SUBCLASS_ACM) {
    ESP_LOGV(TAG, "Device is single CDC-ACM device");
    // single CDC-ACM device
    auto eps = get_cdc(config_desc, 0);
    if (eps)
      cdc_devs.push_back(*eps);
    return cdc_devs;
  }
  if (((device_desc->bDeviceClass == USB_CLASS_MISC) && (device_desc->bDeviceSubClass == USB_SUBCLASS_COMMON) &&
       (device_desc->bDeviceProtocol == USB_DEVICE_PROTOCOL_IAD)) ||
      ((device_desc->bDeviceClass == USB_CLASS_PER_INTERFACE) && (device_desc->bDeviceSubClass == USB_SUBCLASS_NULL) &&
       (device_desc->bDeviceProtocol == USB_PROTOCOL_NULL))) {
    // This is a composite device, that uses Interface Association Descriptor
    const auto *this_desc = reinterpret_cast<const usb_standard_desc_t *>(config_desc);
    for (;;) {
      this_desc = usb_parse_next_descriptor_of_type(this_desc, config_desc->wTotalLength,
                                                    USB_B_DESCRIPTOR_TYPE_INTERFACE_ASSOCIATION, &desc_offset);
      if (!this_desc)
        break;
      const auto *iad_desc = reinterpret_cast<const usb_iad_desc_t *>(this_desc);

      if (iad_desc->bFunctionClass == USB_CLASS_COMM && iad_desc->bFunctionSubClass == USB_CDC_SUBCLASS_ACM) {
        ESP_LOGV(TAG, "Found CDC-ACM device in composite device");
        auto eps = get_cdc(config_desc, iad_desc->bFirstInterface);
        if (eps)
          cdc_devs.push_back(*eps);
      }
    }
  }
  return cdc_devs;
}

void RingBuffer::push(uint8_t item) {
  this->buffer_[this->insert_pos_] = item;
  this->insert_pos_ = (this->insert_pos_ + 1) % this->buffer_size;
}
void RingBuffer::push(const uint8_t *data, size_t len) {
  for (size_t i = 0; i != len; i++) {
    this->buffer_[this->insert_pos_] = *data++;
    this->insert_pos_ = (this->insert_pos_ + 1) % this->buffer_size;
  }
}

uint8_t RingBuffer::pop() {
  uint8_t item = this->buffer_[this->read_pos_];
  this->read_pos_ = (this->read_pos_ + 1) % this->buffer_size;
  return item;
}
size_t RingBuffer::pop(uint8_t *data, size_t len) {
  len = std::min(len, this->get_available());
  for (size_t i = 0; i != len; i++) {
    *data++ = this->buffer_[this->read_pos_];
    this->read_pos_ = (this->read_pos_ + 1) % this->buffer_size;
  }
  return len;
}
void USBUartChannel::write_array(const uint8_t *data, size_t len) {
  if (!this->initialised_) {
    ESP_LOGW(TAG, "Channel not initialised - write ignored");
    return;
  }
  while (this->output_buffer_.get_free_space() != 0 && len-- != 0) {
    this->output_buffer_.push(*data++);
  }
  len++;
  if (len > 0) {
    ESP_LOGE(TAG, "Buffer full - failed to write %d bytes", len);
  }
  this->parent_->start_output(this);
}

bool USBUartChannel::peek_byte(uint8_t *data) {
  if (this->input_buffer_.is_empty()) {
    return false;
  }
  *data = this->input_buffer_.peek();
  return true;
}
bool USBUartChannel::read_array(uint8_t *data, size_t len) {
  if (!this->initialised_) {
    ESP_LOGW(TAG, "Channel not initialised - read ignored");
    return false;
  }
  auto available = this->available();
  bool status = true;
  if (len > available) {
    ESP_LOGD(TAG, "underflow: requested %zu but returned %d, bytes", len, available);
    len = available;
    status = false;
  }
  for (size_t i = 0; i != len; i++) {
    *data++ = this->input_buffer_.pop();
  }
  if (this->input_buffer_.get_free_space() == 0) {
    this->parent_->start_input(this);
  }
  return status;
}
void USBUartComponent::setup() { USBClient::setup(); }
void USBUartComponent::loop() { USBClient::loop(); }
void USBUartComponent::dump_config() {
  USBClient::dump_config();
  for (auto &channel : this->channels_) {
    ESP_LOGCONFIG(TAG, "  UART Channel %d", channel->index_);
    ESP_LOGCONFIG(TAG, "    Baud Rate: %" PRIu32 " baud", channel->baud_rate_);
    ESP_LOGCONFIG(TAG, "    Data Bits: %u", channel->data_bits_);
    ESP_LOGCONFIG(TAG, "    Parity: %s", parity_names[channel->parity_]);
    ESP_LOGCONFIG(TAG, "    Stop bits: %u", channel->stop_bits_);
  }
}
void USBUartTypeCdcAcm::on_disconnected_() {}

void USBUartComponent::start_input(USBUartChannel *channel) {
  if (!channel->initialised_ || channel->input_started_)
    return;
  auto ep = channel->cdc_dev_.in_ep;
  auto callback = [=](const usb_host::transfer_status_t &status) {
    ESP_LOGD(TAG, "Transfer result: length: %u; status %X", status.data_len, status.error_code);
    if (this->debug_) {
      uart::UARTDebug::log_hex(uart::UART_DIRECTION_RX,
                               std::vector<uint8_t>(status.data, status.data + status.data_len), ',');  // NOLINT()
    }
    channel->input_started_ = false;
    if (status.data_len != 0 && !this->dummy_receiver_) {
      for (size_t i = 0; i != status.data_len; i++) {
        channel->input_buffer_.push(status.data[i]);
      }
    }
    if (channel->input_buffer_.get_free_space() >= channel->cdc_dev_.in_ep->wMaxPacketSize) {
      this->defer([=] { this->start_input(channel); });
    }
  };
  channel->input_started_ = true;
  this->transfer_in(ep->bEndpointAddress, callback, ep->wMaxPacketSize);
}

void USBUartComponent::start_output(USBUartChannel *channel) {
  if (channel->output_started_)
    return;
  if (channel->output_buffer_.is_empty()) {
    return;
  }
  auto ep = channel->cdc_dev_.out_ep;
  auto callback = [=](const usb_host::transfer_status_t &status) {
    ESP_LOGD(TAG, "Output Transfer result: length: %u; status %X", status.data_len, status.error_code);
    channel->output_started_ = false;
    this->defer([=] { this->start_output(channel); });
  };
  channel->output_started_ = true;
  uint8_t data[ep->wMaxPacketSize];
  auto len = channel->output_buffer_.pop(data, ep->wMaxPacketSize);
  this->transfer_out(ep->bEndpointAddress, callback, data, len);
  if (this->debug_) {
    uart::UARTDebug::log_hex(uart::UART_DIRECTION_TX, std::vector<uint8_t>(data, data + len), ',');  // NOLINT()
  }
  ESP_LOGD(TAG, "Output %d bytes started", len);
}
void USBUartTypeCdcAcm::on_connected_() {
  ESP_LOGD(TAG, "on_connected");
  auto cdc_devs = this->parse_descriptors_(this->device_handle_);
  if (cdc_devs.empty()) {
    this->status_set_error("No CDC-ACM device found");
    this->mark_failed();
    this->disconnect_();
    return;
  }
  ESP_LOGD(TAG, "Found %u CDC-ACM devices", cdc_devs.size());
  auto i = 0;
  for (auto channel : this->channels_) {
    if (i == cdc_devs.size()) {
      ESP_LOGE(TAG, "No configuration found for channel %d", channel->index_);
      this->status_set_warning("No configuration found for channel");
      break;
    }
    channel->cdc_dev_ = cdc_devs[i++];
    channel->initialised_ = true;
    auto err =
        usb_host_interface_claim(this->handle_, this->device_handle_, channel->cdc_dev_.intf->bInterfaceNumber, 0);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "usb_host_interface_claim failed: %x", err);
      this->status_set_error("usb_host_interface_claim failed");
      this->mark_failed();
      this->disconnect_();
      return;
    }
  }
  this->enable_channels_();
}

void USBUartTypeCdcAcm::enable_channels_() {
  for (auto channel : this->channels_) {
    if (!channel->initialised_)
      continue;
    channel->input_started_ = false;
    channel->output_started_ = false;
    this->start_input(channel);
  }
}

}  // namespace usb_uart
}  // namespace esphome