#pragma once

#include "esphome/core/component.h"
#include "esphome/core/defines.h"
#include "esphome/core/helpers.h"
#include "esphome/components/canbus/canbus.h"
#include "esphome/components/sensor/sensor.h"

#include "esphome/components/bytebuffer/bytebuffer.h"

#include <map>

namespace esphome::mastervolt {

static const char *const TAG = "mastervolt";

// MasterBus 29-bit extended CAN IDs are structured as kind(10 bits) << 18 | IDB(18 bits).
// IDB is the unique per-unit address assigned at manufacture; the low 5 bits of kind
// (IDAL) identify the device class. See masterbus-protocol.md for details.
static constexpr uint32_t IDB_MASK = 0x3FFFF;  // 18 bits
static constexpr uint8_t IDAL_MASK = 0x1F;     // 5 bits
// Sentinel for a device configured by class only, awaiting binding via discovery
static constexpr uint32_t IDB_UNASSIGNED = 0xFFFFFFFF;

constexpr uint16_t kind_of(uint32_t can_id) { return can_id >> 18; }
constexpr uint32_t idb_of(uint32_t can_id) { return can_id & IDB_MASK; }
constexpr uint8_t idal_of(uint16_t kind) { return kind & IDAL_MASK; }
constexpr uint32_t make_can_id(uint16_t kind, uint32_t idb) { return (static_cast<uint32_t>(kind) << 18) | idb; }

// Query frames have this bit set in kind (e.g. 0x613 query vs 0x213 response)
static constexpr uint16_t KIND_QUERY_FLAG = 0x400;
// Directed ping request/response kinds (§5.4)
static constexpr uint16_t PING_KIND_BASE = 0x1C0;
static constexpr uint16_t PING_RESPONSE_KIND_BASE = 0x180;

// Announcement: E=0, MT=10, Tab=00 (kind = 0x100 | IDAL)
constexpr bool is_announcement(uint16_t kind) { return (kind & ~static_cast<uint16_t>(IDAL_MASK)) == 0x100; }
// Tab 0 monitoring data response: kind = 0x200 | IDAL
constexpr bool is_tab0_response(uint16_t kind) { return (kind & ~static_cast<uint16_t>(IDAL_MASK)) == 0x200; }
constexpr bool is_ping_request(uint16_t kind) { return (kind & ~static_cast<uint16_t>(IDAL_MASK)) == PING_KIND_BASE; }
constexpr bool is_ping_response(uint16_t kind) {
  return (kind & ~static_cast<uint16_t>(IDAL_MASK)) == PING_RESPONSE_KIND_BASE;
}
// Queries and ping requests are addressed *to* a device: their CAN ID carries the
// target's IDAL and IDB, not the sender's (observed live — a directed frame arrived
// bearing our own IDB after we announced). Such frames are not evidence that the
// addressed device is alive.
constexpr bool is_directed(uint16_t kind) { return (kind & KIND_QUERY_FLAG) != 0 || is_ping_request(kind); }

const char *idal_name(uint8_t idal);

class MastervoltSensor : public sensor::Sensor {
 public:
  MastervoltSensor(uint16_t message_id, uint16_t offset) : message_id_(message_id), offset_(offset) {}

  uint16_t message_id() { return message_id_; }

  void process_message(bytebuffer::ByteBuffer data) {
    if (data.get_uint16(0) != message_id_)
      return;
    this->publish_state(data.get_float(this->offset_));
  }

 protected:
  uint16_t message_id_;
  uint16_t offset_;
};

class MastervoltDevice {
  friend class Mastervolt;

 public:
  MastervoltDevice(uint8_t idal, uint32_t idb) : idal_(idal), idb_(idb) {}

  void on_receive(uint16_t kind, bytebuffer::ByteBuffer data);
  void add_sensor(MastervoltSensor *sensor) { this->sensors_[sensor->message_id()] = sensor; }

  bool is_bound() const { return this->idb_ != IDB_UNASSIGNED; }

 protected:
  uint8_t idal_;
  uint32_t idb_;
  uint32_t last_seen_ms_{0};
  bool online_{false};
  bool duplicate_warned_{false};
  std::map<uint16_t, MastervoltSensor *> sensors_;
};

// A device seen on the bus, whether configured or not
struct DiscoveredDevice {
  uint32_t idb;
  uint8_t idal;
  uint32_t last_seen_ms;
  bool online;
};

class Mastervolt : public Component {
 public:
  Mastervolt(canbus::Canbus *canbus) : canbus_(canbus) {}
  void setup() override;
  void dump_config() override;

  void add_device(MastervoltDevice *device) { devices_.push_back(device); }

  // Send a directed ping (§5.4) to a device. Callable from lambdas, e.g. a template
  // button, to test devices on demand. The transmission is logged at INFO and frames
  // from the target are traced at INFO for a short window afterwards.
  void send_ping(uint8_t idal, uint32_t idb);
  // Send an arbitrary MasterBus frame — for protocol experiments from lambdas.
  // Logs and traces like send_ping().
  void send_frame(uint16_t kind, uint32_t idb, const std::vector<uint8_t> &data);
  // Log frames from this IDB at INFO for a few seconds, without sending anything —
  // for watching a device's unsolicited reaction to some other event (e.g. our own
  // announcement), which send_frame()'s target-based tracing can't observe
  void trace_device(uint32_t idb);
  // Re-send our announcement immediately, e.g. to provoke a newcomer reaction from
  // a device while trace_device() is watching it. Requires announce to be enabled.
  void announce_now();

  void set_debug(bool debug) { this->debug_ = debug; }
  void set_offline_timeout(uint32_t timeout_ms) { this->offline_timeout_ms_ = timeout_ms; }
  void set_announce(uint32_t own_idb, uint8_t own_idal) {
    this->announce_ = true;
    this->own_idb_ = own_idb;
    this->own_idal_ = own_idal;
  }

 protected:
  void on_receive_(uint32_t can_id, bool extended_id, bool rtr, const std::vector<uint8_t> &data);
  void process_announcement_(uint16_t kind, uint32_t idb, const std::vector<uint8_t> &data);
  // Record a device in the discovery table, creating or refreshing its entry
  void record_device_(uint32_t idb, uint8_t idal, uint32_t now);
  void update_presence_();
  void send_announcement_();
  // Send a Tab 0 query for each of the device's configured sensors — needed for
  // values the device does not broadcast unsolicited (e.g. battery temperature)
  void poll_sensors_(MastervoltDevice *device);
  // Transmit without the logging/tracing of the public send_frame()
  void send_frame_(uint16_t kind, uint32_t idb, const std::vector<uint8_t> &data);
  // Send one chunk of a string label (§7.5) in response to a query for it
  void send_label_chunk_(uint8_t idx_lo, uint8_t idx_hi, uint8_t chunk, const char *str);

  canbus::Canbus *canbus_;
  std::vector<MastervoltDevice *> devices_{};
  StaticVector<DiscoveredDevice, MASTERVOLT_MAX_DEVICES> discovered_{};
  bool debug_{};
  bool announce_{false};
  bool table_full_warned_{false};
  bool conflict_warned_{false};
  uint32_t own_idb_{IDB_UNASSIGNED};
  uint8_t own_idal_{IDAL_MASK};
  uint16_t announce_counter_{0};
  // Must match the default of the 'offline_timeout' config option
  uint32_t offline_timeout_ms_{45000};
  // Frames from this IDB are logged at INFO until the deadline — set by the manual
  // send_frame()/send_ping() so their responses are visible without full debug output
  uint32_t trace_idb_{IDB_UNASSIGNED};
  uint32_t trace_until_ms_{0};
};

}  // namespace esphome::mastervolt
