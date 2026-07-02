#include "mastervolt.h"
#include "esphome/core/application.h"

namespace esphome::mastervolt {

using namespace bytebuffer;

// Announcements repeat ~every 30 s; a device is offline after missing three of them
static constexpr uint32_t PRESENCE_INTERVAL_MS = 30000;
static constexpr uint32_t PRESENCE_TIMEOUT_MS = 90000;
// Presence beacon: E=0, MT=10, Tab=2 (kind = 0x140 | IDAL) — observed from the display,
// not yet verified as required for other participants
static constexpr uint16_t BEACON_KIND_BASE = 0x140;
static constexpr uint16_t ANNOUNCEMENT_KIND_BASE = 0x100;

struct IdalName {
  uint8_t idal;
  const char *name;
};

static constexpr IdalName IDAL_NAMES[] = {
    {0x0a, "charger"}, {0x0e, "switch_output"}, {0x13, "dc_shunt"}, {0x14, "display"}, {0x18, "dc_dc"},
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
  this->set_interval("presence", PRESENCE_INTERVAL_MS, [this]() { this->update_presence_(); });
  if (this->announce_) {
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
  }

  if (this->announce_ && idb == this->own_idb_ && !this->conflict_warned_) {
    this->conflict_warned_ = true;
    ESP_LOGW(TAG, "Received frame with our own IDB 0x%05" PRIX32 " — address conflict, choose another own_idb", idb);
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
    if (device->online_ && now - device->last_seen_ms_ > PRESENCE_TIMEOUT_MS) {
      device->online_ = false;
      ESP_LOGI(TAG, "Device IDB 0x%05" PRIX32 " (%s) is offline", device->idb_, idal_name(device->idal_));
    }
  }
  for (auto &entry : this->discovered_) {
    if (entry.online && now - entry.last_seen_ms > PRESENCE_TIMEOUT_MS) {
      entry.online = false;
      ESP_LOGI(TAG, "Device IDB 0x%05" PRIX32 " (%s) is offline", entry.idb, idal_name(entry.idal));
    }
  }
  if (this->announce_)
    this->send_announcement_();
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
