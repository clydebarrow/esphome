//
// Created by Clyde Stubbs on 3/11/2022.
//

#pragma once

namespace esphome::skyecho {

extern float greatCircleDistance(float lat1, float lon1, float lat2, float lon2);

extern float toRadians(float degrees);

extern float easting(float lat1, float lon1, float lat2, float lon2);

extern float northing(float lat1, float lat2);

}  // namespace esphome::skyecho
