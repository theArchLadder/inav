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

#include <stdbool.h>
#include <stdint.h>
#include <math.h>

#include "platform.h"

#include "common/axis.h"
#include "common/maths.h"
#include "common/filter.h"

#include "config/parameter_group.h"
#include "config/parameter_group_ids.h"

#include "drivers/sensor.h"
#include "drivers/accgyro.h"
#include "drivers/gyro_sync.h"

#include "sensors/sensors.h"

#include "io/beeper.h"
#include "io/statusindicator.h"

#include "sensors/boardalignment.h"
#include "sensors/gyro.h"

uint16_t calibratingG = 0;
int16_t gyroADCRaw[XYZ_AXIS_COUNT];
int32_t gyroADC[XYZ_AXIS_COUNT];
int32_t gyroZero[FLIGHT_DYNAMICS_INDEX_COUNT] = { 0, 0, 0 };

static biquad_t gyroFilterState[XYZ_AXIS_COUNT];
static bool gyroFilterInitialised = false;

gyro_t gyro;                      // gyro access functions
gyroConfig_t gyroConfig;
sensor_align_e gyroAlign = 0;

static const pgRegistry_t gyroConfigRegistry PG_REGISTRY_SECTION =
{
    .base = (uint8_t *)&gyroConfig,
    .size = sizeof(gyroConfig),
    .pgn = PG_GYRO_CONFIG,
    .format = 0,
    .flags = PGC_SYSTEM
};

void gyroSetCalibrationCycles(uint16_t calibrationCyclesRequired)
{
    calibratingG = calibrationCyclesRequired;
}

bool isGyroCalibrationComplete(void)
{
    return calibratingG == 0;
}

bool isOnFinalGyroCalibrationCycle(void)
{
    return calibratingG == 1;
}

bool isOnFirstGyroCalibrationCycle(void)
{
    return calibratingG == CALIBRATING_GYRO_CYCLES;
}

static void performAcclerationCalibration(uint8_t gyroMovementCalibrationThreshold)
{
    int8_t axis;
    static int32_t g[3];
    static stdev_t var[3];

    for (axis = 0; axis < 3; axis++) {

        // Reset g[axis] at start of calibration
        if (isOnFirstGyroCalibrationCycle()) {
            g[axis] = 0;
            devClear(&var[axis]);
        }

        // Sum up CALIBRATING_GYRO_CYCLES readings
        g[axis] += gyroADC[axis];
        devPush(&var[axis], gyroADC[axis]);

        // Reset global variables to prevent other code from using un-calibrated data
        gyroADC[axis] = 0;
        gyroZero[axis] = 0;

        if (isOnFinalGyroCalibrationCycle()) {
            float dev = devStandardDeviation(&var[axis]);
            // check deviation and startover in case the model was moved
            if (gyroMovementCalibrationThreshold && dev > gyroMovementCalibrationThreshold) {
                gyroSetCalibrationCycles(CALIBRATING_GYRO_CYCLES);
                return;
            }
            gyroZero[axis] = (g[axis] + (CALIBRATING_GYRO_CYCLES / 2)) / CALIBRATING_GYRO_CYCLES;
        }
    }

    if (isOnFinalGyroCalibrationCycle()) {
        beeper(BEEPER_GYRO_CALIBRATED);
    }
    calibratingG--;

}

static void applyGyroZero(void)
{
    int8_t axis;
    for (axis = 0; axis < 3; axis++) {
        gyroADC[axis] -= gyroZero[axis];
    }
}

void gyroUpdate(void)
{
    int8_t axis;

    // range: +/- 8192; +/- 2000 deg/sec
    if (!gyro.read(gyroADCRaw)) {
        return;
    }

    // Prepare a copy of int32_t gyroADC for mangling to prevent overflow
    for (axis = 0; axis < XYZ_AXIS_COUNT; axis++) gyroADC[axis] = gyroADCRaw[axis];

    if (gyroConfig.gyro_soft_lpf_hz) {
        if (!gyroFilterInitialised) {
            if (targetLooptime) {  /* Initialisation needs to happen once sample rate is known */
                for (axis = 0; axis < 3; axis++) {
                    filterInitBiQuad(gyroConfig.gyro_soft_lpf_hz, &gyroFilterState[axis], 0);
                }

                gyroFilterInitialised = true;
            }
        }

        if (gyroFilterInitialised) {
            for (axis = 0; axis < XYZ_AXIS_COUNT; axis++) {
                gyroADC[axis] = lrintf(filterApplyBiQuad((float) gyroADC[axis], &gyroFilterState[axis]));
            }
        }
    }

    alignSensors(gyroADC, gyroADC, gyroAlign);

    if (!isGyroCalibrationComplete()) {
        performAcclerationCalibration(gyroConfig.gyroMovementCalibrationThreshold);
    }

    applyGyroZero();
}
