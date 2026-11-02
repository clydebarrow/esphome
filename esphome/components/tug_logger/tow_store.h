#pragma once

#include <cstddef>
#include <cstdint>
#include <ctime>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "flight_detector.h"

namespace esphome::tug_logger {

/// What the pilot enters for a tow.
struct TowDetails {
  std::string glider_registration;
  std::string glider_pilot_1;
  std::string glider_pilot_2;
  std::string tug_pilot;
};

/// The registration to record for a glider from its callsign, as the club writes it: "VHWGQ" or "VH-WGQ" is "WGQ".
/// Empty if the callsign is blank.
std::string glider_from_callsign(const char *callsign);

/// A name as Capital Case: "alex SMITH-JONES" is "Alex Smith-Jones". A word with both cases, like "McDonald", is
/// left as it was typed.
std::string capital_case(const std::string &name);

/// What the status bar counts. Flight time is the tug's, takeoff to landing.
struct TowTotals {
  /// The local date as YYYYMMDD that tows and flight_s count, or 0 until the clock is known.
  uint32_t day{0};
  uint32_t tows{0};
  uint32_t flight_s{0};
  /// Flight time since the pilot last marked the tug as refuelled.
  uint32_t fuel_s{0};

  /// Moves to a date, which starts the day's counts again; a count made before the clock was set belongs to the first
  /// date known. True if anything changed.
  bool roll_to(uint32_t new_day);
  void add_flight(uint32_t seconds);
  /// Takes a tow back out of the day's counts. It still burned the fuel.
  void remove_flight(uint32_t seconds);
};

/// A landed tow waiting for the pilot, on the review list.
struct TowRecord {
  /// Identifies the record on the list; stays the same when it is edited.
  uint32_t id{0};
  /// UTC seconds of takeoff; 0 if the clock was not set, and then it cannot be confirmed.
  time_t launch{0};
  /// When launch was on the monotonic clock, while the launch is unknown. Lost at a restart.
  uint32_t launch_ms{0};
  bool launch_ms_valid{false};
  uint32_t duration_s{0};
  uint32_t climb_s{0};
  int32_t max_height_ft{0};
  bool approximate{false};
  TowDetails details;
};

/// The tug's own settings, from the file the Worker's admin page gives for it.
struct Provisioning {
  std::string organisation;
  std::string tug_registration;
  float field_elevation_ft{0.0f};
  std::string upload_url;
  std::string upload_key;
};

enum class ConfirmResult : uint8_t {
  CONFIRM_RESULT_OK,
  CONFIRM_RESULT_NOT_FOUND,
  CONFIRM_RESULT_NO_GLIDER,
  CONFIRM_RESULT_NO_TIME,
  CONFIRM_RESULT_NO_TUG,
  CONFIRM_RESULT_WRITE_FAILED,
};

enum class UploadResult : uint8_t {
  UPLOAD_RESULT_SENT,
  /// Network or server trouble, or HTTP 401 (the key may be fixed): send it again later
  UPLOAD_RESULT_RETRY,
  /// Any other 4xx: the server will never take this tow
  UPLOAD_RESULT_REFUSED,
};

/// How to treat a server's answer; status is 0 or less if there was none.
UploadResult classify_status(int status);

/// The body of POST /api/tows.
std::string tow_to_upload_json(const TowRecord &record, const std::string &tug_registration);
/// Parses the provisioning file; false if it lacks the tug's registration, upload URL or key.
bool parse_provisioning(const std::string &text, Provisioning &out);
/// "2026-11-01T08:59:01Z"
std::string format_utc(time_t time);

/// The tug's files on the SD card. Any method may be called from any task; none holds the lock across an upload.
class TowStore {
 public:
  /// card_root is where the provisioning file is; the store's own files go in card_root/tug.
  explicit TowStore(std::string card_root);

  const std::string &root() const { return this->root_; }

  /// Makes the directory and reads the review list. False if the card cannot be used.
  bool begin();

  /// Reads card_root/tug.json, or the only tug-*.json there. False if none, more than one, or it isn't valid.
  bool load_provisioning(Provisioning &out);

  // The flight in progress
  bool save_flight(const FlightSnapshot &snapshot, const TowDetails &details, time_t saved_at);
  bool load_flight(FlightSnapshot &snapshot, TowDetails &details, time_t &saved_at);
  void clear_flight();

  // The review list
  std::vector<TowRecord> review_list();
  bool add_review(TowRecord record);
  bool update_details(uint32_t id, const TowDetails &details);
  ConfirmResult confirm(uint32_t id, const std::string &tug_registration);
  /// Takes a tow off the list, as not a tow. It is kept in a file of its own.
  bool discard(uint32_t id);
  /// Fills in launch times once the clock is known; true if any changed.
  bool resolve_launch_times(time_t now, uint32_t now_ms);

  // The counts in the status bar; all zero if none are saved
  TowTotals load_totals();
  bool save_totals(const TowTotals &totals);

  // Recent names, most recent first
  std::vector<std::string> recent_gliders();
  std::vector<std::string> recent_pilots();
  void remember(const TowDetails &details);

  /// Sends each queued tow with post. Tows to try again go back in the queue, refused ones to the failed file.
  void process_queue(const std::function<UploadResult(const std::string &body)> &post);
  size_t queued_count();
  size_t failed_count();

 protected:
  std::string path_(const char *name) const;
  bool write_atomic_(const char *name, const std::string &content);
  bool read_with_fallback_(const char *name, std::string &content);
  bool append_line_(const char *name, const std::string &line);
  size_t count_lines_(const char *name) const;
  bool save_review_();
  bool load_recent_();
  bool save_recent_();

  std::string root_;
  std::string dir_;
  std::mutex lock_;
  std::vector<TowRecord> review_;
  uint32_t next_id_{1};
  std::vector<std::string> recent_gliders_;
  std::vector<std::string> recent_pilots_;
};

}  // namespace esphome::tug_logger
