#pragma once

#include <cstddef>
#include <string>

namespace esphome::ble_serial {

/**
 * Text waiting to go out over a serial link that sends in small pieces. When it is full, whole lines are dropped from
 * the front, oldest first, so a reader that falls behind loses sentences rather than getting broken ones. A line
 * longer than the buffer is dropped.
 */
class SerialBuffer {
 public:
  explicit SerialBuffer(size_t capacity) : capacity_(capacity) { this->text_.reserve(capacity); }

  /// Adds text, dropping the oldest lines to make room. Returns the number of bytes dropped.
  size_t write(const std::string &text) {
    if (text.size() > this->capacity_)
      return text.size();
    size_t dropped = 0;
    while (this->text_.size() + text.size() > this->capacity_) {
      size_t end = this->text_.find('\n');
      end = end == std::string::npos ? this->text_.size() : end + 1;
      this->text_.erase(0, end);
      dropped += end;
    }
    this->text_.append(text);
    return dropped;
  }

  /// The text waiting, and the size of the next piece to send: at most max bytes, 0 when there is nothing waiting.
  const char *data() const { return this->text_.data(); }
  size_t piece(size_t max) const { return this->text_.size() < max ? this->text_.size() : max; }
  /// Removes a piece once it is sent.
  void consume(size_t count) { this->text_.erase(0, count); }
  void clear() { this->text_.clear(); }
  size_t size() const { return this->text_.size(); }
  bool empty() const { return this->text_.empty(); }

 protected:
  size_t capacity_;
  std::string text_;
};

}  // namespace esphome::ble_serial
