#include <gtest/gtest.h>

#include "esphome/components/flarm_receiver/gnss_sentences.h"

namespace esphome::flarm_receiver::testing {

static gps::GPSData fix() {
  gps::GPSData data;
  data.latitude_e7 = -314123456;   // 31 deg 24.74074 min S
  data.longitude_e7 = 1527654321;  // 152 deg 45.92593 min E
  data.altitude_e3 = 45300;
  data.speed_e3 = 12345;
  data.course_e2 = 27050;
  data.hdop_e2 = 66;
  data.satellites = 8;
  data.year = 2026;
  data.month = 11;
  data.day = 28;
  data.hour = 3;
  data.minute = 48;
  data.second = 39;
  data.valid = 0xFFFF & ~gps::GPS_FIELD_GEOID_SEPARATION;
  return data;
}

TEST(GnssSentences, Rmc) {
  gps::GPSData data = fix();
  data.updated = gps::GPS_FIELD_LOCATION | gps::GPS_FIELD_SPEED | gps::GPS_FIELD_COURSE | gps::GPS_FIELD_TIME |
                 gps::GPS_FIELD_DATE;
  char body[100];
  ASSERT_TRUE(rmc_body(data, body, sizeof(body)));
  EXPECT_STREQ(body, "GPRMC,034839.00,A,3124.74074,S,15245.92593,E,12.34,270.50,281126,,,A");
  EXPECT_FALSE(gga_body(data, body, sizeof(body)));
}

TEST(GnssSentences, GgaFix) {
  gps::GPSData data = fix();
  data.updated = gps::GPS_FIELD_LOCATION | gps::GPS_FIELD_ALTITUDE | gps::GPS_FIELD_SATELLITES | gps::GPS_FIELD_HDOP |
                 gps::GPS_FIELD_TIME;
  char body[100];
  ASSERT_TRUE(gga_body(data, body, sizeof(body)));
  EXPECT_STREQ(body, "GPGGA,034839.00,3124.74074,S,15245.92593,E,1,08,0.66,45.30,M,,M,,");
  EXPECT_FALSE(rmc_body(data, body, sizeof(body)));
}

#ifdef USE_GPS_GEOID_SEPARATION
TEST(GnssSentences, GgaGeoidSeparation) {
  gps::GPSData data = fix();
  data.geoid_separation_e3 = -27700;
  data.valid |= gps::GPS_FIELD_GEOID_SEPARATION;
  data.updated = gps::GPS_FIELD_LOCATION | gps::GPS_FIELD_SATELLITES;
  char body[100];
  ASSERT_TRUE(gga_body(data, body, sizeof(body)));
  EXPECT_STREQ(body, "GPGGA,034839.00,3124.74074,S,15245.92593,E,1,08,0.66,45.30,M,-27.70,M,,");
}
#endif

TEST(GnssSentences, MinutesRounded) {
  char text[20];
  nmea_coordinate(99999999, 2, 'N', 'S', text, sizeof(text));
  EXPECT_STREQ(text, "0959.99999,N");
  nmea_coordinate(-1801, 3, 'E', 'W', text, sizeof(text));
  EXPECT_STREQ(text, "00000.01081,W");
}

TEST(GnssSentences, GgaNoFix) {
  gps::GPSData data = fix();
  data.updated = gps::GPS_FIELD_SATELLITES | gps::GPS_FIELD_HDOP;
  data.satellites = 3;
  data.hdop_e2 = 2550;
  char body[100];
  ASSERT_TRUE(gga_body(data, body, sizeof(body)));
  EXPECT_STREQ(body, "GPGGA,034839.00,,,,,0,03,25.50,,M,,M,,");
}

TEST(GnssSentences, NorthWestAndNegativeAltitude) {
  gps::GPSData data = fix();
  data.latitude_e7 = 515000000;
  data.longitude_e7 = -1250000;
  data.altitude_e3 = -12345;
  data.updated = gps::GPS_FIELD_LOCATION | gps::GPS_FIELD_SATELLITES;
  char body[100];
  ASSERT_TRUE(gga_body(data, body, sizeof(body)));
  EXPECT_STREQ(body, "GPGGA,034839.00,5130.00000,N,00007.50000,W,1,08,0.66,-12.34,M,,M,,");
}

TEST(GnssSentences, NoTime) {
  gps::GPSData data = fix();
  data.valid = gps::GPS_FIELD_SATELLITES;
  data.updated = gps::GPS_FIELD_SATELLITES;
  char body[100];
  ASSERT_TRUE(gga_body(data, body, sizeof(body)));
  EXPECT_STREQ(body, "GPGGA,,,,,,0,08,0.66,,M,,M,,");
}

}  // namespace esphome::flarm_receiver::testing
