//
// Created by Clyde Stubbs on 20/9/2023.
//

#pragma once

#ifdef __cplusplus
#define EXTERNC extern "C"
#else
#define EXTERNC extern
#endif

namespace esphome {
namespace lvgl {
static const char *TAG = "lvgl";
}  // namespace lvgl
}  // namespace esphome
EXTERNC unsigned long lv_millis(void);
EXTERNC void *lv_custom_mem_alloc(unsigned int size);
EXTERNC void lv_custom_mem_free(void *ptr);
EXTERNC void *lv_custom_mem_realloc(void *ptr, unsigned int size);
