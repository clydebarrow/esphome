#pragma once

#include "image_decoder.h"
#include "runtime_image.h"
#include "esphome/core/defines.h"
#ifdef USE_RUNTIME_IMAGE_JPEG_HW
#include <driver/jpeg_decode.h>

namespace esphome::runtime_image {

/**
 * @brief Image decoder specialization for JPEG images, using the ESP32-P4's hardware
 * JPEG codec instead of a software library.
 *
 * Like the software decoder, this requires the complete encoded image to be available
 * before decoding starts, and only supports baseline (non-progressive) JPEG - the
 * hardware codec cannot decode progressive JPEG at all.
 */
class JpegHwDecoder : public ImageDecoder {
 public:
  JpegHwDecoder(RuntimeImage *image) : ImageDecoder(image, JPEG) {}
  ~JpegHwDecoder() override;

  int HOT decode(uint8_t *buffer, size_t size) override;

 protected:
  // Created lazily on first use, and kept alive across decode sessions to avoid
  // repeatedly allocating/freeing the driver's internal DMA descriptors.
  jpeg_decoder_handle_t engine_{nullptr};
  // DMA-capable scratch buffer the hardware writes decoded RGB888 pixels into;
  // kept and grown as needed rather than freed after every decode.
  uint8_t *output_buffer_{nullptr};
  size_t output_buffer_size_{0};
};

}  // namespace esphome::runtime_image

#endif  // USE_RUNTIME_IMAGE_JPEG_HW
