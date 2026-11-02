#include <gtest/gtest.h>

#include "esphome/components/tug_logger/flight_detector.h"

namespace esphome::tug_logger::testing {

static constexpr time_t START = 1793000000;
static constexpr float KT = KNOTS_TO_MPS;
static constexpr float FT = METERS_PER_FOOT;

// Feeds one sample a second, on a clock kept between calls
class Flight {
 public:
  explicit Flight(const DetectorSettings &settings = {}) : detector(settings) {}

  FlightEvent step(float knots, float geometric_ft, float pressure_ft = -1.0f) {
    PositionSample sample;
    sample.now_ms = this->seconds * 1000;
    sample.epoch = this->clock_set ? START + this->seconds : 0;
    sample.speed_valid = true;
    sample.ground_speed_mps = knots * KT;
    sample.geometric_valid = geometric_ft >= 0;
    sample.geometric_m = geometric_ft * FT;
    sample.pressure_valid = pressure_ft >= 0;
    sample.pressure_m = pressure_ft * FT;
    this->seconds++;
    return this->detector.update(sample);
  }

  // Repeats a sample and returns the first event
  FlightEvent run(int count, float knots, float geometric_ft, float pressure_ft = -1.0f) {
    FlightEvent event = FlightEvent::FLIGHT_EVENT_NONE;
    for (int i = 0; i != count; i++) {
      const FlightEvent now = this->step(knots, geometric_ft, pressure_ft);
      if (event == FlightEvent::FLIGHT_EVENT_NONE)
        event = now;
    }
    return event;
  }

  FlightDetector detector;
  uint32_t seconds{1000};
  bool clock_set{true};
};

// On the ground at 1500 ft for 30 s, then a takeoff run, flight, and landing: launch at second 1030
static void normal_flight(Flight &flight) {
  ASSERT_EQ(flight.run(30, 0, 1500), FlightEvent::FLIGHT_EVENT_NONE);
  ASSERT_EQ(flight.run(10, 60, 1500), FlightEvent::FLIGHT_EVENT_TAKEOFF);
  for (int i = 1; i <= 200; i++)
    ASSERT_EQ(flight.step(70, 1500 + i * 10), FlightEvent::FLIGHT_EVENT_NONE);
  ASSERT_EQ(flight.run(300, 90, 3500), FlightEvent::FLIGHT_EVENT_NONE);
}

TEST(TugFlightDetector, MeasuresANormalFlight) {
  Flight flight;
  normal_flight(flight);
  EXPECT_TRUE(flight.detector.is_flying());
  ASSERT_EQ(flight.run(40, 5, 1500), FlightEvent::FLIGHT_EVENT_LANDED);
  EXPECT_FALSE(flight.detector.is_flying());
  const FlightSnapshot &result = flight.detector.flight();
  EXPECT_EQ(result.launch, START + 1030);
  // 10 s of takeoff run, 200 s of climb, 300 s of descent; landing is when it first slowed
  EXPECT_EQ(result.elapsed_s, 510u);
  EXPECT_EQ(result.max_height_ft, 2000);
  // The filter against bad fixes confirms the peak on its second sample
  EXPECT_EQ(result.climb_s, 210u);
  EXPECT_EQ(result.kind, AltitudeKind::ALTITUDE_KIND_GEOMETRIC);
  EXPECT_FALSE(result.approximate);
}

TEST(TugFlightDetector, TaxiingFastIsNotATakeoff) {
  Flight flight;
  flight.run(30, 0, 1500);
  // Above the threshold for less than the confirmation time, repeatedly
  for (int i = 0; i != 5; i++) {
    EXPECT_EQ(flight.run(4, 45, 1500), FlightEvent::FLIGHT_EVENT_NONE);
    EXPECT_EQ(flight.run(5, 20, 1500), FlightEvent::FLIGHT_EVENT_NONE);
  }
  EXPECT_FALSE(flight.detector.is_flying());
}

TEST(TugFlightDetector, SlowingBrieflyInFlightIsNotALanding) {
  Flight flight;
  normal_flight(flight);
  EXPECT_EQ(flight.run(29, 10, 2000), FlightEvent::FLIGHT_EVENT_NONE);
  EXPECT_EQ(flight.run(5, 70, 2000), FlightEvent::FLIGHT_EVENT_NONE);
  EXPECT_TRUE(flight.detector.is_flying());
}

TEST(TugFlightDetector, QuickRelaunchMeasuresEachFlightOnItsOwn) {
  Flight flight;
  normal_flight(flight);
  ASSERT_EQ(flight.run(40, 5, 1500), FlightEvent::FLIGHT_EVENT_LANDED);
  ASSERT_EQ(flight.run(10, 60, 1500), FlightEvent::FLIGHT_EVENT_TAKEOFF);
  for (int i = 1; i <= 100; i++)
    flight.step(70, 1500 + i * 10);
  flight.run(5, 70, 2500);
  ASSERT_EQ(flight.run(40, 5, 1500), FlightEvent::FLIGHT_EVENT_LANDED);
  EXPECT_EQ(flight.detector.flight().max_height_ft, 1000);
  EXPECT_EQ(flight.detector.flight().launch, START + 1580);
  EXPECT_FALSE(flight.detector.flight().approximate);
}

TEST(TugFlightDetector, MeasuresFromTheGroundNotTheFieldElevation) {
  DetectorSettings settings;
  settings.field_elevation_ft = 1500;
  Flight flight(settings);
  // GPS reads 130 ft high here, as from the geoid: the height must not include that
  flight.run(30, 0, 1630);
  flight.run(10, 60, 1630);
  flight.run(10, 70, 3630);
  EXPECT_EQ(flight.detector.flight().max_height_ft, 2000);
  EXPECT_FALSE(flight.detector.flight().approximate);
}

TEST(TugFlightDetector, UsesTheFieldElevationWithoutAGroundAltitude) {
  DetectorSettings settings;
  settings.field_elevation_ft = 1500;
  Flight flight(settings);
  // First data arrives in the air
  ASSERT_EQ(flight.run(10, 70, 2500), FlightEvent::FLIGHT_EVENT_TAKEOFF);
  flight.run(5, 70, 3000);
  EXPECT_EQ(flight.detector.flight().max_height_ft, 1500);
  EXPECT_TRUE(flight.detector.flight().approximate);
}

TEST(TugFlightDetector, StaleGroundAltitudeIsNotUsed) {
  DetectorSettings settings;
  settings.field_elevation_ft = 1500;
  Flight flight(settings);
  flight.run(30, 0, 9000);
  // Ground altitude is now over 5 minutes old
  flight.seconds += 400;
  ASSERT_EQ(flight.run(10, 60, 2000), FlightEvent::FLIGHT_EVENT_TAKEOFF);
  flight.run(5, 70, 2000);
  EXPECT_EQ(flight.detector.flight().max_height_ft, 500);
  EXPECT_TRUE(flight.detector.flight().approximate);
}

TEST(TugFlightDetector, NeverMixesAltitudeKinds) {
  Flight flight;
  // Geometric on the ground, then only pressure, reading far higher because of QNH
  flight.run(30, 0, 1500, 900);
  flight.run(10, 60, 1500, 900);
  flight.run(5, 70, 2500, 1900);
  EXPECT_EQ(flight.detector.flight().kind, AltitudeKind::ALTITUDE_KIND_GEOMETRIC);
  EXPECT_EQ(flight.detector.flight().max_height_ft, 1000);
  flight.run(20, 70, -1, 9000);
  EXPECT_EQ(flight.detector.flight().max_height_ft, 1000);
}

TEST(TugFlightDetector, FallsBackToPressureAltitude) {
  Flight flight;
  flight.run(30, 0, -1, 900);
  flight.run(10, 60, -1, 900);
  flight.run(5, 70, -1, 1900);
  EXPECT_EQ(flight.detector.flight().kind, AltitudeKind::ALTITUDE_KIND_PRESSURE);
  EXPECT_EQ(flight.detector.flight().max_height_ft, 1000);
  EXPECT_FALSE(flight.detector.flight().approximate);
}

TEST(TugFlightDetector, OneBadFixDoesNotSetThePeak) {
  Flight flight;
  flight.run(30, 0, 1500);
  flight.run(10, 60, 1500);
  flight.run(5, 70, 2000);
  flight.step(70, 30000);
  flight.run(5, 70, 2000);
  EXPECT_EQ(flight.detector.flight().max_height_ft, 500);
}

TEST(TugFlightDetector, ClockSetDuringFlightGivesTheLaunchTime) {
  Flight flight;
  flight.clock_set = false;
  flight.run(30, 0, 1500);
  ASSERT_EQ(flight.run(10, 60, 1500), FlightEvent::FLIGHT_EVENT_TAKEOFF);
  flight.run(100, 70, 2000);
  EXPECT_EQ(flight.detector.flight().launch, 0);
  flight.clock_set = true;
  flight.step(70, 2000);
  EXPECT_EQ(flight.detector.flight().launch, START + 1030);
}

TEST(TugFlightDetector, LongSilenceInFlightMakesItApproximate) {
  Flight flight;
  normal_flight(flight);
  EXPECT_FALSE(flight.detector.flight().approximate);
  flight.seconds += 120;
  EXPECT_EQ(flight.step(70, 2000), FlightEvent::FLIGHT_EVENT_NONE);
  EXPECT_TRUE(flight.detector.is_flying());
  EXPECT_TRUE(flight.detector.flight().approximate);
}

TEST(TugFlightDetector, NoLandingWithoutFreshData) {
  Flight flight;
  normal_flight(flight);
  // Invalid speed neither lands nor interrupts a slowdown in progress
  EXPECT_EQ(flight.run(10, 5, 1500), FlightEvent::FLIGHT_EVENT_NONE);
  PositionSample sample;
  sample.now_ms = flight.seconds * 1000;
  flight.seconds += 100;
  EXPECT_EQ(flight.detector.update(sample), FlightEvent::FLIGHT_EVENT_NONE);
  EXPECT_TRUE(flight.detector.is_flying());
  EXPECT_EQ(flight.step(5, 1500), FlightEvent::FLIGHT_EVENT_LANDED);
}

TEST(TugFlightDetector, ResumesAFlightAfterARestart) {
  FlightSnapshot snapshot;
  snapshot.launch = START;
  snapshot.elapsed_s = 300;
  snapshot.climb_s = 200;
  snapshot.max_height_ft = 2000;
  snapshot.kind = AltitudeKind::ALTITUDE_KIND_GEOMETRIC;
  snapshot.reference_m = 1500 * FT;
  snapshot.has_reference = true;
  Flight flight;
  flight.seconds = 10;
  flight.detector.resume(snapshot, 10000);
  flight.run(5, 70, 3000);
  EXPECT_EQ(flight.detector.flight().max_height_ft, 2000);
  flight.run(5, 70, 4000);
  EXPECT_EQ(flight.detector.flight().max_height_ft, 2500);
  ASSERT_EQ(flight.run(40, 5, 1500), FlightEvent::FLIGHT_EVENT_LANDED);
  EXPECT_EQ(flight.detector.flight().launch, START);
  EXPECT_EQ(flight.detector.flight().elapsed_s, 310u);
}

}  // namespace esphome::tug_logger::testing
