#include "../../core/hal.h"
#include "../../core/helpers.h"
#include "../../core/log.h"
#include "lvgl_hal.h"

static const char *TAG = "lvgl";
unsigned long lv_millis(void) {
  return esphome::millis();
}

esphome::ExternalRAMAllocator<unsigned char> allocator(esphome::ExternalRAMAllocator<unsigned char>::ALLOW_FAILURE);

void * lv_custom_mem_alloc(size_t size) {
  auto ptr =  allocator.allocate(size);
  esphome::ESP_LOGD(TAG, "allocate %u - > %p", size, ptr);
  return ptr;
}

void lv_custom_mem_free(void * ptr) {
  esphome::ESP_LOGD(TAG, "free %p", ptr);
  if (ptr == nullptr)
    return;
  //allocator.deallocate((uint8_t *)ptr, 0);
}

void * lv_custom_mem_realloc(void * ptr, size_t size) {
  esphome::ESP_LOGD(TAG, "realloc %p: %u", ptr, size);
  if (ptr != nullptr)
    lv_custom_mem_free(ptr);
  return lv_custom_mem_alloc(size);
}


