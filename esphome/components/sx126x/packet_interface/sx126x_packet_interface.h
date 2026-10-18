#pragma once

#include "esphome/components/packet_interface/packet_interface.h"
#include "esphome/components/sx126x/sx126x.h"

namespace esphome::sx126x {

/// A packet interface link over an SX126x radio: each packet is one radio packet, with the RSSI and SNR of received
/// packets in the metadata. Sending waits for the radio to finish transmitting.
class SX126xPacketInterface : public packet_interface::PacketInterface, public SX126xListener {
 public:
  explicit SX126xPacketInterface(SX126x *radio) : radio_(radio) {}

  void setup() override { this->radio_->register_listener(this); }
  bool send_to_interface(const packet_interface::PacketBuffer &data,
                         const packet_interface::PacketMetaData &meta_data) override;
  void on_packet(const std::vector<uint8_t> &packet, float rssi, float snr) override;

 protected:
  SX126x *radio_;
};

}  // namespace esphome::sx126x
