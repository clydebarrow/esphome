//
// Created by Clyde Stubbs on 3/11/2022.
//

#ifndef TRAFFIX_DISPLAY_RIEMANN_H
#define TRAFFIX_DISPLAY_RIEMANN_H

extern float greatCircleDistance(float lat1, float lon1, float lat2, float lon2);

extern float toRadians(float degrees);

extern float easting(float lat1, float lon1, float lat2, float lon2);

extern float northing(float lat1, float lat2);

#endif  // TRAFFIX_DISPLAY_RIEMANN_H
