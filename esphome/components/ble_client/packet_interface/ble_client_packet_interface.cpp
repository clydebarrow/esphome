#include "ble_client_packet_interface.h"

#ifdef USE_ESP32

#include "esphome/core/log.h"

namespace esphome::ble_client {

using packet_interface::PacketBuffer;
using packet_interface::PacketMetaData;

ESPHOME_LOG_TAG(TAG, "ble_client.packet_interface");

// A write carries the ATT MTU less the opcode and handle
static constexpr uint16_t ATT_WRITE_OVERHEAD = 3;

void BLEClientPacketInterface::dump_config() {
  char service[esp32_ble::UUID_STR_LEN];
  char characteristic[esp32_ble::UUID_STR_LEN];
  ESP_LOGCONFIG(TAG,
                "BLE client packet interface:\n"
                "  Service UUID: %s\n"
                "  Characteristic UUID: %s",
                this->service_uuid_.to_str(service), this->characteristic_uuid_.to_str(characteristic));
  packet_interface::PacketInterface::dump_config();
}

void BLEClientPacketInterface::reset_() {
  this->handle_ = 0;
  this->cccd_handle_ = 0;
  this->mtu_ = ESP_GATT_DEF_BLE_MTU_SIZE;
  this->ready_ = false;
}

void BLEClientPacketInterface::gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                                                   esp_ble_gattc_cb_param_t *param) {
  switch (event) {
    case ESP_GATTC_OPEN_EVT:
      if (param->open.status == ESP_GATT_OK)
        this->status_clear_warning();
      break;
    case ESP_GATTC_CFG_MTU_EVT:
      if (param->cfg_mtu.status == ESP_GATT_OK) {
        this->mtu_ = param->cfg_mtu.mtu;
        ESP_LOGD(TAG, "MTU %u", this->mtu_);
      }
      break;
    case ESP_GATTC_DISCONNECT_EVT:
    case ESP_GATTC_CLOSE_EVT:
      if (this->ready_) {
        ESP_LOGI(TAG, "Disconnected");
      }
      this->reset_();
      this->node_state = espbt::ClientState::IDLE;
      break;
    case ESP_GATTC_SEARCH_CMPL_EVT: {
      if (param->search_cmpl.status != ESP_GATT_OK) {
        ESP_LOGW(TAG, "Service discovery failed, status=%d", param->search_cmpl.status);
        this->status_set_warning();
        this->parent()->disconnect();
        break;
      }
      auto *characteristic = this->parent()->get_characteristic(this->service_uuid_, this->characteristic_uuid_);
      // Reconnecting would not help a device without the service, so the connection is left as it is
      if (characteristic == nullptr) {
        this->status_set_warning(LOG_STR("No packet interface characteristic found on this device"));
        break;
      }
      auto *cccd = characteristic->get_descriptor(ESP_GATT_UUID_CHAR_CLIENT_CONFIG);
      if (cccd == nullptr) {
        this->status_set_warning(LOG_STR("The packet interface characteristic has no notification descriptor"));
        break;
      }
      this->handle_ = characteristic->handle;
      this->cccd_handle_ = cccd->handle;
      this->write_without_response_ = (characteristic->properties & ESP_GATT_CHAR_PROP_BIT_WRITE_NR) != 0;
      esp_err_t err = this->parent()->register_for_notify(characteristic->handle);
      if (err != ESP_OK) {
        ESP_LOGW(TAG, "register_for_notify failed, error=%d", err);
        this->status_set_warning();
        this->parent()->disconnect();
      }
      break;
    }
    case ESP_GATTC_REG_FOR_NOTIFY_EVT:
      if (param->reg_for_notify.handle != this->handle_)
        break;
      if (param->reg_for_notify.status != ESP_GATT_OK) {
        ESP_LOGW(TAG, "Registering for notifications failed, status=%d", param->reg_for_notify.status);
        this->status_set_warning();
        this->parent()->disconnect();
      }
      // On success the parent writes the descriptor; the server has accepted it once WRITE_DESCR_EVT reports OK
      break;
    case ESP_GATTC_WRITE_DESCR_EVT:
      if (this->cccd_handle_ == 0 || param->write.handle != this->cccd_handle_ || this->ready_)
        break;
      if (param->write.status != ESP_GATT_OK) {
        ESP_LOGW(TAG, "Server refused to turn notifications on, status=%d", param->write.status);
        this->status_set_warning();
        this->parent()->disconnect();
        break;
      }
      this->ready_ = true;
      this->node_state = espbt::ClientState::ESTABLISHED;
      ESP_LOGI(TAG, "Connected, MTU %u", this->mtu_);
      break;
    case ESP_GATTC_WRITE_CHAR_EVT:
      // A write with response reports its outcome here; one without response reports nothing
      if (param->write.handle == this->handle_ && param->write.status != ESP_GATT_OK) {
        ESP_LOGW(TAG, "Write rejected, status=%d", param->write.status);
      }
      break;
    case ESP_GATTC_NOTIFY_EVT:
      if (param->notify.handle != this->handle_ || param->notify.value_len == 0)
        break;
      if (param->notify.value_len > this->max_packet_size_) {
        ESP_LOGW(TAG, "Packet of %u bytes is larger than %zu, discarding", param->notify.value_len,
                 this->max_packet_size_);
        break;
      }
      this->on_receive_from_interface_(PacketBuffer(param->notify.value, param->notify.value_len), {});
      break;
    default:
      break;
  }
}

// The packet is sent from tx_buffer_, which a log line further down could run an automation that sends into
bool BLEClientPacketInterface::send_to_interface(const PacketBuffer &data, const PacketMetaData &meta_data) {
  return this->send_exclusive_([&] { return this->send_(data); });
}

bool BLEClientPacketInterface::send_(const PacketBuffer &data) {
  size_t size = data.size();
  if (size == 0)
    return true;
  if (!this->ready_)
    return false;
  if (size > this->max_packet_size_) {
    ESP_LOGW(TAG, "Packet of %zu bytes is larger than %zu", size, this->max_packet_size_);
    return false;
  }
  if (size > this->mtu_ - ATT_WRITE_OVERHEAD) {
    ESP_LOGW(TAG, "Packet of %zu bytes does not fit the MTU of %u", size, this->mtu_);
    return false;
  }
  data.copy_to(this->tx_buffer_.data(), size);
  esp_err_t err = esp_ble_gattc_write_char(
      this->parent()->get_gattc_if(), this->parent()->get_conn_id(), this->handle_, size, this->tx_buffer_.data(),
      this->write_without_response_ ? ESP_GATT_WRITE_TYPE_NO_RSP : ESP_GATT_WRITE_TYPE_RSP, ESP_GATT_AUTH_REQ_NONE);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Write failed, error=%d", err);
    return false;
  }
  return true;
}

}  // namespace esphome::ble_client

#endif  // USE_ESP32
