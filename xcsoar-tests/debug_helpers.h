#pragma once
// Allocation reports for xcsoar-tab5-app-sim.yaml

#include <esp_heap_caps.h>
#include <esp_rom_sys.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <climits>
#include <cstdint>
#include <cstring>

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

// Logs every task with its state and the code addresses on its stack, from the context it saved when it last
// stopped running (RISC-V: the PC is the first word), for addr2line on the host. Finds where a thread is stuck.
inline void dump_tasks() {
  static TaskStatus_t status[40];
  static constexpr const char *STATES = "RrBSDI";  // running, ready, blocked, suspended, deleted, invalid
  const UBaseType_t n = uxTaskGetSystemState(status, 40, nullptr);
  for (UBaseType_t i = 0; i < n; i++) {
    const TaskStatus_t &t = status[i];
    const uint32_t *sp = *reinterpret_cast<uint32_t *const *>(t.xHandle);
    esp_rom_printf("task %-16s %p %c prio %u:", t.pcTaskName, t.xHandle,
                   STATES[t.eCurrentState < 6 ? t.eCurrentState : 5], (unsigned) t.uxCurrentPriority);
    if (t.eCurrentState != eRunning) {
      for (unsigned k = 0; k < 400; k++) {
        if (sp[k] >= 0x40000000 && sp[k] < 0x44000000)
          esp_rom_printf(" 0x%08x", (unsigned) sp[k]);
      }
    }
    esp_rom_printf("\n");
  }
}

// Sampling profiler: every millisecond, records where each XCSoar thread that is waiting for a core (preempted
// while running) was interrupted, read from the context saved on its stack (RISC-V: the PC is the first word).
// Every 2 minutes it logs the 60 most frequent addresses and return addresses, for addr2line on the host. Start it
// with prof::start() from a lambda.
namespace prof {
static constexpr unsigned TABLE_SIZE = 8192;
struct Slot {
  uint32_t pc;
  uint32_t count;
};
static Slot *table;
static Slot *ra_table;
static uint32_t samples;

static void add(Slot *table, uint32_t pc) {
  uint32_t h = ((pc >> 1) * 2654435761u) >> 19;
  for (unsigned i = 0; i != TABLE_SIZE; i++) {
    Slot &s = table[(h + i) % TABLE_SIZE];
    if (s.pc == pc) {
      s.count++;
      return;
    }
    if (s.pc == 0) {
      s.pc = pc;
      s.count = 1;
      return;
    }
  }
}

static void dump(const Slot *table, const char *what) {
  esp_rom_printf("prof: %u samples\n", (unsigned) samples);
  uint32_t last = UINT32_MAX;
  for (unsigned k = 0; k != 60; k++) {
    unsigned best = TABLE_SIZE;
    for (unsigned i = 0; i != TABLE_SIZE; i++) {
      if (table[i].pc != 0 && table[i].count < last && (best == TABLE_SIZE || table[i].count > table[best].count))
        best = i;
    }
    if (best == TABLE_SIZE)
      break;
    // ties at the same count are skipped; good enough for a ranking
    esp_rom_printf("prof %s %u 0x%08x\n", what, (unsigned) table[best].count, (unsigned) table[best].pc);
    last = table[best].count;
  }
}

static void task(void *) {
  static TaskStatus_t status[40];
  TaskHandle_t handles[16];
  unsigned count = 0;
  uint32_t last_scan = 0, last_dump = xTaskGetTickCount();
  for (;;) {
    vTaskDelay(1);
    const uint32_t now = xTaskGetTickCount();
    if (count == 0 || now - last_scan > 5000) {
      const UBaseType_t n = uxTaskGetSystemState(status, 40, nullptr);
      count = 0;
      for (UBaseType_t i = 0; i < n && count < 16; i++) {
        if (strcmp(status[i].pcTaskName, "xcsoar") == 0)
          handles[count++] = status[i].xHandle;
      }
      last_scan = now;
    }
    for (unsigned i = 0; i != count; i++) {
      if (eTaskGetState(handles[i]) != eReady)
        continue;
      const uint32_t *sp = *reinterpret_cast<uint32_t *const *>(handles[i]);
      add(table, sp[0]);
      add(ra_table, sp[1]);
      samples++;
    }
    if (now - last_dump > 120000) {
      dump(table, "pc");
      dump(ra_table, "ra");
      last_dump = now;
    }
  }
}

inline void start() {
  table = static_cast<Slot *>(heap_caps_calloc(TABLE_SIZE, sizeof(Slot), MALLOC_CAP_SPIRAM));
  ra_table = static_cast<Slot *>(heap_caps_calloc(TABLE_SIZE, sizeof(Slot), MALLOC_CAP_SPIRAM));
  xTaskCreate(task, "prof", 4096, nullptr, 10, nullptr);
}
}  // namespace prof
