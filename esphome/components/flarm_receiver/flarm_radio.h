#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "esphome/core/defines.h"

#include "legacy_protocol.h"

#ifdef USE_FLARM_RECEIVER_SX126X
#include "esphome/components/sx126x/sx126x.h"
#endif
#ifdef USE_FLARM_RECEIVER_SX127X
#include "esphome/components/sx127x/sx127x.h"
#endif

namespace esphome::flarm_receiver {

/// The legacy protocol's FSK settings
static constexpr uint32_t LEGACY_BITRATE = 100000;
static constexpr uint32_t LEGACY_DEVIATION = 50000;

/// Where a radio's received frames go
class FrameSink {
 public:
  virtual void on_frame(const std::vector<uint8_t> &frame, float rssi) = 0;
};

/**
 * What the receiver needs of its radio. A radio set up for LoRa is shared: the receiver puts the legacy protocol's
 * FSK settings in place while it listens, and gives the radio back for each LoRa packet.
 */
class FlarmRadio {
 public:
  virtual void setup(FrameSink *sink, uint32_t base_frequency) = 0;
  /// True if the radio is set up for LoRa, and so shared
  virtual bool shared() const = 0;
  /// Puts the legacy protocol's settings in place, on the base frequency; false if the radio is busy
  virtual bool start_fsk() = 0;
  /// Back to the configured settings; false if the radio is busy
  virtual bool stop_fsk() = 0;
  virtual void retune(uint32_t frequency) = 0;
  /// Sending, or receiving a packet that sending would lose
  virtual bool busy() = 0;
  virtual bool transmit(const uint8_t *data, size_t len) = 0;
};

#ifdef USE_FLARM_RECEIVER_SX126X
class SX126xFlarmRadio : public FlarmRadio, public sx126x::SX126xListener {
 public:
  explicit SX126xFlarmRadio(sx126x::SX126x *radio) : radio_(radio) {}
  void setup(FrameSink *sink, uint32_t base_frequency) override {
    this->sink_ = sink;
    this->radio_->register_listener(this);
    // The radio's own settings, such as its TCXO, RF switch and power, with the legacy protocol's modulation
    sx126x::RadioProfile &fsk = this->fsk_;
    fsk = this->radio_->get_requested_profile();
    fsk.modulation = sx126x::PACKET_TYPE_GFSK;
    fsk.frequency = base_frequency;
    fsk.bitrate = LEGACY_BITRATE;
    fsk.deviation = LEGACY_DEVIATION;
    fsk.bandwidth = sx126x::SX126X_BW_234300;
    fsk.shaping = sx126x::GAUSSIAN_BT_0_5;
    fsk.preamble_size = 8;
    fsk.preamble_detect = 0;
    fsk.set_sync_value(LEGACY_SYNC_WORD);
    fsk.payload_length = LEGACY_FRAME_SIZE;
    fsk.crc_enable = false;
    fsk.whitening_enable = false;
    fsk.rx_start = true;
  }
  bool shared() const override { return this->radio_->get_requested_profile().modulation == sx126x::PACKET_TYPE_LORA; }
  bool start_fsk() override { return this->radio_->apply_profile(&this->fsk_, this); }
  bool stop_fsk() override { return this->radio_->apply_profile(nullptr, nullptr); }
  void retune(uint32_t frequency) override { this->radio_->retune(frequency); }
  bool busy() override { return this->radio_->is_transmitting() || this->radio_->is_receiving(); }
  bool transmit(const uint8_t *data, size_t len) override {
    return this->radio_->transmit_packet(data, len) == sx126x::SX126xError::SX126X_ERROR_NONE;
  }
  void on_packet(const std::vector<uint8_t> &packet, float rssi, float snr) override {
    this->sink_->on_frame(packet, rssi);
  }

 protected:
  sx126x::SX126x *radio_;
  FrameSink *sink_{nullptr};
  sx126x::RadioProfile fsk_;
};
#endif

#ifdef USE_FLARM_RECEIVER_SX127X
/// An SX1276 or similar, which shares its LoRa radio only
class SX127xFlarmRadio : public FlarmRadio, public sx127x::SX127xListener {
 public:
  explicit SX127xFlarmRadio(sx127x::SX127x *radio) : radio_(radio) {}
  void setup(FrameSink *sink, uint32_t base_frequency) override {
    this->sink_ = sink;
    this->radio_->register_listener(this);
    sx127x::SX127xProfile &fsk = this->fsk_;
    fsk = this->radio_->get_requested_profile();
    fsk.modulation = sx127x::MOD_FSK;
    fsk.frequency = base_frequency;
    fsk.bitrate = LEGACY_BITRATE;
    fsk.deviation = LEGACY_DEVIATION;
    // Wide enough for 100 kb/s with 50 kHz deviation
    fsk.bandwidth = sx127x::SX127X_BW_250_0;
    fsk.afc_bandwidth = sx127x::SX127X_BW_250_0;
    fsk.shaping = sx127x::GAUSSIAN_BT_0_5;
    // In bytes; the sync word starts with more of it
    fsk.preamble_size = 1;
    fsk.preamble_detect = 0;
    fsk.preamble_polarity = 0x55;
    fsk.sync_value.assign(LEGACY_SYNC_WORD, LEGACY_SYNC_WORD + sizeof(LEGACY_SYNC_WORD));
    fsk.payload_length = LEGACY_FRAME_SIZE;
    fsk.packet_mode = true;
    // Packet mode needs the bit synchronizer
    fsk.bitsync = true;
    fsk.crc_enable = false;
    fsk.rx_start = true;
  }
  bool shared() const override { return this->radio_->get_requested_profile().modulation == sx127x::MOD_LORA; }
  bool start_fsk() override { return this->radio_->apply_profile(&this->fsk_, this); }
  bool stop_fsk() override { return this->radio_->apply_profile(nullptr, nullptr); }
  void retune(uint32_t frequency) override { this->radio_->retune(frequency); }
  bool busy() override { return this->radio_->is_transmitting() || this->radio_->is_receiving(); }
  bool transmit(const uint8_t *data, size_t len) override {
    return this->radio_->transmit_packet(data, len) == sx127x::SX127xError::SX127X_ERROR_NONE;
  }
  void on_packet(const std::vector<uint8_t> &packet, float rssi, float snr) override {
    this->sink_->on_frame(packet, rssi);
  }

 protected:
  sx127x::SX127x *radio_;
  FrameSink *sink_{nullptr};
  sx127x::SX127xProfile fsk_;
};
#endif

}  // namespace esphome::flarm_receiver
