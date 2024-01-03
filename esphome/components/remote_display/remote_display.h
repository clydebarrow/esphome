//
// Created by Clyde Stubbs on 3/1/2024.
//

#pragma once

#include "esphome/components/display/display_buffer.h"
#include "esphome/components/display/display_color_utils.h"
#include "esphome/components/web_server/web_server.h"
#include "esphome/core/log.h"

namespace esphome {
namespace remote_display {

static const char *const TAG = "remote_display";

class RemoteDisplay : public display::Display, public web_server_idf::AsyncWebHandler {
 public:
  void setup() override { this->webserver_->add_handler(this); }

  void update() override {}

  bool canHandle(AsyncWebServerRequest *request) override {
    esph_log_d(TAG, "Request %s:%s", request->host().c_str(), request->url().c_str());
    return request->url() == "/ws/display";
  }
  void handleRequest(AsyncWebServerRequest *request) override {}

  void set_webserver(web_server_base::WebServerBase *webserver) { this->webserver_ = webserver; }
  void set_dimensions(size_t width, size_t height) {
    this->width_ = width;
    this->height_ = height;
  }

  int get_height() override { return this->height_; }
  int get_width() override { return this->width_; }

  void draw_pixel_at(int x, int y, Color color) override {}

  display::DisplayType get_display_type() override { return display::DISPLAY_TYPE_COLOR; }

 protected:
  web_server_base::WebServerBase *webserver_{};
  size_t width_{};
  size_t height_{};
};

}  // namespace remote_display
}  // namespace esphome
