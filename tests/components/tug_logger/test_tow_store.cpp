#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <unistd.h>

#include "esphome/components/json/json_util.h"
#include "esphome/components/tug_logger/position_packet.h"
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
    record.field_elevation_ft = 1500;
    record.details.glider_registration = glider;
    record.details.glider_pilot_1 = "Alex";
    record.details.tug_pilot = "Sam";
    return record;
  }

  std::string root;
  std::unique_ptr<TowStore> store;
};

TEST(TugTowRecord, UploadBodyMarksAnApproximateHeight) {
  TowRecord record;
  record.launch = LAUNCH;
  record.approximate = true;
  record.details = {"WGQ", "Alex", "", "Sam"};
  bool checked = false;
  ASSERT_TRUE(json::parse_json(tow_to_upload_json(record, "VH-TUG"), [&](JsonObject root) -> bool {
    EXPECT_TRUE(root["height_approximate"].as<bool>());
    checked = true;
    return true;
  }));
  EXPECT_TRUE(checked);
}

TEST(TugTowRecord, UploadBodyHasTheFieldsTheServerRequires) {
  TowRecord record;
  record.launch = LAUNCH;
  record.duration_s = 840;
  record.climb_s = 420;
  record.max_height_ft = 2000;
  record.field_elevation_ft = 1500;
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
    EXPECT_EQ(root["max_altitude_ft"].as<int>(), 3500);
    EXPECT_EQ(root["climb_seconds"].as<int>(), 420);
    EXPECT_EQ(root["duration"].as<int>(), 840);
    EXPECT_FALSE(root["height_approximate"].as<bool>());
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
  // A wrong or moved address, not a bad tow
  EXPECT_EQ(classify_status(404), UploadResult::UPLOAD_RESULT_RETRY);
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
  // A file made before the dashboard gave LoRa keys has none
  EXPECT_EQ(settings.lora_address, 0);
  EXPECT_TRUE(settings.lora_key.empty());
}

TEST(TugTowRecord, ReadsTheAirfieldsInTheFile) {
  Provisioning settings;
  ASSERT_TRUE(parse_provisioning(
      R"({"tug":{"registration":"VH-TUG"},"field_elevation_ft":1500,"flight_upload_url":"u","flight_upload_key":"k",)"
      R"("airfields":[{"name":"Home","lat":-28.2,"lon":152.03,"elevation_ft":1500},)"
      R"({"name":"Away","lat":-27.4,"lon":151.7,"elevation_ft":1200},{"name":"Broken"}]})",
      settings));
  ASSERT_EQ(settings.airfields.size(), 2u);
  EXPECT_EQ(settings.airfields[1].name, "Away");
  EXPECT_NEAR(settings.airfields[1].lat, -27.4, 1e-5);
  EXPECT_FLOAT_EQ(settings.airfields[1].elevation_ft, 1200.0f);
}

TEST(TugTowRecord, ReadsTheWorkersAirfields) {
  Provisioning settings;
  settings.field_elevation_ft = 100;
  ASSERT_TRUE(parse_airfields(
      R"({"field_elevation_ft":1500,"airfields":[{"name":"Home","lat":-28.2,"lon":152.03,"elevation_ft":1500}]})",
      settings));
  EXPECT_FLOAT_EQ(settings.field_elevation_ft, 1500.0f);
  ASSERT_EQ(settings.airfields.size(), 1u);
  EXPECT_FALSE(parse_airfields(R"({"error":"invalid_signature"})", settings));
  EXPECT_EQ(settings.airfields.size(), 1u);
}

TEST(TugAirfields, TakesTheNearestWithinThreeKilometres) {
  const std::vector<Airfield> airfields{{"Home", -28.2, 152.03, 1500}, {"Near", -28.21, 152.03, 1400}};
  // About 330 m from Home and 780 m from Near
  const Airfield *at = nearest_airfield(airfields, -28.203, 152.03);
  ASSERT_NE(at, nullptr);
  EXPECT_EQ(at->name, "Home");
  // About 1.1 km east of Near
  at = nearest_airfield(airfields, -28.21, 152.041);
  ASSERT_NE(at, nullptr);
  EXPECT_EQ(at->name, "Near");
  // About 5.5 km away from both
  EXPECT_EQ(nearest_airfield(airfields, -28.25, 152.03), nullptr);
  EXPECT_EQ(nearest_airfield({}, -28.2, 152.03), nullptr);
}

TEST_F(TowStoreTest, KeepsTheFetchedAirfields) {
  Provisioning settings;
  EXPECT_FALSE(this->store->load_airfields(settings));
  ASSERT_TRUE(this->store->save_airfields(
      R"({"field_elevation_ft":1500,"airfields":[{"name":"Home","lat":-28.2,"lon":152.03,"elevation_ft":1500}]})"));
  this->reopen();
  ASSERT_TRUE(this->store->load_airfields(settings));
  EXPECT_EQ(settings.airfields.size(), 1u);
}

TEST_F(TowStoreTest, AnOlderRecordTakesTheHomeFieldElevation) {
  write_all(this->file("review.jsonl"), R"({"id":1,"launch":1793000000,"height":2000,"glider":"WGQ"})"
                                        "\n");
  this->reopen();
  this->store->set_default_field_elevation_ft(1500);
  ASSERT_EQ(this->store->review_list().size(), 1u);
  EXPECT_FLOAT_EQ(this->store->review_list()[0].field_elevation_ft, 1500.0f);
}

TEST(TugTowRecord, ReadsTheLoraAddressAndKey) {
  Provisioning settings;
  ASSERT_TRUE(parse_provisioning(
      R"({"tug":{"registration":"VH-TUG"},"flight_upload_url":"https://x/api/tows","flight_upload_key":"k",)"
      R"("lora":{"address":16,"key":"000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f"}})",
      settings));
  EXPECT_EQ(settings.lora_address, 0x0010);
  EXPECT_EQ(settings.lora_key, "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f");
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
  EXPECT_FLOAT_EQ(list[0].field_elevation_ft, 1500.0f);
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

TEST_F(TowStoreTest, ATowGoesUnderTheTugItFlewWith) {
  TowRecord changed = tow("WGQ");
  changed.details.tug_registration = "VH-ABC";
  ASSERT_TRUE(this->store->add_review(changed));
  ASSERT_TRUE(this->store->add_review(tow("GBK")));
  this->reopen();
  auto list = this->store->review_list();
  ASSERT_EQ(list[0].details.tug_registration, "VH-ABC");
  EXPECT_EQ(this->store->confirm(list[0].id, "VH-TUG"), ConfirmResult::CONFIRM_RESULT_OK);
  // One from before the tug could be changed goes under the tug file's
  EXPECT_EQ(this->store->confirm(list[1].id, "VH-TUG"), ConfirmResult::CONFIRM_RESULT_OK);
  const std::string queued = read_all(this->file("tows-pending.jsonl"));
  EXPECT_NE(queued.find("\"tug_registration\":\"VH-ABC\",\"glider_registration\":\"WGQ\""), std::string::npos);
  EXPECT_NE(queued.find("\"tug_registration\":\"VH-TUG\",\"glider_registration\":\"GBK\""), std::string::npos);
}

TEST_F(TowStoreTest, AFlightInProgressKeepsItsTug) {
  FlightSnapshot snapshot;
  snapshot.elapsed_s = 60;
  TowDetails details{"WGQ", "Alex", "", "Sam"};
  details.tug_registration = "VH-ABC";
  ASSERT_TRUE(this->store->save_flight(snapshot, details, LAUNCH));
  this->reopen();
  TowDetails loaded;
  time_t saved_at;
  ASSERT_TRUE(this->store->load_flight(snapshot, loaded, saved_at));
  EXPECT_EQ(loaded.tug_registration, "VH-ABC");
}

TEST_F(TowStoreTest, RemembersTugsUsed) {
  TowDetails details;
  details.tug_registration = "VH-ABC";
  this->store->remember(details);
  details.tug_registration = "VH-XYZ";
  this->store->remember(details);
  this->reopen();
  EXPECT_EQ(this->store->recent_tugs(), (std::vector<std::string>{"VH-XYZ", "VH-ABC"}));
  this->store->forget_name(TowStore::NameList::NAME_LIST_TUGS, "vh-xyz");
  EXPECT_EQ(this->store->recent_tugs(), (std::vector<std::string>{"VH-ABC"}));
  EXPECT_TRUE(this->store->recent_pilots().empty());
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
  snapshot.pressure_source = 2;
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
  EXPECT_EQ(loaded.pressure_source, 2);
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

TEST(TowTotals, ANewDayGoesBackToTheTugFilesTug) {
  TowTotals totals;
  totals.roll_to(20261102);
  totals.tug_registration = "VH-ABC";
  EXPECT_FALSE(totals.roll_to(20261102));
  EXPECT_EQ(totals.tug_registration, "VH-ABC");
  EXPECT_TRUE(totals.roll_to(20261103));
  EXPECT_TRUE(totals.tug_registration.empty());
}

TEST(TowTotals, ATugChosenBeforeTheClockIsSetIsKept) {
  TowTotals totals;
  totals.tug_registration = "VH-ABC";
  EXPECT_TRUE(totals.roll_to(20261102));
  EXPECT_EQ(totals.tug_registration, "VH-ABC");
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
  totals.tug_registration = "VH-ABC";
  ASSERT_TRUE(this->store->save_totals(totals));
  this->reopen();
  const TowTotals loaded = this->store->load_totals();
  EXPECT_EQ(loaded.tug_registration, "VH-ABC");
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

TEST_F(TowStoreTest, KeepsAFlightRecordPerFlight) {
  ASSERT_TRUE(this->store->start_flight_log(LAUNCH, 5000));
  ASSERT_TRUE(this->store->append_flight_log("header\n"));
  ASSERT_TRUE(this->store->append_flight_log("line 1\n"));
  EXPECT_EQ(read_all(this->file("flights/20261101-085901.csv")), "header\nline 1\n");
  // Before the clock is set, named by the uptime
  ASSERT_TRUE(this->store->start_flight_log(0, 123456));
  ASSERT_TRUE(this->store->append_flight_log("line\n"));
  EXPECT_EQ(read_all(this->file("flights/no-clock-123456.csv")), "line\n");
}

TEST_F(TowStoreTest, SendsEachFinishedFlightRecordOnceUnlessItGrows) {
  ASSERT_TRUE(this->store->start_flight_log(LAUNCH, 5000));
  ASSERT_TRUE(this->store->append_flight_log("header\n"));
  // The record being written is not sent
  EXPECT_TRUE(this->store->unsent_flight_logs().empty());
  this->store->end_flight_log();
  ASSERT_TRUE(this->store->start_flight_log(0, 123456));
  ASSERT_TRUE(this->store->append_flight_log("line\n"));
  this->store->end_flight_log();

  auto unsent = this->store->unsent_flight_logs();
  ASSERT_EQ(unsent.size(), 2u);
  EXPECT_EQ(unsent[0].name, "20261101-085901");
  EXPECT_EQ(unsent[0].size, 7u);
  EXPECT_EQ(unsent[1].name, "no-clock-123456");
  std::string csv;
  ASSERT_TRUE(this->store->read_flight_log(unsent[0].name, csv, 1024));
  EXPECT_EQ(csv, "header\n");

  ASSERT_TRUE(this->store->mark_flight_log_sent(unsent[0]));
  ASSERT_TRUE(this->store->mark_flight_log_sent(unsent[1]));
  this->reopen();
  EXPECT_TRUE(this->store->unsent_flight_logs().empty());

  // Carried on after a restart in flight: the same file grows, and goes again
  ASSERT_TRUE(this->store->start_flight_log(LAUNCH, 9000));
  ASSERT_TRUE(this->store->append_flight_log("line 2\n"));
  this->store->end_flight_log();
  unsent = this->store->unsent_flight_logs();
  ASSERT_EQ(unsent.size(), 1u);
  EXPECT_EQ(unsent[0].name, "20261101-085901");
  EXPECT_EQ(unsent[0].size, 14u);
}

TEST_F(TowStoreTest, SendsRefusedTowsAgain) {
  write_all(this->file("tows-failed.jsonl"), "{\"a\":1}\n{\"b\":2}\n");
  EXPECT_EQ(this->store->failed_count(), 2u);
  EXPECT_EQ(this->store->resend_failed(), 2u);
  EXPECT_EQ(this->store->failed_count(), 0u);
  EXPECT_EQ(this->store->queued_count(), 2u);
  std::vector<std::string> sent;
  this->store->process_queue([&sent](const std::string &body) {
    sent.push_back(body);
    return UploadResult::UPLOAD_RESULT_SENT;
  });
  EXPECT_EQ(sent, (std::vector<std::string>{"{\"a\":1}", "{\"b\":2}"}));
  EXPECT_EQ(this->store->resend_failed(), 0u);
}

TEST(FlightLogBody, EscapesTheRecordAsJson) {
  EXPECT_EQ(flight_log_body("20261101-085901", "# a \"b\" c\\d\n1,2\r\n"),
            "{\"name\":\"20261101-085901\",\"csv\":\"# a \\\"b\\\" c\\\\d\\n1,2 \\n\"}");
  // It parses, and gives the text back
  std::string csv;
  ASSERT_TRUE(json::parse_json(flight_log_body("x", "a\"\\\nb\n"), [&csv](JsonObject root) {
    csv = root["csv"].as<std::string>();
    return true;
  }));
  EXPECT_EQ(csv, "a\"\\\nb\n");
}

TEST_F(TowStoreTest, ForgetsANameAndKeepsItForgotten) {
  TowDetails details{"WGQ", "Dan", "Dan P", "Sam"};
  this->store->remember(details);
  this->store->forget_name(TowStore::NameList::NAME_LIST_PILOTS, "dan");
  this->store->forget_name(TowStore::NameList::NAME_LIST_GLIDERS, "WGQ");
  this->reopen();
  EXPECT_EQ(this->store->recent_pilots(), (std::vector<std::string>{"Sam", "Dan P"}));
  EXPECT_TRUE(this->store->recent_gliders().empty());
}

TEST_F(TowStoreTest, ThePilotsReleaseHeightReplacesTheMeasuredOne) {
  TowRecord record = tow();
  record.approximate = true;
  ASSERT_TRUE(this->store->add_review(record));
  const uint32_t id = this->store->review_list().front().id;
  ASSERT_TRUE(this->store->update_height(id, 1900));
  this->reopen();
  const TowRecord stored = this->store->review_list().front();
  EXPECT_EQ(stored.max_height_ft, 1900);
  EXPECT_FALSE(stored.approximate);
  EXPECT_TRUE(stored.height_entered);
  EXPECT_FALSE(this->store->update_height(id + 100, 1000));
}

TEST(TugNameReport, EncodesTheRegistration) {
  NameReport name;
  name.registration = "VH-ABC";
  uint8_t p[NameReport::MAX_SIZE];
  ASSERT_EQ(name.encode(p), 8u);
  EXPECT_EQ(p[0], 'N');
  EXPECT_EQ(p[1], 6);
  EXPECT_EQ(std::string(reinterpret_cast<const char *>(p + 2), 6), "VH-ABC");
  name.registration = "";
  EXPECT_EQ(name.encode(p), 0u);
  name.registration = std::string(16, 'A');
  EXPECT_EQ(name.encode(p), 0u);
}

// The same bytes as a glider tracker's position (glider_packets.h), which the receiver decodes for both
TEST(TugPositionReport, EncodesAsAGliderTrackerDoes) {
  PositionReport report;
  report.fix = true;
  report.flying = true;
  report.time = 0x12345678;
  report.lat = -31.4210270;
  report.lon = 152.7573110;
  report.alt_m = 457;
  report.speed_kmh = 120.4f;
  report.course_deg = 271.3f;
  report.climb_cms = -150;
  report.satellites = 11;
  uint8_t p[PositionReport::SIZE];
  ASSERT_EQ(report.encode(p), 24u);
  auto get16 = [&](size_t at) { return uint16_t(p[at] | (p[at + 1] << 8)); };
  auto get32 = [&](size_t at) {
    return uint32_t(p[at]) | (uint32_t(p[at + 1]) << 8) | (uint32_t(p[at + 2]) << 16) | (uint32_t(p[at + 3]) << 24);
  };
  EXPECT_EQ(p[0], 'P');
  EXPECT_EQ(p[1], 3);
  EXPECT_EQ(get32(2), 0x12345678u);
  EXPECT_EQ(int32_t(get32(6)), -314210270);
  EXPECT_EQ(int32_t(get32(10)), 1527573110);
  EXPECT_EQ(int16_t(get16(14)), 457);
  EXPECT_EQ(get16(16), 1204);
  EXPECT_EQ(get16(18), 2713);
  EXPECT_EQ(int16_t(get16(20)), -150);
  EXPECT_EQ(p[22], 11);
  // No battery reading
  EXPECT_EQ(p[23], 0);
  report.battery_v = 3.92f;
  report.encode(p);
  EXPECT_EQ(p[23], 196);
}

}  // namespace esphome::tug_logger::testing
