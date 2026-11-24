#pragma once

#include <cstddef>
#include <cstdint>
#include <ctime>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "airfields.h"
#include "flight_detector.h"

namespace esphome::tug_logger {

/// What the pilot enters for a tow.
struct TowDetails {
  std::string glider_registration;
  std::string glider_pilot_1;
  std::string glider_pilot_2;
  std::string tug_pilot;
  /// The tug flown; empty means the one in the tug file
  std::string tug_registration;
};

/// The registration to record for a glider from its callsign, as the club writes it: "VHWGQ" or "VH-WGQ" is "WGQ".
/// Empty if the callsign is blank.
std::string glider_from_callsign(const char *callsign);

/// A name as Capital Case: "alex SMITH-JONES" is "Alex Smith-Jones". A word with both cases, like "McDonald", is
/// left as it was typed.
std::string capital_case(const std::string &name);

/// A flight record on the card to send: its name without .csv, and its size now.
struct FlightLogFile {
  std::string name;
  size_t size{0};
};

/// The body that sends a flight record to the Worker: {"name": ..., "csv": ...}.
std::string flight_log_body(const std::string &name, const std::string &csv);

/// What the status bar counts. Flight time is the tug's, takeoff to landing.
struct TowTotals {
  /// The local date as YYYYMMDD that tows and flight_s count, or 0 until the clock is known.
  uint32_t day{0};
  uint32_t tows{0};
  uint32_t flight_s{0};
  /// Flight time since the pilot last marked the tug as refuelled.
  uint32_t fuel_s{0};
  /// The tug chosen for the day, if not the one in the tug file. A new day goes back to the tug file's.
  std::string tug_registration;

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
  /// Above the field it launched from
  int32_t max_height_ft{0};
  /// That field's elevation: the height plus this is the altitude above mean sea level
  float field_elevation_ft{0.0f};
  bool approximate{false};
  /// The pilot entered the height in review, in place of the measured one.
  bool height_entered{false};
  TowDetails details;
};

/// The tug's own settings, from the file the Worker's admin page gives for it.
struct Provisioning {
  std::string organisation;
  std::string tug_registration;
  /// The home field, for a tow when there is no list of airfields
  float field_elevation_ft{0.0f};
  /// Where the tug may launch, from the Worker; a takeoff takes the elevation of the one it is at
  std::vector<Airfield> airfields;
  std::string upload_url;
  std::string upload_key;
  /// The tug's LoRa address and key (64 hex digits) for tows sent through a receiver; 0 and empty in a file made
  /// before the dashboard gave them
  uint16_t lora_address{0};
  std::string lora_key;
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
/// The body of POST /api/tows: the height above the field, and the altitude above mean sea level.
std::string tow_to_upload_json(const TowRecord &record, const std::string &tug_registration);
/// Reads the Worker's answer to POST /api/airfields, as the airfields file holds it, into out. False if not valid.
bool parse_airfields(const std::string &text, Provisioning &out);
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
  /// The Worker's airfields as last fetched, which replace those in the tug file. False if there are none yet.
  bool load_airfields(Provisioning &out);
  bool save_airfields(const std::string &text);
  /// For records and a flight in progress saved before each had its field's elevation
  void set_default_field_elevation_ft(float elevation_ft);

  // The flight in progress
  bool save_flight(const FlightSnapshot &snapshot, const TowDetails &details, time_t saved_at);
  bool load_flight(FlightSnapshot &snapshot, TowDetails &details, time_t &saved_at);
  void clear_flight();

  // The review list
  std::vector<TowRecord> review_list();
  bool add_review(TowRecord record);
  bool update_details(uint32_t id, const TowDetails &details);
  /// Puts the release height the pilot gives in place of the measured one.
  bool update_height(uint32_t id, int32_t height_ft);
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
  std::vector<std::string> recent_tugs();
  void remember(const TowDetails &details);
  enum class NameList : uint8_t { NAME_LIST_GLIDERS, NAME_LIST_PILOTS, NAME_LIST_TUGS };
  /// Takes a name off one of the lists of recent names.
  void forget_name(NameList list, const std::string &name);

  /// Sends each queued tow with post. Tows to try again go back in the queue, refused ones to the failed file.
  void process_queue(const std::function<UploadResult(const std::string &body)> &post);

  /// Starts a file in tug/flights/ for the record of one flight, named by its start (UTC), or by the uptime when the
  /// clock is not set.
  bool start_flight_log(time_t start, uint32_t uptime_ms);
  /// Adds lines, each ending in a newline, to that file.
  bool append_flight_log(const std::string &text);
  /// The flight is over: its record can be sent.
  void end_flight_log();
  /// Records in tug/flights/ not yet sent, or grown since they were sent, oldest first; never the one being written.
  std::vector<FlightLogFile> unsent_flight_logs();
  bool read_flight_log(const std::string &name, std::string &content, size_t max_size);
  /// Notes that the record was sent at this size, so it goes again only if it grows.
  bool mark_flight_log_sent(const FlightLogFile &file);
  size_t queued_count();
  size_t failed_count();
  /// Puts the refused tows back in the queue, to be sent again. Returns how many.
  size_t resend_failed();

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
  std::string flight_log_path_;
  bool flight_log_open_{false};
  float default_field_elevation_ft_{0.0f};
  std::mutex lock_;
  std::vector<TowRecord> review_;
  uint32_t next_id_{1};
  std::vector<std::string> recent_gliders_;
  std::vector<std::string> recent_pilots_;
  std::vector<std::string> recent_tugs_;
};

}  // namespace esphome::tug_logger
