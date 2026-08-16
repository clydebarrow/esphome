#pragma once

// defines.h MUST be the first include: define-gated action classes exist in this header, and
// main.cpp placement-news them into sizeof()-sized static buffers -- any TU parsing these class
// declarations with a different define state gets a different class size (ODR violation, boot
// crash). Including defines.h first guarantees the gates are resolved from the generated defines.
#include "esphome/core/defines.h"

#if defined(USE_PREFERENCES_BACKUP) && defined(USE_ESP32)

#include "esphome/components/storage/storage.h"
#include "preferences_backup.h"  // PrefSelection -- file scope, NOT inside the namespace below

#include "esphome/core/automation.h"
#include "esphome/core/entity_base.h"
#include "esphome/core/helpers.h"

#include <string>
#include <vector>

namespace esphome::preferences {

// The actions target the storage KeyValueStorage/RawStorage interfaces; pull the storage
// namespace in rather than qualifying every reference.
using namespace esphome::storage;  // NOLINT(google-build-using-namespace)

// preferences.export / preferences.import -- see preferences_backup.h. The selection table
// (name/key/type/count) is codegen-baked per action instance from its optional `preferences:`
// list; empty selection = all preferences (hex round-trip, types unknown).
// The two actions take either a path on a mounted storage (rendered, kv/json) or a raw device
// plus address (the encoded blob as stored). Codegen picks exactly one and hands the raw variant
// its window -- the room up to the next region on that device, 0 meaning "to the end of the
// device", which only the device itself knows.
template<typename... Ts> class ExportPreferencesAction : public Action<Ts...> {
 public:
  TEMPLATABLE_VALUE(std::string, path)
  void set_format(const char *format) { this->format_ = format; }
  void set_raw_target(RawStorage *device, uint32_t address, uint32_t window) {
    this->device_ = device;
    this->address_ = address;
    this->window_ = window;
  }
  void set_selection(const PrefSelection *selection, size_t count, bool restrict_to_selection) {
    this->selection_ = selection;
    this->count_ = count;
    this->restrict_ = restrict_to_selection;
  }
  void add_selected_entity(esphome::EntityBase *entity) { this->selected_entities_.push_back(entity); }

  void play(const Ts &...x) override {
    if (this->device_ != nullptr) {
      preferences_export_to_raw(this->device_, this->address_, this->resolved_window_(), this->selection_, this->count_,
                                this->restrict_, this->selected_entities_.data(), this->selected_entities_.size());
      return;
    }
    const std::string path = this->path_.value(x...);
    preferences_export_to_storage(path.c_str(), this->format_, this->selection_, this->count_, this->restrict_,
                                  this->selected_entities_.data(), this->selected_entities_.size());
  }

 protected:
  // window 0 = the last region on this device: everything from here to the end of it.
  uint64_t resolved_window_() {
    if (this->window_ != 0)
      return this->window_;
    RawGeometry geo;
    this->device_->get_raw_geometry(&geo);
    return geo.capacity > this->address_ ? geo.capacity - this->address_ : 0;
  }

  RawStorage *device_{nullptr};
  uint32_t address_{0};
  uint32_t window_{0};
  const char *format_{"kv"};
  const PrefSelection *selection_{nullptr};
  size_t count_{0};
  bool restrict_{false};
  std::vector<esphome::EntityBase *> selected_entities_;
};

template<typename... Ts> class ImportPreferencesAction : public Action<Ts...> {
 public:
  TEMPLATABLE_VALUE(std::string, path)
  void set_raw_target(RawStorage *device, uint32_t address, uint32_t window) {
    this->device_ = device;
    this->address_ = address;
    this->window_ = window;
  }
  void set_format(const char *format) { this->format_ = format; }
  void set_reboot(bool reboot) { this->reboot_ = reboot; }
  void set_selection(const PrefSelection *selection, size_t count, bool restrict_to_selection) {
    this->selection_ = selection;
    this->count_ = count;
    this->restrict_ = restrict_to_selection;
  }
  void add_selected_entity(esphome::EntityBase *entity) { this->selected_entities_.push_back(entity); }

  void play(const Ts &...x) override {
    if (this->device_ != nullptr) {
      preferences_import_from_raw(this->device_, this->address_, this->resolved_window_(), this->reboot_,
                                  this->selection_, this->count_, this->restrict_, this->selected_entities_.data(),
                                  this->selected_entities_.size());
      return;
    }
    const std::string path = this->path_.value(x...);
    preferences_import_from_storage(path.c_str(), this->format_, this->reboot_, this->selection_, this->count_,
                                    this->restrict_, this->selected_entities_.data(), this->selected_entities_.size());
  }

 protected:
  // window 0 = the last region on this device: everything from here to the end of it.
  uint64_t resolved_window_() {
    if (this->window_ != 0)
      return this->window_;
    RawGeometry geo;
    this->device_->get_raw_geometry(&geo);
    return geo.capacity > this->address_ ? geo.capacity - this->address_ : 0;
  }

  RawStorage *device_{nullptr};
  uint32_t address_{0};
  uint32_t window_{0};
  const char *format_{"kv"};
  bool reboot_{false};
  const PrefSelection *selection_{nullptr};
  size_t count_{0};
  bool restrict_{false};
  std::vector<esphome::EntityBase *> selected_entities_;
};

}  // namespace esphome::preferences

#endif  // USE_PREFERENCES_BACKUP && USE_ESP32
