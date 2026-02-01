#include "skyecho.h"
#include "text_sensor/skyecho_text_sensor.h"
#include "text_sensor/skyecho_traffic_list_sensor.h"

namespace esphome {
namespace skyecho {

static const char *const TAG = "skyecho";

void SkyEcho::setup() {
  this->socket_ = socket::socket_ip(SOCK_DGRAM, IPPROTO_UDP);
  this->ping_socket_ = socket::socket_ip(SOCK_DGRAM, IPPROTO_UDP);
  int enable = 1;
  int err = this->socket_->setsockopt(SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int));
  if (err != 0) {
    ESP_LOGW(TAG, "Socket unable to set reuseaddr: errno %d", err);
    // we can still continue
  }
  err = this->socket_->setblocking(false);
  if (err != 0) {
    ESP_LOGW(TAG, "Socket unable to set nonblocking mode: errno %d", err);
    this->mark_failed();
    return;
  }

  struct sockaddr_storage server;

  socklen_t sl = socket::set_sockaddr_any((struct sockaddr *) &server, sizeof(server), PORT);
  if (sl == 0) {
    ESP_LOGW(TAG, "Socket unable to set sockaddr: errno %d", errno);
    this->mark_failed();
    return;
  }

  err = this->socket_->bind((struct sockaddr *) &server, sizeof(server));
  if (err != 0) {
    ESP_LOGW(TAG, "Socket unable to bind: errno %d", errno);
    this->mark_failed();
    return;
  }
  this->set_interval("ping", 10000, [this]() { this->ping_(); });
}

void SkyEcho::dump_config() {
  ESP_LOGCONFIG(TAG, "SkyEcho:");
  ESP_LOGCONFIG(TAG, "  Port: %d", PORT);
}

void SkyEcho::loop() {
  uint8_t buf[1024];
  if (!network::is_connected())
    return;
  sockaddr_in from_addr;
  socklen_t addr_len = sizeof from_addr;
  auto len = this->socket_->recvfrom(buf, sizeof buf, (sockaddr *) &from_addr, &addr_len);
  if (len >= 0) {
    ESP_LOGV(TAG, "Received %d bytes", len);
    gdl90GetBlocks(
        buf, len,
        [this](const gdlDataPacket_t *packet, in_addr *srcAddr) -> void { this->block_callback(packet, srcAddr); },
        &from_addr.sin_addr);
  }
}

void SkyEcho::update() {
  // Generate simulated data if no real data is being received
  this->generate_simulated_ownship();
  this->generate_simulated_traffic();

  // Trigger update on all registered text sensors
  for (auto *sensor : this->text_sensors_) {
    sensor->update();
  }

  // Trigger update on all registered traffic list sensors
  for (auto *sensor : this->traffic_list_sensors_) {
    sensor->update();
  }
}

bool SkyEcho::getOwnshipPosition(ownship_t *position) {
  *position = this->ownship_;
  return this->isGpsConnected();
}

bool SkyEcho::getHeartbeat(gdl90Heartbeat_t *heartbeat) {
  *heartbeat = this->heartbeat_;
  return this->isGpsConnected();
}

void SkyEcho::getTraffic(traffic_t *buffer, size_t cnt) {
  this->sortTraffic();
  size_t copy_cnt = std::min(cnt, MAX_TRAFFIC_TRACKED);
  memcpy(buffer, this->traffic_, copy_cnt * sizeof(traffic_t));
}

void SkyEcho::processPacket(gdl90Data_t *packet, struct in_addr *srcAddr) {
  switch (packet->id) {
    case GDL90_HEARTBEAT:
      ESP_LOGV(TAG, "Heartbeat @%d GPSvalid %s", (int) packet->heartbeat.timeStamp,
               packet->heartbeat.gpsPosValid ? "true" : "false");
      this->setHeartbeat(&packet->heartbeat);
      break;

    case GDL90_OWNSHIP_REPORT:
      ESP_LOGV(TAG, "Ownship report");
      this->setOwnshipPosition(&packet->positionReport);
      break;

    case GDL90_TRAFFIC_REPORT:
      ESP_LOGV(TAG, "Traffic report");
      this->processTraffic(&packet->positionReport);
      break;

    case GDL90_OWNSHIP_GEO_ALT:
      ESP_LOGV(TAG, "Geo Alt: %.1f m", packet->ownshipAltitude.geoAltitude);
      this->ownship_altitude_ = packet->ownshipAltitude;
      break;

    case GDL90_FOREFLIGHT_ID:
      ESP_LOGV(TAG, "Foreflight ID: %s", packet->foreflightId.name);
      break;

    default:
      ESP_LOGD(TAG, "Received unhandled packet type %d", packet->id);
      break;
  }
}

void SkyEcho::block_callback(const gdlDataPacket_t *packet, in_addr *srcAddr) {
  gdl90Data_t data;
  if (packet->err != GDL90_ERR_NONE) {
    ESP_LOGD(TAG, "DecodeBlock failed - %s: id %d, len %d", gdl90ErrorMessage(packet->err), packet->data[0],
             packet->len);
  } else {
    memset(&data, 0, sizeof(data));
    gdl90Err_t err = gdl90DecodeBlock(packet->data, packet->len, &data);
    if (err == GDL90_ERR_NONE) {
      processPacket(&data, srcAddr);
    } else
      ESP_LOGD(TAG, "Decode packet id %d failed with err %s", data.id, gdl90ErrorMessage(err));
  }
}

void SkyEcho::ping_() {
  if (!network::is_connected())
    return;
  for (size_t i = 0; i != LWIP_ARRAYSIZE(pingPorts); i++) {
    struct sockaddr addr;
    socket::set_sockaddr(&addr, sizeof(addr), {"255.255.255.255"}, pingPorts[i]);
    auto err = this->ping_socket_->sendto(pingPayload, strlen(pingPayload), 0, &addr, sizeof(addr));
    if (err < 0) {
      ESP_LOGD(TAG, "Sendto failed on port %d with err %d", pingPorts[i], err);
    }
  }
}

void SkyEcho::generate_simulated_ownship() {
  // Only generate if simulation is enabled and we don't have a valid position
  if (!this->simulate_ || this->isGpsConnected())
    return;

  // Create simulated ownship position (somewhere over Europe as an example)
  gdl90PositionReport_t sim_ownship;
  memset(&sim_ownship, 0, sizeof(sim_ownship));

  // Simulated location with some variation based on time
  uint32_t now = millis();
  float time_offset = (now / 10000) % 100;  // Changes slowly over time

  sim_ownship.latitude = 46.5f + (time_offset * 0.001f);
  sim_ownship.longitude = 13.5f + (time_offset * 0.001f);
  sim_ownship.altitude = 1500.0f + (time_offset * 2.0f);  // 1500-1700m
  sim_ownship.groundSpeed = 30.0f;                        // 30 m/s (~60 knots)
  sim_ownship.track = 90.0f;                              // Heading east
  sim_ownship.verticalSpeed = 0.5f;                       // Slight climb
  sim_ownship.address = 0x123456;
  sim_ownship.addressType = EmitterAdsbIcao;
  sim_ownship.category = Small;
  snprintf(sim_ownship.callsign, sizeof(sim_ownship.callsign), "SIMOWN");
  sim_ownship.isInFlight = true;
  sim_ownship.NACp = 9;

  // Set simulated ownship
  this->setOwnshipPosition(&sim_ownship);

  // Also set a simulated heartbeat
  gdl90Heartbeat_t sim_heartbeat;
  memset(&sim_heartbeat, 0, sizeof(sim_heartbeat));
  sim_heartbeat.gpsPosValid = true;
  sim_heartbeat.timeStamp = (now / 1000) % 86400;  // Seconds since midnight
  sim_heartbeat.utcOk = true;
  this->setHeartbeat(&sim_heartbeat);

  ESP_LOGV(TAG, "Generated simulated ownship position");
}

void SkyEcho::generate_simulated_traffic() {
  // Only generate simulated traffic if simulation enabled, we have ownship, but no real traffic
  if (!this->simulate_ || !this->isGpsConnected() || this->getActiveTrafficCount() > 0)
    return;

  // Generate 2 simulated traffic targets
  for (int i = 0; i < 2; i++) {
    gdl90PositionReport_t sim_traffic;
    memset(&sim_traffic, 0, sizeof(sim_traffic));

    // Position relative to ownship (offset in degrees)
    float lat_offset = (i == 0 ? 0.01f : -0.01f);   // ~1.1 km north/south
    float lon_offset = (i == 0 ? 0.01f : -0.005f);  // ~1.1 km east or 0.5 km west

    sim_traffic.latitude = this->ownship_.report.latitude + lat_offset;
    sim_traffic.longitude = this->ownship_.report.longitude + lon_offset;
    sim_traffic.altitude = this->ownship_.report.altitude + (i == 0 ? 100.0f : -50.0f);  // +100m or -50m
    sim_traffic.groundSpeed = 25.0f + i * 5.0f;                                          // 25 or 30 m/s
    sim_traffic.track = 45.0f + i * 90.0f;                                               // 45° or 135°
    sim_traffic.verticalSpeed = (i == 0 ? 2.0f : -1.0f);                                 // +2 or -1 m/s
    sim_traffic.address = 0xABCD00 + i;                                                  // Simulated addresses
    sim_traffic.addressType = EmitterAdsbIcao;
    sim_traffic.category = (i == 0 ? Small : Glider);
    snprintf(sim_traffic.callsign, sizeof(sim_traffic.callsign), "SIM%d", i + 1);
    sim_traffic.isInFlight = true;
    sim_traffic.NACp = 9;

    // Process as if it were real traffic
    this->processTraffic(&sim_traffic);
  }
}

void SkyEcho::setOwnshipPosition(const gdl90PositionReport_t *position) {
  this->ownship_.report = *position;
  this->ownship_.timestampMs = millis();
  ESP_LOGV(TAG, "Ownship: lat=%.4f lon=%.4f alt=%.0fm spd=%.0fm/s", position->latitude, position->longitude,
           position->altitude, position->groundSpeed);
}

void SkyEcho::setHeartbeat(const gdl90Heartbeat_t *heartbeat) { this->heartbeat_ = *heartbeat; }

bool SkyEcho::isGpsConnected() {
  return this->heartbeat_.gpsPosValid && this->ownship_.timestampMs > 0 &&
         (millis() - this->ownship_.timestampMs) < MAX_POSITION_AGE_MS;
}

int SkyEcho::compareTraffic(const void *tp1, const void *tp2) {
  const traffic_t *t1 = (const traffic_t *) tp1;
  const traffic_t *t2 = (const traffic_t *) tp2;

  if (!t1->active && !t2->active)
    return 0;
  if (!t2->active)
    return -1;
  if (!t1->active)
    return 1;
  return (int) ((t1->distance - t2->distance) * 1000.0f);
}

void SkyEcho::sortTraffic() {
  uint32_t now = millis();
  uint32_t oldMs = now - MAX_TRAFFIC_AGE_MS;

  // Mark old traffic as inactive
  for (size_t i = 0; i < MAX_TRAFFIC_TRACKED; i++) {
    if (this->traffic_[i].active && this->traffic_[i].timestampMs < oldMs) {
      this->traffic_[i].active = false;
    }
  }

  // Sort by active status and distance
  qsort(this->traffic_, MAX_TRAFFIC_TRACKED, sizeof(traffic_t), compareTraffic);
}

traffic_t *SkyEcho::getEntry() {
  this->sortTraffic();
  // The last one is the furthest away, or unused
  return &this->traffic_[MAX_TRAFFIC_TRACKED - 1];
}

void SkyEcho::updateTraffic(traffic_t *tp, const gdl90PositionReport_t *report, float distance) {
  tp->timestampMs = millis();
  tp->report = *report;
  tp->distance = distance;
  tp->active = true;
  esph_log_i(TAG, "Traffic: %s dist=%.0fm alt=%.0fm spd=%.0fm/s", report->callsign, distance, report->altitude,
             report->groundSpeed);
}

void SkyEcho::processTraffic(const gdl90PositionReport_t *report) {
  if (!this->isGpsConnected())
    return;

  float distance = trafficDistance(&this->ownship_.report, report);

  // Check if target is already in our list
  for (size_t i = 0; i != MAX_TRAFFIC_TRACKED; i++) {
    if (this->traffic_[i].report.addressType == report->addressType &&
        this->traffic_[i].report.address == report->address) {
      this->updateTraffic(&this->traffic_[i], report, distance);
      return;
    }
  }

  // Get a slot for new traffic
  traffic_t *tp = this->getEntry();

  // Use the slot if it's outdated or further away than the new target
  if (!tp->active || tp->distance > distance) {
    this->updateTraffic(tp, report, distance);
  }
}

size_t SkyEcho::getActiveTrafficCount() {
  size_t count = 0;
  uint32_t now = millis();
  uint32_t oldMs = now - MAX_TRAFFIC_AGE_MS;

  for (size_t i = 0; i < MAX_TRAFFIC_TRACKED; i++) {
    if (this->traffic_[i].active && this->traffic_[i].timestampMs >= oldMs) {
      count++;
    }
  }
  return count;
}

}  // namespace skyecho
}  // namespace esphome
