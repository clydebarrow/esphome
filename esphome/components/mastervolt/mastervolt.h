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

// Announcement: E=0, MT=10, Tab=00 (kind = 0x100 | IDAL)
constexpr bool is_announcement(uint16_t kind) { return (kind & ~static_cast<uint16_t>(IDAL_MASK)) == 0x100; }
// Tab 0 monitoring data response: kind = 0x200 | IDAL
constexpr bool is_tab0_response(uint16_t kind) { return (kind & ~static_cast<uint16_t>(IDAL_MASK)) == 0x200; }

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

  void set_debug(bool debug) { this->debug_ = debug; }
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
};

}  // namespace esphome::mastervolt
