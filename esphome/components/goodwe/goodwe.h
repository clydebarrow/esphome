#pragma once

#include "esphome/components/bytebuffer/bytebuffer.h"
#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "esphome/components/transport/transport.h"

#include <map>

namespace esphome {
namespace goodwe {
static const char *TAG = "goodwe";

/**
 * Base class for parameters read using Goodwe protocol.
 */
class Parameter {
 public:
  Parameter(const char *type) : type_(type) {}

  virtual ~Parameter() = default;
  /**
   * Process the data received from the Goodwe device
   *
   * @param data The byte buffer containing the data received from the Goodwe device
   */
  virtual void decode(bytebuffer::ByteBuffer &data) = 0;
  virtual void dump_config() = 0;

 protected:
  const char *type_;
};

class Message {
  friend class Goodwe;

 public:
  Message(uint16_t code, uint16_t interval) : code_(code), interval_(interval) {}

  void add_parameter(Parameter *parameter) { this->parameters_.push_back(parameter); }

  void decode(bytebuffer::ByteBuffer &data) {
    for (auto *p : this->parameters_)
      p->decode(data);
  }

 protected:
  uint16_t code_;
  uint16_t interval_;
  std::vector<Parameter *> parameters_{};
};

class Goodwe : public PollingComponent {
 public:
  Goodwe(transport::Transport *transport) : transport_(transport){};
  virtual ~Goodwe() = default;

  void setup() override {
    this->transport_->add([this](const std::vector<uint8_t> &data) { this->on_receive_(data); });
  }

  void update() override;

  void loop() override{};

  void add_message(uint16_t code, uint16_t interval) {
    this->messages_.emplace(code, Message(code, interval));
    this->queries_.emplace_back(code, interval);
  }

  void add_parameter(uint16_t code, Parameter *parameter) {
    auto message = this->messages_.find(code);
    if (message != this->messages_.end())
      message->second.add_parameter(parameter);
  }

  void dump_config() override;

 protected:
  transport::Transport *transport_;
  std::vector<std::pair<uint16_t, uint16_t>> queries_{};
  std::map<uint16_t, Message> messages_{};
  uint8_t arm_version_{0};
  uint16_t counter_{0};
  std::vector<uint8_t> buffer_;

  void on_receive_(const std::vector<uint8_t> &data);
  void send_command_(uint16_t cmd) const;
  void process_data_();
};
}  // namespace goodwe
}  // namespace esphome
