#pragma once

#include "esphome/core/defines.h"

#ifdef USE_ESP32

namespace esphome::tug_logger {

class TugLogger;

/// Starts the task that sends queued tows. The logger and its provisioning must stay valid.
bool start_tow_uploader(TugLogger *logger);

}  // namespace esphome::tug_logger

#endif
