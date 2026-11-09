#include "tow_store.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cinttypes>
#include <ctime>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "esphome/components/json/json_util.h"

namespace esphome::tug_logger {

std::string glider_from_callsign(const char *callsign) {
  std::string text;
  for (const char *c = callsign; *c != 0; c++) {
    if (*c != ' ')
      text += static_cast<char>(std::toupper(static_cast<unsigned char>(*c)));
  }
  for (const char *prefix : {"VH-", "VH"}) {
    const size_t length = strlen(prefix);
    if (text.size() == length + 3 && text.compare(0, length, prefix) == 0)
      return text.substr(length);
  }
  return text;
}

std::string capital_case(const std::string &name) {
  std::string out;
  size_t start = 0;
  while (start < name.size()) {
    size_t end = name.find(' ', start);
    if (end == std::string::npos)
      end = name.size();
    std::string word = name.substr(start, end - start);
    bool upper = false, lower = false;
    for (const unsigned char c : word) {
      upper = upper || std::isupper(c);
      lower = lower || std::islower(c);
    }
    if (!upper || !lower) {
      bool capital = true;
      for (char &c : word) {
        const auto letter = static_cast<unsigned char>(c);
        c = static_cast<char>(capital ? std::toupper(letter) : std::tolower(letter));
        capital = c == '-' || c == '\'';
      }
    }
    out += word;
    if (end != name.size())
      out += ' ';
    start = end + 1;
  }
  return out;
}

bool TowTotals::roll_to(uint32_t new_day) {
  if (new_day == 0 || new_day == this->day)
    return false;
  if (this->day != 0) {
    this->tows = 0;
    this->flight_s = 0;
  }
  this->day = new_day;
  return true;
}

void TowTotals::add_flight(uint32_t seconds) {
  this->tows++;
  this->flight_s += seconds;
  this->fuel_s += seconds;
}

void TowTotals::remove_flight(uint32_t seconds) {
  this->tows = this->tows != 0 ? this->tows - 1 : 0;
  this->flight_s = this->flight_s > seconds ? this->flight_s - seconds : 0;
}

static constexpr const char *TOTALS_FILE = "totals.json";
static constexpr const char *REVIEW_FILE = "review.jsonl";
static constexpr const char *CURRENT_FILE = "current.json";
static constexpr const char *RECENT_FILE = "recent.json";
static constexpr const char *PENDING_FILE = "tows-pending.jsonl";
static constexpr const char *SENDING_FILE = "tows-sending.jsonl";
static constexpr const char *FAILED_FILE = "tows-failed.jsonl";
static constexpr const char *SENT_FILE = "tows-sent.jsonl";
static constexpr const char *DISCARDED_FILE = "tows-discarded.jsonl";
static constexpr const char *TEMP_SUFFIX = ".tmp";
// The server takes up to 4 KB, and refuses more for good
static constexpr size_t MAX_LINE = 4096;
static constexpr size_t MAX_FILE = 256 * 1024;
static constexpr size_t MAX_RECENT = 40;
static constexpr size_t MAX_PROVISIONING = 4096;

UploadResult classify_status(int status) {
  if (status >= 200 && status < 300)
    return UploadResult::UPLOAD_RESULT_SENT;
  if (status >= 400 && status < 500 && status != 401)
    return UploadResult::UPLOAD_RESULT_REFUSED;
  return UploadResult::UPLOAD_RESULT_RETRY;
}

std::string format_utc(time_t time) {
  struct tm parts;
  gmtime_r(&time, &parts);
  char text[32];
  strftime(text, sizeof(text), "%Y-%m-%dT%H:%M:%SZ", &parts);
  return text;
}

std::string tow_to_upload_json(const TowRecord &record, const std::string &tug_registration) {
  return json::build_json([&](JsonObject root) {
    root["tug_registration"] = tug_registration;
    root["glider_registration"] = record.details.glider_registration;
    root["tug_pilot"] = record.details.tug_pilot;
    root["glider_pilot_1"] = record.details.glider_pilot_1;
    if (!record.details.glider_pilot_2.empty())
      root["glider_pilot_2"] = record.details.glider_pilot_2;
    root["launch"] = format_utc(record.launch);
    root["max_height_ft"] = std::max<int32_t>(record.max_height_ft, 0);
    root["climb_seconds"] = record.climb_s;
    root["duration"] = record.duration_s;
    root["height_approximate"] = record.approximate;
  });
}

static std::string record_to_json(const TowRecord &record) {
  return json::build_json([&](JsonObject root) {
    root["id"] = record.id;
    root["launch"] = (int64_t) record.launch;
    root["duration"] = record.duration_s;
    root["climb"] = record.climb_s;
    root["height"] = record.max_height_ft;
    root["approximate"] = record.approximate;
    root["entered"] = record.height_entered;
    root["glider"] = record.details.glider_registration;
    root["pilot1"] = record.details.glider_pilot_1;
    root["pilot2"] = record.details.glider_pilot_2;
    root["tug_pilot"] = record.details.tug_pilot;
  });
}

static void details_from_json(JsonObject root, TowDetails &details) {
  details.glider_registration = root["glider"] | "";
  details.glider_pilot_1 = root["pilot1"] | "";
  details.glider_pilot_2 = root["pilot2"] | "";
  details.tug_pilot = root["tug_pilot"] | "";
}

static bool record_from_json(const std::string &line, TowRecord &record) {
  return json::parse_json(line, [&](JsonObject root) -> bool {
    if (!root["id"].is<uint32_t>())
      return false;
    record.id = root["id"];
    record.launch = (time_t) (root["launch"] | (int64_t) 0);
    record.duration_s = root["duration"] | 0u;
    record.climb_s = root["climb"] | 0u;
    record.max_height_ft = root["height"] | 0;
    record.approximate = root["approximate"] | false;
    record.height_entered = root["entered"] | false;
    details_from_json(root, record.details);
    return true;
  });
}

bool parse_provisioning(const std::string &text, Provisioning &out) {
  bool valid = false;
  json::parse_json(text, [&](JsonObject root) -> bool {
    out.organisation = root["organisation"] | "";
    out.tug_registration = root["tug"]["registration"] | "";
    out.field_elevation_ft = root["field_elevation_ft"] | 0.0f;
    out.upload_url = root["flight_upload_url"] | "";
    out.upload_key = root["flight_upload_key"] | "";
    valid = !out.tug_registration.empty() && !out.upload_url.empty() && !out.upload_key.empty();
    return true;
  });
  return valid;
}

static bool read_file(const std::string &path, std::string &content, size_t max_size = MAX_FILE) {
  FILE *file = fopen(path.c_str(), "rb");
  if (file == nullptr)
    return false;
  content.clear();
  char buffer[512];
  size_t count;
  while ((count = fread(buffer, 1, sizeof(buffer), file)) != 0 && content.size() < max_size)
    content.append(buffer, count);
  fclose(file);
  return true;
}

static bool file_exists(const std::string &path) {
  struct stat st;
  return stat(path.c_str(), &st) == 0;
}

static std::vector<std::string> split_lines(const std::string &text) {
  std::vector<std::string> lines;
  size_t start = 0;
  while (start < text.size()) {
    size_t end = text.find('\n', start);
    if (end == std::string::npos)
      end = text.size();
    size_t stop = end;
    while (stop > start && text[stop - 1] == '\r')
      stop--;
    if (stop > start)
      lines.emplace_back(text, start, stop - start);
    start = end + 1;
  }
  return lines;
}

TowStore::TowStore(std::string card_root) : root_(std::move(card_root)), dir_(root_ + "/tug") {}

std::string TowStore::path_(const char *name) const { return this->dir_ + "/" + name; }

// FAT will not rename over a file, so the old one goes first. A restart between the two leaves the temp file as the
// only copy, which read_with_fallback_() finds; a temp file next to the real one may be partial and is ignored.
bool TowStore::write_atomic_(const char *name, const std::string &content) {
  const std::string path = this->path_(name);
  const std::string temp = path + TEMP_SUFFIX;
  FILE *file = fopen(temp.c_str(), "wb");
  if (file == nullptr)
    return false;
  const bool written = fwrite(content.data(), 1, content.size(), file) == content.size() && fflush(file) == 0 &&
                       fsync(fileno(file)) == 0;
  fclose(file);
  if (!written) {
    remove(temp.c_str());
    return false;
  }
  remove(path.c_str());
  return rename(temp.c_str(), path.c_str()) == 0;
}

bool TowStore::read_with_fallback_(const char *name, std::string &content) {
  const std::string path = this->path_(name);
  if (read_file(path, content))
    return true;
  const std::string temp = path + TEMP_SUFFIX;
  if (!read_file(temp, content))
    return false;
  rename(temp.c_str(), path.c_str());
  return true;
}

bool TowStore::start_flight_log(time_t start, uint32_t uptime_ms) {
  std::lock_guard<std::mutex> guard(this->lock_);
  const std::string dir = this->dir_ + "/flights";
  if (mkdir(dir.c_str(), 0775) != 0 && errno != EEXIST)
    return false;
  char name[32];
  if (start != 0) {
    struct tm utc;
    gmtime_r(&start, &utc);
    strftime(name, sizeof(name), "%Y%m%d-%H%M%S", &utc);
  } else {
    snprintf(name, sizeof(name), "no-clock-%" PRIu32, uptime_ms);
  }
  this->flight_log_path_ = dir + "/" + name + ".csv";
  return true;
}

bool TowStore::append_flight_log(const std::string &text) {
  std::lock_guard<std::mutex> guard(this->lock_);
  if (this->flight_log_path_.empty())
    return false;
  FILE *file = fopen(this->flight_log_path_.c_str(), "ab");
  if (file == nullptr)
    return false;
  const bool written = fwrite(text.data(), 1, text.size(), file) == text.size() && fflush(file) == 0;
  fclose(file);
  return written;
}

bool TowStore::append_line_(const char *name, const std::string &line) {
  FILE *file = fopen(this->path_(name).c_str(), "ab");
  if (file == nullptr)
    return false;
  const bool written = fprintf(file, "%s\n", line.c_str()) > 0 && fflush(file) == 0 && fsync(fileno(file)) == 0;
  fclose(file);
  return written;
}

size_t TowStore::count_lines_(const char *name) const {
  std::string content;
  if (!read_file(this->path_(name), content))
    return 0;
  return split_lines(content).size();
}

bool TowStore::begin() {
  std::lock_guard<std::mutex> guard(this->lock_);
  if (mkdir(this->dir_.c_str(), 0775) != 0 && errno != EEXIST)
    return false;
  this->review_.clear();
  std::string content;
  if (this->read_with_fallback_(REVIEW_FILE, content)) {
    for (const std::string &line : split_lines(content)) {
      TowRecord record;
      if (record_from_json(line, record)) {
        this->review_.push_back(std::move(record));
        this->next_id_ = std::max(this->next_id_, this->review_.back().id + 1);
      }
    }
  }
  this->load_recent_();
  return true;
}

bool TowStore::load_provisioning(Provisioning &out) {
  std::string path = this->root_ + "/tug.json";
  if (!file_exists(path)) {
    DIR *dir = opendir(this->root_.c_str());
    if (dir == nullptr)
      return false;
    int found = 0;
    while (struct dirent *entry = readdir(dir)) {
      const char *name = entry->d_name;
      const size_t length = strlen(name);
      if (length > 9 && strncasecmp(name, "tug-", 4) == 0 && strcasecmp(name + length - 5, ".json") == 0) {
        path = this->root_ + "/" + name;
        found++;
      }
    }
    closedir(dir);
    if (found != 1)
      return false;
  }
  std::string content;
  return read_file(path, content, MAX_PROVISIONING) && parse_provisioning(content, out);
}

bool TowStore::save_flight(const FlightSnapshot &snapshot, const TowDetails &details, time_t saved_at) {
  const std::string text = json::build_json([&](JsonObject root) {
    root["launch"] = (int64_t) snapshot.launch;
    root["elapsed"] = snapshot.elapsed_s;
    root["climb"] = snapshot.climb_s;
    root["height"] = snapshot.max_height_ft;
    root["approximate"] = snapshot.approximate;
    root["kind"] = (uint8_t) snapshot.kind;
    root["reference"] = snapshot.reference_m;
    root["has_reference"] = snapshot.has_reference;
    root["saved_at"] = (int64_t) saved_at;
    root["glider"] = details.glider_registration;
    root["pilot1"] = details.glider_pilot_1;
    root["pilot2"] = details.glider_pilot_2;
    root["tug_pilot"] = details.tug_pilot;
  });
  std::lock_guard<std::mutex> guard(this->lock_);
  return this->write_atomic_(CURRENT_FILE, text);
}

bool TowStore::load_flight(FlightSnapshot &snapshot, TowDetails &details, time_t &saved_at) {
  std::lock_guard<std::mutex> guard(this->lock_);
  std::string content;
  if (!this->read_with_fallback_(CURRENT_FILE, content))
    return false;
  return json::parse_json(content, [&](JsonObject root) -> bool {
    if (!root["elapsed"].is<uint32_t>())
      return false;
    snapshot.launch = (time_t) (root["launch"] | (int64_t) 0);
    snapshot.elapsed_s = root["elapsed"] | 0u;
    snapshot.climb_s = root["climb"] | 0u;
    snapshot.max_height_ft = root["height"] | 0;
    snapshot.approximate = root["approximate"] | false;
    snapshot.kind = (AltitudeKind) (root["kind"] | (uint8_t) 0);
    snapshot.reference_m = root["reference"] | 0.0f;
    snapshot.has_reference = root["has_reference"] | false;
    saved_at = (time_t) (root["saved_at"] | (int64_t) 0);
    details_from_json(root, details);
    return true;
  });
}

void TowStore::clear_flight() {
  std::lock_guard<std::mutex> guard(this->lock_);
  remove(this->path_(CURRENT_FILE).c_str());
  remove((this->path_(CURRENT_FILE) + TEMP_SUFFIX).c_str());
}

std::vector<TowRecord> TowStore::review_list() {
  std::lock_guard<std::mutex> guard(this->lock_);
  return this->review_;
}

bool TowStore::save_review_() {
  std::string content;
  for (const TowRecord &record : this->review_) {
    content += record_to_json(record);
    content += '\n';
  }
  return this->write_atomic_(REVIEW_FILE, content);
}

bool TowStore::add_review(TowRecord record) {
  std::lock_guard<std::mutex> guard(this->lock_);
  record.id = this->next_id_++;
  this->review_.push_back(std::move(record));
  return this->save_review_();
}

bool TowStore::update_height(uint32_t id, int32_t height_ft) {
  std::lock_guard<std::mutex> guard(this->lock_);
  for (TowRecord &record : this->review_) {
    if (record.id == id) {
      record.max_height_ft = height_ft;
      // The pilot's figure is not the tug's estimate
      record.approximate = false;
      record.height_entered = true;
      return this->save_review_();
    }
  }
  return false;
}

bool TowStore::update_details(uint32_t id, const TowDetails &details) {
  std::lock_guard<std::mutex> guard(this->lock_);
  for (TowRecord &record : this->review_) {
    if (record.id == id) {
      record.details = details;
      return this->save_review_();
    }
  }
  return false;
}

ConfirmResult TowStore::confirm(uint32_t id, const std::string &tug_registration) {
  std::lock_guard<std::mutex> guard(this->lock_);
  auto found = std::find_if(this->review_.begin(), this->review_.end(),
                            [id](const TowRecord &record) { return record.id == id; });
  if (found == this->review_.end())
    return ConfirmResult::CONFIRM_RESULT_NOT_FOUND;
  if (found->details.glider_registration.empty())
    return ConfirmResult::CONFIRM_RESULT_NO_GLIDER;
  if (found->launch == 0)
    return ConfirmResult::CONFIRM_RESULT_NO_TIME;
  if (tug_registration.empty())
    return ConfirmResult::CONFIRM_RESULT_NO_TUG;
  // Queued first: a restart before the list is saved confirms it twice, which the server takes as a duplicate
  if (!this->append_line_(PENDING_FILE, tow_to_upload_json(*found, tug_registration)))
    return ConfirmResult::CONFIRM_RESULT_WRITE_FAILED;
  this->review_.erase(found);
  this->save_review_();
  return ConfirmResult::CONFIRM_RESULT_OK;
}

bool TowStore::discard(uint32_t id) {
  std::lock_guard<std::mutex> guard(this->lock_);
  auto found = std::find_if(this->review_.begin(), this->review_.end(),
                            [id](const TowRecord &record) { return record.id == id; });
  if (found == this->review_.end())
    return false;
  this->append_line_(DISCARDED_FILE, record_to_json(*found));
  this->review_.erase(found);
  return this->save_review_();
}

bool TowStore::resolve_launch_times(time_t now, uint32_t now_ms) {
  std::lock_guard<std::mutex> guard(this->lock_);
  bool changed = false;
  for (TowRecord &record : this->review_) {
    if (record.launch == 0 && record.launch_ms_valid) {
      record.launch = now - time_t((now_ms - record.launch_ms) / 1000);
      record.launch_ms_valid = false;
      changed = true;
    }
  }
  if (changed)
    this->save_review_();
  return changed;
}

static void remember_name(std::vector<std::string> &list, const std::string &name) {
  if (name.empty())
    return;
  auto same = [&name](const std::string &other) { return strcasecmp(other.c_str(), name.c_str()) == 0; };
  list.erase(std::remove_if(list.begin(), list.end(), same), list.end());
  list.insert(list.begin(), name);
  if (list.size() > MAX_RECENT)
    list.resize(MAX_RECENT);
}

TowTotals TowStore::load_totals() {
  std::lock_guard<std::mutex> guard(this->lock_);
  TowTotals totals;
  std::string content;
  if (!this->read_with_fallback_(TOTALS_FILE, content))
    return totals;
  json::parse_json(content, [&](JsonObject root) -> bool {
    totals.day = root["day"] | 0u;
    totals.tows = root["tows"] | 0u;
    totals.flight_s = root["flight"] | 0u;
    totals.fuel_s = root["fuel"] | 0u;
    return true;
  });
  return totals;
}

bool TowStore::save_totals(const TowTotals &totals) {
  const std::string text = json::build_json([&](JsonObject root) {
    root["day"] = totals.day;
    root["tows"] = totals.tows;
    root["flight"] = totals.flight_s;
    root["fuel"] = totals.fuel_s;
  });
  std::lock_guard<std::mutex> guard(this->lock_);
  return this->write_atomic_(TOTALS_FILE, text);
}

std::vector<std::string> TowStore::recent_gliders() {
  std::lock_guard<std::mutex> guard(this->lock_);
  return this->recent_gliders_;
}

std::vector<std::string> TowStore::recent_pilots() {
  std::lock_guard<std::mutex> guard(this->lock_);
  return this->recent_pilots_;
}

void TowStore::remember(const TowDetails &details) {
  std::lock_guard<std::mutex> guard(this->lock_);
  remember_name(this->recent_gliders_, details.glider_registration);
  remember_name(this->recent_pilots_, details.glider_pilot_2);
  remember_name(this->recent_pilots_, details.glider_pilot_1);
  remember_name(this->recent_pilots_, details.tug_pilot);
  this->save_recent_();
}

void TowStore::forget_name(bool glider, const std::string &name) {
  std::lock_guard<std::mutex> guard(this->lock_);
  auto &list = glider ? this->recent_gliders_ : this->recent_pilots_;
  auto same = [&name](const std::string &other) { return strcasecmp(other.c_str(), name.c_str()) == 0; };
  list.erase(std::remove_if(list.begin(), list.end(), same), list.end());
  this->save_recent_();
}

bool TowStore::load_recent_() {
  std::string content;
  if (!this->read_with_fallback_(RECENT_FILE, content))
    return false;
  return json::parse_json(content, [this](JsonObject root) -> bool {
    this->recent_gliders_.clear();
    this->recent_pilots_.clear();
    for (const char *name : root["gliders"].as<JsonArray>())
      this->recent_gliders_.emplace_back(name);
    for (const char *name : root["pilots"].as<JsonArray>())
      this->recent_pilots_.emplace_back(name);
    return true;
  });
}

bool TowStore::save_recent_() {
  const std::string text = json::build_json([this](JsonObject root) {
    JsonArray gliders = root["gliders"].to<JsonArray>();
    for (const std::string &name : this->recent_gliders_)
      gliders.add(name);
    JsonArray pilots = root["pilots"].to<JsonArray>();
    for (const std::string &name : this->recent_pilots_)
      pilots.add(name);
  });
  return this->write_atomic_(RECENT_FILE, text);
}

void TowStore::process_queue(const std::function<UploadResult(const std::string &body)> &post) {
  const std::string sending = this->path_(SENDING_FILE);
  std::string content;
  {
    std::lock_guard<std::mutex> guard(this->lock_);
    // A sending file left by a restart is sent before new tows are taken
    if (!file_exists(sending)) {
      const std::string pending = this->path_(PENDING_FILE);
      if (!file_exists(pending) || rename(pending.c_str(), sending.c_str()) != 0)
        return;
    }
    if (!read_file(sending, content))
      return;
  }
  for (const std::string &line : split_lines(content)) {
    // An oversized line would be refused anyway, and must not be sent
    const UploadResult result = line.size() > MAX_LINE ? UploadResult::UPLOAD_RESULT_REFUSED : post(line);
    std::lock_guard<std::mutex> guard(this->lock_);
    switch (result) {
      case UploadResult::UPLOAD_RESULT_SENT:
        this->append_line_(SENT_FILE, line);
        break;
      case UploadResult::UPLOAD_RESULT_REFUSED:
        this->append_line_(FAILED_FILE, line);
        break;
      case UploadResult::UPLOAD_RESULT_RETRY:
        this->append_line_(PENDING_FILE, line);
        break;
    }
  }
  std::lock_guard<std::mutex> guard(this->lock_);
  remove(sending.c_str());
}

size_t TowStore::queued_count() {
  std::lock_guard<std::mutex> guard(this->lock_);
  return this->count_lines_(PENDING_FILE) + this->count_lines_(SENDING_FILE);
}

size_t TowStore::failed_count() {
  std::lock_guard<std::mutex> guard(this->lock_);
  return this->count_lines_(FAILED_FILE);
}

}  // namespace esphome::tug_logger
