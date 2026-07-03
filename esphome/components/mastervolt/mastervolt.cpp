#include "mastervolt.h"
#include "esphome/core/application.h"

#include <cmath>
#include <cstring>

namespace esphome::mastervolt {

using namespace bytebuffer;

// Announcements repeat ~every 30 s, matching the observed bus devices
static constexpr uint32_t ANNOUNCE_INTERVAL_MS = 30000;
// Presence beacon: E=0, MT=10, Tab=2 (kind = 0x140 | IDAL) — observed from the display,
// not yet verified as required for other participants
static constexpr uint16_t BEACON_KIND_BASE = 0x140;
static constexpr uint16_t ANNOUNCEMENT_KIND_BASE = 0x100;
// Tab 0 monitoring query (§7.1); some values (e.g. battery temperature) are never
// broadcast unsolicited and must be polled
static constexpr uint16_t TAB0_QUERY_KIND_BASE = 0x610;
static constexpr uint32_t SENSOR_POLL_INTERVAL_MS = 10000;
// After a manual send_frame()/send_ping(), frames from the target are logged at INFO
// for this long so its responses can be seen without full debug output
static constexpr uint32_t TRACE_WINDOW_MS = 2000;
// Device-type byte for our ping responses: we present ourselves as a display,
// the closest match for a monitoring device (§5.4)
static constexpr uint8_t PING_DEVICE_TYPE_DISPLAY = 0x10;

// String label indices we advertise in our 09:03 (device name) and 09:01 (article
// number) answers, and then serve when queried (§5.4, §7.5). Values copied from a
// real Masterview display.
static constexpr uint16_t LABEL_INDEX_NAME = 0x0000;
static constexpr uint16_t LABEL_INDEX_ARTICLE = 0x0022;
// Served for the article-number label; real devices report a Mastervolt part number
static constexpr char LABEL_ARTICLE[] = "ESPHome";

// Low-level queries (§5.4) we answer, echoing the two query bytes followed by these
// two reply bytes. Values copied from a real Masterview display's answers; it leaves
// other queries in the family ([08 08], [08 23]) unanswered, so we do too.
struct QueryReply {
  uint8_t q0, q1;
  uint8_t r2, r3;
};
static constexpr QueryReply QUERY_REPLIES[] = {
    {0x08, 0x3F, PING_DEVICE_TYPE_DISPLAY, 0x00},  // ping: device type
    {0x08, 0x02, 0x04, 0x00},                      // meaning unknown (battery answers 02:00)
    {0x08, 0x0B, 0x0E, 0x02},                      // meaning unknown
    {0x08, 0x12, 0x00, 0x00},                      // monitoring variable count? (battery answers 0D:00)
    {0x09, 0x01, LABEL_INDEX_ARTICLE, 0x00},       // article-number label index
    {0x09, 0x03, LABEL_INDEX_NAME, 0x00},          // device-name label index
};

struct IdalName {
  uint8_t idal;
  const char *name;
};

static constexpr IdalName IDAL_NAMES[] = {
    {0x0a, "charger"}, {0x0e, "switch_output"}, {0x13, "dc_shunt"},
    {0x14, "display"}, {0x18, "dc_dc"},         {0x1a, "usb_interface"},
};

const char *idal_name(uint8_t idal) {
  for (const auto &entry : IDAL_NAMES) {
    if (entry.idal == idal)
      return entry.name;
  }
  return "unknown";
}

void Mastervolt::setup() {
  this->canbus_->add_callback([this](uint32_t can_id, bool extended_id, bool rtr, const std::vector<uint8_t> &data) {
    this->on_receive_(can_id, extended_id, rtr, data);
  });
  this->set_interval("poll", SENSOR_POLL_INTERVAL_MS, [this]() {
    for (auto *device : this->devices_)
      this->poll_sensors_(device);
    this->update_presence_();
  });
  if (this->announce_) {
    this->set_interval("announce", ANNOUNCE_INTERVAL_MS, [this]() { this->send_announcement_(); });
    // Join the bus per protocol §5.2 after allowing the CAN driver to settle.
    // Every device replies with its own announcement, populating the discovery table.
    this->set_timeout("join", 500, [this]() {
      uint16_t beacon_kind = BEACON_KIND_BASE | this->own_idal_;
      ESP_LOGD(TAG, "Sending presence beacon, kind 0x%03X", beacon_kind);
      this->canbus_->send_data(make_can_id(beacon_kind, this->own_idb_), true, {});
      this->send_announcement_();
    });
  }
}

void Mastervolt::on_receive_(uint32_t can_id, bool extended_id, bool rtr, const std::vector<uint8_t> &data) {
  if (!extended_id)
    return;
  auto kind = kind_of(can_id);
  auto idb = idb_of(can_id);
  auto idal = idal_of(kind);
  auto now = App.get_loop_component_start_time();

  if (this->debug_) {
    char hexbuf[format_hex_pretty_size(canbus::CAN_MAX_DATA_LENGTH)];
    ESP_LOGD(TAG, "RX kind 0x%03X, IDB 0x%05" PRIX32 ", data: %s", kind, idb, format_hex_pretty_to(hexbuf, data));
  } else if (idb == this->trace_idb_ && millis() < this->trace_until_ms_) {
    // Trace responses to a recent manual send_frame()/send_ping() without full debug
    char hexbuf[format_hex_pretty_size(canbus::CAN_MAX_DATA_LENGTH)];
    ESP_LOGI(TAG, "RX kind 0x%03X, IDB 0x%05" PRIX32 ", data: %s", kind, idb, format_hex_pretty_to(hexbuf, data));
  }

  // Frames carrying our own IDB are addressed *to* us (pings, queries) — normal
  // traffic, except an announcement claiming our IDB, which is an address conflict.
  if (idb == this->own_idb_) {
    if (is_announcement(kind)) {
      if (!this->conflict_warned_) {
        this->conflict_warned_ = true;
        ESP_LOGW(TAG, "Another device announced our own IDB 0x%05" PRIX32 " — address conflict, choose another own_idb",
                 idb);
      }
      return;
    }
    if (is_ping_request(kind) && data.size() >= 2) {
      // Serve the string labels our 09:01/09:03 answers point at (§7.5)
      if (data[0] == 0x30 && data.size() >= 4) {
        const char *str = nullptr;
        switch (data[1] | (data[2] << 8)) {
          case LABEL_INDEX_NAME:
            str = App.get_name().c_str();
            break;
          case LABEL_INDEX_ARTICLE:
            str = LABEL_ARTICLE;
            break;
        }
        if (str != nullptr) {
          this->send_label_chunk_(data[1], data[2], data[3], str);
          return;
        }
      }
      // [08 1F] asks for the device class; devices answer with their own IDAL
      if (data[0] == 0x08 && data[1] == 0x1F) {
        ESP_LOGV(TAG, "Answering query [08 1F] (kind 0x%03X)", kind);
        this->send_frame_(PING_RESPONSE_KIND_BASE | this->own_idal_, this->own_idb_,
                          {0x08, 0x1F, this->own_idal_, 0x00});
        return;
      }
      // Answer known low-level queries so the sender doesn't keep retrying (§5.4)
      for (const auto &reply : QUERY_REPLIES) {
        if (data[0] == reply.q0 && data[1] == reply.q1) {
          ESP_LOGV(TAG, "Answering query [%02X %02X] (kind 0x%03X)", data[0], data[1], kind);
          this->send_frame_(PING_RESPONSE_KIND_BASE | this->own_idal_, this->own_idb_,
                            {data[0], data[1], reply.r2, reply.r3});
          return;
        }
      }
    }
    if (this->debug_) {
      char hexbuf[format_hex_pretty_size(canbus::CAN_MAX_DATA_LENGTH)];
      ESP_LOGD(TAG, "Directed frame to us: kind 0x%03X, data: %s", kind, format_hex_pretty_to(hexbuf, data));
    }
    return;
  }

  // Directed frames name their recipient, not their sender — no discovery or routing
  if (is_directed(kind))
    return;

  // The 0x180 response kind family is shared with string-label and other low-level
  // query responses; a real ping response echoes the request payload [0x08, 0x3F]
  if (this->debug_ && is_ping_response(kind)) {
    char hexbuf[format_hex_pretty_size(canbus::CAN_MAX_DATA_LENGTH)];
    if (data.size() >= 2 && data[0] == 0x08 && data[1] == 0x3F) {
      ESP_LOGI(TAG, "Ping response from IDB 0x%05" PRIX32 " (%s), data: %s", idb, idal_name(idal),
               format_hex_pretty_to(hexbuf, data));
    } else {
      ESP_LOGI(TAG, "Query response from IDB 0x%05" PRIX32 " (%s), data: %s", idb, idal_name(idal),
               format_hex_pretty_to(hexbuf, data));
    }
  }

  if (is_announcement(kind)) {
    this->process_announcement_(kind, idb, data);
  } else {
    this->record_device_(idb, idal, now);
  }

  // Route to the configured device already bound to this IDB
  for (auto *device : this->devices_) {
    if (device->idb_ == idb) {
      device->last_seen_ms_ = now;
      if (!device->online_) {
        device->online_ = true;
        ESP_LOGI(TAG, "Device IDB 0x%05" PRIX32 " (%s) is online", idb, idal_name(device->idal_));
      }
      device->on_receive(kind, ByteBuffer::wrap(data, LITTLE));
      return;
    }
  }

  // Bind an auto-discover device of the same class. Validation guarantees at most one
  // unbound entry per class, so first match is deterministic.
  for (auto *device : this->devices_) {
    if (!device->is_bound() && device->idal_ == idal) {
      device->idb_ = idb;
      device->last_seen_ms_ = now;
      device->online_ = true;
      ESP_LOGI(TAG, "Bound '%s' device to IDB 0x%05" PRIX32, idal_name(idal), idb);
      device->on_receive(kind, ByteBuffer::wrap(data, LITTLE));
      this->poll_sensors_(device);
      return;
    }
  }

  // Frame from an unconfigured device: if a bound device shares its class, the user may
  // have intended this unit instead — suggest pinning by IDB (once per configured device)
  for (auto *device : this->devices_) {
    if (device->is_bound() && device->idal_ == idal && device->idb_ != idb && !device->duplicate_warned_) {
      device->duplicate_warned_ = true;
      ESP_LOGW(TAG,
               "Another %s (IDB 0x%05" PRIX32 ") is on the bus; configured device is bound to IDB 0x%05" PRIX32
               ". Set 'idb' to select a specific unit",
               idal_name(idal), idb, device->idb_);
    }
  }
}

void Mastervolt::process_announcement_(uint16_t kind, uint32_t idb, const std::vector<uint8_t> &data) {
  auto idal = idal_of(kind);
  auto now = App.get_loop_component_start_time();
  if (data.size() >= 6) {
    if (data[0] != idal) {
      ESP_LOGW(TAG, "Announcement from IDB 0x%05" PRIX32 ": payload IDAL 0x%02X does not match kind IDAL 0x%02X", idb,
               data[0], idal);
    }
    uint32_t payload_idb = data[1] | (static_cast<uint32_t>(data[2]) << 8) | (static_cast<uint32_t>(data[3]) << 16);
    if ((payload_idb & IDB_MASK) != idb) {
      ESP_LOGW(TAG, "Announcement from IDB 0x%05" PRIX32 ": payload IDB 0x%05" PRIX32 " does not match CAN ID", idb,
               payload_idb);
    }
  } else {
    ESP_LOGW(TAG, "Short announcement (%zu bytes) from IDB 0x%05" PRIX32, data.size(), idb);
  }
  this->record_device_(idb, idal, now);
}

void Mastervolt::record_device_(uint32_t idb, uint8_t idal, uint32_t now) {
  for (auto &entry : this->discovered_) {
    if (entry.idb == idb) {
      entry.last_seen_ms = now;
      if (!entry.online) {
        entry.online = true;
        ESP_LOGI(TAG, "Device IDB 0x%05" PRIX32 " (%s) is back online", idb, idal_name(entry.idal));
      }
      return;
    }
  }
  if (this->discovered_.size() >= MASTERVOLT_MAX_DEVICES) {
    if (!this->table_full_warned_) {
      this->table_full_warned_ = true;
      ESP_LOGW(TAG, "Discovery table full (%d entries); increase max_devices", MASTERVOLT_MAX_DEVICES);
    }
    return;
  }
  ESP_LOGI(TAG, "Discovered device: IDB 0x%05" PRIX32 ", class 0x%02X (%s)", idb, idal, idal_name(idal));
  this->discovered_.push_back({idb, idal, now, true});
}

void Mastervolt::update_presence_() {
  const uint32_t now = millis();
  for (auto *device : this->devices_) {
    if (device->online_ && now - device->last_seen_ms_ > this->offline_timeout_ms_) {
      device->online_ = false;
      ESP_LOGI(TAG, "Device IDB 0x%05" PRIX32 " (%s) is offline", device->idb_, idal_name(device->idal_));
      // Mark the device's sensors unknown until fresh data arrives
      for (const auto &pair : device->sensors_)
        pair.second->publish_state(NAN);
    }
  }
  for (auto &entry : this->discovered_) {
    if (entry.online && now - entry.last_seen_ms > this->offline_timeout_ms_) {
      entry.online = false;
      ESP_LOGI(TAG, "Device IDB 0x%05" PRIX32 " (%s) is offline", entry.idb, idal_name(entry.idal));
    }
  }
}

void Mastervolt::send_announcement_() {
  uint16_t kind = ANNOUNCEMENT_KIND_BASE | this->own_idal_;
  std::vector<uint8_t> payload = {
      this->own_idal_,
      static_cast<uint8_t>(this->own_idb_),
      static_cast<uint8_t>(this->own_idb_ >> 8),
      static_cast<uint8_t>(this->own_idb_ >> 16),
      static_cast<uint8_t>(this->announce_counter_),
      static_cast<uint8_t>(this->announce_counter_ >> 8),
      0,
      0,
  };
  this->canbus_->send_data(make_can_id(kind, this->own_idb_), true, payload);
  this->announce_counter_++;
}

void Mastervolt::send_ping(uint8_t idal, uint32_t idb) {
  uint16_t kind = PING_KIND_BASE | (idal & IDAL_MASK);
  this->send_frame(kind, idb, {0x08, 0x3F});
}

void Mastervolt::send_frame(uint16_t kind, uint32_t idb, const std::vector<uint8_t> &data) {
  char hexbuf[format_hex_pretty_size(canbus::CAN_MAX_DATA_LENGTH)];
  ESP_LOGI(TAG, "TX kind 0x%03X to IDB 0x%05" PRIX32 " (%s), data: %s", kind, idb, idal_name(idal_of(kind)),
           format_hex_pretty_to(hexbuf, data));
  this->trace_idb_ = idb & IDB_MASK;
  this->trace_until_ms_ = millis() + TRACE_WINDOW_MS;
  this->send_frame_(kind, idb, data);
}

void Mastervolt::send_frame_(uint16_t kind, uint32_t idb, const std::vector<uint8_t> &data) {
  this->canbus_->send_data(make_can_id(kind, idb & IDB_MASK), true, data);
}

void Mastervolt::trace_device(uint32_t idb) {
  ESP_LOGI(TAG, "Tracing IDB 0x%05" PRIX32 " for %u ms", idb & IDB_MASK, TRACE_WINDOW_MS);
  this->trace_idb_ = idb & IDB_MASK;
  this->trace_until_ms_ = millis() + TRACE_WINDOW_MS;
}

void Mastervolt::announce_now() {
  if (!this->announce_) {
    ESP_LOGW(TAG, "announce_now() requires 'announce' to be enabled");
    return;
  }
  ESP_LOGI(TAG, "Re-sending announcement (IDB 0x%05" PRIX32 ", IDAL 0x%02X)", this->own_idb_, this->own_idal_);
  this->send_announcement_();
}

void Mastervolt::poll_sensors_(MastervoltDevice *device) {
  if (!device->is_bound())
    return;
  uint16_t kind = TAB0_QUERY_KIND_BASE | device->idal_;
  for (const auto &pair : device->sensors_) {
    this->send_frame_(kind, device->idb_, {static_cast<uint8_t>(pair.first), static_cast<uint8_t>(pair.first >> 8)});
  }
}

void Mastervolt::send_label_chunk_(uint8_t idx_lo, uint8_t idx_hi, uint8_t chunk, const char *str) {
  std::vector<uint8_t> payload{0x30, idx_lo, idx_hi, chunk};
  size_t len = strlen(str);
  size_t offset = static_cast<size_t>(chunk) * 4;
  // Up to four characters per chunk; a chunk containing the terminating NUL ends early
  for (size_t i = offset; i < offset + 4; i++) {
    payload.push_back(i < len ? str[i] : 0);
    if (i >= len)
      break;
  }
  ESP_LOGV(TAG, "Answering label 0x%04X chunk %u", idx_lo | (idx_hi << 8), chunk);
  this->send_frame_(PING_RESPONSE_KIND_BASE | this->own_idal_, this->own_idb_, payload);
}

void Mastervolt::dump_config() {
  ESP_LOGCONFIG(TAG, "Mastervolt:");
  if (this->announce_) {
    ESP_LOGCONFIG(TAG, "  Announce: own IDB 0x%05" PRIX32 ", own IDAL 0x%02X", this->own_idb_, this->own_idal_);
  }
  ESP_LOGCONFIG(TAG, "  Configured devices:");
  for (auto *device : this->devices_) {
    if (device->is_bound()) {
      ESP_LOGCONFIG(TAG, "    %s: IDB 0x%05" PRIX32 " (%s)", idal_name(device->idal_), device->idb_,
                    device->online_ ? "online" : "offline");
    } else {
      ESP_LOGCONFIG(TAG, "    %s: unbound (waiting for discovery)", idal_name(device->idal_));
    }
  }
  ESP_LOGCONFIG(TAG, "  Discovered devices:");
  for (auto &entry : this->discovered_) {
    ESP_LOGCONFIG(TAG, "    IDB 0x%05" PRIX32 ", class 0x%02X (%s), %s", entry.idb, entry.idal, idal_name(entry.idal),
                  entry.online ? "online" : "offline");
  }
}

void MastervoltDevice::on_receive(uint16_t kind, ByteBuffer data) {
  if (!is_tab0_response(kind)) {
    // Other message kinds (announcements, Tab 1-3 responses, pings) carry no sensor data
    ESP_LOGV(TAG, "Ignoring kind 0x%03X from IDB 0x%05" PRIX32, kind, this->idb_);
    return;
  }
  char hexbuf[format_hex_pretty_size(canbus::CAN_MAX_DATA_LENGTH)];
  if (data.get_capacity() < 6) {
    ESP_LOGW(TAG, "Short Tab 0 frame from IDB 0x%05" PRIX32 ": %s", this->idb_,
             format_hex_pretty_to(hexbuf, data.get_data()));
    return;
  }
  auto command = data.get_uint16(0);
  if (this->sensors_.contains(command)) {
    this->sensors_[command]->process_message(data);
    return;
  }
  ESP_LOGD(TAG, "Unknown data %s for device IDB 0x%05" PRIX32, format_hex_pretty_to(hexbuf, data.get_data()),
           this->idb_);
}

}  // namespace esphome::mastervolt
