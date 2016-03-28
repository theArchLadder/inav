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

#include "sensors/battery.h"
#include "sensors/sensors.h"
#include "io/beeper.h"
#include "sensors/boardalignment.h"
#include "config/runtime_config.h"
#include "config/config.h"

#include "sensors/acceleration.h"

int16_t accADCRaw[XYZ_AXIS_COUNT];
int32_t accADC[XYZ_AXIS_COUNT];

acc_t acc;                       // acc access functions
sensor_align_e accAlign = 0;
uint16_t acc_1G = 256;          // this is the 1G measured acceleration.

uint16_t calibratingA = 0;      // the calibration is done is the main loop. Calibrating decreases at each cycle down to 0, then we enter in a normal mode.

static biquad_t accFilterState[XYZ_AXIS_COUNT];
static bool accFilterInitialised = false;

accConfig_t accConfig;

static const pgRegistry_t accConfigRegistry PG_REGISTRY_SECTION =
{
    .base = (uint8_t *)&accConfig,
    .size = sizeof(accConfig),
    .pgn = PG_ACC_CONFIG,
    .format = 0,
    .flags = PGC_SYSTEM
};

void accSetCalibrationCycles(uint16_t calibrationCyclesRequired)
{
    calibratingA = calibrationCyclesRequired;
}

bool isAccelerationCalibrationComplete(void)
{
    return calibratingA == 0;
}

bool isOnFinalAccelerationCalibrationCycle(void)
{
    return calibratingA == 1;
}

bool isOnFirstAccelerationCalibrationCycle(void)
{
    return calibratingA == CALIBRATING_ACC_CYCLES;
}

static sensorCalibrationState_t calState;
static bool calibratedAxis[6];
static int32_t accSamples[6][3];
static int  calibratedAxisCount = 0;

int getPrimaryAxisIndex(int32_t sample[3])
{
    if (ABS(sample[Z]) > ABS(sample[X]) && ABS(sample[Z]) > ABS(sample[Y])) {
        //Z-axis
        return (sample[Z] > 0) ? 0 : 1;
    }
    else if (ABS(sample[X]) > ABS(sample[Y]) && ABS(sample[X]) > ABS(sample[Z])) {
        //X-axis
        return (sample[X] > 0) ? 2 : 3;
    }
    else if (ABS(sample[Y]) > ABS(sample[X]) && ABS(sample[Y]) > ABS(sample[Z])) {
        //Y-axis
        return (sample[Y] > 0) ? 4 : 5;
    }
    else 
        return -1;
}

void performAcclerationCalibration(void)
{
    int axisIndex = getPrimaryAxisIndex(accADC);
    uint8_t axis;

    // Check if sample is usable
    if (axisIndex < 0) {
        return;
    }

    // Top-up and first calibration cycle, reset everything
    if (axisIndex == 0 && isOnFirstAccelerationCalibrationCycle()) {
        for (axis = 0; axis < 6; axis++) {
            calibratedAxis[axis] = false;
            accSamples[axis][X] = 0;
            accSamples[axis][Y] = 0;
            accSamples[axis][Z] = 0;
        }

        calibratedAxisCount = 0;
        sensorCalibrationResetState(&calState);
    }

    if (!calibratedAxis[axisIndex]) {
        sensorCalibrationPushSampleForOffsetCalculation(&calState, accADC);
        accSamples[axisIndex][X] += accADC[X];
        accSamples[axisIndex][Y] += accADC[Y];
        accSamples[axisIndex][Z] += accADC[Z];

        if (isOnFinalAccelerationCalibrationCycle()) {
            calibratedAxis[axisIndex] = true;
            calibratedAxisCount++;

            beeperConfirmationBeeps(2);
        }
    }

    if (calibratedAxisCount == 6) {
        float accTmp[3];
        int32_t accSample[3];

        /* Calculate offset */
        sensorCalibrationSolveForOffset(&calState, accTmp);

        for (axis = 0; axis < 3; axis++) {
            accConfig.accZero.raw[axis] = lrintf(accTmp[axis]);
        }

        /* Not we can offset our accumulated averages samples and calculate scale factors and calculate gains */
        sensorCalibrationResetState(&calState);

        for (axis = 0; axis < 6; axis++) {
            accSample[X] = accSamples[axis][X] / CALIBRATING_ACC_CYCLES - accConfig.accZero.raw[X];
            accSample[Y] = accSamples[axis][Y] / CALIBRATING_ACC_CYCLES - accConfig.accZero.raw[Y];
            accSample[Z] = accSamples[axis][Z] / CALIBRATING_ACC_CYCLES - accConfig.accZero.raw[Z];

            sensorCalibrationPushSampleForScaleCalculation(&calState, axis / 2, accSample, acc_1G);
        }

        sensorCalibrationSolveForScale(&calState, accTmp);

        for (axis = 0; axis < 3; axis++) {
            accConfig.accGain.raw[axis] = lrintf(accTmp[axis] * 4096);
        }

        saveConfigAndNotify();
    }

    calibratingA--;
}

void applyAccelerationZero(void)
{
    accADC[X] = (accADC[X] - accConfig.accZero.raw[X]) * accConfig.accGain.raw[X] / 4096;
    accADC[Y] = (accADC[Y] - accConfig.accZero.raw[Y]) * accConfig.accGain.raw[Y] / 4096;
    accADC[Z] = (accADC[Z] - accConfig.accZero.raw[Z]) * accConfig.accGain.raw[Z] / 4096;
}

void updateAccelerationReadings(void)
{
    int axis;

    if (!acc.read(accADCRaw)) {
        return;
    }

    for (axis = 0; axis < XYZ_AXIS_COUNT; axis++) accADC[axis] = accADCRaw[axis];

    if (accConfig.acc_soft_lpf_hz) {
        if (!accFilterInitialised) {
            if (targetLooptime) {  /* Initialisation needs to happen once sample rate is known */
                for (axis = 0; axis < 3; axis++) {
                    filterInitBiQuad(accConfig.acc_soft_lpf_hz, &accFilterState[axis], 0);
                }

                accFilterInitialised = true;
            }
        }

        if (accFilterInitialised) {
            for (axis = 0; axis < XYZ_AXIS_COUNT; axis++) {
                accADC[axis] = lrintf(filterApplyBiQuad((float) accADC[axis], &accFilterState[axis]));
            }
        }
    }

    if (!isAccelerationCalibrationComplete()) {
        performAcclerationCalibration();
    }

    alignSensors(accADC, accADC, accAlign);

    applyAccelerationZero();
}

