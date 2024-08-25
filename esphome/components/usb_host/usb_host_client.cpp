#include "usb_host.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include "esphome/core/bytebuffer.h"

#include <cinttypes>
#include <cstring>
namespace esphome {
namespace usb_host {
static std::string get_descriptor_string(const usb_str_desc_t *desc) {
  char buffer[256];
  char *p = buffer;
  for (size_t i = 0; i != desc->bLength / 2; i++) {
    auto c = desc->wData[i];
    if (c < 0x100)
      *p++ = static_cast<char>(c);
  }
  *p = '\0';
  return {buffer};
}

static void client_event_cb(const usb_host_client_event_msg_t *event_msg, void *ptr) {
  auto *client = static_cast<USBClient *>(ptr);
  switch (event_msg->event) {
    case USB_HOST_CLIENT_EVENT_NEW_DEV: {
      auto addr = event_msg->new_dev.address;
      ESP_LOGD(TAG, "New device %d", event_msg->new_dev.address);
      client->on_opened(addr);
      break;
    }
    case USB_HOST_CLIENT_EVENT_DEV_GONE: {
      client->on_closed(event_msg->dev_gone.dev_hdl);
      break;
    }
    default:
      ESP_LOGD(TAG, "Unknown event %d", event_msg->event);
      break;
  }
}
void USBClient::setup() {
  usb_host_client_config_t config{.is_synchronous = false,
                                  .max_num_event_msg = 5,
                                  .async = {.client_event_callback = client_event_cb, .callback_arg = this}};
  auto err = usb_host_client_register(&config, &this->handle_);
  if (err != ESP_OK) {
    ESP_LOGD(TAG, "client register failed: %s", esp_err_to_name(err));
    this->status_set_error("Client register failed");
    this->mark_failed();
    return;
  }
  ESP_LOGD(TAG, "client setup complete");
  for (auto trq : this->trq_pool_) {
    usb_host_transfer_alloc(64, 0, &trq->transfer);
    trq->client = this;
  }
}

void USBClient::loop() {
  switch (this->state_) {
    case USB_CLIENT_OPEN: {
      int err;
      ESP_LOGD(TAG, "Open device %d", this->device_addr_);
      if ((err = usb_host_device_open(this->handle_, this->device_addr_, &this->device_handle_)) != ESP_OK) {
        ESP_LOGD(TAG, "Device open failed: %s", esp_err_to_name(err));
        this->state_ = USB_CLIENT_INIT;
        break;
      }
      ESP_LOGD(TAG, "Get descriptor device %d", this->device_addr_);
      const usb_device_desc_t *desc;
      if ((err = usb_host_get_device_descriptor(this->device_handle_, &desc)) != ESP_OK) {
        ESP_LOGD(TAG, "Device get_desc failed: %s", esp_err_to_name(err));
        this->disconnect_();
      } else {
        ESP_LOGD(TAG, "Device descriptor: vid %X pid %X", desc->idVendor, desc->idProduct);
        if (desc->idVendor == this->vid_ && desc->idProduct == this->pid_) {
          ESP_LOGD(TAG, "Getting info");
          usb_device_info_t dev_info;
          if ((err = usb_host_device_info(this->device_handle_, &dev_info)) != ESP_OK) {
            ESP_LOGD(TAG, "Device info failed: %s", esp_err_to_name(err));
            this->disconnect_();
            break;
          }
          this->state_ = USB_CLIENT_CONNECTED;
          ESP_LOGD(TAG, "Manuf: %s; Prod: %s; Serial: %s",
                   get_descriptor_string(dev_info.str_desc_manufacturer).c_str(),
                   get_descriptor_string(dev_info.str_desc_product).c_str(),
                   get_descriptor_string(dev_info.str_desc_serial_num).c_str());

#if ESPHOME_LOG_LEVEL >= ESPHOME_LOG_LEVEL_VERBOSE
          const usb_device_desc_t *device_desc;
          err = usb_host_get_device_descriptor(this->device_handle_, &device_desc);
          if (err == ESP_OK)
            usb_print_device_descriptor(device_desc);
          const usb_config_desc_t *config_desc;
          err = usb_host_get_active_config_descriptor(this->device_handle_, &config_desc);
          if (err == ESP_OK)
            usb_print_config_descriptor(config_desc, nullptr);
#endif
          this->on_connected_();
        } else {
          ESP_LOGD(TAG, "Not our device, closing");
          this->disconnect_();
        }
      }
      break;
    }

    default:
      usb_host_client_handle_events(this->handle_, 0);
      break;
  }
}

void USBClient::on_opened(uint8_t addr) {
  ESP_LOGD(TAG, "Opened device %d", addr);
  if (this->state_ == USB_CLIENT_INIT) {
    this->device_addr_ = addr;
    this->state_ = USB_CLIENT_OPEN;
  }
}
void USBClient::on_closed(usb_device_handle_t handle) {
  if (this->device_handle_ == handle) {
    this->device_handle_ = nullptr;
    this->state_ = USB_CLIENT_INIT;
    this->device_addr_ = -1;
  }
}

static void control_callback(const usb_transfer_t *xfer) {
  auto trq = static_cast<transfer_request_t *>(xfer->context);
  trq->status.error_code = xfer->status;
  trq->status.success = xfer->status == USB_TRANSFER_STATUS_COMPLETED;
  trq->status.endpoint = xfer->bEndpointAddress;
  trq->status.data = xfer->data_buffer;
  trq->status.data_len = xfer->actual_num_bytes;
  if (trq->callback != nullptr)
    trq->callback(trq->status);
  trq->client->release_trq(trq);
}

transfer_request_t *USBClient::get_trq_() {
  if (this->trq_pool_.empty()) {
    ESP_LOGE(TAG, "Too many requests queued");
    return nullptr;
  }
  auto trq = this->trq_pool_.front();
  this->trq_pool_.pop_front();
  trq->client = this;
  trq->transfer->context = trq;
  trq->transfer->device_handle = this->device_handle_;
  return trq;
}
transfer_request_t *USBClient::setup_control_transfer_(uint8_t type, uint8_t request, uint16_t value, uint16_t index,
                                                       const transfer_cb_t &callback, uint16_t length, uint8_t dir) {
  auto trq = this->get_trq_();
  if (trq == nullptr)
    return nullptr;
  if (length > sizeof(trq->transfer->data_buffer_size) - sizeof(usb_setup_packet_t)) {
    ESP_LOGE(TAG, "Control transfer data size too large: %u > %u", length,
             sizeof(trq->transfer->data_buffer_size) - sizeof(usb_setup_packet_t));
    this->release_trq(trq);
    return nullptr;
  }
  auto control_packet = ByteBuffer(8, LITTLE);
  control_packet.put_uint8(type);
  control_packet.put_uint8(request);
  control_packet.put_uint16(value);
  control_packet.put_uint16(index);
  control_packet.put_uint16(length);
  memcpy(trq->transfer->data_buffer, control_packet.get_data().data(), 8);
  trq->callback = callback;
  trq->transfer->bEndpointAddress = dir;
  trq->transfer->num_bytes = static_cast<int>(length + sizeof(usb_setup_packet_t));
  trq->transfer->callback = reinterpret_cast<usb_transfer_cb_t>(control_callback);
  return trq;
}

void USBClient::control_transfer_in_(uint8_t type, uint8_t request, uint16_t value, uint16_t index,
                                     transfer_cb_t const &callback, uint16_t length) {
  type |= USB_DIR_IN;
  usb_setup_packet_t setup_packet = {};
  auto *trq = setup_control_transfer_(type, request, value, index, callback, length, USB_DIR_IN);
  if (trq == nullptr)
    return;
  memcpy(trq->transfer->data_buffer, &setup_packet, sizeof(setup_packet));
  auto err = usb_host_transfer_submit_control(this->handle_, trq->transfer);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to submit control transfer, err=%d", err);
    this->release_trq(trq);
  }
}

void USBClient::control_transfer_out_(uint8_t type, uint8_t request, uint16_t value, uint16_t index,
                                      const transfer_cb_t &callback, uint16_t length, const uint8_t *data) {
  type &= ~USB_DIR_MASK;
  auto *trq = setup_control_transfer_(type, request, value, index, callback, length, USB_DIR_OUT);
  if (trq == nullptr)
    return;
  memcpy(trq->transfer->data_buffer + sizeof(usb_setup_packet_t), data, length);
  auto err = usb_host_transfer_submit_control(this->handle_, trq->transfer);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to submit control transfer, err=%d", err);
    this->release_trq(trq);
  }
}

static void transfer_callback(usb_transfer_t *xfer) {
  auto *trq = static_cast<transfer_request_t *>(xfer->context);
  trq->status.error_code = xfer->status;
  trq->status.success = xfer->status == USB_TRANSFER_STATUS_COMPLETED;
  trq->status.endpoint = xfer->bEndpointAddress;
  trq->status.data = xfer->data_buffer;
  trq->status.data_len = xfer->actual_num_bytes;
  if (trq->callback != nullptr)
    trq->callback(trq->status);
  trq->client->release_trq(trq);
}
/**
 * Performs a transfer input operation.
 *
 * @param ep_address The endpoint address.
 * @param callback The callback function to be called when the transfer is complete.
 * @param length The length of the data to be transferred.
 *
 * @throws None.
 */
void USBClient::transfer_in(uint8_t ep_address, const transfer_cb_t &callback, uint16_t length) {
  auto trq = this->get_trq_();
  if (trq == nullptr) {
    ESP_LOGE(TAG, "Too many requests queued");
    return;
  }
  trq->callback = callback;
  trq->transfer->callback = transfer_callback;
  trq->transfer->bEndpointAddress = ep_address | USB_DIR_IN;
  trq->transfer->num_bytes = length;
  auto err = usb_host_transfer_submit(trq->transfer);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to submit transfer, address=%x, length=%d, err=%x", ep_address, length, err);
    this->release_trq(trq);
  }
}

/**
 * Performs an output transfer operation.
 *
 * @param ep_address The endpoint address.
 * @param callback The callback function to be called when the transfer is complete.
 * @param data The data to be transferred.
 * @param length The length of the data to be transferred.
 *
 * @throws None.
 */
void USBClient::transfer_out(uint8_t ep_address, const transfer_cb_t &callback, const uint8_t *data, uint16_t length) {
  auto trq = this->get_trq_();
  if (trq == nullptr) {
    ESP_LOGE(TAG, "Too many requests queued");
    return;
  }
  trq->callback = callback;
  trq->transfer->callback = transfer_callback;
  trq->transfer->bEndpointAddress = ep_address | USB_DIR_OUT;
  trq->transfer->num_bytes = length;
  memcpy(trq->transfer->data_buffer, data, length);
  auto err = usb_host_transfer_submit(trq->transfer);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to submit transfer, address=%x, length=%d, err=%x", ep_address, length, err);
    this->release_trq(trq);
  }
}
void USBClient::dump_config() {
  ESP_LOGCONFIG(TAG, "USBClient");
  ESP_LOGCONFIG(TAG, "  Vendor id %04X", this->vid_);
  ESP_LOGCONFIG(TAG, "  Product id %04X", this->pid_);
}
void USBClient::release_trq(transfer_request_t *trq) { this->trq_pool_.push_back(trq); }

}  // namespace usb_host
}  // namespace esphome
