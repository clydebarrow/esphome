#include "esphome/core/log.h"
#include "esphome/core/helpers.h"
#include "esphome/core/hal.h"
#include "lvgl_hal.h"

static const char *TAG = "lvgl";
unsigned long lv_millis(void) { return esphome::millis(); }

void *lv_custom_mem_alloc(unsigned int size) {
  auto ptr = (heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
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
  return heap_caps_realloc(ptr, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}
