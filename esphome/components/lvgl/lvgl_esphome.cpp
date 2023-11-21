#include "esphome/core/log.h"
#include "esphome/core/helpers.h"
#include "esphome/core/hal.h"
#include "lvgl_hal.h"

static const char *TAG = "lvgl";
unsigned long lv_millis(void) { return esphome::millis(); }

static unsigned cap_bits = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;

void *lv_custom_mem_alloc(unsigned int size) {
  void *ptr;
  ptr = heap_caps_malloc(size, cap_bits);
  if (ptr == nullptr) {
    cap_bits = MALLOC_CAP_8BIT;
    ptr = heap_caps_malloc(size, cap_bits);
  }
  if (ptr == nullptr) {
    esphome::esph_log_e(TAG, "Failed to allocate %u bytes", size);
    return nullptr;
  }
#ifdef ESPHOME_LOG_HAS_VERBOSE
  esphome::esph_log_v(TAG, "allocate %u - > %p", size, ptr);
#endif
  return ptr;
}

void lv_custom_mem_free(void *ptr) {
#ifdef ESPHOME_LOG_HAS_VERBOSE
  esphome::esph_log_v(TAG, "free %p", ptr);
#endif
  if (ptr == nullptr)
    return;
  heap_caps_free(ptr);
}

void *lv_custom_mem_realloc(void *ptr, unsigned int size) {
#ifdef ESPHOME_LOG_HAS_VERBOSE
  esphome::esph_log_v(TAG, "realloc %p: %u", ptr, size);
#endif
  return heap_caps_realloc(ptr, size, cap_bits);
}