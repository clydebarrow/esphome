#pragma once

#include "esphome/core/defines.h"

#ifdef USE_TUG_LOGGER_UPLOAD_LINK

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "esphome/components/packet_interface/packet_interface.h"

#include "tow_store.h"

namespace esphome::tug_logger {

/**
 * Tows sent over a packet interface link, such as LoRa, to a receiver that posts them to the Worker and answers with
 * the Worker's status. The receiver forwards the body and the tug's own signature unchanged, so the Worker checks the
 * tug's key as it does for an upload over the internet, and the receiver needs no key of its own for tugs.
 *
 * All fields are little endian:
 * - Tow, from the tug: 'T', the 32-byte HMAC-SHA256 of the body with the tug's key, then the JSON body of POST
 *   /api/tows.
 * - Answer, from the receiver: 'K', the first 4 bytes of that HMAC, then the 16-bit HTTP status from the Worker, or 0
 *   if the Worker could not be reached.
 *
 * A tow is several fragments over LoRa, so the link needs a fragmentation layer; an encryption layer keeps the names
 * in it private. The Worker takes the same tow again as a duplicate, so a tow whose answer is lost is sent again.
 */
namespace tow_packet {
static constexpr uint8_t TYPE_TOW = 'T';
static constexpr uint8_t TYPE_ANSWER = 'K';
static constexpr size_t MAC_SIZE = 32;
static constexpr size_t TAG_SIZE = 4;
static constexpr size_t TOW_HEADER_SIZE = 1 + MAC_SIZE;
static constexpr size_t ANSWER_SIZE = 1 + TAG_SIZE + 2;
}  // namespace tow_packet

class TowLink {
 public:
  explicit TowLink(packet_interface::PacketInterface *link) : link_(link) {}

  /// Main loop: listens for answers.
  void setup();
  /// Main loop: sends the tow that post() is waiting on.
  void loop();

  enum class Outcome : uint8_t {
    /// The receiver answered; the status says what the Worker did
    OUTCOME_ANSWERED,
    /// The link could not take the tow, as when it is not connected, or the tow is too large for it
    OUTCOME_NOT_SENT,
    /// Sent, but no answer came in time
    OUTCOME_NO_ANSWER,
  };

  /**
   * Upload task: sends one tow and waits for the receiver's answer. Blocks for up to timeout_ms.
   * @param mac The HMAC-SHA256 of body with the tug's key
   * @param status Set to the Worker's status when the receiver answered
   */
  Outcome post(const std::string &body, const uint8_t *mac, uint32_t timeout_ms, int &status);

 protected:
  enum class State : uint8_t {
    STATE_IDLE,
    // post() has a tow for loop() to send
    STATE_WAITING_TO_SEND,
    STATE_SENT,
    STATE_NOT_SENT,
    STATE_ANSWERED,
  };

  void on_packet_(const packet_interface::PacketBuffer &data);

  packet_interface::PacketInterface *link_;
  std::mutex lock_;
  State state_{State::STATE_IDLE};
  std::vector<uint8_t> packet_;
  uint8_t tag_[tow_packet::TAG_SIZE]{};
  uint16_t status_{0};
};

}  // namespace esphome::tug_logger

#endif  // USE_TUG_LOGGER_UPLOAD_LINK
