/*
 * Copyright (c) 2021, Sensirion AG
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright notice, this
 *   list of conditions and the following disclaimer.
 *
 * * Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 *
 * * Neither the name of Sensirion AG nor the names of its
 *   contributors may be used to endorse or promote products derived from
 *   this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <Arduino.h>
#include <SensirionI2CScd4x.h>
#include <Wire.h>
#include <Adafruit_NeoPixel.h>

SensirionI2CScd4x scd4x;
Adafruit_NeoPixel pixels = Adafruit_NeoPixel(4, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);

uint32_t get_co2_color(uint16_t co2);

const uint32_t WHITE = pixels.Color(255, 255, 255);
const uint32_t RED = pixels.Color(255, 0, 0);
const uint32_t GREEN = pixels.Color(0, 255, 0);
const uint32_t BLUE = pixels.Color(0, 0, 255);
const uint32_t YELLOW = pixels.Color(255, 255, 0);
const uint32_t ORANGE = pixels.Color(255, 128, 0);
const uint32_t MAGENTA = pixels.Color(255, 0, 255);
const uint32_t PURPLE = pixels.Color(128, 0, 128);
const uint32_t HOT_PINK = pixels.Color(255, 0, 128);

void setup() {
    uint16_t error;
    char errorMessage[256];

    // Pulling NeoPixel power pin low powers them.
    pinMode(NEOPIXEL_POWER, OUTPUT);
    digitalWrite(NEOPIXEL_POWER, LOW);

    pinMode(BUTTON_A, INPUT_PULLUP);
    pinMode(BUTTON_B, INPUT_PULLUP);
    pinMode(BUTTON_C, INPUT_PULLUP);
    pinMode(BUTTON_D, INPUT_PULLUP);

    // Turn off all pixels.
    pixels.begin();
    pixels.setBrightness(50);
    pixels.fill(0x000000);
    pixels.show();

    // Sweep a single purple pixel while waiting for serial.
    Serial.begin(115200);
    for (int i = 0; !Serial; i++) {
        pixels.fill(0x000000);
        pixels.setPixelColor(i % 4, 0xFF00FF);
        pixels.show();
        delay(100);
    }

    // Solid purple after serial.
    pixels.fill(0xFF00FF);
    pixels.show();

    Wire.begin();
    scd4x.begin(Wire);

    // The sensor may still be running periodic measurement, so make sure it
    // isn't before trying to start again.
    error = scd4x.stopPeriodicMeasurement();
    if (error) {
        Serial.print("Failed to stop periodic measurement: ");
        errorToString(error, errorMessage, 256);
        Serial.println(errorMessage);
    }

    error = scd4x.startPeriodicMeasurement();
    if (error) {
        Serial.print("Failed to start periodic measurement: ");
        errorToString(error, errorMessage, 256);
        Serial.println(errorMessage);
    }

    Serial.println("Waiting for first measurement...");
}

void loop() {
    static int waitingForFirst = 0;
    static long lastUpdate = millis();
    char errorMessage[256];
    uint16_t dataReady;
    int ret;

    long now = millis();
    long since_update = max(now, lastUpdate) - min(now, lastUpdate);

    // Sweep a single blue pixel while waiting for the first measurement.
    if (waitingForFirst >= 0) {
        pixels.fill(0x000000);
        pixels.setPixelColor(waitingForFirst % 4, 0x0000FF);
        pixels.show();
        waitingForFirst++;
        delay(100);
    }

    // Don't poll the SCD4x too often.
    if (waitingForFirst == -1 && since_update < 555) return;
    lastUpdate = now;

    // Wait for the next SCD4x data release.
    ret = scd4x.getDataReadyStatus(dataReady);
    if (ret) {
        errorToString(ret, errorMessage, sizeof(errorMessage));
        Serial.printf("Failed to check if SCD4x has data ready: %s\r\n", errorMessage);
        return;
    } else if ((dataReady & ((1 << 11) - 1)) == 0) {
        // From the datasheet:
        // > If the least significant 11 bits of
        // > word[0] are 0 → data not ready
        // > else → data ready for read-out
        return;
    }

    uint16_t co2;
    float temperature;
    float humidity;
    ret = scd4x.readMeasurement(co2, temperature, humidity);
    if (ret) {
        errorToString(ret, errorMessage, sizeof(errorMessage));
        Serial.printf("Failed to read SCD4x measurement: %s\r\n", errorMessage);
        return;
    } else if (co2 == 0) {
        Serial.println("Ignoring reading of 0 PPM CO2.");
        return;
    }

    // Stop sweep after first successful measurement.
    if (waitingForFirst >= 0) {
        waitingForFirst = -1;
    }

    pixels.fill(get_co2_color(co2));
    pixels.show();

    Serial.print("CO2:");
    Serial.print(co2);
    Serial.print("\t");
    Serial.print("Temperature:");
    Serial.print(temperature);
    Serial.print("\t");
    Serial.print("Humidity:");
    Serial.println(humidity);
}

// clang-format off
/*
 * 350-400 ppm     Normal background concentration in outdoor ambient air
 * 870 ppm         American Society of Heating, Refrigeration, and
 *                 Air-conditioning Engineers indoor steady-state
 *                 recommendation.
 * 400-1000 ppm    Well-ventilated occupied indoor space.
 * 900-2,000 ppm   Complaints of drowsiness and poor air.
 * 2,000-5,000 ppm Headaches, sleepiness and stagnant, stale, stuffy air.
 *                 Poor concentration, loss of attention, increased heart
 *                 rate and slight nausea may also be present.
 * 5,000           Workplace exposure limit (as 8-hour TWA) in most
 *                 jurisdictions.
 * >40,000 ppm     Immediately harmful due to oxygen deprivation.
 *
 * SCD30 datasheet lists its measurement range as 400 ppm – 10,000 ppm.
 * SCD40 datasheet lists its measurement range as 400 ppm - 2,000 ppm.
 *
 * Working from
 * https://www.dhs.wisconsin.gov/chemical/carbondioxide.htm
 * https://www.epa.gov/sites/default/files/2014-08/documents/base_3c2o2.pdf
 */
// clang-format on
uint32_t get_co2_color(uint16_t CO2) {
  // clang-format off
  if (CO2 < 350) return  WHITE;
  if (CO2 < 400) return  BLUE;
  if (CO2 < 870) return  GREEN;
  if (CO2 < 1000) return YELLOW;
  if (CO2 < 2000) return ORANGE;
  return                 MAGENTA;
  // clang-format on
}