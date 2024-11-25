#pragma once

#include "esphome/components/light/addressable_light.h"
#include "esphome/components/spi/spi.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"

namespace esphome {
namespace neopixel {

enum RGBOrder : uint8_t {
  ORDER_RGB,
  ORDER_RBG,
  ORDER_GRB,
  ORDER_GBR,
  ORDER_BGR,
  ORDER_BRG,
};

static const char *const TAG = "neopixel";

extern const uint8_t bit_table[][3];

template<size_t NUM_LEDS, uint8_t R_OFFS, uint8_t G_OFFS, uint8_t B_OFFS, uint8_t W_OFFS, uint8_t STRIDE>
class NeoPixelLEDStripLightOutput : public light::AddressableLight,
                                    public spi::SPIDevice<spi::BIT_ORDER_MSB_FIRST, spi::CLOCK_POLARITY_HIGH,
                                                          spi::CLOCK_PHASE_TRAILING, spi::DATA_RATE_20MHZ> {
 public:
  void setup() override {
    size_t buffer_size = NUM_LEDS * STRIDE;
    RAMAllocator<uint8_t> allocator(0);
    this->buf_ = allocator.allocate(buffer_size);
    if (this->buf_ == nullptr) {
      esph_log_e(TAG, "Cannot allocate LED buffer!");
      this->mark_failed();
      return;
    }

    this->effect_data_ = allocator.allocate(NUM_LEDS);
    if (this->effect_data_ == nullptr) {
      esph_log_e(TAG, "Cannot allocate effect data!");
      this->mark_failed();
      return;
    }

    this->tx_buf_ = allocator.allocate(buffer_size * 3 + this->reset_bytes_);
    if (this->tx_buf_ == nullptr) {
      esph_log_e(TAG, "Cannot allocate SPI TX buffer!");
      this->mark_failed();
      return;
    }
    memset(this->tx_buf_, 0, this->reset_bytes_);
    this->spi_setup();
  }

  float get_setup_priority() const override { return setup_priority::HARDWARE; }

  int32_t size() const override { return NUM_LEDS; }
  light::LightTraits get_traits() override {
    auto traits = light::LightTraits();
    if (this->has_white_()) {
      traits.set_supported_color_modes({light::ColorMode::RGB_WHITE, light::ColorMode::WHITE});
    } else {
      traits.set_supported_color_modes({light::ColorMode::RGB});
    }
    return traits;
  }

  void clear_effect_data() override { memset(this->effect_data_, 0, NUM_LEDS * sizeof(*this->effect_data_)); }

  void dump_config() override {
    esph_log_config(TAG, "ESP32 RMT LED Strip:");
    char buf[5] = {};
    if (this->has_white_())
      buf[W_OFFS] = 'W';
    buf[R_OFFS] = 'R';
    buf[G_OFFS] = 'G';
    buf[B_OFFS] = 'B';
    esph_log_config(TAG, "  RGB Order: %s", buf);
    esph_log_config(TAG, "  Number of LEDs: %u", NUM_LEDS);
  }

  void write_state(light::LightState *state) override {
    auto tx = this->tx_buf_ + this->reset_bytes_;
    auto src = this->buf_;
    for (size_t i = 0; i != NUM_LEDS; i++) {
      for (size_t j = 0; j != STRIDE; j++) {
        auto data = bit_table[*src++];
        *tx++ = *data++;
        *tx++ = *data++;
        *tx++ = *data;
      }
    }
    this->write_array(this->tx_buf_, NUM_LEDS * 3 * STRIDE + this->reset_bytes_);
  }

  void set_reset_bytes(size_t reset_bytes) { this->reset_bytes_ = reset_bytes; }

 protected:
  bool has_white_() const { return STRIDE == 4; }
  light::ESPColorView get_view_internal(int32_t index) const override {
    auto base = this->buf_ + index * STRIDE;
    return {base + R_OFFS,
            base + G_OFFS,
            base + B_OFFS,
            has_white_() ? base + W_OFFS : nullptr,
            &this->effect_data_[index],
            &this->correction_};
  }

  uint8_t *buf_{};
  uint8_t *effect_data_{};
  uint8_t *tx_buf_{};
  size_t reset_bytes_{15};
};

}  // namespace neopixel
}  // namespace esphome
