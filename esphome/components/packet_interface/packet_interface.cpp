#include "packet_interface.h"
#include "esphome/core/log.h"

namespace esphome::packet_interface {

ESPHOME_LOG_TAG(TAG, "packet_interface");

void PacketInterface::dump_config() {
  ESP_LOGCONFIG(TAG,
                "Packet Interface:\n"
                "  Max Packet Size: %zu",
                this->get_max_packet_size());
}

void PacketInterface::on_receive_from_interface_(const PacketBuffer &data, const PacketMetaData &meta_data) {
#ifdef ESPHOME_LOG_HAS_VERBOSE
  static constexpr size_t LOG_BYTES = 24;
  uint8_t head[LOG_BYTES];
  size_t len = data.copy_to(head, LOG_BYTES);
  ESP_LOGV_HEX(TAG, head, len, LOG_BYTES, "Received %zu bytes %s", data.size());
#endif
  this->callback_.call(data, meta_data);
}
}  // namespace esphome::packet_interface
