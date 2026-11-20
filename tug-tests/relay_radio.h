#pragma once
// A hook in the tug's LoRa relay (tug-relay-common.yaml), called before each packet from the tug goes to LoRa, with
// the packet's destination. A relay that shares its radio with a FLARM receiver (tug-relay-tracker.yaml) takes the
// radio back for LoRa here; the others leave it unset.

#include <cstdint>
#include <functional>

namespace relay_radio {

/// The broadcast address: a packet to it, such as the tug's position, gets no answer
static constexpr uint16_t BROADCAST = 0xFFFF;

inline std::function<void(uint16_t destination)> before_lora_send;

}  // namespace relay_radio
