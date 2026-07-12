#include "skyecho_text_sensor.h"
#include "esphome/core/log.h"
#include "esphome/core/time.h"
#include <cstdio>
#include <cmath>
#include <ctime>

namespace esphome {
namespace skyecho {

static const char *const TAG = "skyecho.text_sensor";

// Accuracy map based on NACp values (in meters)
static const int ACCURACY_MAP[] = {
    -1,     // 0: unknown
    18520,  // 1: < 10 NM
    7408,   // 2: < 4 NM
    3704,   // 3: < 2 NM
    1852,   // 4: < 1 NM
    926,    // 5: < 0.5 NM
    555,    // 6: < 0.3 NM
    185,    // 7: < 0.1 NM
    92,     // 8: < 0.05 NM
    30,     // 9: < 30 m
    10,     // 10: < 10 m
    3,      // 11: < 3 m
};

void SkyEchoTextSensor::dump_config() { ESP_LOGCONFIG(TAG, "SkyEcho Text Sensor"); }

int SkyEchoTextSensor::get_accuracy(int nacP) {
  if (nacP < 0 || nacP >= (int) (sizeof(ACCURACY_MAP) / sizeof(ACCURACY_MAP[0])))
    return -1;
  return ACCURACY_MAP[nacP];
}

uint8_t SkyEchoTextSensor::calculate_checksum(const char *sentence) {
  uint8_t sum = 0;
  // Skip the '$' and calculate XOR of all characters until '*' or end
  for (size_t i = 1; sentence[i] != '\0' && sentence[i] != '*'; i++) {
    sum ^= sentence[i];
  }
  return sum;
}

void SkyEchoTextSensor::add_checksum(std::string &sentence) {
  uint8_t checksum = this->calculate_checksum(sentence.c_str());
  char buf[8];
  snprintf(buf, sizeof(buf), "*%02X\r\n", checksum);
  sentence += buf;
}

void SkyEchoTextSensor::generate_gpgga(std::string &output) {
  OwnshipT ownship;
  gdl90Heartbeat_t heartbeat;

  if (!this->parent_->get_ownship_position(&ownship)) {
    ESP_LOGD(TAG, "No valid position");
    return;  // No valid position
  }
  this->parent_->get_heartbeat(&heartbeat);

  float abs_lat = fabsf(ownship.report.latitude);
  int lat_deg = (int) abs_lat;
  float abs_lon = fabsf(ownship.report.longitude);
  int lon_deg = (int) abs_lon;

  snprintf(this->nmea_buf_, sizeof(this->nmea_buf_),
           "$GPGGA,%02u%02u%02u,%02d%05.2f,%c,%03d%05.2f,%c,%d,%02d,%d,%.1f,M,%.1f,M,,",
           (unsigned int) (heartbeat.timeStamp / 3600), (unsigned int) ((heartbeat.timeStamp / 60) % 60),
           (unsigned int) (heartbeat.timeStamp % 60), lat_deg, (abs_lat - lat_deg) * 60.0f,
           ownship.report.latitude < 0 ? 'S' : 'N', lon_deg, (abs_lon - lon_deg) * 60.0f,
           ownship.report.longitude < 0 ? 'W' : 'E', heartbeat.gpsPosValid ? 1 : 0,
           0,  // satellites (not available from GDL90 heartbeat)
           this->get_accuracy(ownship.report.NACp), ownship.report.altitude,
           0.0f  // geoid separation (not available)
  );

  std::string sentence = this->nmea_buf_;
  this->add_checksum(sentence);
  output += sentence;
}

void SkyEchoTextSensor::generate_gprmc(std::string &output) {
  OwnshipT ownship;
  gdl90Heartbeat_t heartbeat;

  if (!this->parent_->get_ownship_position(&ownship)) {
    return;  // No valid position
  }
  this->parent_->get_heartbeat(&heartbeat);

  float abs_lat = fabsf(ownship.report.latitude);
  int lat_deg = (int) abs_lat;
  float abs_lon = fabsf(ownship.report.longitude);
  int lon_deg = (int) abs_lon;

  // Convert ground speed from m/s to knots
  float speed_knots = ownship.report.groundSpeed * 1.94384f;

  // Use current time for date (simplified - C version uses GPS time)
  time_t now_time = time(nullptr);
  struct tm *now = gmtime(&now_time);

  snprintf(this->nmea_buf_, sizeof(this->nmea_buf_),
           "$GPRMC,%02u%02u%02u,%c,%02d%05.2f,%c,%03d%05.2f,%c,%05.1f,%05.1f,%02d%02d%02d,%05.1f,%c",
           (unsigned int) (heartbeat.timeStamp / 3600), (unsigned int) ((heartbeat.timeStamp / 60) % 60),
           (unsigned int) (heartbeat.timeStamp % 60), heartbeat.gpsPosValid ? 'A' : 'V', lat_deg,
           (abs_lat - lat_deg) * 60.0f, ownship.report.latitude < 0 ? 'S' : 'N', lon_deg, (abs_lon - lon_deg) * 60.0f,
           ownship.report.longitude < 0 ? 'W' : 'E', speed_knots, ownship.report.track, now->tm_mday, now->tm_mon + 1,
           now->tm_year % 100, 0.0f, 'E'  // magnetic variation (not available)
  );

  std::string sentence = this->nmea_buf_;
  this->add_checksum(sentence);
  output += sentence;
}

void SkyEchoTextSensor::generate_pgrmz(std::string &output) {
  OwnshipT ownship;

  if (!this->parent_->get_ownship_position(&ownship)) {
    return;  // No valid position
  }

  // Convert altitude from meters to feet
  int altitude_feet = (int) (ownship.report.altitude / 0.3048f);

  snprintf(this->nmea_buf_, sizeof(this->nmea_buf_), "$PGRMZ,%d,F,2", altitude_feet);

  std::string sentence = this->nmea_buf_;
  this->add_checksum(sentence);
  output += sentence;
}

int SkyEchoTextSensor::category_to_flarm_type(AircraftCategory category) {
  // Convert unified aircraft category to FLARM aircraft type
  // FLARM types: 0=unknown, 1=glider, 2=tow, 3=heli, 4=parachute, 5=drop, 6=hang-glider,
  //              7=para-glider, 8=powered, 9=jet, A=UFO, B=balloon, C=airship, D=UAV, F=static
  switch (category) {
    case CATEGORY_GLIDER:
      return 1;  // glider/motor glider
    case CATEGORY_TOW_PLANE:
      return 2;  // tow plane
    case CATEGORY_ROTORCRAFT:
      return 3;  // helicopter/rotorcraft
    case CATEGORY_PARACHUTE:
      return 4;  // parachute
    case CATEGORY_DROP_PLANE:
      return 5;  // drop plane
    case CATEGORY_HANG_GLIDER:
      return 6;  // hang glider
    case CATEGORY_PARAGLIDER:
      return 7;  // paraglider
    case CATEGORY_LIGHT:
    case CATEGORY_SMALL:
      return 8;  // powered aircraft
    case CATEGORY_LARGE:
    case CATEGORY_HIGH_VORTEX:
    case CATEGORY_HEAVY:
    case CATEGORY_FIGHTER:
      return 9;  // jet aircraft
    case CATEGORY_BALLOON:
      return 0xB;  // balloon
    case CATEGORY_UAV:
      return 0xD;  // UAV
    case CATEGORY_SURFACE_EMERGENCY:
    case CATEGORY_SURFACE_SERVICE:
      return 0xF;  // static object
    default:
      return 0;  // unknown
  }
}

void SkyEchoTextSensor::generate_pflaa(std::string &output) {
  // Get merged traffic from TrafficManager
  const TrafficManager &tm = this->parent_->get_traffic_manager();
  const std::vector<Traffic> &traffic_list = tm.get_traffic();

  // Generate PFLAA sentence for each traffic target
  for (const auto &t : traffic_list) {
    // Use relative position if available, otherwise skip (need ownship for PFLAA)
    if (!t.relative_valid && !t.position_valid)
      continue;

    int rel_north, rel_east, rel_vert;

    if (t.relative_valid) {
      rel_north = t.relative_north;
      rel_east = t.relative_east;
      rel_vert = t.relative_vertical;
    } else {
      // Fall back to calculating from absolute position if ownship is available
      OwnshipT ownship;
      if (!this->parent_->get_ownship_position(&ownship))
        continue;

      // Calculate relative position from absolute coordinates
      float lat_diff = t.latitude - ownship.report.latitude;
      float lon_diff = t.longitude - ownship.report.longitude;
      rel_north = static_cast<int>(lat_diff * 111111.0f);
      rel_east = static_cast<int>(lon_diff * 111111.0f * cosf(ownship.report.latitude * M_PI / 180.0f));
      rel_vert = static_cast<int>(t.altitude - ownship.report.altitude);
    }

    // Convert speeds from m/s to km/h for FLARM format (FLARM uses m/s*10 but some displays expect km/h)
    int ground_speed = t.speed_valid ? static_cast<int>(t.ground_speed * 10) : 0;  // dm/s
    int climb_rate = t.climb_valid ? static_cast<int>(t.climb_rate * 10) : 0;      // dm/s

    // Convert category to FLARM aircraft type
    int flarm_type = category_to_flarm_type(t.category);

    // Determine ID type based on source
    int id_type = (t.id_type == static_cast<uint8_t>(FLARM_ID_ICAO) || t.source == SOURCE_GDL90) ? 1 : 2;

    // PFLAA format:
    // $PFLAA,<AlarmLevel>,<RelativeNorth>,<RelativeEast>,<RelativeVertical>,
    //        <IDType>,<ID>,<Track>,<TurnRate>,<GroundSpeed>,<ClimbRate>,<AcftType>
    snprintf(this->nmea_buf_, sizeof(this->nmea_buf_), "$PFLAA,%d,%d,%d,%d,%d,%06X,%d,,%d,%d,%X", t.alarm_level,
             rel_north, rel_east, rel_vert, id_type, t.id, t.track_valid ? static_cast<int>(t.track) : 0, ground_speed,
             climb_rate, flarm_type);

    std::string sentence = this->nmea_buf_;
    this->add_checksum(sentence);
    output += sentence;
  }
}

void SkyEchoTextSensor::update() {
  if (this->parent_ == nullptr) {
    return;
  }

  std::string nmea_output;

  // Generate all NMEA sentences
  this->generate_gpgga(nmea_output);
  this->generate_gprmc(nmea_output);
  this->generate_pgrmz(nmea_output);
  this->generate_pflaa(nmea_output);

  // Publish the combined output
  if (!nmea_output.empty()) {
    ESP_LOGD(TAG, "NMEA %s", nmea_output.c_str());
    this->publish_state(nmea_output);
  }
}

}  // namespace skyecho
}  // namespace esphome
