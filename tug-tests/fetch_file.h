// Bench only: fetches a URL to a file on the card, in a task of its own.
#pragma once
#include <cstdio>
#include <string>
#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "esphome/core/log.h"

struct FetchJob {
  std::string url, path;
};

inline esp_err_t fetch_on_event(esp_http_client_event_t *event) {
  if (event->event_id == HTTP_EVENT_ON_DATA)
    fwrite(event->data, 1, event->data_len, static_cast<FILE *>(event->user_data));
  return ESP_OK;
}

inline void fetch_task(void *arg) {
  auto *job = static_cast<FetchJob *>(arg);
  const std::string temp = job->path + ".tmp";
  FILE *file = fopen(temp.c_str(), "wb");
  bool good = false;
  int status = 0;
  if (file != nullptr) {
    esp_http_client_config_t config{};
    config.url = job->url.c_str();
    config.event_handler = fetch_on_event;
    config.user_data = file;
    config.timeout_ms = 30000;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    esp_http_client_handle_t client = esp_http_client_init(&config);
    good = esp_http_client_perform(client) == ESP_OK;
    status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    fclose(file);
  }
  if (good && status == 200) {
    remove(job->path.c_str());
    good = rename(temp.c_str(), job->path.c_str()) == 0;
    ESP_LOGI("fetch", "%s: %s", job->path.c_str(), good ? "written" : "rename failed");  // NOLINT
  } else {
    remove(temp.c_str());
    ESP_LOGW("fetch", "%s: failed, HTTP %d", job->path.c_str(), status);  // NOLINT
  }
  delete job;
  vTaskDelete(nullptr);
}

inline void fetch_file(const std::string &url, const std::string &path) {
  xTaskCreate(fetch_task, "fetch", 8192, new FetchJob{url, path}, 2, nullptr);
}
