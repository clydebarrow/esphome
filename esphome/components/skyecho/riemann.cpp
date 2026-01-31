//
// Created by Clyde Stubbs on 3/11/2022.
//

#include "riemann.h"
#include "math.h"

#define PI 3.14159265358979323846f
#define EARTH_RADIUS 6371000.0f

float toRadians(float degrees) { return degrees / 180.0f * PI; }

/**
 * Haversine function.
 * @param The angle in radians
 * @return The haversine
 */
float hav(float radians) {
  float s = sinf(radians / 2);
  return s * s;
}

/**
 * Calculate a great circle distance between two points. Input values are in degrees
 * @param lat1  The first point latitude
 * @param lon1  The first point longitude
 * @param lat2  The second point latitude
 * @param lon2  The second point longitude
 * @return  The great circle distance between the points in meters.
 */
float greatCircleDistance(float lat1, float lon1, float lat2, float lon2) {
  float lat1r = toRadians(lat1);
  float lat2r = toRadians(lat2);

  float haver = hav(lat2r - lat1r) + hav(toRadians(lon2) - toRadians(lon1)) * cosf(lat1r) * cosf(lat2r);
  return EARTH_RADIUS * 2 * asinf(sqrtf(haver));
}

/**
 * Calculate the distance north (+ve) or south (-ve) of the second point from the first.
 */

float northing(float lat1, float lat2) { return sinf(toRadians(lat2 - lat1)) * EARTH_RADIUS; }

/**
 * Calculate the distance east (+ve) or west (-ve) of the second point from the first.
 */
float easting(float lat1, float lon1, float lat2, float lon2) {
  return northing(lon1, lon2) * cosf(toRadians(fabsf(lat1 + lat2) / 2));
}
