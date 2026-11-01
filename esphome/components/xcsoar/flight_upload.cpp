#include "flight_upload.h"

#ifdef USE_XCSOAR_FLIGHT_UPLOAD

#include "esphome/components/json/json_util.h"
#include "esphome/components/network/util.h"
#include "esphome/core/log.h"

#include <esp_http_client.h>
#include <mbedtls/md.h>
#ifdef CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
#include <esp_crt_bundle.h>
#endif
#include <freertos/FreeRTOS.h>
#include <freertos/idf_additions.h>
#include <freertos/task.h>

#include <cstdio>
#include <cstring>
#include <memory>
#include <sys/stat.h>

namespace esphome::xcsoar {

ESPHOME_LOG_TAG(TAG, "xcsoar.upload");

static constexpr uint32_t CHECK_INTERVAL_MS = 60000;
static constexpr uint32_t HTTP_TIMEOUT_MS = 15000;
// TLS needs more than a plain HTTP request; the stack is in PSRAM
static constexpr uint32_t TASK_STACK_SIZE = 12288;
static constexpr UBaseType_t TASK_PRIORITY = 1;
// One queued flight; XCSoar writes about 250 bytes
static constexpr size_t MAX_LINE = 1024;
static constexpr size_t MAX_URL = 256;
static constexpr size_t MAX_KEY = 128;
static constexpr size_t SHA256_SIZE = 32;
static constexpr size_t MAX_PATH = 96;
static constexpr size_t MAX_CLUB_JSON = 4096;

// Written by XCSoar (ClubConfig.hpp, CLUB_FLIGHTS_PENDING); renamed before sending, so XCSoar starts a new one
static constexpr const char *PENDING_FILE = "flights-pending.jsonl";
static constexpr const char *SENDING_FILE = "flights-sending.jsonl";
static constexpr const char *CLUB_FILE = "club.json";

static bool file_exists(const char *path) {
  struct stat st;
  return stat(path, &st) == 0;
}

struct UploadSettings {
  char url[MAX_URL];
  // Signs each flight when not empty
  char key[MAX_KEY];
};

// The flight_upload_url and flight_upload_key of club.json; false if there is no URL
static bool read_upload_settings(const char *data_path, UploadSettings &settings) {
  char path[MAX_PATH];
  snprintf(path, sizeof(path), "%s/%s", data_path, CLUB_FILE);
  FILE *file = fopen(path, "rb");
  if (file == nullptr)
    return false;
  auto buffer = std::make_unique<uint8_t[]>(MAX_CLUB_JSON);
  const size_t length = fread(buffer.get(), 1, MAX_CLUB_JSON, file);
  fclose(file);

  settings.url[0] = '\0';
  settings.key[0] = '\0';
  json::parse_json(buffer.get(), length, [&settings](JsonObject root) -> bool {
    if (const char *url = root["flight_upload_url"]; url != nullptr)
      snprintf(settings.url, sizeof(settings.url), "%s", url);
    if (const char *key = root["flight_upload_key"]; key != nullptr)
      snprintf(settings.key, sizeof(settings.key), "%s", key);
    return true;
  });
  return settings.url[0] != '\0';
}

// "sha256=" and the HMAC-SHA256 of the body in hex, as GitHub signs its webhooks
static bool sign(const char *key, const char *body, size_t length, char *signature, size_t size) {
  uint8_t mac[SHA256_SIZE];
  if (mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), reinterpret_cast<const uint8_t *>(key), strlen(key),
                      reinterpret_cast<const uint8_t *>(body), length, mac) != 0)
    return false;
  size_t pos = snprintf(signature, size, "sha256=");
  for (uint8_t b : mac)
    pos += snprintf(signature + pos, size - pos, "%02x", b);
  return true;
}

// POST one flight; true if the server accepted it
static bool post_flight(const UploadSettings &settings, const char *body, size_t length) {
  char signature[8 + 2 * SHA256_SIZE];
  if (settings.key[0] != '\0' && !sign(settings.key, body, length, signature, sizeof(signature))) {
    ESP_LOGW(TAG, "Signing failed");
    return false;
  }
  esp_http_client_config_t config{};
  config.url = settings.url;
  config.method = HTTP_METHOD_POST;
  config.timeout_ms = HTTP_TIMEOUT_MS;
#ifdef CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
  config.crt_bundle_attach = esp_crt_bundle_attach;
#endif
  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (client == nullptr)
    return false;
  esp_http_client_set_header(client, "Content-Type", "application/json");
  if (settings.key[0] != '\0')
    esp_http_client_set_header(client, "X-Signature", signature);
  esp_http_client_set_post_field(client, body, int(length));
  const esp_err_t err = esp_http_client_perform(client);
  const int status = esp_http_client_get_status_code(client);
  esp_http_client_cleanup(client);
  // A server that closes the connection after its answer gives an error, but the status says it was accepted
  if (status >= 200 && status < 300)
    return true;
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Upload failed: %s", esp_err_to_name(err));
  } else {
    ESP_LOGW(TAG, "Upload refused: HTTP %d", status);
  }
  return false;
}

// Send the flights in the sending file; the ones not accepted go back into the pending file
static void send_flights(const char *data_path) {
  char pending[MAX_PATH], sending[MAX_PATH];
  snprintf(pending, sizeof(pending), "%s/%s", data_path, PENDING_FILE);
  snprintf(sending, sizeof(sending), "%s/%s", data_path, SENDING_FILE);

  // A sending file left by a restart is sent before new flights are taken
  if (!file_exists(sending)) {
    if (!file_exists(pending) || rename(pending, sending) != 0)
      return;
  }

  UploadSettings settings;
  if (!read_upload_settings(data_path, settings)) {
    ESP_LOGW(TAG, "Flights are queued, but %s has no flight_upload_url", CLUB_FILE);
    return;
  }

  FILE *in = fopen(sending, "rb");
  if (in == nullptr)
    return;
  auto line = std::make_unique<char[]>(MAX_LINE);
  unsigned sent = 0, kept = 0;
  while (fgets(line.get(), MAX_LINE, in) != nullptr) {
    size_t length = strlen(line.get());
    while (length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r'))
      line[--length] = '\0';
    if (length == 0)
      continue;
    if (network::is_connected() && post_flight(settings, line.get(), length)) {
      sent++;
      continue;
    }
    FILE *out = fopen(pending, "ab");
    if (out != nullptr) {
      fprintf(out, "%s\n", line.get());
      fclose(out);
    }
    kept++;
  }
  fclose(in);
  remove(sending);
  if (sent > 0 || kept > 0) {
    ESP_LOGI(TAG, "Flights sent: %u, still queued: %u", sent, kept);
  }
}

static void uploader_task(void *param) {
  const char *data_path = static_cast<const char *>(param);
  while (true) {
    vTaskDelay(pdMS_TO_TICKS(CHECK_INTERVAL_MS));
    if (network::is_connected())
      send_flights(data_path);
  }
}

bool start_flight_uploader(const char *data_path) {
  return xTaskCreatePinnedToCoreWithCaps(uploader_task, "flight_upload", TASK_STACK_SIZE, const_cast<char *>(data_path),
                                         TASK_PRIORITY, nullptr, tskNO_AFFINITY,
                                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) == pdPASS;
}

}  // namespace esphome::xcsoar

#endif
