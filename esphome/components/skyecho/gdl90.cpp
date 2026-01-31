#include "gdl90.h"
#include "riemann.h"

namespace esphome {
namespace skyecho {
#define FLAG_BYTE 0x7E
#define ESCAPE_BYTE 0x7D

#define TAG "gdl90"

#ifndef LOG_MESSAGE

#include <stdio.h>

#define LOG_MESSAGE(a, ...) fprintf(stderr, a, __VA_ARGS__)
#endif

static const uint16_t crcTable[] = {
    0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50A5, 0x60C6, 0x70E7, 0x8108, 0x9129, 0xA14A, 0xB16B, 0xC18C, 0xD1AD,
    0xE1CE, 0xF1EF, 0x1231, 0x0210, 0x3273, 0x2252, 0x52B5, 0x4294, 0x72F7, 0x62D6, 0x9339, 0x8318, 0xB37B, 0xA35A,
    0xD3BD, 0xC39C, 0xF3FF, 0xE3DE, 0x2462, 0x3443, 0x0420, 0x1401, 0x64E6, 0x74C7, 0x44A4, 0x5485, 0xA56A, 0xB54B,
    0x8528, 0x9509, 0xE5EE, 0xF5CF, 0xC5AC, 0xD58D, 0x3653, 0x2672, 0x1611, 0x0630, 0x76D7, 0x66F6, 0x5695, 0x46B4,
    0xB75B, 0xA77A, 0x9719, 0x8738, 0xF7DF, 0xE7FE, 0xD79D, 0xC7BC, 0x48C4, 0x58E5, 0x6886, 0x78A7, 0x0840, 0x1861,
    0x2802, 0x3823, 0xC9CC, 0xD9ED, 0xE98E, 0xF9AF, 0x8948, 0x9969, 0xA90A, 0xB92B, 0x5AF5, 0x4AD4, 0x7AB7, 0x6A96,
    0x1A71, 0x0A50, 0x3A33, 0x2A12, 0xDBFD, 0xCBDC, 0xFBBF, 0xEB9E, 0x9B79, 0x8B58, 0xBB3B, 0xAB1A, 0x6CA6, 0x7C87,
    0x4CE4, 0x5CC5, 0x2C22, 0x3C03, 0x0C60, 0x1C41, 0xEDAE, 0xFD8F, 0xCDEC, 0xDDCD, 0xAD2A, 0xBD0B, 0x8D68, 0x9D49,
    0x7E97, 0x6EB6, 0x5ED5, 0x4EF4, 0x3E13, 0x2E32, 0x1E51, 0x0E70, 0xFF9F, 0xEFBE, 0xDFDD, 0xCFFC, 0xBF1B, 0xAF3A,
    0x9F59, 0x8F78, 0x9188, 0x81A9, 0xB1CA, 0xA1EB, 0xD10C, 0xC12D, 0xF14E, 0xE16F, 0x1080, 0x00A1, 0x30C2, 0x20E3,
    0x5004, 0x4025, 0x7046, 0x6067, 0x83B9, 0x9398, 0xA3FB, 0xB3DA, 0xC33D, 0xD31C, 0xE37F, 0xF35E, 0x02B1, 0x1290,
    0x22F3, 0x32D2, 0x4235, 0x5214, 0x6277, 0x7256, 0xB5EA, 0xA5CB, 0x95A8, 0x8589, 0xF56E, 0xE54F, 0xD52C, 0xC50D,
    0x34E2, 0x24C3, 0x14A0, 0x0481, 0x7466, 0x6447, 0x5424, 0x4405, 0xA7DB, 0xB7FA, 0x8799, 0x97B8, 0xE75F, 0xF77E,
    0xC71D, 0xD73C, 0x26D3, 0x36F2, 0x0691, 0x16B0, 0x6657, 0x7676, 0x4615, 0x5634, 0xD94C, 0xC96D, 0xF90E, 0xE92F,
    0x99C8, 0x89E9, 0xB98A, 0xA9AB, 0x5844, 0x4865, 0x7806, 0x6827, 0x18C0, 0x08E1, 0x3882, 0x28A3, 0xCB7D, 0xDB5C,
    0xEB3F, 0xFB1E, 0x8BF9, 0x9BD8, 0xABBB, 0xBB9A, 0x4A75, 0x5A54, 0x6A37, 0x7A16, 0x0AF1, 0x1AD0, 0x2AB3, 0x3A92,
    0xFD2E, 0xED0F, 0xDD6C, 0xCD4D, 0xBDAA, 0xAD8B, 0x9DE8, 0x8DC9, 0x7C26, 0x6C07, 0x5C64, 0x4C45, 0x3CA2, 0x2C83,
    0x1CE0, 0x0CC1, 0xEF1F, 0xFF3E, 0xCF5D, 0xDF7C, 0xAF9B, 0xBFBA, 0x8FD9, 0x9FF8, 0x6E17, 0x7E36, 0x4E55, 0x5E74,
    0x2E93, 0x3EB2, 0x0ED1, 0x1EF0};

static const char *const gdl90ErrStrings[] = {
    "GDL90_ERR_NONE",   "GDL90_ERR_BADFLAG",   "GDL90_ERR_MISSINGEND",
    "GDL90_ERR_BADCRC", "GDL90_ERR_BADLENGTH", "GDL90_ERR_UNKNOWN_ID",
};

const char *gdl90ErrorMessage(gdl90Err_t err) {
  if (err >= GDL90_ERR_LAST_ENTRY)
    return "Unknown error";
  return gdl90ErrStrings[err];
}

/**
 * Calculate the CRC for a block
 */

uint16_t gdl90Crc(const uint8_t *buffer, size_t size) {
  size_t i;
  uint16_t crc = 0;

  for (i = 0; i != size; i++)
    crc = crcTable[crc >> 8] ^ (crc << 8) ^ buffer[i];
  return crc;
}

float fromFt(float feet) { return feet * 0.3048f; }

/**
 * Get the distance between two traffic reports
 * @param p1 first object
 * @param p2  second object
 * @return distance in meters
 */
float trafficDistance(const gdl90PositionReport_t *p1, const gdl90PositionReport_t *p2) {
  return greatCircleDistance(p1->latitude, p1->longitude, p2->latitude, p2->longitude);
}

float trafficEasting(const gdl90PositionReport_t *p1, const gdl90PositionReport_t *p2) {
  return easting(p1->latitude, p1->longitude, p2->latitude, p2->longitude);
}

float trafficNorthing(const gdl90PositionReport_t *p1, const gdl90PositionReport_t *p2) {
  return northing(p1->latitude, p2->latitude);
}

/**
 * Get a little-endian 16 bit word from a buffer
 * @param buffer
 * @return
 */
static uint16_t getLittleWord(const uint8_t *buffer) { return buffer[0] + (buffer[1] << 8); }

static uint16_t get16Bit(const uint8_t *buffer) { return buffer[1] + (buffer[0] << 8); }

static uint32_t get24Bit(const uint8_t *buffer) { return buffer[2] + (buffer[1] << 8) + (buffer[0] << 16); }

static int32_t signExtend(uint32_t value, int bits) { return ((int32_t) value << (32 - bits)) >> (32 - bits); }

static int32_t get24BitSigned(const uint8_t *buffer) { return signExtend(get24Bit(buffer), 24); }

#define FLAGSET(byte, bit) (((byte) & (1 << (bit))) != 0)

static gdl90Err_t decodeHeartbeat(const uint8_t *buffer, gdl90Data_t *out) {
  uint8_t byte = buffer[1];
  out->heartbeat.uatInitialized = FLAGSET(byte, 0);
  out->heartbeat.rAtcS = FLAGSET(byte, 2);
  out->heartbeat.gpsBattLow = FLAGSET(byte, 3);
  out->heartbeat.addrType = FLAGSET(byte, 4);
  out->heartbeat.ident = FLAGSET(byte, 5);
  out->heartbeat.maintReqd = FLAGSET(byte, 6);
  out->heartbeat.gpsPosValid = FLAGSET(byte, 7);
  byte = buffer[2];
  out->heartbeat.utcOk = FLAGSET(byte, 0);
  out->heartbeat.csaNotAvailable = FLAGSET(byte, 5);
  out->heartbeat.csaRequested = FLAGSET(byte, 6);
  out->heartbeat.timeStamp = getLittleWord(buffer + 3) + (((uint32_t) byte & 0x80) << 9);
  out->heartbeat.uplinkRecvd = buffer[5] >> 3;
  out->heartbeat.totalRecvd = buffer[6] + ((buffer[5] & 3) << 8);
  return GDL90_ERR_NONE;
}

static gdl90Err_t decodePosition(const uint8_t *buffer, gdl90Data_t *out) {
  uint8_t byte = buffer[1];
  out->positionReport.isHazard = FLAGSET(byte, 4);
  out->positionReport.addressType = (emitterAddressType_t) (byte & 0xF);
  out->positionReport.address = get24Bit(buffer + 2);
  out->positionReport.latitude = (float) get24BitSigned(buffer + 5) * 360.0f / 0x1000000;
  out->positionReport.longitude = (float) get24BitSigned(buffer + 8) * 360.0f / 0x1000000;
  // convert altitude in feet to meters
  out->positionReport.altitude = fromFt(((float) ((buffer[11] << 4) + (buffer[12] >> 4)) * 25.0f - 1000.0f));
  byte = buffer[12];
  out->positionReport.isInFlight = FLAGSET(byte, 3);
  out->positionReport.isExtrapolated = FLAGSET(byte, 2);
  out->positionReport.isHeading = FLAGSET(byte, 1);
  out->positionReport.nic = buffer[13] >> 4;
  out->positionReport.NACp = buffer[13] & 0xF;
  // convert speed in knots to m/s
  out->positionReport.groundSpeed = ((float) ((buffer[14] << 4) + (buffer[15] >> 4))) * 1852.0f / 3600;
  uint16_t vs = buffer[16] + ((buffer[15] & 0xF) << 8);
  int16_t value = -4096;
  if (vs < 0x1FF || vs > 0xE01)
    value = (int16_t) ((int16_t) (vs << 4) >> 4);  // sign extend
  out->positionReport.verticalSpeed = fromFt((float) value * 64.0f / 60);
  out->positionReport.track = (float) buffer[17] * 360 / 256.0f;
  out->positionReport.category = (emitterCategory_t) buffer[18];
  memccpy(out->positionReport.callsign, buffer + 19, 0, GDL_CALLSIGN_LENGTH);
  out->positionReport.callsign[GDL_CALLSIGN_LENGTH] = 0;
  out->positionReport.priorityCode = buffer[20] >> 4;
  return GDL90_ERR_NONE;
}

static gdl90Err_t decodeOwnshipAltitude(const uint8_t *buffer, gdl90Data_t *out) {
  out->ownshipAltitude.warning = FLAGSET(buffer[3], 7);
  out->ownshipAltitude.vfom = get16Bit(buffer + 3) & 0x7FFF;
  out->ownshipAltitude.geoAltitude = fromFt((float) signExtend(get16Bit(buffer + 1), 16) * 5);
  return GDL90_ERR_NONE;
}

static gdl90Err_t decodeForeflight(const uint8_t *buffer, gdl90Data_t *out) {
  out->foreflightId.subid = buffer[1];
  out->foreflightId.version = buffer[2];
  memcpy(out->foreflightId.serialNo, buffer + 3, sizeof(out->foreflightId.serialNo));
  memcpy(out->foreflightId.name, buffer + 11, sizeof(out->foreflightId.name) - 1);
  out->foreflightId.name[8] = 0;
  memcpy(out->foreflightId.longName, buffer + 19, sizeof(out->foreflightId.longName) - 1);
  out->foreflightId.longName[16] = 0;
  out->foreflightId.wgs84 = !FLAGSET(buffer[38], 0);
  return GDL90_ERR_NONE;
}

static struct {
  uint8_t id;
  int len;

  gdl90Err_t (*decoder)(const uint8_t *, gdl90Data_t *);
} decodeTable[] = {
    {GDL90_HEARTBEAT, 7, decodeHeartbeat},       {GDL90_OWNSHIP_GEO_ALT, 5, decodeOwnshipAltitude},
    {GDL90_TRAFFIC_REPORT, 28, decodePosition},  {GDL90_OWNSHIP_REPORT, 28, decodePosition},
    {GDL90_FOREFLIGHT_ID, 39, decodeForeflight},

};

/**
 * Identify GDL90 data packets by looking for control and flag bytes
 * @param buffer The input buffer. This will be modified!
 * @param len The number of bytes in the buffer
 * @param blockCb A callback which will receive the extracted packets.
 */

void gdl90GetBlocks(uint8_t *buffer, size_t len, std::function<void(const gdlDataPacket_t *, struct in_addr *)> blockCb,
                    struct in_addr *srcAddr) {
  bool controlSeen = false;
  gdlDataPacket_t packet;
  packet.len = 0;
  uint8_t *blockStart = buffer, *blockEnd = buffer;
  while (len != 0) {
    uint8_t c = *buffer++;
    len--;
    if (c == FLAG_BYTE) {
      if (!controlSeen) {  // start of block
        if (len < 7) {
          packet.len = 0;
          // blockCb(GDL90_ERR_BADLENGTH, &packet);
          return;
        }
        if (*buffer == FLAG_BYTE)  // two consecutive flags mark start of block.
          continue;
        controlSeen = true;
        blockEnd = blockStart = buffer;
        continue;
      }
      controlSeen = false;
      int blockLen = (int) (blockEnd - blockStart - 2);
      packet.len = blockLen;
      if (blockLen <= 4 || blockLen > sizeof(packet.data)) {
        // blockCb(GDL90_ERR_BADLENGTH, &packet);
        continue;
      }
      memcpy(packet.data, blockStart, blockLen);
      if (getLittleWord(blockEnd - 2) != gdl90Crc(blockStart, blockLen)) {
        packet.err = GDL90_ERR_BADCRC;
        blockCb(&packet, srcAddr);
        continue;
      }

      uint8_t id = blockStart[0];
      bool found = false;
      for (int i = 0; i != sizeof(decodeTable) / sizeof(decodeTable[0]); i++) {
        if (decodeTable[i].id == id && blockLen == decodeTable[i].len) {
          found = true;
          packet.err = GDL90_ERR_NONE;
          blockCb(&packet, srcAddr);
          break;
        }
      }
      if (!found) {
        packet.err = GDL90_ERR_UNKNOWN_ID;
        blockCb(&packet, srcAddr);
      }
    } else {
      if (!controlSeen)
        continue;
      if (c == ESCAPE_BYTE) {
        if (len == 0)
          break;
        *blockEnd++ = *buffer++ ^ 0x20;
        len--;
        continue;
      }
      *blockEnd++ = c;
    }
  }
  if (controlSeen) {
    packet.err = GDL90_ERR_MISSINGEND;
    blockCb(&packet, srcAddr);
  }
}

/**
 * Given a GDL90 data block, decode it into a gdlData_t. The length is assumed to have been
 * previously checked and found correct.
 * @param block The input data
 * @param out   Where to store the decoded data
 * @return an error code, or GDL90_ERR_NONE if decode was successful.
 */
gdl90Err_t gdl90DecodeBlock(const uint8_t *block, uint32_t len, gdl90Data_t *out) {
  uint8_t id = block[0];
  out->id = id;
  for (int i = 0; i != sizeof(decodeTable) / sizeof(decodeTable[0]); i++) {
    if (decodeTable[i].id == id && decodeTable[i].len == len) {
      return decodeTable[i].decoder(block, out);
    }
  }
  return GDL90_ERR_UNKNOWN_ID;
}
}  // namespace skyecho
}  // namespace esphome
