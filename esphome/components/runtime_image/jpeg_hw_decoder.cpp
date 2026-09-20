#include "jpeg_hw_decoder.h"
#ifdef USE_RUNTIME_IMAGE_JPEG_HW

#include <cinttypes>

#include "esp_err.h"
#include "esphome/core/application.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

#ifdef USE_ESP_IDF
#include "esp_task_wdt.h"
#endif

static const char *const TAG = "image_decoder.jpeg_hw";

namespace esphome::runtime_image {

namespace {

// The hardware codec only decodes baseline JPEG (SOF0). Progressive JPEG uses a SOF2
// marker instead, so a quick scan of the marker stream is enough to reject it up front,
// the same restriction the software decoder enforces via JPEGDEC::getJPEGType().
bool is_progressive_jpeg(const uint8_t *buffer, size_t size) {
  for (size_t i = 0; i + 1 < size;) {
    if (buffer[i] != 0xFF) {
      i++;
      continue;
    }
    uint8_t marker = buffer[i + 1];
    if (marker == 0xC2) {
      return true;
    }
    if (marker == 0xC0 || marker == 0xD9) {
      // Baseline SOF, or end-of-image reached before any SOF: not progressive.
      return false;
    }
    i += 2;
  }
  return false;
}

}  // namespace

JpegHwDecoder::~JpegHwDecoder() {
  if (this->output_buffer_ != nullptr) {
    free(this->output_buffer_);  // NOLINT(cppcoreguidelines-owning-memory)
  }
  if (this->engine_ != nullptr) {
    jpeg_del_decoder_engine(this->engine_);
  }
}

int HOT JpegHwDecoder::decode(uint8_t *buffer, size_t size) {
  // The hardware decoder, like JPEGDEC, requires the complete encoded image up front.
  if (this->expected_size_ > 0 && size < this->expected_size_) {
    ESP_LOGV(TAG, "Download not complete. Size: %zu/%zu", size, this->expected_size_);
    return 0;
  }

  if (is_progressive_jpeg(buffer, size)) {
    ESP_LOGE(TAG, "Progressive JPEG images not supported");
    return DECODE_ERROR_INVALID_TYPE;
  }

  if (this->engine_ == nullptr) {
    jpeg_decode_engine_cfg_t engine_cfg{.timeout_ms = 5000};
    if (jpeg_new_decoder_engine(&engine_cfg, &this->engine_) != ESP_OK) {
      ESP_LOGE(TAG, "Could not create hardware JPEG decoder engine");
      return DECODE_ERROR_INTERNAL_DECODER_ERROR;
    }
  }

  jpeg_decode_picture_info_t header{};
  if (jpeg_decoder_get_info(buffer, size, &header) != ESP_OK) {
    ESP_LOGE(TAG, "Could not parse JPEG header");
    return DECODE_ERROR_INVALID_TYPE;
  }
  ESP_LOGD(TAG, "Image size: %" PRIu32 " x %" PRIu32, header.width, header.height);

  if (!this->set_size(static_cast<int>(header.width), static_cast<int>(header.height))) {
    return DECODE_ERROR_OUT_OF_MEMORY;
  }

  // Decode to a scratch RGB888 buffer; draw() below scales/converts it into the
  // RuntimeImage's own buffer, the same two-step approach the software decoder's
  // row callback uses.
  size_t needed = static_cast<size_t>(header.width) * header.height * 3;
  if (this->output_buffer_size_ < needed) {
    if (this->output_buffer_ != nullptr) {
      free(this->output_buffer_);  // NOLINT(cppcoreguidelines-owning-memory)
      this->output_buffer_ = nullptr;
      this->output_buffer_size_ = 0;
    }
    jpeg_decode_memory_alloc_cfg_t mem_cfg{.buffer_direction = JPEG_DEC_ALLOC_OUTPUT_BUFFER};
    size_t allocated = 0;
    this->output_buffer_ = static_cast<uint8_t *>(jpeg_alloc_decoder_mem(needed, &mem_cfg, &allocated));
    if (this->output_buffer_ == nullptr) {
      ESP_LOGE(TAG, "Could not allocate %zu bytes for the JPEG output buffer", needed);
      return DECODE_ERROR_OUT_OF_MEMORY;
    }
    this->output_buffer_size_ = allocated;
  }

  jpeg_decode_cfg_t decode_cfg{
      .output_format = JPEG_DECODE_OUT_FORMAT_RGB888,
      .rgb_order = JPEG_DEC_RGB_ELEMENT_ORDER_RGB,
  };

#ifdef USE_ESP_IDF
  if (esp_task_wdt_status(nullptr) == ESP_OK) {
#endif
    App.feed_wdt();
#ifdef USE_ESP_IDF
  }
#endif

  uint32_t out_size = 0;
  esp_err_t err = jpeg_decoder_process(this->engine_, &decode_cfg, buffer, size, this->output_buffer_,
                                       this->output_buffer_size_, &out_size);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Hardware JPEG decode failed: %s", esp_err_to_name(err));
    return DECODE_ERROR_INTERNAL_DECODER_ERROR;
  }
  ESP_LOGV(TAG, "Hardware decoder produced %" PRIu32 " bytes", out_size);

  size_t position = 0;
  for (uint32_t y = 0; y < header.height; y++) {
    for (uint32_t x = 0; x < header.width; x++) {
      Color color(this->output_buffer_[position], this->output_buffer_[position + 1],
                  this->output_buffer_[position + 2], 0xFF);
      position += 3;
      this->draw(static_cast<int>(x), static_cast<int>(y), 1, 1, color);
    }
  }

  this->decoded_bytes_ = size;
  return size;
}

}  // namespace esphome::runtime_image

#endif  // USE_RUNTIME_IMAGE_JPEG_HW
