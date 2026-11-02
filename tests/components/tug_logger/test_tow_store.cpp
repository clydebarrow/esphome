#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <unistd.h>

#include "esphome/components/json/json_util.h"
#include "esphome/components/tug_logger/tow_store.h"

namespace esphome::tug_logger::testing {

static constexpr time_t LAUNCH = 1793523541;  // 2026-11-01T08:59:01Z

static std::string read_all(const std::string &path) {
  std::ifstream in(path);
  std::stringstream text;
  text << in.rdbuf();
  return text.str();
}

static void write_all(const std::string &path, const std::string &text) { std::ofstream(path) << text; }

class TowStoreTest : public ::testing::Test {
 protected:
  void SetUp() override {
    char pattern[] = "/tmp/tug-store-XXXXXX";
    ASSERT_NE(mkdtemp(pattern), nullptr);
    this->root = pattern;
    this->store = std::make_unique<TowStore>(this->root);
    ASSERT_TRUE(this->store->begin());
  }
  void TearDown() override { ASSERT_EQ(system(("rm -rf " + this->root).c_str()), 0); }

  std::string file(const char *name) const { return this->root + "/tug/" + name; }
  void reopen() {
    this->store = std::make_unique<TowStore>(this->root);
    ASSERT_TRUE(this->store->begin());
  }
  static TowRecord tow(const char *glider = "WGQ") {
    TowRecord record;
    record.launch = LAUNCH;
    record.duration_s = 840;
    record.climb_s = 420;
    record.max_height_ft = 2000;
    record.details.glider_registration = glider;
    record.details.glider_pilot_1 = "Alex";
    record.details.tug_pilot = "Sam";
    return record;
  }

  std::string root;
  std::unique_ptr<TowStore> store;
};

TEST(TugTowRecord, UploadBodyHasTheFieldsTheServerRequires) {
  TowRecord record;
  record.launch = LAUNCH;
  record.duration_s = 840;
  record.climb_s = 420;
  record.max_height_ft = 2000;
  record.details = {"WGQ", "Alex", "Jo \"JJ\"", "Sam"};
  bool checked = false;
  ASSERT_TRUE(json::parse_json(tow_to_upload_json(record, "VH-TUG"), [&](JsonObject root) -> bool {
    EXPECT_STREQ(root["tug_registration"], "VH-TUG");
    EXPECT_STREQ(root["glider_registration"], "WGQ");
    EXPECT_STREQ(root["tug_pilot"], "Sam");
    EXPECT_STREQ(root["glider_pilot_1"], "Alex");
    EXPECT_STREQ(root["glider_pilot_2"], "Jo \"JJ\"");
    EXPECT_STREQ(root["launch"], "2026-11-01T08:59:01Z");
    EXPECT_EQ(root["max_height_ft"].as<int>(), 2000);
    EXPECT_EQ(root["climb_seconds"].as<int>(), 420);
    EXPECT_EQ(root["duration"].as<int>(), 840);
    checked = true;
    return true;
  }));
  EXPECT_TRUE(checked);
}

TEST(TugTowRecord, LeavesOutASecondPilotAndNegativeHeight) {
  TowRecord record;
  record.launch = LAUNCH;
  record.max_height_ft = -20;
  record.details.glider_registration = "WGQ";
  const std::string body = tow_to_upload_json(record, "VH-TUG");
  EXPECT_EQ(body.find("glider_pilot_2"), std::string::npos);
  EXPECT_NE(body.find("\"max_height_ft\":0"), std::string::npos);
}

TEST(TugTowRecord, ClassifiesServerAnswers) {
  EXPECT_EQ(classify_status(201), UploadResult::UPLOAD_RESULT_SENT);
  EXPECT_EQ(classify_status(200), UploadResult::UPLOAD_RESULT_SENT);
  EXPECT_EQ(classify_status(400), UploadResult::UPLOAD_RESULT_REFUSED);
  EXPECT_EQ(classify_status(409), UploadResult::UPLOAD_RESULT_REFUSED);
  EXPECT_EQ(classify_status(413), UploadResult::UPLOAD_RESULT_REFUSED);
  EXPECT_EQ(classify_status(401), UploadResult::UPLOAD_RESULT_RETRY);
  EXPECT_EQ(classify_status(500), UploadResult::UPLOAD_RESULT_RETRY);
  EXPECT_EQ(classify_status(0), UploadResult::UPLOAD_RESULT_RETRY);
  EXPECT_EQ(classify_status(-1), UploadResult::UPLOAD_RESULT_RETRY);
}

TEST(TugTowRecord, ParsesTheFileTheWorkerGives) {
  Provisioning settings;
  ASSERT_TRUE(parse_provisioning(
      R"({"organisation":"Warwick Gliding Club","tug":{"registration":"VH-TUG"},"field_elevation_ft":1500,)"
      R"("flight_upload_url":"https://x.workers.dev/api/tows","flight_upload_key":"secret"})",
      settings));
  EXPECT_EQ(settings.organisation, "Warwick Gliding Club");
  EXPECT_EQ(settings.tug_registration, "VH-TUG");
  EXPECT_FLOAT_EQ(settings.field_elevation_ft, 1500.0f);
  EXPECT_EQ(settings.upload_url, "https://x.workers.dev/api/tows");
  EXPECT_EQ(settings.upload_key, "secret");
}

TEST(TugTowRecord, RefusesAnIncompleteProvisioningFile) {
  Provisioning settings;
  EXPECT_FALSE(parse_provisioning(R"({"tug":{"registration":"VH-TUG"}})", settings));
  EXPECT_FALSE(parse_provisioning("not json", settings));
}

TEST_F(TowStoreTest, FindsTheProvisioningFileByItsDownloadName) {
  Provisioning settings;
  EXPECT_FALSE(this->store->load_provisioning(settings));
  const std::string body = R"({"tug":{"registration":"VH-TUG"},"flight_upload_url":"u","flight_upload_key":"k"})";
  write_all(this->root + "/tug-VH-TUG-device-1.json", body);
  EXPECT_TRUE(this->store->load_provisioning(settings));
  // Two downloads on the card: which one is meant isn't known
  write_all(this->root + "/tug-VH-TUG-device-2.json", body);
  EXPECT_FALSE(this->store->load_provisioning(settings));
  // The plain name settles it
  write_all(this->root + "/tug.json", body);
  EXPECT_TRUE(this->store->load_provisioning(settings));
}

TEST_F(TowStoreTest, ReviewListSurvivesARestart) {
  ASSERT_TRUE(this->store->add_review(tow("WGQ")));
  ASSERT_TRUE(this->store->add_review(tow("ABC")));
  this->reopen();
  auto list = this->store->review_list();
  ASSERT_EQ(list.size(), 2u);
  EXPECT_EQ(list[0].details.glider_registration, "WGQ");
  EXPECT_EQ(list[0].launch, LAUNCH);
  EXPECT_EQ(list[0].max_height_ft, 2000);
  // A new tow must not reuse an id
  ASSERT_TRUE(this->store->add_review(tow("NEW")));
  list = this->store->review_list();
  EXPECT_NE(list[2].id, list[0].id);
  EXPECT_NE(list[2].id, list[1].id);
}

TEST_F(TowStoreTest, RecoversTheListFromARestartInTheMiddleOfASave) {
  ASSERT_TRUE(this->store->add_review(tow("WGQ")));
  // The old file was removed and the new one not yet renamed
  ASSERT_EQ(rename(this->file("review.jsonl").c_str(), this->file("review.jsonl.tmp").c_str()), 0);
  this->reopen();
  EXPECT_EQ(this->store->review_list().size(), 1u);
}

TEST_F(TowStoreTest, IgnoresAPartialTempFileNextToTheRealOne) {
  ASSERT_TRUE(this->store->add_review(tow("WGQ")));
  write_all(this->file("review.jsonl.tmp"), "{\"id\":9,\"lau");
  this->reopen();
  auto list = this->store->review_list();
  ASSERT_EQ(list.size(), 1u);
  EXPECT_EQ(list[0].details.glider_registration, "WGQ");
}

TEST_F(TowStoreTest, SkipsADamagedLine) {
  ASSERT_TRUE(this->store->add_review(tow("WGQ")));
  std::ofstream(this->file("review.jsonl"), std::ios::app) << "{\"id\":9,\"lau\n";
  this->reopen();
  EXPECT_EQ(this->store->review_list().size(), 1u);
}

TEST_F(TowStoreTest, ConfirmNeedsAGliderAndATime) {
  ASSERT_TRUE(this->store->add_review(tow("")));
  TowRecord unknown = tow();
  unknown.launch = 0;
  ASSERT_TRUE(this->store->add_review(unknown));
  auto list = this->store->review_list();
  EXPECT_EQ(this->store->confirm(list[0].id, "VH-TUG"), ConfirmResult::CONFIRM_RESULT_NO_GLIDER);
  EXPECT_EQ(this->store->confirm(list[1].id, "VH-TUG"), ConfirmResult::CONFIRM_RESULT_NO_TIME);
  EXPECT_EQ(this->store->confirm(list[1].id + 100, "VH-TUG"), ConfirmResult::CONFIRM_RESULT_NOT_FOUND);
  EXPECT_EQ(this->store->queued_count(), 0u);
  EXPECT_EQ(this->store->review_list().size(), 2u);
}

TEST_F(TowStoreTest, EditedDetailsReachTheQueue) {
  ASSERT_TRUE(this->store->add_review(tow("")));
  const uint32_t id = this->store->review_list()[0].id;
  ASSERT_TRUE(this->store->update_details(id, {"WGQ", "Alex", "", "Sam"}));
  EXPECT_EQ(this->store->confirm(id, "VH-TUG"), ConfirmResult::CONFIRM_RESULT_OK);
  EXPECT_TRUE(this->store->review_list().empty());
  EXPECT_EQ(this->store->queued_count(), 1u);
  EXPECT_NE(read_all(this->file("tows-pending.jsonl")).find("\"glider_registration\":\"WGQ\""), std::string::npos);
}

TEST_F(TowStoreTest, DiscardedTowIsKeptButNotSent) {
  ASSERT_TRUE(this->store->add_review(tow()));
  ASSERT_TRUE(this->store->discard(this->store->review_list()[0].id));
  EXPECT_TRUE(this->store->review_list().empty());
  EXPECT_EQ(this->store->queued_count(), 0u);
  EXPECT_NE(read_all(this->file("tows-discarded.jsonl")).find("WGQ"), std::string::npos);
}

TEST_F(TowStoreTest, LaunchTimeIsWorkedOutWhenTheClockIsSet) {
  TowRecord record = tow();
  record.launch = 0;
  record.launch_ms = 10000;
  record.launch_ms_valid = true;
  ASSERT_TRUE(this->store->add_review(record));
  ASSERT_TRUE(this->store->resolve_launch_times(LAUNCH + 100, 110000));
  EXPECT_EQ(this->store->review_list()[0].launch, LAUNCH);
  EXPECT_FALSE(this->store->resolve_launch_times(LAUNCH + 200, 210000));
}

TEST_F(TowStoreTest, UnknownLaunchTimeCannotBeWorkedOutAfterARestart) {
  TowRecord record = tow();
  record.launch = 0;
  record.launch_ms = 10000;
  record.launch_ms_valid = true;
  ASSERT_TRUE(this->store->add_review(record));
  this->reopen();
  EXPECT_FALSE(this->store->resolve_launch_times(LAUNCH + 100, 110000));
  EXPECT_EQ(this->store->review_list()[0].launch, 0);
}

TEST_F(TowStoreTest, QueueSortsEachTowByTheServersAnswer) {
  for (const char *glider : {"SENT", "RETRY", "REFUSED"}) {
    ASSERT_TRUE(this->store->add_review(tow(glider)));
    ASSERT_EQ(this->store->confirm(this->store->review_list()[0].id, "VH-TUG"), ConfirmResult::CONFIRM_RESULT_OK);
  }
  ASSERT_EQ(this->store->queued_count(), 3u);
  this->store->process_queue([](const std::string &body) {
    if (body.find("\"SENT\"") != std::string::npos)
      return UploadResult::UPLOAD_RESULT_SENT;
    if (body.find("\"RETRY\"") != std::string::npos)
      return UploadResult::UPLOAD_RESULT_RETRY;
    return UploadResult::UPLOAD_RESULT_REFUSED;
  });
  EXPECT_EQ(this->store->queued_count(), 1u);
  EXPECT_EQ(this->store->failed_count(), 1u);
  EXPECT_NE(read_all(this->file("tows-pending.jsonl")).find("RETRY"), std::string::npos);
  EXPECT_NE(read_all(this->file("tows-failed.jsonl")).find("REFUSED"), std::string::npos);
  EXPECT_NE(read_all(this->file("tows-sent.jsonl")).find("SENT"), std::string::npos);
  EXPECT_EQ(access(this->file("tows-sending.jsonl").c_str(), F_OK), -1);
}

TEST_F(TowStoreTest, SendsTheExactBytesThatWereQueued) {
  ASSERT_TRUE(this->store->add_review(tow()));
  ASSERT_EQ(this->store->confirm(this->store->review_list()[0].id, "VH-TUG"), ConfirmResult::CONFIRM_RESULT_OK);
  std::string queued = read_all(this->file("tows-pending.jsonl"));
  queued.pop_back();
  std::string sent;
  this->store->process_queue([&sent](const std::string &body) {
    sent = body;
    return UploadResult::UPLOAD_RESULT_SENT;
  });
  EXPECT_EQ(sent, queued);
}

TEST_F(TowStoreTest, ASendingFileLeftByARestartIsSentFirst) {
  write_all(this->file("tows-sending.jsonl"), "{\"old\":1}\n");
  write_all(this->file("tows-pending.jsonl"), "{\"new\":1}\n");
  EXPECT_EQ(this->store->queued_count(), 2u);
  std::vector<std::string> bodies;
  auto post = [&bodies](const std::string &body) {
    bodies.push_back(body);
    return UploadResult::UPLOAD_RESULT_SENT;
  };
  this->store->process_queue(post);
  ASSERT_EQ(bodies.size(), 1u);
  EXPECT_EQ(bodies[0], "{\"old\":1}");
  this->store->process_queue(post);
  ASSERT_EQ(bodies.size(), 2u);
  EXPECT_EQ(bodies[1], "{\"new\":1}");
  EXPECT_EQ(this->store->queued_count(), 0u);
}

TEST_F(TowStoreTest, AnOversizedTowIsNotSent) {
  write_all(this->file("tows-pending.jsonl"), std::string(5000, 'x') + "\n");
  int posts = 0;
  this->store->process_queue([&posts](const std::string &) {
    posts++;
    return UploadResult::UPLOAD_RESULT_SENT;
  });
  EXPECT_EQ(posts, 0);
  EXPECT_EQ(this->store->failed_count(), 1u);
}

TEST_F(TowStoreTest, FlightInProgressSurvivesARestart) {
  FlightSnapshot snapshot;
  snapshot.launch = LAUNCH;
  snapshot.elapsed_s = 300;
  snapshot.climb_s = 200;
  snapshot.max_height_ft = 1800;
  snapshot.kind = AltitudeKind::ALTITUDE_KIND_PRESSURE;
  snapshot.reference_m = 450.5f;
  snapshot.has_reference = true;
  ASSERT_TRUE(this->store->save_flight(snapshot, {"WGQ", "Alex", "", "Sam"}, LAUNCH + 300));
  this->reopen();
  FlightSnapshot loaded;
  TowDetails details;
  time_t saved_at = 0;
  ASSERT_TRUE(this->store->load_flight(loaded, details, saved_at));
  EXPECT_EQ(loaded.launch, LAUNCH);
  EXPECT_EQ(loaded.elapsed_s, 300u);
  EXPECT_EQ(loaded.max_height_ft, 1800);
  EXPECT_EQ(loaded.kind, AltitudeKind::ALTITUDE_KIND_PRESSURE);
  EXPECT_FLOAT_EQ(loaded.reference_m, 450.5f);
  EXPECT_TRUE(loaded.has_reference);
  EXPECT_EQ(saved_at, LAUNCH + 300);
  EXPECT_EQ(details.glider_registration, "WGQ");
  this->store->clear_flight();
  EXPECT_FALSE(this->store->load_flight(loaded, details, saved_at));
}

TEST_F(TowStoreTest, RemembersNamesMostRecentFirstWithoutRepeats) {
  this->store->remember({"WGQ", "Alex", "", "Sam"});
  this->store->remember({"ABC", "alex", "Jo", "Sam"});
  this->reopen();
  EXPECT_EQ(this->store->recent_gliders(), (std::vector<std::string>{"ABC", "WGQ"}));
  EXPECT_EQ(this->store->recent_pilots(), (std::vector<std::string>{"Sam", "alex", "Jo"}));
}

TEST(CapitalCase, CapitalisesEachWord) {
  EXPECT_EQ(capital_case("alex smith"), "Alex Smith");
  EXPECT_EQ(capital_case("ALEX SMITH"), "Alex Smith");
  EXPECT_EQ(capital_case("smith-jones"), "Smith-Jones");
  EXPECT_EQ(capital_case("o'brien"), "O'Brien");
}

TEST(CapitalCase, LeavesAWordWithBothCases) { EXPECT_EQ(capital_case("alex McDonald"), "Alex McDonald"); }

TEST(CapitalCase, HandlesEmptyAndRepeatedSpaces) {
  EXPECT_EQ(capital_case(""), "");
  EXPECT_EQ(capital_case("a  b"), "A  B");
}

TEST(TowTotals, StartAtZeroAndCountAFlight) {
  TowTotals totals;
  EXPECT_EQ(totals.tows, 0u);
  totals.roll_to(20261102);
  totals.add_flight(840);
  EXPECT_EQ(totals.tows, 1u);
  EXPECT_EQ(totals.flight_s, 840u);
  EXPECT_EQ(totals.fuel_s, 840u);
}

TEST(TowTotals, ANewDayStartsTheDayCountsButNotTheFuel) {
  TowTotals totals;
  totals.roll_to(20261102);
  totals.add_flight(840);
  EXPECT_TRUE(totals.roll_to(20261103));
  EXPECT_EQ(totals.tows, 0u);
  EXPECT_EQ(totals.flight_s, 0u);
  EXPECT_EQ(totals.fuel_s, 840u);
}

TEST(TowTotals, ACountBeforeTheClockIsSetBelongsToTheFirstDate) {
  TowTotals totals;
  EXPECT_FALSE(totals.roll_to(0));
  totals.add_flight(840);
  EXPECT_TRUE(totals.roll_to(20261102));
  EXPECT_EQ(totals.tows, 1u);
  EXPECT_FALSE(totals.roll_to(20261102));
  EXPECT_FALSE(totals.roll_to(0));
  EXPECT_EQ(totals.day, 20261102u);
}

TEST(TowTotals, RemovingAFlightKeepsTheFuelAndStopsAtZero) {
  TowTotals totals;
  totals.add_flight(100);
  totals.remove_flight(100);
  EXPECT_EQ(totals.tows, 0u);
  EXPECT_EQ(totals.flight_s, 0u);
  EXPECT_EQ(totals.fuel_s, 100u);
  totals.remove_flight(100);
  EXPECT_EQ(totals.tows, 0u);
  EXPECT_EQ(totals.flight_s, 0u);
}

TEST_F(TowStoreTest, TotalsAreZeroWithNoFileAndSurviveARestart) {
  EXPECT_EQ(this->store->load_totals().tows, 0u);
  TowTotals totals;
  totals.roll_to(20261102);
  totals.add_flight(840);
  ASSERT_TRUE(this->store->save_totals(totals));
  this->reopen();
  const TowTotals loaded = this->store->load_totals();
  EXPECT_EQ(loaded.day, 20261102u);
  EXPECT_EQ(loaded.tows, 1u);
  EXPECT_EQ(loaded.flight_s, 840u);
  EXPECT_EQ(loaded.fuel_s, 840u);
}

TEST(GliderFromCallsign, MatchesHowTheClubWritesRegistrations) {
  EXPECT_EQ(glider_from_callsign("VHWGQ   "), "WGQ");
  EXPECT_EQ(glider_from_callsign("VH-WGQ"), "WGQ");
  EXPECT_EQ(glider_from_callsign("vh-wgq"), "WGQ");
  EXPECT_EQ(glider_from_callsign("WGQ"), "WGQ");
}

TEST(GliderFromCallsign, LeavesOtherCallsignsAlone) {
  EXPECT_EQ(glider_from_callsign("VH"), "VH");
  EXPECT_EQ(glider_from_callsign("VHABCD"), "VHABCD");
  EXPECT_EQ(glider_from_callsign("ZK-GAB"), "ZK-GAB");
}

TEST(GliderFromCallsign, IsEmptyForABlankCallsign) {
  EXPECT_EQ(glider_from_callsign(""), "");
  EXPECT_EQ(glider_from_callsign("        "), "");
}

}  // namespace esphome::tug_logger::testing
