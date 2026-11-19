#include "tow_link.h"

#ifdef USE_TUG_LOGGER_UPLOAD_LINK

#include <cstring>

#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#ifdef USE_ESP32
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif

namespace esphome::tug_logger {

using packet_interface::PacketBuffer;
using packet_interface::PacketMetaData;

ESPHOME_LOG_TAG(TAG, "tug_logger.link");

// How often post() looks for the answer
static constexpr uint32_t POLL_MS = 50;

void TowLink::setup() {
  this->link_->top()->add_packet_interface_listener(
      [this](const PacketBuffer &data, const PacketMetaData &meta_data) { this->on_packet_(data); });
}

void TowLink::loop() {
  std::lock_guard<std::mutex> guard(this->lock_);
  if (this->state_ != State::STATE_WAITING_TO_SEND)
    return;
  auto *top = this->link_->top();
  if (this->packet_.size() > top->get_max_packet_size()) {
    ESP_LOGW(TAG, "A tow of %zu bytes is larger than the link takes (%zu)", this->packet_.size(),
             top->get_max_packet_size());
    this->state_ = State::STATE_NOT_SENT;
    return;
  }
  this->state_ = top->send_to_interface(PacketBuffer(this->packet_), {}) ? State::STATE_SENT : State::STATE_NOT_SENT;
}

void TowLink::on_packet_(const PacketBuffer &data) {
  uint8_t answer[tow_packet::ANSWER_SIZE];
  if (data.size() != sizeof(answer)) {
    ESP_LOGD(TAG, "Packet of %zu bytes is not an answer", data.size());
    return;
  }
  data.copy_to(answer, sizeof(answer));
  if (answer[0] != tow_packet::TYPE_ANSWER) {
    ESP_LOGD(TAG, "Packet of type 0x%02X is not an answer", answer[0]);
    return;
  }
  const uint16_t status = answer[1 + tow_packet::TAG_SIZE] | (answer[2 + tow_packet::TAG_SIZE] << 8);
  std::lock_guard<std::mutex> guard(this->lock_);
  // An answer to a tow sent before, whose answer came late, or to a tow sent again after a lost answer
  if (this->state_ != State::STATE_SENT || memcmp(answer + 1, this->tag_, tow_packet::TAG_SIZE) != 0) {
    ESP_LOGD(TAG, "Answer %u for a tow not waiting", status);
    return;
  }
  this->status_ = status;
  this->state_ = State::STATE_ANSWERED;
}

TowLink::Outcome TowLink::post(const std::string &body, const uint8_t *mac, uint32_t timeout_ms, int &status) {
  {
    std::lock_guard<std::mutex> guard(this->lock_);
    this->packet_.resize(tow_packet::TOW_HEADER_SIZE + body.size());
    this->packet_[0] = tow_packet::TYPE_TOW;
    memcpy(this->packet_.data() + 1, mac, tow_packet::MAC_SIZE);
    memcpy(this->packet_.data() + tow_packet::TOW_HEADER_SIZE, body.data(), body.size());
    memcpy(this->tag_, mac, tow_packet::TAG_SIZE);
    this->state_ = State::STATE_WAITING_TO_SEND;
  }
  const uint32_t start = millis();
  Outcome outcome = Outcome::OUTCOME_NO_ANSWER;
  while (true) {
#ifdef USE_ESP32
    vTaskDelay(pdMS_TO_TICKS(POLL_MS));
#else
    delay(POLL_MS);
#endif
    std::lock_guard<std::mutex> guard(this->lock_);
    if (this->state_ == State::STATE_NOT_SENT) {
      outcome = Outcome::OUTCOME_NOT_SENT;
      break;
    }
    if (this->state_ == State::STATE_ANSWERED) {
      status = this->status_;
      outcome = Outcome::OUTCOME_ANSWERED;
      break;
    }
    // A tow loop() has not sent yet is taken back too, so it is not sent after post() has given up
    if (millis() - start >= timeout_ms)
      break;
  }
  std::lock_guard<std::mutex> guard(this->lock_);
  this->state_ = State::STATE_IDLE;
  return outcome;
}

}  // namespace esphome::tug_logger

#endif  // USE_TUG_LOGGER_UPLOAD_LINK
