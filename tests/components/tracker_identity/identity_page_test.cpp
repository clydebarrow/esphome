#include <gtest/gtest.h>

#include <cstring>

#include "esphome/components/tracker_identity/identity_page.h"

namespace esphome::tracker_identity::testing {

static Identity sample() {
  Identity identity;
  identity.address = 0x1234;
  for (size_t i = 0; i != KEY_SIZE; i++)
    identity.key[i] = uint8_t(i);
  strcpy(identity.registration, "VH-GEW");
  return identity;
}

// The same bytes are checked by the Worker's tests of the page it writes
static const char *const SAMPLE_PAGE_HEX = "4749443201003412000102030405060708090a0b0c0d0e0f"
                                           "101112131415161718191a1b1c1d1e1f56482d474557000000000000"
                                           "000000000000000044f791fd";

static std::string hex(const uint8_t *data, size_t length) {
  std::string text;
  char digits[3];
  for (size_t i = 0; i != length; i++) {
    snprintf(digits, sizeof(digits), "%02x", data[i]);
    text += digits;
  }
  return text;
}

TEST(IdentityPage, MakeAndParse) {
  uint8_t page[IDENTITY_PAGE_SIZE];
  make_identity_page(sample(), page);
  EXPECT_EQ(hex(page, IDENTITY_PAGE_SIZE), SAMPLE_PAGE_HEX);
  Identity parsed;
  ASSERT_TRUE(parse_identity_page(page, parsed));
  EXPECT_EQ(parsed.address, 0x1234);
  EXPECT_STREQ(parsed.registration, "VH-GEW");
  EXPECT_EQ(memcmp(parsed.key, sample().key, KEY_SIZE), 0);
}

TEST(IdentityPage, Crc) {
  // The standard check value of CRC-32 (IEEE)
  const char *text = "123456789";
  EXPECT_EQ(identity_page_crc(reinterpret_cast<const uint8_t *>(text), 9), 0xCBF43926u);
}

TEST(IdentityPage, RejectsErasedAndDamaged) {
  uint8_t page[IDENTITY_PAGE_SIZE];
  Identity parsed;
  memset(page, 0xFF, sizeof(page));
  EXPECT_FALSE(parse_identity_page(page, parsed));
  make_identity_page(sample(), page);
  page[10] ^= 1;
  EXPECT_FALSE(parse_identity_page(page, parsed));
  make_identity_page(sample(), page);
  page[4] = 2;
  EXPECT_FALSE(parse_identity_page(page, parsed));
}

TEST(IdentityPage, RejectsBadFields) {
  uint8_t page[IDENTITY_PAGE_SIZE];
  Identity parsed;
  Identity identity = sample();
  identity.address = 0;
  make_identity_page(identity, page);
  EXPECT_FALSE(parse_identity_page(page, parsed));
  identity = sample();
  strcpy(identity.registration, "VH GEW");
  make_identity_page(identity, page);
  EXPECT_FALSE(parse_identity_page(page, parsed));
  identity = sample();
  identity.registration[0] = '\0';
  make_identity_page(identity, page);
  EXPECT_FALSE(parse_identity_page(page, parsed));
}

TEST(IdentityPage, RegistrationUpperCase) {
  Identity identity = sample();
  strcpy(identity.registration, "vh-gew");
  uint8_t page[IDENTITY_PAGE_SIZE];
  make_identity_page(identity, page);
  Identity parsed;
  ASSERT_TRUE(parse_identity_page(page, parsed));
  EXPECT_STREQ(parsed.registration, "VH-GEW");
}

}  // namespace esphome::tracker_identity::testing
