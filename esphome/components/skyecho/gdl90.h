#pragma once

#include "esphome/core/log.h"
#include "esphome/core/component.h"
#include "esphome/components/network/util.h"
#include "esphome/components/socket/socket.h"

namespace esphome {
namespace skyecho {

static const size_t GDL_MAX_BLOCK_SIZE = 50;
static const size_t GDL_CALLSIGN_LENGTH = 8;  // max 8 chars in callsign
typedef enum {
  GDL90_ERR_NONE,
  GDL90_ERR_BADFLAG,
  GDL90_ERR_MISSINGEND,
  GDL90_ERR_BADCRC,
  GDL90_ERR_BADLENGTH,
  GDL90_ERR_UNKNOWN_ID,
  GDL90_ERR_LAST_ENTRY
} gdl90Err_t;

/**
 * Get a description of the error
 * @param err The error number
 * @return A string describing the error
 */
const char *gdl90ErrorMessage(gdl90Err_t err);

typedef enum {
  GDL90_HEARTBEAT = 0,
  GDL90_INITIALIZATION = 2,
  GDL90_UPLINK_DATA = 7,
  GDL90_HEIGHT_ABOVE_TERRAIN = 9,
  GDL90_OWNSHIP_REPORT = 10,
  GDL90_OWNSHIP_GEO_ALT = 11,
  GDL90_TRAFFIC_REPORT = 20,
  GDL90_BASIC_REPORT = 30,
  GDL90_LONG_REPORT = 31,
  GDL90_FOREFLIGHT_ID = 101,
} gdl90MessageId_t;

typedef enum {
  None = 0,
  Light,
  Small,
  Large,
  HighVortex,
  Heavy,
  Fighter,
  Rotorcraft,
  Unassigned8,
  Glider,
  Balloon,
  Parachute,
  HangGlider,
  Unassigned13,
  UAV,
  Spaceship,
  Unassigned16,
  SurfaceEmergency,
  SurfaceService,
  Obstacle,
  ObstacleCluster,
  ObstacleLine
} emitterCategory_t;

typedef enum {
  EmitterAdsbIcao,
  EmitterAdsbSelf,
  EmitterTisbIcao,
  EmitterTisbFile,
  EmitterSurface,
  EmitterBeacon,
  EmitterFlarm,
  EmitterNone
} emitterAddressType_t;

typedef struct {
  bool gpsPosValid;
  bool maintReqd;
  bool ident;
  bool addrType;
  bool gpsBattLow;
  bool rAtcS;
  bool uatInitialized;
  bool csaRequested;
  bool csaNotAvailable;
  bool utcOk;
  uint32_t timeStamp;
  uint32_t uplinkRecvd;
  uint32_t totalRecvd;
} gdl90Heartbeat_t;

typedef struct {
  uint8_t subid;
  uint8_t version;
  uint8_t serialNo[8];
  char name[9];
  char longName[17];
  bool wgs84;
} gdl90ForeflightId_t;

typedef struct {
  uint32_t address;
  emitterAddressType_t addressType;
  emitterCategory_t category;
  char callsign[GDL_CALLSIGN_LENGTH + 1];  // null terminated
  float latitude;
  float longitude;
  float altitude;
  float groundSpeed;
  float track;
  float accuracy;
  float verticalSpeed;
  uint8_t priorityCode;
  uint8_t nic;
  uint8_t NACp;
  bool isInFlight;
  bool isExtrapolated;
  bool isHeading;
  bool isHazard;
} gdl90PositionReport_t;

typedef struct {
  float geoAltitude;  // meters
  uint16_t vfom;
  bool warning;
} gdl90OwnshipAltitude_t;

typedef struct {
  uint8_t id;
  union {
    gdl90Heartbeat_t heartbeat;
    gdl90PositionReport_t positionReport;  // for traffic or ownship
    gdl90OwnshipAltitude_t ownshipAltitude;
    gdl90ForeflightId_t foreflightId;
  };
} gdl90Data_t;

/**
 */

typedef struct {
  uint16_t len;
  gdl90Err_t err;
  uint8_t data[GDL_MAX_BLOCK_SIZE];
} gdlDataPacket_t;

extern void gdl90GetBlocks(uint8_t *buffer, size_t len,
                           std::function<void(const gdlDataPacket_t *, struct in_addr *pAddr)>,
                           struct in_addr *srcAddr);

extern gdl90Err_t gdl90DecodeBlock(const uint8_t *block, uint32_t len, gdl90Data_t *out);

extern float trafficDistance(const gdl90PositionReport_t *p1, const gdl90PositionReport_t *p2);

extern float trafficNorthing(const gdl90PositionReport_t *p1, const gdl90PositionReport_t *p2);

extern float trafficEasting(const gdl90PositionReport_t *p1, const gdl90PositionReport_t *p2);

extern bool isGdl90TrafficConnected();

}  // namespace skyecho
}  // namespace esphome
