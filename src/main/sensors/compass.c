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

#include "config/parameter_group.h"
#include "config/parameter_group_ids.h"

#include "drivers/sensor.h"
#include "drivers/compass.h"
#include "drivers/compass_hmc5883l.h"
#include "drivers/gpio.h"
#include "drivers/light_led.h"

#include "sensors/boardalignment.h"
#include "config/runtime_config.h"
#include "config/config.h"

#include "sensors/sensors.h"
#include "sensors/compass.h"

#ifdef NAZE
#include "hardware_revision.h"
#endif

mag_t mag;                   // mag access functions

extern uint32_t currentTime; // FIXME dependency on global variable, pass it in instead.

int16_t magADCRaw[XYZ_AXIS_COUNT];
int32_t magADC[XYZ_AXIS_COUNT];
sensor_align_e magAlign = 0;
#ifdef MAG
static uint8_t magInit = 0;
static uint8_t magUpdatedAtLeastOnce = 0;

magConfig_t magConfig;

static const pgRegistry_t magConfigRegistry PG_REGISTRY_SECTION =
{
    .base = (uint8_t *)&magConfig,
    .size = sizeof(magConfig),
    .pgn = PG_MAG_CONFIG,
    .format = 0,
    .flags = PGC_SYSTEM
};

void compassInit(void)
{
    // initialize and calibration. turn on led during mag calibration (calibration routine blinks it)
    LED1_ON;
    mag.init();
    LED1_OFF;
    magInit = 1;
}

bool isCompassReady(void)
{
    return magUpdatedAtLeastOnce;
}

static sensorCalibrationState_t calState;

#define COMPASS_UPDATE_FREQUENCY_10HZ   (1000 * 100)

void updateCompass(void)
{
    static uint32_t nextUpdateAt, calStartedAt = 0;
    static int16_t magPrev[XYZ_AXIS_COUNT];
    uint32_t axis;

    if ((int32_t)(currentTime - nextUpdateAt) < 0)
        return;

    nextUpdateAt = currentTime + COMPASS_UPDATE_FREQUENCY_10HZ;

    mag.read(magADCRaw);
    for (axis = 0; axis < XYZ_AXIS_COUNT; axis++) magADC[axis] = magADCRaw[axis];  // int32_t copy to work with

    if (STATE(CALIBRATE_MAG)) {
        calStartedAt = nextUpdateAt;

        for (axis = 0; axis < 3; axis++) {
            magConfig.magZero.raw[axis] = 0;
            magPrev[axis] = 0;
        }

        sensorCalibrationResetState(&calState);
        DISABLE_STATE(CALIBRATE_MAG);
    }

    if (magInit) {              // we apply offset only once mag calibration is done
        magADC[X] -= magConfig.magZero.raw[X];
        magADC[Y] -= magConfig.magZero.raw[Y];
        magADC[Z] -= magConfig.magZero.raw[Z];
    }

    if (calStartedAt != 0) {
        if ((nextUpdateAt - calStartedAt) < 30000000) {    // 30s: you have 30s to turn the multi in all directions
            LED0_TOGGLE;

            float diffMag = 0;
            float avgMag = 0;

            for (axis = 0; axis < 3; axis++) {
                diffMag += (magADC[axis] - magPrev[axis]) * (magADC[axis] - magPrev[axis]);
                avgMag += (magADC[axis] + magPrev[axis]) * (magADC[axis] + magPrev[axis]) / 4.0f;
            }

            // sqrtf(diffMag / avgMag) is a rough approximation of tangent of angle between magADC and magPrev. tan(8 deg) = 0.14
            if ((avgMag > 0.01f) && ((diffMag / avgMag) > (0.14f * 0.14f))) {
                sensorCalibrationPushSampleForOffsetCalculation(&calState, magADC);

                for (axis = 0; axis < 3; axis++) {
                    magPrev[axis] = magADC[axis];
                }
            }
        } else {
            float magZerof[3];
            sensorCalibrationSolveForOffset(&calState, magZerof);

            for (axis = 0; axis < 3; axis++) {
                magConfig.magZero.raw[axis] = lrintf(magZerof[axis]);
            }

            calStartedAt = 0;
            persistentFlagSet(FLAG_MAG_CALIBRATION_DONE);
            saveConfigAndNotify();
        }
    }

    alignSensors(magADC, magADC, magAlign);

    magUpdatedAtLeastOnce = 1;
}
#endif
