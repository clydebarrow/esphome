#pragma once
// Allocation reports for xcsoar-tab5-app-sim.yaml

#include <esp_heap_caps.h>
#include <esp_rom_sys.h>

#include <cstdint>

// There is no unwinding through XCSoar's frames: print the code addresses found on the stack instead
inline void print_stack_code_addresses() {
  const uint32_t *sp = static_cast<const uint32_t *>(__builtin_frame_address(0));
  for (unsigned i = 0; i < 1024; i++) {
    if (sp[i] >= 0x40000000 && sp[i] < 0x44000000)
      esp_rom_printf(" 0x%08x", (unsigned) sp[i]);
  }
  esp_rom_printf("\n");
}

// Called by ESP-IDF for every allocation (CONFIG_HEAP_USE_HOOKS)
extern "C" void esp_heap_trace_alloc_hook(void *ptr, size_t size, uint32_t caps) {
  if (size < 1024 * 1024)
    return;
  esp_rom_printf("alloc %u bytes at %p:", (unsigned) size, ptr);
  print_stack_code_addresses();
}
