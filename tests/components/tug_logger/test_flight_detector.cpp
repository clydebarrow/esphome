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
    sample.geometric_source = this->source;
    sample.pressure_valid = pressure_ft >= 0;
    sample.pressure_m = pressure_ft * FT;
    sample.pressure_source = this->pressure_source;
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
  // The receiver the GPS altitude is from
  uint8_t source{2};
  // The barometer the pressure altitude is from
  uint8_t pressure_source{2};
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
  // Under the landing speed for just less than the confirmation time
  EXPECT_EQ(flight.run(9, 25, 2000), FlightEvent::FLIGHT_EVENT_NONE);
  EXPECT_EQ(flight.run(5, 70, 2000), FlightEvent::FLIGHT_EVENT_NONE);
  EXPECT_TRUE(flight.detector.is_flying());
}

TEST(TugFlightDetector, LandsTenSecondsAfterSlowingBelowThirtyKnots) {
  Flight flight;
  normal_flight(flight);
  // A landing roll as DGS logged it: touchdown at 55 kt, slowing 2 to 3 kt a second
  for (float knots = 55; knots >= 31; knots -= 3)
    ASSERT_EQ(flight.step(knots, 1500), FlightEvent::FLIGHT_EVENT_NONE);
  const uint32_t slowed = flight.seconds;
  ASSERT_EQ(flight.run(10, 20, 1500), FlightEvent::FLIGHT_EVENT_NONE);
  ASSERT_EQ(flight.step(15, 1500), FlightEvent::FLIGHT_EVENT_LANDED);
  // The flight ends when it first slowed below 30 kt, not when that was confirmed
  EXPECT_EQ(flight.detector.flight().launch + flight.detector.flight().elapsed_s, START + slowed);
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

TEST(TugFlightDetector, PrefersPressureAltitude) {
  Flight flight;
  // As the relay's GNSS logged a tow: GPS reads the climb short, pressure altitude matches the altimeter
  for (int i = 0; i != 30; i++)
    flight.step(0, i % 2 ? 1800 : 1500, 1275);
  flight.run(10, 60, 1650, 1275);
  flight.run(5, 70, 2600, 2875);
  EXPECT_EQ(flight.detector.flight().kind, AltitudeKind::ALTITUDE_KIND_PRESSURE);
  EXPECT_EQ(flight.detector.flight().max_height_ft, 1600);
  EXPECT_FALSE(flight.detector.flight().approximate);
}

TEST(TugFlightDetector, ShortPressureGapWaitsForTheNextOne) {
  Flight flight;
  flight.run(30, 0, 1500, 900);
  flight.run(10, 60, 1500, 900);
  flight.run(5, 70, 2500, 1900);
  EXPECT_EQ(flight.detector.flight().kind, AltitudeKind::ALTITUDE_KIND_PRESSURE);
  EXPECT_EQ(flight.detector.flight().max_height_ft, 1000);
  // Under 10 s without it, GPS altitude is not used
  flight.run(9, 70, 9000, -1);
  EXPECT_EQ(flight.detector.flight().max_height_ft, 1000);
  flight.run(5, 70, 2500, 1900);
  EXPECT_EQ(flight.detector.flight().max_height_ft, 1000);
  EXPECT_FALSE(flight.detector.flight().approximate);
}

TEST(TugFlightDetector, LostPressureAltitudeCarriesOnWithTheGpsClimb) {
  Flight flight;
  // GPS reads the climb short and well apart from pressure altitude: only its change since the last pressure counts
  flight.run(30, 0, 1500, 900);
  flight.run(10, 60, 1500, 900);
  flight.run(5, 70, 2300, 1900);
  ASSERT_EQ(flight.detector.flight().max_height_ft, 1000);
  for (int i = 1; i <= 30; i++)
    flight.step(70, 2300 + i * 10, -1);
  flight.run(3, 70, 2600, -1);
  EXPECT_EQ(flight.detector.flight().max_height_ft, 1300);
  // Launched at second 1030, and the GPS climb went on to second 1074
  EXPECT_EQ(flight.detector.flight().climb_s, 45u);
  EXPECT_TRUE(flight.detector.flight().approximate);
  // When it comes back it is used again, and the GPS height does not stick
  flight.run(5, 70, 2600, 2000);
  EXPECT_EQ(flight.detector.current_height_ft(), 1100);
  EXPECT_EQ(flight.detector.flight().max_height_ft, 1300);
}

TEST(TugFlightDetector, PressureLostAtTakeoffUsesTheGroundGpsAltitude) {
  Flight flight;
  // As DGS logged a tow: pressure altitude on the ground, then none in the climb
  flight.run(30, 0, 1640, 1275);
  flight.run(10, 60, 1640, 1275);
  for (int i = 1; i <= 150; i++)
    flight.step(65, 1640 + i * 12, -1);
  flight.run(3, 65, 3440, -1);
  EXPECT_EQ(flight.detector.flight().kind, AltitudeKind::ALTITUDE_KIND_PRESSURE);
  EXPECT_EQ(flight.detector.flight().max_height_ft, 1800);
  EXPECT_TRUE(flight.detector.flight().approximate);
}

TEST(TugFlightDetector, LostPressureAltitudeIgnoresAnotherGpsReceiver) {
  Flight flight;
  flight.run(30, 0, 1500, 900);
  flight.run(10, 60, 1500, 900);
  flight.run(5, 70, 2500, 1900);
  flight.source = 1;
  flight.run(20, 70, 9000, -1);
  EXPECT_EQ(flight.detector.flight().max_height_ft, 1000);
  EXPECT_FALSE(flight.detector.flight().approximate);
}

TEST(TugFlightDetector, KeepsToOneBarometer) {
  Flight flight;
  flight.run(30, 0, 1500, 900);
  flight.run(10, 60, 1500, 900);
  flight.run(5, 70, 2500, 1900);
  EXPECT_EQ(flight.detector.flight().pressure_source, 2);
  ASSERT_EQ(flight.detector.flight().max_height_ft, 1000);
  // The other barometer reads higher: it must not set a new highest, and a short spell of it is a gap
  flight.pressure_source = 1;
  flight.run(5, 70, 2500, 2400);
  EXPECT_EQ(flight.detector.flight().max_height_ft, 1000);
  flight.pressure_source = 2;
  flight.run(5, 70, 2300, 1700);
  EXPECT_EQ(flight.detector.current_height_ft(), 800);
  EXPECT_FALSE(flight.detector.flight().approximate);
}

TEST(TugFlightDetector, GroundReferenceIsFromOneBarometer) {
  Flight flight;
  // Readings from the other barometer before the last one are not part of the mean
  flight.pressure_source = 1;
  flight.run(20, 0, 1500, 1200);
  flight.pressure_source = 2;
  flight.run(10, 0, 1500, 900);
  flight.run(10, 60, 1500, 900);
  flight.run(5, 70, 2500, 1900);
  EXPECT_EQ(flight.detector.flight().pressure_source, 2);
  EXPECT_EQ(flight.detector.flight().max_height_ft, 1000);
}

TEST(TugFlightDetector, PressureAltitudeIsTheStandardAtmosphere) {
  EXPECT_NEAR(pressure_altitude_m(1013.25f), 0.0f, 0.1f);
  EXPECT_NEAR(pressure_altitude_m(900.0f), 988.6f, 1.0f);
  // About 27 ft for each hPa near sea level
  EXPECT_NEAR((pressure_altitude_m(1012.25f) - pressure_altitude_m(1013.25f)) / FT, 27.4f, 0.5f);
}

TEST(TugFlightDetector, FallsBackToGpsAltitude) {
  Flight flight;
  flight.run(30, 0, 1500);
  flight.run(10, 60, 1500);
  flight.run(5, 70, 2500);
  EXPECT_EQ(flight.detector.flight().kind, AltitudeKind::ALTITUDE_KIND_GEOMETRIC);
  EXPECT_EQ(flight.detector.flight().max_height_ft, 1000);
  EXPECT_FALSE(flight.detector.flight().approximate);
}

TEST(TugFlightDetector, KeepsToOneGpsReceiver) {
  Flight flight;
  flight.run(30, 0, 1680);
  flight.run(10, 60, 1680);
  flight.run(5, 70, 3600);
  EXPECT_EQ(flight.detector.flight().max_height_ft, 1920);
  // The other receiver reads 320 ft higher: switching to it in the descent must not set a new highest
  flight.source = 1;
  flight.run(20, 100, 3755);
  EXPECT_EQ(flight.detector.flight().max_height_ft, 1920);
  flight.source = 2;
  flight.run(5, 100, 3000);
  EXPECT_EQ(flight.detector.current_height_ft(), 1320);
}

TEST(TugFlightDetector, GroundReferenceIsFromOneGpsReceiver) {
  Flight flight;
  // Readings from another receiver before the last one are not part of the mean
  flight.source = 1;
  flight.run(20, 0, 2000);
  flight.source = 2;
  flight.run(10, 0, 1600);
  flight.run(10, 60, 1600);
  flight.run(5, 70, 3600);
  EXPECT_EQ(flight.detector.flight().max_height_ft, 2000);
}

TEST(TugFlightDetector, GivesAltitudeAboveSeaLevel) {
  DetectorSettings settings;
  settings.field_elevation_ft = 1500;
  Flight flight(settings);
  // Pressure altitude on the ground is 225 ft below the field, as with a QNH of about 1021 hPa
  flight.run(30, 0, -1, 1275);
  flight.run(10, 60, -1, 1275);
  flight.run(5, 70, -1, 2875);
  EXPECT_EQ(flight.detector.current_altitude_ft(), 3100);
  EXPECT_EQ(flight.detector.to_altitude_ft(flight.detector.flight().max_height_ft), 3100);
}

TEST(TugFlightDetector, TakesTheElevationOfTheAirfieldItLaunchesFrom) {
  DetectorSettings settings;
  settings.field_elevation_ft = 1500;
  Flight flight(settings);
  flight.detector.set_launch_field(LaunchField::LAUNCH_FIELD_KNOWN, 400);
  flight.run(30, 0, 520, 300);
  flight.run(10, 60, 520, 300);
  flight.run(5, 70, 2500, 2300);
  EXPECT_EQ(flight.detector.flight().max_height_ft, 2000);
  EXPECT_EQ(flight.detector.current_altitude_ft(), 2400);
  EXPECT_FALSE(flight.detector.flight().approximate);
}

TEST(TugFlightDetector, AwayFromTheAirfieldsUsesGpsOnTheGround) {
  DetectorSettings settings;
  settings.field_elevation_ft = 1500;
  Flight flight(settings);
  flight.detector.set_launch_field(LaunchField::LAUNCH_FIELD_UNKNOWN);
  flight.run(30, 0, 820, 600);
  flight.run(10, 60, 820, 600);
  flight.run(5, 70, 2800, 2600);
  EXPECT_EQ(flight.detector.flight().max_height_ft, 2000);
  EXPECT_EQ(flight.detector.current_altitude_ft(), 2820);
  EXPECT_TRUE(flight.detector.flight().approximate);
}

TEST(TugFlightDetector, ReferenceIsTheMeanOfTheLast30Seconds) {
  Flight flight;
  // An old reading, outside the window, then readings that wander either side of 1600 ft
  flight.run(20, 0, 3000);
  for (int i = 0; i != 30; i++)
    flight.step(0, i % 2 ? 1700 : 1500);
  flight.run(10, 60, 1600);
  flight.run(5, 70, 3600);
  EXPECT_EQ(flight.detector.flight().max_height_ft, 2000);
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
