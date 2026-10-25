#include <gtest/gtest.h>

#include <array>
#include <cstring>
#include <vector>

#include "esphome/components/sd_spi/sd_spi.h"
#include "esphome/core/application.h"
#include "esphome/core/hal.h"
#include "fake_card.h"

namespace esphome::sd_spi::testing {

using storage::StorageError;

static constexpr uint32_t SECTOR = 512;
static constexpr uint32_t FAST_RATE = 20000000;

// Talks to a FakeCard instead of a real bus, and records each data rate the driver asks for.
class TestSdSpi : public SdSpi {
 public:
  explicit TestSdSpi(FakeCard *card) : card_(card) {
    this->set_cs_pin(&card->cs);
    this->set_data_rate(FAST_RATE);
  }

  void spi_setup() override {
    this->delegate_ = this->card_;
    this->rates.push_back(this->data_rate_);
  }
  void spi_teardown() override {}

  void check() { this->check_card_(); }

  // One main loop pass, with the loop clock following millis() as it does on a device
  void tick() {
    LoopBlockingGuard clock{nullptr, nullptr, millis()};
    this->loop();
  }

  // Run the start-up loop until the card leaves the initializing state
  void run_init() {
    for (int i = 0; i != 20 && this->get_card_state() == CardState::CARD_STATE_INITIALIZING; i++)
      this->tick();
  }

  void start() {
    this->setup();
    this->run_init();
  }

  std::vector<uint32_t> rates;

 protected:
  FakeCard *card_;
};

static void fill(std::vector<uint8_t> &buf, uint8_t seed) {
  for (size_t i = 0; i != buf.size(); i++)
    buf[i] = static_cast<uint8_t>(seed + i * 13);
}

TEST(SdSpi, StartsHighCapacityCard) {
  FakeCard card({});
  TestSdSpi sd(&card);
  sd.start();
  ASSERT_EQ(sd.get_card_state(), CardState::CARD_STATE_READY);
  EXPECT_EQ(sd.get_card_type(), CardType::CARD_TYPE_SDHC);
  EXPECT_TRUE(sd.has_media());
  EXPECT_EQ(sd.get_geometry().block_size, SECTOR);
  EXPECT_EQ(sd.get_geometry().block_count, card.block_count());
  EXPECT_TRUE(sd.get_geometry().flags & storage::BLOCK_FLAG_REMOVABLE);
  // Started slow, then switched to the configured rate
  ASSERT_EQ(sd.rates.size(), 2u);
  EXPECT_EQ(sd.rates[0], 400000u);
  EXPECT_EQ(sd.rates[1], FAST_RATE);
}

TEST(SdSpi, StartsVersion1Card) {
  FakeCard card({.high_capacity = false, .version1 = true});
  TestSdSpi sd(&card);
  sd.start();
  ASSERT_EQ(sd.get_card_state(), CardState::CARD_STATE_READY);
  EXPECT_EQ(sd.get_card_type(), CardType::CARD_TYPE_SD1);
  EXPECT_EQ(sd.get_geometry().block_count, 64u);
}

TEST(SdSpi, StartsStandardCapacityVersion2Card) {
  FakeCard card({.high_capacity = false});
  TestSdSpi sd(&card);
  sd.start();
  ASSERT_EQ(sd.get_card_state(), CardState::CARD_STATE_READY);
  EXPECT_EQ(sd.get_card_type(), CardType::CARD_TYPE_SD2);
}

TEST(SdSpi, NoCardMeansNoMedia) {
  FakeCard card({});
  card.present = false;
  TestSdSpi sd(&card);
  sd.start();
  EXPECT_EQ(sd.get_card_state(), CardState::CARD_STATE_NONE);
  EXPECT_FALSE(sd.has_media());
  uint8_t buf[SECTOR];
  EXPECT_EQ(sd.read(0, 0, buf, SECTOR), StorageError::STORAGE_ERROR_NO_MEDIA);
}

TEST(SdSpi, SingleBlockRoundTrip) {
  FakeCard card({});
  TestSdSpi sd(&card);
  sd.start();
  std::vector<uint8_t> out(SECTOR), in(SECTOR);
  fill(out, 7);
  ASSERT_EQ(sd.program(10, 0, out.data(), SECTOR), StorageError::STORAGE_ERROR_NONE);
  EXPECT_EQ(memcmp(card.data.data() + 10 * SECTOR, out.data(), SECTOR), 0);
  ASSERT_EQ(sd.read(10, 0, in.data(), SECTOR), StorageError::STORAGE_ERROR_NONE);
  EXPECT_EQ(in, out);
}

TEST(SdSpi, MultiBlockRoundTrip) {
  FakeCard card({});
  TestSdSpi sd(&card);
  sd.start();
  std::vector<uint8_t> out(4 * SECTOR), in(4 * SECTOR);
  fill(out, 99);
  // Block plus offset addressing: block 20, two sectors in
  ASSERT_EQ(sd.program(20, 2 * SECTOR, out.data(), out.size()), StorageError::STORAGE_ERROR_NONE);
  EXPECT_EQ(memcmp(card.data.data() + 22 * SECTOR, out.data(), out.size()), 0);
  ASSERT_EQ(sd.read(22, 0, in.data(), in.size()), StorageError::STORAGE_ERROR_NONE);
  EXPECT_EQ(in, out);
}

TEST(SdSpi, StandardCapacityUsesByteAddresses) {
  FakeCard card({.high_capacity = false, .version1 = true});
  TestSdSpi sd(&card);
  sd.start();
  std::vector<uint8_t> out(SECTOR), in(SECTOR);
  fill(out, 1);
  ASSERT_EQ(sd.program(5, 0, out.data(), SECTOR), StorageError::STORAGE_ERROR_NONE);
  EXPECT_EQ(memcmp(card.data.data() + 5 * SECTOR, out.data(), SECTOR), 0);
  ASSERT_EQ(sd.read(5, 0, in.data(), SECTOR), StorageError::STORAGE_ERROR_NONE);
  EXPECT_EQ(in, out);
}

TEST(SdSpi, RemovedCardIsDroppedAndFoundAgain) {
  FakeCard card({});
  TestSdSpi sd(&card);
  sd.start();
  ASSERT_TRUE(sd.has_media());
  const uint8_t generation = sd.get_media_generation();

  card.present = false;
  uint8_t buf[SECTOR];
  EXPECT_EQ(sd.read(0, 0, buf, SECTOR), StorageError::STORAGE_ERROR_TIMEOUT);
  EXPECT_FALSE(sd.has_media());
  EXPECT_NE(sd.get_media_generation(), generation);

  card.present = true;
  sd.check();
  ASSERT_EQ(sd.get_card_state(), CardState::CARD_STATE_INITIALIZING);
  sd.run_init();
  EXPECT_EQ(sd.get_card_state(), CardState::CARD_STATE_READY);
  EXPECT_EQ(sd.read(0, 0, buf, SECTOR), StorageError::STORAGE_ERROR_NONE);
}

TEST(SdSpi, CardDetectPinGatesStartAndRemoval) {
  FakeCard card({});
  FakePin detect;
  detect.level = false;
  TestSdSpi sd(&card);
  sd.set_card_detect_pin(&detect);
  sd.start();
  EXPECT_EQ(sd.get_card_state(), CardState::CARD_STATE_NONE);
  EXPECT_EQ(card.commands, 0u);

  detect.level = true;
  sd.check();
  sd.run_init();
  ASSERT_EQ(sd.get_card_state(), CardState::CARD_STATE_READY);

  detect.level = false;
  sd.check();
  EXPECT_EQ(sd.get_card_state(), CardState::CARD_STATE_NONE);
  EXPECT_FALSE(sd.has_media());
}

TEST(SdSpi, GivesUpWhenCardStaysIdle) {
  FakeCard card({.idle_polls = 255});
  TestSdSpi sd(&card);
  sd.setup();
  ASSERT_EQ(sd.get_card_state(), CardState::CARD_STATE_INITIALIZING);
  sd.tick();
  EXPECT_EQ(sd.get_card_state(), CardState::CARD_STATE_INITIALIZING);
  {
    LoopBlockingGuard clock{nullptr, nullptr, millis() + 5000};
    sd.loop();
  }
  EXPECT_EQ(sd.get_card_state(), CardState::CARD_STATE_NONE);
  EXPECT_FALSE(sd.has_media());
}

}  // namespace esphome::sd_spi::testing
