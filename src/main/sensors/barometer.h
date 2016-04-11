/*
 * This file is part of Cleanflight.
 *
 * Cleanflight is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Cleanflight is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Cleanflight.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

typedef enum {
    BARO_DEFAULT = 0,
    BARO_NONE = 1,
    BARO_BMP085 = 2,
    BARO_MS5611 = 3,
    BARO_BMP280 = 4,
    BARO_FAKE = 5
} baroSensor_e;

#define BARO_SAMPLE_COUNT_MAX   48
#define BARO_MAX BARO_FAKE

typedef struct barometerConfig_s {
    uint8_t use_median_filtering;       // Use 3-point median filtering
} barometerConfig_t;

extern barometerConfig_t barometerConfig;

extern int32_t BaroAlt;
extern int32_t baroTemperature;             // Use temperature for telemetry

#ifdef BARO
bool isBaroCalibrationComplete(void);
void baroSetCalibrationCycles(uint16_t calibrationCyclesRequired);
uint32_t baroUpdate(void);
bool isBaroReady(void);
int32_t baroCalculateAltitude(void);
#endif
