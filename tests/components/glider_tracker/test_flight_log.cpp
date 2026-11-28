#include <gtest/gtest.h>

#include "esphome/components/glider_tracker/flight_log.h"

namespace esphome::glider_tracker::testing {

static constexpr uint32_t START = 1800000000;

// Feeds one sample a second at a speed, on a clock kept between calls
class Flight {
 public:
  FlightLog log;
  uint32_t now{START};
  FlightLog::Event last{FlightLog::Event::NONE};

  // Returns the first event in the run, if any
  FlightLog::Event run(float speed_kmh, uint32_t seconds) {
    FlightLog::Event first = FlightLog::Event::NONE;
    for (uint32_t i = 0; i != seconds; i++) {
      Sample sample{now++, true, -28.2 + now * 1e-6, 152.03, 450, true, speed_kmh};
      FlightLog::Event event = this->log.update(sample);
      if (event != FlightLog::Event::NONE && first == FlightLog::Event::NONE)
        first = event;
    }
    return first;
  }
};

TEST(GliderFlightLog, TakeoffAfterFiveSecondsFastFromWhereTheRunBegan) {
  Flight f;
  EXPECT_EQ(f.run(20, 10), FlightLog::Event::NONE);
  EXPECT_EQ(f.run(60, 4), FlightLog::Event::NONE);
  EXPECT_FALSE(f.log.is_flying());
  // A slow second starts the count again
  f.run(20, 1);
  const uint32_t run_start = f.now;
  EXPECT_EQ(f.run(60, 5), FlightLog::Event::TAKEOFF);
  ASSERT_TRUE(f.log.is_flying());
  Summary flight;
  ASSERT_TRUE(f.log.get_flight(&flight));
  EXPECT_EQ(flight.takeoff, run_start);
  EXPECT_EQ(flight.landing, 0u);
  Takeoff report;
  ASSERT_TRUE(f.log.get_takeoff_report(&report));
  EXPECT_EQ(report.takeoff, run_start);
  EXPECT_TRUE(f.log.take_changed());
}

TEST(GliderFlightLog, LandsAfterThirtySecondsSlowUnlessMovingAgain) {
  Flight f;
  f.run(80, 5);
  f.run(90, 600);
  // Slows, then speeds up again: a touch-and-go
  f.run(5, 20);
  f.run(60, 10);
  EXPECT_TRUE(f.log.is_flying());
  const uint32_t landing = f.now;
  // Rolling out between the two limits doesn't count as moving again
  EXPECT_EQ(f.run(8, 1), FlightLog::Event::NONE);
  EXPECT_EQ(f.run(20, 10), FlightLog::Event::NONE);
  EXPECT_EQ(f.run(3, 25), FlightLog::Event::LANDED);
  EXPECT_FALSE(f.log.is_flying());
  ASSERT_EQ(f.log.queued(), 1u);
  Summary summary;
  ASSERT_TRUE(summary.decode(f.log.next_summary(), SLOT));
  EXPECT_EQ(summary.takeoff, START);
  EXPECT_EQ(summary.landing, landing);
  Takeoff report;
  EXPECT_FALSE(f.log.get_takeoff_report(&report));
}

TEST(GliderFlightLog, AcknowledgementsAreMatchedByTypeAndTime) {
  Flight f;
  f.run(80, 5);
  Ack summary_ack{START, ACK};
  // A summary's acknowledgement doesn't answer the takeoff report
  EXPECT_FALSE(f.log.acknowledge(summary_ack));
  Ack takeoff_ack{START, TAKEOFF_ACK};
  EXPECT_TRUE(f.log.acknowledge(takeoff_ack));
  Takeoff report;
  EXPECT_FALSE(f.log.get_takeoff_report(&report));
  f.run(2, 40);
  ASSERT_EQ(f.log.queued(), 1u);
  EXPECT_FALSE(f.log.acknowledge(Ack{START + 1, ACK}));
  EXPECT_TRUE(f.log.acknowledge(summary_ack));
  EXPECT_EQ(f.log.queued(), 0u);
  EXPECT_EQ(f.log.next_summary(), nullptr);
}

TEST(GliderFlightLog, AwaitsAckForTheTakeoffAndTheSummary) {
  Flight f;
  EXPECT_FALSE(f.log.awaiting_ack());
  f.run(80, 5);
  EXPECT_TRUE(f.log.awaiting_ack());
  f.log.acknowledge(Ack{START, TAKEOFF_ACK});
  EXPECT_FALSE(f.log.awaiting_ack());
  f.run(2, 40);
  EXPECT_TRUE(f.log.awaiting_ack());
  f.log.acknowledge(Ack{START, ACK});
  EXPECT_FALSE(f.log.awaiting_ack());
}

TEST(GliderFlightLog, QueueKeepsEightAndDropsTheOldest) {
  FlightLog log;
  Sample sample{};
  for (uint32_t i = 0; i != 9; i++) {
    sample.now = START + i * 10000;
    log.fake_flight(sample, 0);
  }
  EXPECT_EQ(log.queued(), QUEUE_SLOTS);
  Summary oldest;
  ASSERT_TRUE(oldest.decode(log.next_summary(), SLOT));
  EXPECT_EQ(oldest.landing, START + 10000);
  // The same flight again replaces its summary
  log.fake_flight(sample, 0);
  EXPECT_EQ(log.queued(), QUEUE_SLOTS);
}

TEST(GliderFlightLog, FlightInProgressSurvivesARestart) {
  Flight f;
  f.run(80, 5);
  f.log.acknowledge(Ack{START, TAKEOFF_ACK});
  FlightStore saved = f.log.store();
  FlightLog restarted;
  restarted.load(saved);
  EXPECT_TRUE(restarted.is_flying());
  // The takeoff is reported again, which the Worker takes as the same flight
  Takeoff report;
  ASSERT_TRUE(restarted.get_takeoff_report(&report));
  EXPECT_EQ(report.takeoff, START);
  // A blank store starts empty, and is saved
  FlightStore blank{};
  FlightLog fresh;
  fresh.load(blank);
  EXPECT_FALSE(fresh.is_flying());
  EXPECT_TRUE(fresh.take_changed());
}

TEST(GliderFlightLog, FakeFlightLandsOnlyByHand) {
  FlightLog log;
  Sample sample{START, true, -28.2, 152.03, 450, true, 0};
  EXPECT_EQ(log.fake_takeoff_or_landing(sample, 0), FlightLog::Event::TAKEOFF);
  for (uint32_t i = 0; i != 60; i++) {
    sample.now++;
    EXPECT_EQ(log.update(sample), FlightLog::Event::NONE);
  }
  EXPECT_TRUE(log.is_flying());
  EXPECT_EQ(log.fake_takeoff_or_landing(sample, 0), FlightLog::Event::LANDED);
  EXPECT_EQ(log.queued(), 1u);
}

TEST(GliderFlightLog, NoDetectionWithoutAClockOrFix) {
  FlightLog log;
  for (uint32_t i = 0; i != 10; i++) {
    EXPECT_EQ(log.update(Sample{0, true, 0, 0, 0, true, 80}), FlightLog::Event::NONE);
    EXPECT_EQ(log.update(Sample{START + i, false, 0, 0, 0, true, 80}), FlightLog::Event::NONE);
  }
  EXPECT_FALSE(log.is_flying());
}

}  // namespace esphome::glider_tracker::testing
