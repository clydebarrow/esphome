#include "tow_uploader.h"

#ifdef USE_ESP32

#include <esp_app_desc.h>
#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#include <freertos/FreeRTOS.h>
#include <freertos/idf_additions.h>
#include <freertos/task.h>
#include <mbedtls/md.h>

#include <cstdio>
#include <cstring>

#include "esphome/components/json/json_util.h"
#include "esphome/components/network/util.h"
#include "esphome/core/application.h"
#include "esphome/core/log.h"
#include "esphome/core/time.h"

#include "tug_logger.h"

namespace esphome::tug_logger {

ESPHOME_LOG_TAG(TAG, "tug_logger.upload");

static constexpr uint32_t CHECK_INTERVAL_MS = 15000;
static constexpr uint32_t RETRY_INTERVAL_MS = 60000;
static constexpr uint32_t HTTP_TIMEOUT_MS = 15000;
// Over LoRa: the tow's fragments, the receiver's post to the Worker, and the answer back
static constexpr uint32_t LINK_ANSWER_TIMEOUT_MS = 20000;
// TLS needs more than a plain HTTP request; the stack is in PSRAM
static constexpr uint32_t TASK_STACK_SIZE = 12288;
static constexpr UBaseType_t TASK_PRIORITY = 1;
static constexpr size_t SHA256_SIZE = 32;
static constexpr size_t SIGNATURE_SIZE = 8 + 2 * SHA256_SIZE;
// How much of the server's answer is kept: enough for the firmware check's link
static constexpr size_t MAX_ANSWER = 512;
static TaskHandle_t uploader_handle = nullptr;

static bool hmac(const std::string &key, const std::string &body, uint8_t *mac) {
  return mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), reinterpret_cast<const uint8_t *>(key.data()),
                         key.size(), reinterpret_cast<const uint8_t *>(body.data()), body.size(), mac) == 0;
}

// "sha256=" and the HMAC-SHA256 of the body in hex, as the server checks in X-Signature
static bool sign(const std::string &key, const std::string &body, char *signature) {
  uint8_t mac[SHA256_SIZE];
  if (!hmac(key, body, mac))
    return false;
  size_t pos = snprintf(signature, SIGNATURE_SIZE, "sha256=");
  for (uint8_t b : mac)
    pos += snprintf(signature + pos, SIGNATURE_SIZE - pos, "%02x", b);
  return true;
}

struct Answer {
  char text[MAX_ANSWER + 1];
  size_t length;
};

static esp_err_t on_http_event(esp_http_client_event_t *event) {
  if (event->event_id != HTTP_EVENT_ON_DATA)
    return ESP_OK;
  auto *answer = static_cast<Answer *>(event->user_data);
  const auto *data = static_cast<const char *>(event->data);
  for (int i = 0; i != event->data_len && answer->length != MAX_ANSWER; i++) {
    const char c = data[i];
    answer->text[answer->length++] = c >= ' ' && c != 0x7F ? c : ' ';
  }
  answer->text[answer->length] = '\0';
  return ESP_OK;
}

// POSTs a body signed with the tug's key. Returns the HTTP status, or 0 with err set when there was no answer.
static int post_signed(const Provisioning &settings, const std::string &url, const std::string &body, Answer &answer,
                       esp_err_t &err) {
  char signature[SIGNATURE_SIZE];
  if (!sign(settings.upload_key, body, signature)) {
    ESP_LOGW(TAG, "Signing failed");
    err = ESP_FAIL;
    return 0;
  }
  esp_http_client_config_t config{};
  config.url = url.c_str();
  config.event_handler = on_http_event;
  config.user_data = &answer;
  config.method = HTTP_METHOD_POST;
  config.timeout_ms = HTTP_TIMEOUT_MS;
  config.crt_bundle_attach = esp_crt_bundle_attach;
  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (client == nullptr) {
    err = ESP_ERR_NO_MEM;
    return 0;
  }
  esp_http_client_set_header(client, "Content-Type", "application/json");
  esp_http_client_set_header(client, "X-Signature", signature);
  esp_http_client_set_post_field(client, body.data(), int(body.size()));
  err = esp_http_client_perform(client);
  const int status = esp_http_client_get_status_code(client);
  esp_http_client_cleanup(client);
  return status;
}

static UploadResult post_tow(const Provisioning &settings, const std::string &body) {
  Answer answer{};
  esp_err_t err;
  const int status = post_signed(settings, settings.upload_url, body, answer, err);
  // A server that closes the connection after its answer gives an error, but the status says what happened
  const UploadResult result = classify_status(status);
  if (result == UploadResult::UPLOAD_RESULT_REFUSED) {
    ESP_LOGW(TAG, "Upload refused: HTTP %d; the tow is kept and not sent again. Server: %s", status, answer.text);
  } else if (result == UploadResult::UPLOAD_RESULT_RETRY) {
    if (status >= 400) {
      ESP_LOGW(TAG, "Upload refused: HTTP %d; trying again later. Server: %s", status, answer.text);
    } else {
      ESP_LOGW(TAG, "Upload failed: %s", esp_err_to_name(err));
    }
  }
  return result;
}

#ifdef USE_TUG_LOGGER_UPLOAD_LINK
// Sends a tow over the link, for the receiver to post. A tow the link can't take, or whose answer doesn't come, is
// tried again later; the Worker's status is treated as if the tug had posted the tow itself.
static UploadResult post_tow_link(TowLink &link, const Provisioning &settings, const std::string &body,
                                  bool &link_down) {
  uint8_t mac[SHA256_SIZE];
  if (!hmac(settings.upload_key, body, mac)) {
    ESP_LOGW(TAG, "Signing failed");
    return UploadResult::UPLOAD_RESULT_RETRY;
  }
  int status = 0;
  switch (link.post(body, mac, LINK_ANSWER_TIMEOUT_MS, status)) {
    case TowLink::Outcome::OUTCOME_NOT_SENT:
      ESP_LOGD(TAG, "The link can't send now");
      link_down = true;
      return UploadResult::UPLOAD_RESULT_RETRY;
    case TowLink::Outcome::OUTCOME_NO_ANSWER:
      ESP_LOGW(TAG, "No answer over the link");
      link_down = true;
      return UploadResult::UPLOAD_RESULT_RETRY;
    case TowLink::Outcome::OUTCOME_ANSWERED:
      break;
  }
  const UploadResult result = classify_status(status);
  if (status == 0) {
    ESP_LOGW(TAG, "The receiver could not reach the server; trying again later");
  } else if (result == UploadResult::UPLOAD_RESULT_REFUSED) {
    ESP_LOGW(TAG, "Upload over the link refused: HTTP %d; the tow is kept and not sent again", status);
  } else if (result == UploadResult::UPLOAD_RESULT_RETRY) {
    ESP_LOGW(TAG, "Upload over the link refused: HTTP %d; trying again later", status);
  }
  return result;
}
#endif

void TugLogger::upload_step() {
  this->queued_ = this->store_.queued_count();
  this->failed_ = this->store_.failed_count();
  const uint32_t now = millis();
  auto has_internet = [this] { return network::is_connected() && !this->on_skyecho_network_; };
#ifdef USE_TUG_LOGGER_UPLOAD_LINK
  const bool has_link = this->tow_link_ != nullptr;
#else
  const bool has_link = false;
#endif
  if (this->queued_ == 0 || !(has_internet() || has_link) ||
      (this->last_upload_ms_ != 0 && now - this->last_upload_ms_ < RETRY_INTERVAL_MS))
    return;
  this->last_upload_ms_ = now;
  unsigned sent = 0, kept = 0, refused = 0;
  // Once the link fails, the rest of the queue waits for the next try rather than for an answer each
  bool link_down = false;
  this->store_.process_queue([&](const std::string &body) {
    // The internet first; a switch of network in the middle of the queue moves the rest to the link
    UploadResult result = UploadResult::UPLOAD_RESULT_RETRY;
    if (has_internet()) {
      result = post_tow(this->provisioning_, body);
#ifdef USE_TUG_LOGGER_UPLOAD_LINK
    } else if (has_link && !link_down) {
      result = post_tow_link(*this->tow_link_, this->provisioning_, body, link_down);
#endif
    }
    if (result == UploadResult::UPLOAD_RESULT_SENT) {
      sent++;
    } else if (result == UploadResult::UPLOAD_RESULT_REFUSED) {
      refused++;
    } else {
      kept++;
    }
    return result;
  });
  ESP_LOGI(TAG, "Tows sent: %u, still queued: %u, refused: %u", sent, kept, refused);
  this->queued_ = this->store_.queued_count();
  this->failed_ = this->store_.failed_count();
  // Everything went: the next tow need not wait for the retry time
  if (kept == 0)
    this->last_upload_ms_ = 0;
}

// A flight record is about 150 kB an hour; the Worker takes up to 4 MB
static constexpr size_t MAX_FLIGHT_LOG = 4 * 1024 * 1024 - 1024;
// Records sent each time round, so a backlog doesn't hold up the tows and the firmware check
static constexpr unsigned FLIGHT_LOGS_PER_STEP = 4;

void TugLogger::flight_log_step() {
  const uint32_t now = millis();
  // Over the internet only: a record is far too big for the LoRa link
  if (!network::is_connected() || this->on_skyecho_network_ ||
      (this->last_flight_log_ms_ != 0 && now - this->last_flight_log_ms_ < RETRY_INTERVAL_MS))
    return;
  const std::string &upload_url = this->provisioning_.upload_url;
  const size_t slash = upload_url.rfind('/');
  if (slash == std::string::npos)
    return;
  const std::string url = upload_url.substr(0, slash) + "/flight-logs";
  unsigned sent = 0;
  for (const FlightLogFile &file : this->store_.unsent_flight_logs()) {
    if (sent == FLIGHT_LOGS_PER_STEP)
      return;
    if (file.size > MAX_FLIGHT_LOG) {
      ESP_LOGW(TAG, "Flight record %s is too big to send (%zu bytes)", file.name.c_str(), file.size);
      this->store_.mark_flight_log_sent(file);
      continue;
    }
    std::string csv;
    if (!this->store_.read_flight_log(file.name, csv, MAX_FLIGHT_LOG)) {
      ESP_LOGW(TAG, "Flight record %s could not be read", file.name.c_str());
      this->last_flight_log_ms_ = now;
      return;
    }
    // Only what was read is marked sent: a record still growing goes again
    const FlightLogFile read{file.name, csv.size()};
    Answer answer{};
    esp_err_t err;
    const int status = post_signed(this->provisioning_, url, flight_log_body(file.name, csv), answer, err);
    switch (classify_status(status)) {
      case UploadResult::UPLOAD_RESULT_SENT:
        ESP_LOGI(TAG, "Flight record %s sent (%zu bytes)", file.name.c_str(), read.size);
        this->store_.mark_flight_log_sent(read);
        sent++;
        break;
      case UploadResult::UPLOAD_RESULT_REFUSED:
        // Sending it again would get the same answer
        ESP_LOGW(TAG, "Flight record %s refused: HTTP %d. Server: %s", file.name.c_str(), status, answer.text);
        this->store_.mark_flight_log_sent(read);
        break;
      case UploadResult::UPLOAD_RESULT_RETRY:
        if (status >= 400) {
          ESP_LOGW(TAG, "Flight record %s not taken: HTTP %d; trying again later. Server: %s", file.name.c_str(),
                   status, answer.text);
        } else {
          ESP_LOGW(TAG, "Flight record %s not sent: %s; trying again later", file.name.c_str(), esp_err_to_name(err));
        }
        this->last_flight_log_ms_ = now;
        return;
    }
  }
  this->last_flight_log_ms_ = 0;
}

void TugLogger::request_firmware_check() {
  this->firmware_check_callback_.call();
  // Without a tug file the upload task never starts
  if (!this->provisioned_) {
    std::lock_guard<std::mutex> guard(this->firmware_lock_);
    this->firmware_offer_ = {};
    this->firmware_offer_.message = "No tug file on the SD card";
    this->firmware_state_ = FirmwareState::FIRMWARE_STATE_FAILED;
    return;
  }
  this->firmware_state_ = FirmwareState::FIRMWARE_STATE_CHECKING;
  this->firmware_check_requested_ = true;
  wake_tow_uploader();
}

void TugLogger::firmware_install_failed() {
  std::lock_guard<std::mutex> guard(this->firmware_lock_);
  this->firmware_offer_ = {};
  this->firmware_offer_.message = "The update failed. Check again to retry";
  this->firmware_state_ = FirmwareState::FIRMWARE_STATE_FAILED;
}

void TugLogger::firmware_step() {
  if (!this->firmware_check_requested_.exchange(false))
    return;
  FirmwareOffer offer;
  FirmwareState state = FirmwareState::FIRMWARE_STATE_FAILED;
  // The check goes beside the upload address: .../api/tows becomes .../api/firmware/check
  const std::string &upload_url = this->provisioning_.upload_url;
  const size_t slash = upload_url.rfind('/');
  if (!this->provisioned_ || slash == std::string::npos) {
    offer.message = "No tug file on the SD card";
  } else if (!network::is_connected() || this->on_skyecho_network_) {
    offer.message = "Connect to a Wi-Fi network with internet first";
  } else {
    const esp_app_desc_t *app = esp_app_get_description();
    // The whole hash, as the Worker reads it from the image: esp_app_get_elf_sha256() gives only the first
    // CONFIG_APP_RETRIEVE_LEN_ELF_SHA - 1 digits, 8 by default, which never match
    char elf_sha256[2 * SHA256_SIZE + 1];
    format_hex_to(elf_sha256, app->app_elf_sha256, sizeof(app->app_elf_sha256));
    const std::string body = json::build_json([&](JsonObject root) {
      root["project"] = app->project_name;
      root["elf_sha256"] = elf_sha256;
      root["build_time"] = (int64_t) App.get_build_time();
    });
    Answer answer{};
    esp_err_t err;
    const int status =
        post_signed(this->provisioning_, upload_url.substr(0, slash) + "/firmware/check", body, answer, err);
    if (status == 200) {
      bool update = false;
      json::parse_json(std::string(answer.text, answer.length), [&](JsonObject root) -> bool {
        update = root["update"] | false;
        offer.url = root["url"] | "";
        offer.md5 = root["md5"] | "";
        offer.built = root["built"] | (int64_t) 0;
        offer.uploaded = root["uploaded"] | (int64_t) 0;
        return true;
      });
      if (!update) {
        state = FirmwareState::FIRMWARE_STATE_CURRENT;
        offer = {};
        offer.message = "This is the newest firmware";
      } else if (offer.url.empty() || offer.md5.size() != 32) {
        offer.message = "The server's answer was not understood";
      } else {
        state = FirmwareState::FIRMWARE_STATE_AVAILABLE;
        // In this device's time zone, like the time of the running build
        if (offer.built != 0) {
          offer.message = "New firmware, built " + local_time_text(offer.built);
        } else if (offer.uploaded != 0) {
          offer.message = "New firmware, uploaded " + local_time_text(offer.uploaded);
        } else {
          offer.message = "New firmware";
        }
      }
    } else if (status != 0) {
      ESP_LOGW(TAG, "Firmware check refused: HTTP %d. Server: %s", status, answer.text);
      offer.message = status == 401 ? "The server doesn't accept this tug's key" : "The server refused the check";
    } else {
      ESP_LOGW(TAG, "Firmware check failed: %s", esp_err_to_name(err));
      offer.message = "The server could not be reached";
    }
  }
  ESP_LOGI(TAG, "Firmware check: %s", offer.message.c_str());
  std::lock_guard<std::mutex> guard(this->firmware_lock_);
  this->firmware_offer_ = std::move(offer);
  this->firmware_state_ = state;
}

// Airfields change seldom; a failed fetch is tried again sooner
static constexpr uint32_t AIRFIELDS_INTERVAL_MS = 60 * 60 * 1000;
static constexpr uint32_t AIRFIELDS_RETRY_MS = 5 * 60 * 1000;

void TugLogger::airfields_step() {
  const uint32_t now = millis();
  const uint32_t wait = this->airfields_tried_ ? AIRFIELDS_RETRY_MS : AIRFIELDS_INTERVAL_MS;
  if (this->last_airfields_ms_ != 0 && now - this->last_airfields_ms_ < wait)
    return;
  // .../api/tows becomes .../api/airfields
  const std::string &upload_url = this->provisioning_.upload_url;
  const size_t slash = upload_url.rfind('/');
  if (!this->provisioned_ || slash == std::string::npos || !network::is_connected() || this->on_skyecho_network_)
    return;
  this->last_airfields_ms_ = now == 0 ? 1 : now;
  Answer answer{};
  esp_err_t err;
  const int status = post_signed(this->provisioning_, upload_url.substr(0, slash) + "/airfields", "{}", answer, err);
  const std::string text(answer.text, answer.length);
  Provisioning fetched;
  if (status != 200 || !parse_airfields(text, fetched)) {
    this->airfields_tried_ = true;
    if (status != 0) {
      ESP_LOGW(TAG, "Airfields not fetched: HTTP %d", status);
    } else {
      ESP_LOGW(TAG, "Airfields not fetched: %s", esp_err_to_name(err));
    }
    return;
  }
  this->airfields_tried_ = false;
  if (!this->store_.save_airfields(text)) {
    ESP_LOGW(TAG, "The airfields could not be saved");
  }
  std::lock_guard<std::mutex> guard(this->airfields_lock_);
  this->fetched_ = std::move(fetched);
  this->airfields_fetched_ = true;
}

static void uploader_task(void *param) {
  auto *logger = static_cast<TugLogger *>(param);
  while (true) {
    // Woken early for a firmware check
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(CHECK_INTERVAL_MS));
    logger->firmware_step();
    logger->upload_step();
    logger->flight_log_step();
    logger->airfields_step();
  }
}

bool start_tow_uploader(TugLogger *logger) {
  return xTaskCreatePinnedToCoreWithCaps(uploader_task, "tow_upload", TASK_STACK_SIZE, logger, TASK_PRIORITY,
                                         &uploader_handle, tskNO_AFFINITY,
                                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) == pdPASS;
}

void wake_tow_uploader() {
  if (uploader_handle != nullptr)
    xTaskNotifyGive(uploader_handle);
}

}  // namespace esphome::tug_logger

#elif defined(USE_HOST)

#include "tug_logger.h"

namespace esphome::tug_logger {

// A host build is for trying the screens: nothing is uploaded, and there are no firmware updates

void TugLogger::upload_step() {}
void TugLogger::firmware_step() {}
void TugLogger::flight_log_step() {}
void TugLogger::airfields_step() {}

void TugLogger::request_firmware_check() {
  this->firmware_check_callback_.call();
  std::lock_guard<std::mutex> guard(this->firmware_lock_);
  this->firmware_offer_ = {};
  this->firmware_offer_.message = "No updates in a host build";
  this->firmware_state_ = FirmwareState::FIRMWARE_STATE_FAILED;
}

void TugLogger::firmware_install_failed() { this->request_firmware_check(); }

bool start_tow_uploader(TugLogger *logger) { return true; }
void wake_tow_uploader() {}

}  // namespace esphome::tug_logger

#endif  // USE_ESP32
