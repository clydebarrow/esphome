#pragma once

#include "esphome/core/defines.h"

#ifdef USE_XCSOAR_FLIGHT_UPLOAD

namespace esphome::xcsoar {

/// Start a task that sends the flights XCSoar queues in data_path/flights-pending.jsonl, one JSON object per line,
/// to the flight_upload_url of data_path/club.json while the network is up. data_path must stay valid.
bool start_flight_uploader(const char *data_path);

}  // namespace esphome::xcsoar

#endif
