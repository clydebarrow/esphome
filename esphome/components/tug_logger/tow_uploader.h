#pragma once

#include "esphome/core/defines.h"

#if defined(USE_ESP32) || defined(USE_HOST)

namespace esphome::tug_logger {

class TugLogger;

/// Starts the task that sends queued tows. The logger and its provisioning must stay valid.
bool start_tow_uploader(TugLogger *logger);
/// Wakes that task, so that it answers a request at once.
void wake_tow_uploader();

}  // namespace esphome::tug_logger

#endif
