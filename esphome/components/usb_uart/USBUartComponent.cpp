#include "USBUartComponent.h"
#include "usb/usb_host.h"

namespace esphome {
namespace usb_uart {

static constexpr uint8_t USB_CDC_SUBCLASS_ACM = 0x02;
static constexpr uint8_t USB_SUBCLASS_COMMON = 0x02;
static constexpr uint8_t USB_SUBCLASS_NULL = 0x00;
static constexpr uint8_t USB_PROTOCOL_NULL = 0x00;
static constexpr uint8_t USB_DEVICE_PROTOCOL_IAD = 0x01;

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
    return cdc_eps_t{notify_ep, in_ep, out_ep};
  return cdc_eps_t{notify_ep, out_ep, in_ep};
}
static std::vector<cdc_eps_t> find_cdc_acm(usb_device_handle_t dev_hdl) {
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

void USBUartComponent::setup() { USBClient::setup(); }
void USBUartComponent::loop() { USBClient::loop(); }
void USBUartTypeCdcAcm::on_disconnected_() {}
void USBUartTypeCdcAcm::on_connected_() {
  ESP_LOGD(TAG, "on_connected");
  this->cdc_devs = find_cdc_acm(this->device_handle_);
  if (this->cdc_devs.empty()) {
    this->status_set_error("No CDC-ACM device found");
    this->mark_failed();
    return;
  }
  ESP_LOGD(TAG, "Found %u CDC-ACM devices", this->cdc_devs.size());
  this->control_transfer_in_(
      usb_host::USB_RECIP_DEVICE | usb_host::USB_TYPE_VENDOR, 0x5F, 0, 0,
      [=](const usb_host::transfer_status_t &status) {
        ESP_LOGD(TAG, "Transfer result: length: %u; data %X %X", status.data_len, status.data[0], status.data[1]);
      },
      2);
}

}  // namespace usb_uart
}  // namespace esphome