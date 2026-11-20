#include <gtest/gtest.h>

#include <string>

#include "esphome/components/ble_serial/serial_buffer.h"

namespace esphome::ble_serial::testing {

TEST(BLESerialBuffer, SendsInPiecesInOrder) {
  SerialBuffer buffer(64);
  EXPECT_EQ(buffer.write("$GPRMC,1*00\r\n$PFLAU,2*00\r\n"), 0u);
  std::string sent;
  while (!buffer.empty()) {
    const size_t size = buffer.piece(5);
    EXPECT_LE(size, 5u);
    sent.append(buffer.data(), size);
    buffer.consume(size);
  }
  EXPECT_EQ(sent, "$GPRMC,1*00\r\n$PFLAU,2*00\r\n");
}

TEST(BLESerialBuffer, DropsWholeOldLinesWhenFull) {
  SerialBuffer buffer(30);
  buffer.write("$AAAAAAAA*00\r\n");  // 14
  buffer.write("$BBBBBBBB*00\r\n");  // 28
  EXPECT_EQ(buffer.write("$CCCCCCCC*00\r\n"), 14u);
  EXPECT_EQ(std::string(buffer.data(), buffer.piece(100)), "$BBBBBBBB*00\r\n$CCCCCCCC*00\r\n");
  // Part of a line already sent: the rest of it goes first when room is needed
  buffer.consume(3);
  EXPECT_EQ(buffer.write("$DDDDDDDD*00\r\n"), 11u);
  EXPECT_EQ(std::string(buffer.data(), buffer.piece(100)), "$CCCCCCCC*00\r\n$DDDDDDDD*00\r\n");
}

TEST(BLESerialBuffer, TooLongIsDropped) {
  SerialBuffer buffer(8);
  EXPECT_EQ(buffer.write("0123456789"), 10u);
  EXPECT_TRUE(buffer.empty());
}

}  // namespace esphome::ble_serial::testing
