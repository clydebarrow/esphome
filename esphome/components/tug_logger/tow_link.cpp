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

static uint32_t get32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (uint32_t(p[3]) << 24); }

void TowLink::on_packet_(const PacketBuffer &data) {
  if (data.size() == 0)
    return;
  uint8_t type;
  data.copy_to(&type, 1);
  const size_t expected = type == tow_packet::TYPE_ANSWER            ? tow_packet::ANSWER_SIZE
                          : type == tow_packet::TYPE_FIRMWARE_ANSWER ? tow_packet::FIRMWARE_ANSWER_SIZE
                                                                     : 0;
  if (expected == 0 || data.size() != expected) {
    ESP_LOGD(TAG, "Packet of %zu bytes, type 0x%02X, is not an answer", data.size(), type);
    return;
  }
  std::vector<uint8_t> answer(expected);
  data.copy_to(answer.data(), expected);
  std::lock_guard<std::mutex> guard(this->lock_);
  // An answer to a request sent before, whose answer came late, or to one sent again after a lost answer
  if (this->state_ != State::STATE_SENT || type != this->answer_type_ ||
      memcmp(answer.data() + 1, this->tag_, tow_packet::TAG_SIZE) != 0) {
    ESP_LOGD(TAG, "Answer of type 0x%02X for a request not waiting", type);
    return;
  }
  this->answer_ = std::move(answer);
  this->state_ = State::STATE_ANSWERED;
}

TowLink::Outcome TowLink::request_(uint8_t type, uint8_t answer_type, const std::string &body, const uint8_t *mac,
                                   uint32_t timeout_ms) {
  {
    std::lock_guard<std::mutex> guard(this->lock_);
    this->packet_.resize(tow_packet::TOW_HEADER_SIZE + body.size());
    this->packet_[0] = type;
    memcpy(this->packet_.data() + 1, mac, tow_packet::MAC_SIZE);
    memcpy(this->packet_.data() + tow_packet::TOW_HEADER_SIZE, body.data(), body.size());
    memcpy(this->tag_, mac, tow_packet::TAG_SIZE);
    this->answer_type_ = answer_type;
    this->answer_.clear();
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
      outcome = Outcome::OUTCOME_ANSWERED;
      break;
    }
    // A request loop() has not sent yet is taken back too, so it is not sent after this has given up
    if (millis() - start >= timeout_ms)
      break;
  }
  std::lock_guard<std::mutex> guard(this->lock_);
  this->state_ = State::STATE_IDLE;
  return outcome;
}

TowLink::Outcome TowLink::post(const std::string &body, const uint8_t *mac, uint32_t timeout_ms, int &status) {
  const Outcome outcome = this->request_(tow_packet::TYPE_TOW, tow_packet::TYPE_ANSWER, body, mac, timeout_ms);
  if (outcome == Outcome::OUTCOME_ANSWERED) {
    std::lock_guard<std::mutex> guard(this->lock_);
    const uint8_t *p = this->answer_.data() + 1 + tow_packet::TAG_SIZE;
    status = p[0] | (p[1] << 8);
  }
  return outcome;
}

TowLink::Outcome TowLink::check_firmware(const std::string &body, const uint8_t *mac, uint32_t timeout_ms,
                                         FirmwareAnswer &answer) {
  const Outcome outcome =
      this->request_(tow_packet::TYPE_FIRMWARE_CHECK, tow_packet::TYPE_FIRMWARE_ANSWER, body, mac, timeout_ms);
  if (outcome == Outcome::OUTCOME_ANSWERED) {
    std::lock_guard<std::mutex> guard(this->lock_);
    const uint8_t *p = this->answer_.data() + 1 + tow_packet::TAG_SIZE;
    answer.status = p[0] | (p[1] << 8);
    answer.update = p[2] != 0;
    answer.built = get32(p + 3);
    answer.uploaded = get32(p + 7);
  }
  return outcome;
}

}  // namespace esphome::tug_logger

#endif  // USE_TUG_LOGGER_UPLOAD_LINK
