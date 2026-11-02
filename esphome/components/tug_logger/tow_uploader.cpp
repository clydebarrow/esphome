#include "tow_uploader.h"

#ifdef USE_ESP32

#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#include <freertos/FreeRTOS.h>
#include <freertos/idf_additions.h>
#include <freertos/task.h>
#include <mbedtls/md.h>

#include <cstdio>
#include <cstring>

#include "esphome/components/network/util.h"
#include "esphome/core/log.h"

#include "tug_logger.h"

namespace esphome::tug_logger {

ESPHOME_LOG_TAG(TAG, "tug_logger.upload");

static constexpr uint32_t CHECK_INTERVAL_MS = 15000;
static constexpr uint32_t RETRY_INTERVAL_MS = 60000;
static constexpr uint32_t HTTP_TIMEOUT_MS = 15000;
// TLS needs more than a plain HTTP request; the stack is in PSRAM
static constexpr uint32_t TASK_STACK_SIZE = 12288;
static constexpr UBaseType_t TASK_PRIORITY = 1;
static constexpr size_t SHA256_SIZE = 32;
static constexpr size_t SIGNATURE_SIZE = 8 + 2 * SHA256_SIZE;
// How much of the server's answer to a refused tow is logged
static constexpr size_t MAX_ANSWER = 200;

// "sha256=" and the HMAC-SHA256 of the body in hex, as the server checks in X-Signature
static bool sign(const std::string &key, const std::string &body, char *signature) {
  uint8_t mac[SHA256_SIZE];
  if (mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), reinterpret_cast<const uint8_t *>(key.data()),
                      key.size(), reinterpret_cast<const uint8_t *>(body.data()), body.size(), mac) != 0)
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

static UploadResult post_tow(const Provisioning &settings, const std::string &body) {
  char signature[SIGNATURE_SIZE];
  if (!sign(settings.upload_key, body, signature)) {
    ESP_LOGW(TAG, "Signing failed");
    return UploadResult::UPLOAD_RESULT_RETRY;
  }
  Answer answer{};
  esp_http_client_config_t config{};
  config.url = settings.upload_url.c_str();
  config.event_handler = on_http_event;
  config.user_data = &answer;
  config.method = HTTP_METHOD_POST;
  config.timeout_ms = HTTP_TIMEOUT_MS;
  config.crt_bundle_attach = esp_crt_bundle_attach;
  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (client == nullptr)
    return UploadResult::UPLOAD_RESULT_RETRY;
  esp_http_client_set_header(client, "Content-Type", "application/json");
  esp_http_client_set_header(client, "X-Signature", signature);
  esp_http_client_set_post_field(client, body.data(), int(body.size()));
  const esp_err_t err = esp_http_client_perform(client);
  const int status = esp_http_client_get_status_code(client);
  esp_http_client_cleanup(client);
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

void TugLogger::upload_step() {
  this->queued_ = this->store_.queued_count();
  this->failed_ = this->store_.failed_count();
  const uint32_t now = millis();
  if (this->queued_ == 0 || !network::is_connected() || this->on_skyecho_network_ ||
      (this->last_upload_ms_ != 0 && now - this->last_upload_ms_ < RETRY_INTERVAL_MS))
    return;
  this->last_upload_ms_ = now;
  unsigned sent = 0, kept = 0, refused = 0;
  this->store_.process_queue([&](const std::string &body) {
    // A switch of network in the middle of the queue: the rest wait
    const UploadResult result = network::is_connected() && !this->on_skyecho_network_
                                    ? post_tow(this->provisioning_, body)
                                    : UploadResult::UPLOAD_RESULT_RETRY;
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

static void uploader_task(void *param) {
  auto *logger = static_cast<TugLogger *>(param);
  while (true) {
    vTaskDelay(pdMS_TO_TICKS(CHECK_INTERVAL_MS));
    logger->upload_step();
  }
}

bool start_tow_uploader(TugLogger *logger) {
  return xTaskCreatePinnedToCoreWithCaps(uploader_task, "tow_upload", TASK_STACK_SIZE, logger, TASK_PRIORITY, nullptr,
                                         tskNO_AFFINITY, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) == pdPASS;
}

}  // namespace esphome::tug_logger

#endif  // USE_ESP32
