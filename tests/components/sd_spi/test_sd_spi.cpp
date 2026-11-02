#include <gtest/gtest.h>

#include <algorithm>
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
  void tick(uint32_t ahead_ms = 0) {
    LoopBlockingGuard clock{nullptr, nullptr, millis() + ahead_ms};
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

// A cache of 4 sectors keeps writes of up to 2 sectors
static constexpr uint16_t CACHE_SECTORS = 4;
static constexpr uint32_t PAST_FLUSH_DELAY_MS = 100;

static std::vector<uint8_t> card_sector(const FakeCard &card, uint32_t sector) {
  return {card.data.begin() + sector * SECTOR, card.data.begin() + (sector + 1) * SECTOR};
}

TEST(SdSpi, CachedWriteReachesCardFromLoop) {
  FakeCard card({});
  TestSdSpi sd(&card);
  sd.set_write_cache(CACHE_SECTORS);
  sd.start();
  std::vector<uint8_t> out(SECTOR), in(SECTOR);
  fill(out, 7);
  const uint32_t commands = card.commands;
  ASSERT_EQ(sd.program(5, 0, out.data(), SECTOR), StorageError::STORAGE_ERROR_NONE);
  EXPECT_EQ(card.commands, commands);
  EXPECT_NE(card_sector(card, 5), out);
  // Read back from the cache before it is written
  ASSERT_EQ(sd.read(5, 0, in.data(), SECTOR), StorageError::STORAGE_ERROR_NONE);
  EXPECT_EQ(in, out);
  EXPECT_EQ(card.commands, commands);
  // Not written while more writes may follow
  sd.tick();
  EXPECT_EQ(card.commands, commands);
  sd.tick(PAST_FLUSH_DELAY_MS);
  EXPECT_EQ(card.commands, commands + 1);
  EXPECT_EQ(card_sector(card, 5), out);
}

TEST(SdSpi, SyncWritesCacheJoiningSectors) {
  FakeCard card({});
  TestSdSpi sd(&card);
  sd.set_write_cache(CACHE_SECTORS);
  sd.start();
  std::vector<uint8_t> a(2 * SECTOR), b(SECTOR), c(SECTOR);
  fill(a, 1);
  fill(b, 2);
  fill(c, 3);
  // Out of order, with 10 and 11 next to each other and 20 on its own
  ASSERT_EQ(sd.program(11, 0, b.data(), SECTOR), StorageError::STORAGE_ERROR_NONE);
  ASSERT_EQ(sd.program(20, 0, c.data(), SECTOR), StorageError::STORAGE_ERROR_NONE);
  ASSERT_EQ(sd.program(9, 0, a.data(), 2 * SECTOR), StorageError::STORAGE_ERROR_NONE);  // 9 and 10
  const uint32_t commands = card.commands;
  ASSERT_EQ(sd.sync(), StorageError::STORAGE_ERROR_NONE);
  EXPECT_EQ(card.commands, commands + 2);
  EXPECT_EQ(std::vector<uint8_t>(card.data.begin() + 9 * SECTOR, card.data.begin() + 11 * SECTOR), a);
  EXPECT_EQ(card_sector(card, 11), b);
  EXPECT_EQ(card_sector(card, 20), c);
  // Nothing is left to write
  sd.tick(PAST_FLUSH_DELAY_MS);
  EXPECT_EQ(card.commands, commands + 2);
}

TEST(SdSpi, MultiSectorReadSeesCachedSectors) {
  FakeCard card({});
  TestSdSpi sd(&card);
  sd.set_write_cache(CACHE_SECTORS);
  sd.start();
  std::vector<uint8_t> on_card(4 * SECTOR), cached(SECTOR), in(4 * SECTOR);
  fill(on_card, 4);
  fill(cached, 5);
  std::copy(on_card.begin(), on_card.end(), card.data.begin() + 2 * SECTOR);
  ASSERT_EQ(sd.program(3, 0, cached.data(), SECTOR), StorageError::STORAGE_ERROR_NONE);
  ASSERT_EQ(sd.read(2, 0, in.data(), 4 * SECTOR), StorageError::STORAGE_ERROR_NONE);
  std::copy(cached.begin(), cached.end(), on_card.begin() + SECTOR);
  EXPECT_EQ(in, on_card);
}

TEST(SdSpi, LargeWriteReplacesCachedCopies) {
  FakeCard card({});
  TestSdSpi sd(&card);
  sd.set_write_cache(CACHE_SECTORS);
  sd.start();
  std::vector<uint8_t> old_data(SECTOR), big(3 * SECTOR), in(SECTOR);
  fill(old_data, 6);
  fill(big, 7);
  ASSERT_EQ(sd.program(7, 0, old_data.data(), SECTOR), StorageError::STORAGE_ERROR_NONE);
  // 3 sectors is more than half the cache, so this goes straight to the card
  ASSERT_EQ(sd.program(6, 0, big.data(), 3 * SECTOR), StorageError::STORAGE_ERROR_NONE);
  ASSERT_EQ(sd.read(7, 0, in.data(), SECTOR), StorageError::STORAGE_ERROR_NONE);
  EXPECT_TRUE(std::equal(in.begin(), in.end(), big.begin() + SECTOR));
  // The old copy must not be written over the new data later
  ASSERT_EQ(sd.sync(), StorageError::STORAGE_ERROR_NONE);
  EXPECT_TRUE(std::equal(big.begin(), big.end(), card.data.begin() + 6 * SECTOR));
}

TEST(SdSpi, FullCacheMakesRoom) {
  FakeCard card({});
  TestSdSpi sd(&card);
  sd.set_write_cache(CACHE_SECTORS);
  sd.start();
  std::vector<std::vector<uint8_t>> blocks(CACHE_SECTORS + 2, std::vector<uint8_t>(SECTOR));
  for (size_t i = 0; i != blocks.size(); i++) {
    fill(blocks[i], static_cast<uint8_t>(i));
    ASSERT_EQ(sd.program(100 + i * 2, 0, blocks[i].data(), SECTOR), StorageError::STORAGE_ERROR_NONE);
  }
  std::vector<uint8_t> in(SECTOR);
  for (size_t i = 0; i != blocks.size(); i++) {
    ASSERT_EQ(sd.read(100 + i * 2, 0, in.data(), SECTOR), StorageError::STORAGE_ERROR_NONE);
    EXPECT_EQ(in, blocks[i]);
  }
  ASSERT_EQ(sd.sync(), StorageError::STORAGE_ERROR_NONE);
  for (size_t i = 0; i != blocks.size(); i++)
    EXPECT_EQ(card_sector(card, 100 + i * 2), blocks[i]);
}

TEST(SdSpi, LoopDoesNotWaitForBusyCard) {
  FakeCard card({.busy_bytes = 1000});
  TestSdSpi sd(&card);
  sd.set_write_cache(CACHE_SECTORS);
  sd.start();
  std::vector<uint8_t> big(3 * SECTOR), out(SECTOR);
  fill(big, 8);
  fill(out, 9);
  // Returns while the card is still programming
  ASSERT_EQ(sd.program(40, 0, big.data(), 3 * SECTOR), StorageError::STORAGE_ERROR_NONE);
  ASSERT_EQ(sd.program(50, 0, out.data(), SECTOR), StorageError::STORAGE_ERROR_NONE);
  const uint32_t commands = card.commands;
  // Each pass checks the card once and leaves it alone while it is busy
  for (int i = 0; i != 10; i++)
    sd.tick(PAST_FLUSH_DELAY_MS);
  EXPECT_EQ(card.commands, commands);
  // A sync waits as long as it has to
  ASSERT_EQ(sd.sync(), StorageError::STORAGE_ERROR_NONE);
  EXPECT_EQ(card_sector(card, 50), out);
}

TEST(SdSpi, ShutdownWritesCacheThenStopsCaching) {
  FakeCard card({.busy_bytes = 20});
  TestSdSpi sd(&card);
  sd.set_write_cache(CACHE_SECTORS);
  sd.start();
  std::vector<uint8_t> before(SECTOR), after(SECTOR);
  fill(before, 11);
  fill(after, 12);
  ASSERT_EQ(sd.program(30, 0, before.data(), SECTOR), StorageError::STORAGE_ERROR_NONE);
  sd.on_shutdown();
  EXPECT_EQ(card_sector(card, 30), before);
  // A filesystem that shuts down later still reaches the card
  ASSERT_EQ(sd.program(31, 0, after.data(), SECTOR), StorageError::STORAGE_ERROR_NONE);
  EXPECT_EQ(card_sector(card, 31), after);
}

TEST(SdSpi, UncachedWritesStillReachCard) {
  FakeCard card({});
  TestSdSpi sd(&card);
  sd.start();
  std::vector<uint8_t> out(SECTOR);
  fill(out, 10);
  ASSERT_EQ(sd.program(3, 0, out.data(), SECTOR), StorageError::STORAGE_ERROR_NONE);
  EXPECT_EQ(card_sector(card, 3), out);
  ASSERT_EQ(sd.sync(), StorageError::STORAGE_ERROR_NONE);
}

}  // namespace esphome::sd_spi::testing
