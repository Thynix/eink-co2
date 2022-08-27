#include <stdint.h>

/*
 * Temperature offset in °C. This is added to raw measurements - subtract
 * expected from measured.
 *
 * Used with setTemperatureOffset():
 *
 *     Note that the temperature offset can depend on
 *     various factors such as the SCD4x measurement mode, self-heating of close
 *     components, the ambient temperature and air flow. Thus, the SCD4x
 *     temperature offset should be determined inside the customer device under
 *     its typical operation and in thermal equilibrium.
 */
const float tOffset = 0;

// Meters above sea level.
const uint16_t sensorAltitude = 216;

// Whether to wait for a serial connection on startup.
const bool waitForSerial = true;
const long serialWaitTimeoutMs = 10000;