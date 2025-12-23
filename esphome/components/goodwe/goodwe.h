#pragma once

#include "esphome/components/bytebuffer/bytebuffer.h"
#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "esphome/components/transport/transport.h"

#include <map>
#include <optional>

#include "esphome/core/hal.h"

namespace esphome {
namespace goodwe {
static const char *TAG = "goodwe";
static constexpr uint32_t COMMAND_TIMEOUT_MS = 2000;

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

class Setting : public Parameter {
 public:
  Setting(const char *type, uint16_t cmd_code) : Parameter(type), cmd_code_(cmd_code) {}

  // method to get a command to be sent. An empty buffer will not be sent, so means nothing to do.
  virtual std::vector<uint8_t> get_data() = 0;
  uint16_t get_cmd_code() const { return cmd_code_; }
  virtual bool should_send() = 0;

 protected:
  uint16_t cmd_code_;
};

class Message {
 public:
  Message(uint16_t code, uint16_t interval) : code_(code), interval_(interval) {}

  void add_parameter(Parameter *parameter) { this->parameters_.push_back(parameter); }

  void decode(bytebuffer::ByteBuffer &data) const {
    for (auto *p : this->parameters_)
      p->decode(data);
  }

  bool should_send() {
    auto now = millis();
    uint32_t elapsed = now - this->last_time_;
    if (elapsed > this->interval_ * 1000) {
      this->last_time_ = now;
      return true;
    }
    return false;
  }

  uint16_t get_code() const { return this->code_; }
  uint16_t get_interval() const { return this->interval_; }
  std::vector<Parameter *> &get_parameters() { return this->parameters_; }

 protected:
  uint16_t code_;
  uint16_t interval_;
  uint32_t last_time_{};
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

  void loop() override;

  void add_message(uint16_t code, uint16_t interval) { this->messages_.emplace(code, new Message(code, interval)); }

  void add_parameter(uint16_t code, Parameter *parameter) {
    auto message = this->messages_.find(code);
    if (message != this->messages_.end())
      message->second->add_parameter(parameter);
  }
  void add_setting(uint16_t code, Setting *setting) {
    this->add_parameter(code, setting);
    this->settings_.push_back(setting);
  }

  void dump_config() override;

 protected:
  transport::Transport *transport_;
  std::map<uint16_t, Message *> messages_{};
  std::vector<Setting *> settings_{};
  uint8_t arm_version_{0};
  uint16_t counter_{0};
  std::vector<uint8_t> buffer_;
  uint16_t last_command_{};
  uint32_t last_command_time_{0};

  void on_receive_(const std::vector<uint8_t> &data);
  void send_command_(uint16_t cmd, std::optional<std::vector<uint8_t>> data = {});
  bool is_waiting();
  void process_data_();
};
}  // namespace goodwe
}  // namespace esphome
