#include <gtest/gtest.h>

#include "esphome/components/skyecho_wifi/chooser.h"

namespace esphome::skyecho_wifi::testing {

static const char *chosen(const SkyEchoChooser &chooser, const Candidate *candidates, size_t count, uint32_t now = 0) {
  const char *choice = chooser.choose(candidates, count, now);
  return choice == nullptr ? "" : choice;
}

TEST(SkyEchoWifiChooser, StrongestOpenSkyEcho) {
  SkyEchoChooser chooser(-80);
  const Candidate seen[] = {
      {"ClubWiFi", -30, true}, {"SkyEcho_AAA", -75, false}, {"SkyEcho_BBB", -40, false}, {"SkyEcho_CCC", -20, true}};
  // A secured network, even named SkyEcho, isn't one
  EXPECT_STREQ(chosen(chooser, seen, 4), "SkyEcho_BBB");
  const Candidate none[] = {{"ClubWiFi", -30, false}};
  EXPECT_STREQ(chosen(chooser, none, 1), "");
}

TEST(SkyEchoWifiChooser, RememberedFirstWhileItsSignalIsUsable) {
  SkyEchoChooser chooser(-80);
  chooser.set_remembered("SkyEcho_AAA");
  Candidate seen[] = {{"SkyEcho_AAA", -70, false}, {"SkyEcho_BBB", -40, false}};
  EXPECT_STREQ(chosen(chooser, seen, 2), "SkyEcho_AAA");
  // Too weak to use: the strongest instead
  seen[0].rssi = -85;
  EXPECT_STREQ(chosen(chooser, seen, 2), "SkyEcho_BBB");
}

TEST(SkyEchoWifiChooser, SkippedUntilItsTimeIsUp) {
  SkyEchoChooser chooser(-80);
  const Candidate seen[] = {{"SkyEcho_AAA", -40, false}, {"SkyEcho_BBB", -60, false}};
  chooser.skip("SkyEcho_AAA", 10000);
  EXPECT_STREQ(chosen(chooser, seen, 2, 5000), "SkyEcho_BBB");
  EXPECT_STREQ(chosen(chooser, seen, 2, 10000), "SkyEcho_AAA");
  // Skipping again moves its time on, and a full list reuses the slot that ends first
  for (uint32_t i = 0; i != MAX_SKIPPED + 1; i++) {
    char ssid[SSID_SIZE];
    snprintf(ssid, sizeof(ssid), "SkyEcho_%u", unsigned(i));
    chooser.skip(ssid, 20000 + i);
  }
  EXPECT_FALSE(chooser.is_skipped("SkyEcho_0", 15000));
  EXPECT_TRUE(chooser.is_skipped("SkyEcho_4", 15000));
}

TEST(SkyEchoWifiChooser, OnlySkyEchoNames) {
  EXPECT_TRUE(is_skyecho_ssid("SkyEcho_MKJ"));
  EXPECT_FALSE(is_skyecho_ssid("skyecho_mkj"));
  EXPECT_FALSE(is_skyecho_ssid("Sky"));
}

}  // namespace esphome::skyecho_wifi::testing
