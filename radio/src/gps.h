/*
 * Copyright (C) EdgeTX
 *
 * Based on code named
 *   opentx - https://github.com/opentx/opentx
 *   th9x - http://code.google.com/p/th9x
 *   er9x - http://code.google.com/p/er9x
 *   gruvin9x - http://code.google.com/p/gruvin9x
 *
 * License GPLv2: http://www.gnu.org/licenses/gpl-2.0.html
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#pragma once

#include "edgetx.h"
#include <inttypes.h>

struct gpsdata_t
{
  int32_t longitude;              // degrees * 1.000.000
  int32_t latitude;               // degrees * 1.000.000
  uint8_t fix;
  uint8_t numSat;
  uint32_t packetCount;
  uint32_t ggaCount;
  uint32_t gsvCount;
  uint8_t satsInView;
  uint8_t maxCn0;
  uint32_t lastUbxPvtTick; // 10 ms clock; zero indicates unavailable
  uint32_t lastHdopTick;
  uint32_t lastGgaTick;
  uint32_t lastGsvTick;
  uint32_t lastNavSatTick;
  uint32_t ubxPvtCount;
  uint32_t ubxSatCount;
  uint8_t ubxFixType;
  uint8_t ubxFixFlags;
  uint32_t ubxHAccMm;   // NAV-PVT horizontal accuracy estimate in mm
  uint32_t ubxVAccMm;   // NAV-PVT vertical accuracy estimate in mm
  uint32_t ubxSAccMms;  // NAV-PVT speed accuracy estimate in mm/s
  uint16_t ubxPDop;     // NAV-PVT PDOP in units of 0.01
  uint32_t ubxAckCount;
  uint8_t ubxAckType;   // 1 = ACK, 0 = NAK
  uint8_t ubxAckClass;
  uint8_t ubxAckId;
  uint8_t ubxSatsVisible;
  uint8_t ubxMaxCn0;
  uint8_t ubxSatsUsed;
  char ubxVersion[31];
  uint32_t errorCount;
  int32_t altitude;               // altitude in 0.1m
  uint16_t speed;                 // speed in 0.1m/s
  uint16_t groundCourse;          // degrees * 10
  uint16_t hdop;
};

extern gpsdata_t gpsData;
uint8_t gpsGetProtocol();
// A metric is available only for the current protocol and a fresh source frame.
bool gpsUbxQualityFresh();
bool gpsHdopFresh();
bool gpsNavigationFresh();
bool gpsRfFresh();

#if defined(DEBUG)
extern uint8_t gpsTraceEnabled;
#endif

#define GPS_PROTOCOL_AUTO 0
#define GPS_PROTOCOL_NMEA 1
#define GPS_PROTOCOL_UBX 2

// Setup GPS serial port
void gpsSetSerialDriver(void* ctx, const etx_serial_driver_t* drv,
                        int protocol);

// Periodic processing
void gpsWakeup();

// Send a 0-terminated frame
void gpsSendFrame(const char* frame);

uint32_t GPS_coord_to_degrees(const char* coordinateString);
