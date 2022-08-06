#include <Arduino.h>
#include <SensirionI2CScd4x.h>
#include <Wire.h>
#include <Adafruit_NeoPixel.h>
#include <Debouncer.h>
#include "Adafruit_ThinkInk.h"

#define EPD_DC      7 
#define EPD_CS      8
#define EPD_BUSY    -1
#define SRAM_CS     -1 
#define EPD_RESET   6

SensirionI2CScd4x scd4x;
Adafruit_NeoPixel pixels = Adafruit_NeoPixel(4, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);
ThinkInk_290_Grayscale4_T5 display(EPD_DC, EPD_RESET, EPD_CS, SRAM_CS, EPD_BUSY);

const int debounce_ms = 100;

Debouncer button_a(debounce_ms);
Debouncer button_b(debounce_ms);
Debouncer button_c(debounce_ms);
Debouncer button_d(debounce_ms);

uint32_t get_co2_color(uint16_t co2);

const uint32_t OFF = pixels.Color(0, 0, 0);
const uint32_t WHITE = pixels.Color(255, 255, 255);
const uint32_t RED = pixels.Color(255, 0, 0);
const uint32_t GREEN = pixels.Color(0, 255, 0);
const uint32_t BLUE = pixels.Color(0, 0, 255);
const uint32_t YELLOW = pixels.Color(255, 255, 0);
const uint32_t ORANGE = pixels.Color(255, 128, 0);
const uint32_t MAGENTA = pixels.Color(255, 0, 255);
const uint32_t PURPLE = pixels.Color(128, 0, 128);
const uint32_t HOT_PINK = pixels.Color(255, 0, 128);

const uint8_t startup_brightness = 128;
const uint8_t running_brightness = 10;

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
    pixels.setBrightness(startup_brightness);
    pixels.fill(OFF);
    pixels.show();

    // Sweep a single purple pixel while waiting for serial.
    long wait_start = millis();
    Serial.begin(115200);
    for (int i = 0; !Serial; i++) {
        pixels.fill(OFF);
        pixels.setPixelColor(i % 4, PURPLE);
        pixels.show();
        delay(100);

        // Time out after 2 seconds.
        if (millis() - wait_start > 2000) break;
    }

    // Solid purple after serial.
    pixels.fill(PURPLE);
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

        // Without a periodic measurement started it won't get a measurement.
        // Loop indefinitely and blink red pixels.
        while (true) {
            pixels.fill(RED);
            pixels.show();
            delay(500);
            pixels.fill(OFF);
            pixels.show();
            delay(500);
        }
    }

    display.begin(THINKINK_MONO);
    display.clearBuffer();
    display.display();

    Serial.println("Waiting for first measurement...");
}

void loop() {
    static int waitingForFirst = 0;
    static long lastUpdate = millis();
    static long lastDisplay = millis();
    static bool lights = true;
    static bool display_every = false;
    char errorMessage[256];
    uint16_t dataReady;
    static uint16_t co2;
    float temperature;
    float humidity;
    int ret;

    bool got_first_measurement = waitingForFirst == -1;
    long now = millis();
    long since_update = max(now, lastUpdate) - min(now, lastUpdate);

    // Toggle lights on button A release.
    if (button_a.update(digitalRead(BUTTON_A)) && !button_a.get()) {
        lights = !lights;
        if (lights) {
            if (got_first_measurement) {
                // brightness 0 is destructive, so reset the color.
                pixels.setBrightness(running_brightness);
                pixels.fill(get_co2_color(co2));
            } else {
                pixels.setBrightness(startup_brightness);
            }
        } else {
            pixels.setBrightness(0);
        }
        pixels.show();
    }

    // Toggle displaying every update on button D release.
    if (button_d.update(digitalRead(BUTTON_D)) && !button_d.get()) {
        display_every = !display_every;
    }

    // Sweep a single blue pixel while waiting for the first measurement.
    if (!got_first_measurement) {
        pixels.fill(OFF);
        pixels.setPixelColor(waitingForFirst % 4, BLUE);
        pixels.show();
        waitingForFirst++;
        delay(100);
    }

    // Don't poll the SCD4x too often.
    if (got_first_measurement && since_update < 555) return;
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

    ret = scd4x.readMeasurement(co2, temperature, humidity);
    if (ret) {
        errorToString(ret, errorMessage, sizeof(errorMessage));
        Serial.printf("Failed to read SCD4x measurement: %s\r\n", errorMessage);
        return;
    } else if (co2 == 0) {
        Serial.println("Ignoring reading of 0 PPM CO2.");
        return;
    }

    // Stop sweep and dim after first successful measurement.
    if (!got_first_measurement) {
        waitingForFirst = -1;
        if (lights) {
            pixels.setBrightness(running_brightness);
        }
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

    // Update the display for the first measurement, or if showing every update,
    // or after a minute since the last update.
    if (!got_first_measurement || display_every || millis() - lastDisplay > 60000) {
        static uint16_t previous_co2 = 0;
        display.clearBuffer();
        display.setTextColor(EPD_BLACK);

        display.setTextSize(12);
        display.setCursor(0, 15);
        display.print(co2);

        display.setTextSize(3);
        display.setCursor(296 - (3*18), 128 - 24);
        display.print("ppm");

        if (previous_co2 != co2) {
            display.display();
        }
        previous_co2 = co2;
        lastDisplay = millis();
    }
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