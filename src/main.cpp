/******************************************************************************
 * Fake Frog                                                                  *
 * An Arduino-based project to build a frog-shaped temperature logger.        *
 * Author: David Lougheed. Copyright 2017.                                    *
 ******************************************************************************/


#define VERSION "0.1.0"


// Includes

#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal.h>

#include <SD.h>
#include <RTClib.h>


// Compile-Time Settings

#define SERIAL_LOGGING          true    // Log to the serial display for debug.
#define FILE_LOGGING            true    // Log to file on SD card. (recommended)
#define DISPLAY_ENABLED         true    // Show menus and information on an LCD.
#define NUM_SAMPLES             10      // Samples get averaged to reduce noise.
#define SAMPLE_DELAY            10      // Milliseconds between samples.
#define READING_INTERVAL        60      // Seconds between readings.


// Hardware Settings
// - The data logging shield uses A4, A5, and digital pins 10, 11, 12, and 13.

#define THERMISTOR_PIN          A0
#define THERMISTOR_SERIES_RES   10000
#define THERMISTOR_RES_NOM      10000   // Nominal resistance, R0.
#define THERMISTOR_B_COEFF      3950    // Beta coefficient of the thermistor.
#define THERMISTOR_TEMP_NOM     25      // Nominal temperature of R0.

#define SD_CARD_PIN             10
#define RTC_PIN_1               A4
#define RTC_PIN_2               A5
#define LCD_PIN_RS              4
#define LCD_PIN_EN              5
#define LCD_PIN_DB4             6
#define LCD_PIN_DB5             7
#define LCD_PIN_DB6             8
#define LCD_PIN_DB7             9

#define LCD_ROWS                2
#define LCD_COLUMNS             16

#define RTC_TYPE                RTC_PCF8523


// Other Compile-Time Constants

#define MAX_LOG_FILES           1000
#define MAX_DATA_FILES          1000


// Globals

bool serial_logging_started = false;

// - Files
File log_file;
File data_file;

// - Hardware Objects
RTC_TYPE rtc;
LiquidCrystal* lcd;

// - Data Point Variables
//   (to save memory, use global data point variables)
DateTime now;
char formatted_timestamp[] = "0000-00-00T00:00:00";
char* data_file_entry_buffer = (char*) malloc(sizeof(char) * 50);
double latest_resistance;
double latest_temperature;

/*
    DISPLAY MODES (ALL WITH LOGGING)
    0: Idle
    1: Information (clock, space usage)
    2: RTC Editor
*/
uint8_t display_mode = 0;

uint16_t i; // 16-bit iterator
uint8_t timer = 0; // Counts seconds


// Utility Methods

// Log a generic message.
void log(const char* msg, bool with_newline = true) {
    if (SERIAL_LOGGING) {
        if(!serial_logging_started) {
            Serial.begin(9600);
            Serial.println();
            serial_logging_started = true;
        }

        if (with_newline) {
            Serial.println(msg);
        } else {
            Serial.print(msg);
        }
    }

    if (FILE_LOGGING) {
        if (log_file) {
            if (with_newline) {
                log_file.println(msg);
            } else {
                log_file.print(msg);
            }
            log_file.flush();
        }
    }
}

// Log an error message. Uses standard log method, then hangs forever.
void log_error(const char* msg, bool with_newline = true) {
    log(msg, with_newline);
    while (true); // Loop forever
}

void update_screen() {
    if (DISPLAY_ENABLED && lcd) {
        lcd->clear();

        switch (display_mode) {
            case 1:
                lcd->print("TBD");
                break;
            case 2:
                lcd->print("TBD");
                break;
            case 0:
            default:
                break;
        }
    }
}

void switch_display_mode(uint8_t m) {
    display_mode = m;
    update_screen();
}


// Data Methods

// Update the global formatted timestamp string with the contents of 'now'.
void update_formatted_timestamp() {
    sprintf(formatted_timestamp, "%u-%u-%uT%u:%u:%u", now.year(),
        now.month(), now.day(), now.hour(), now.minute(), now.second());
}

double resistance_to_temperature(double resistance) {
    // Formula: T = 1/(1/B * ln(R/R_0) + (1/T0)) - 273.15 (celcius)
    return 1 / ((log(resistance / THERMISTOR_RES_NOM) / THERMISTOR_B_COEFF) + 1
        / (THERMISTOR_TEMP_NOM + 273.15)) - 273.15;
}

void take_reading() {
    now = rtc.now();

    latest_resistance = 0;

    for (i = 0; i < NUM_SAMPLES; i++) {
        latest_resistance += (double) analogRead(THERMISTOR_PIN);
        delay(SAMPLE_DELAY);
    }

    // Formulas: R = sr / (1023 / mean_of_samples - 1)
    //           sr = thermistor series resistance

    latest_resistance = THERMISTOR_SERIES_RES
        / (1023 / (latest_resistance / NUM_SAMPLES) - 1); // Resistance
    latest_temperature = resistance_to_temperature(latest_resistance);

    // TODO: Error calculations
}

void save_reading_to_card() {
    if (data_file) {
        update_formatted_timestamp();
        sprintf(data_file_entry_buffer, "%.2f,%s", latest_temperature,
            formatted_timestamp);
        data_file.println(data_file_entry_buffer);
        data_file.flush();
    }
}


// Main Methods

void setup() {
    // TODO: Set up pins

    // SET UP EXTERNAL ANALOG VOLTAGE REFERENCE
    // Typically from 3.3V Arduino supply. This reduces the voltage noise seen
    // from reading analog values.
    analogReference(EXTERNAL);

    // INITIALIZE SD CARD
    log("Initializing SD card... ", false);
    pinMode(SD_CARD_PIN, OUTPUT);
    if (!SD.begin()) {
        log_error("Failed.");
    }
    log("Done.");

    // SET UP LOG FILE
    if (FILE_LOGGING) {
        log("Creating log file... ", false);
        char log_file_name[] = "log_000.txt";
        for (i = 0; i < MAX_LOG_FILES; i++) {
            // Increment until we can find a log file slot.

            // Need to add 32 to get ASCII number characters.
            log_file_name[6] = i / 100 + 32;
            log_file_name[7] = i / 10 + 32;
            log_file_name[8] = i % 10 + 32;

            if (!SD.exists(log_file_name)) {
                log_file = SD.open(log_file_name, FILE_WRITE);
                break;
            }
        }
        if (log_file) {
            log("Done.");
        } else {
            log_error("Failed.");
        }
    }

    // SET UP RTC
    log("Initializing RTC...", false);
    Wire.begin();
    if (!rtc.begin()) {
        log_error("Failed.");
    }
    log("Done.");

    // TODO: Calibrate RTC

    // SET UP DATA FILE
    log("Creating data file...");
    char data_file_name[] = "data_000.csv";
    for (i = 0; i < MAX_DATA_FILES; i++) {
        // Increment until we can find a data file slot.

        // Need to add 32 to get ASCII digit characters.
        data_file_name[6] = i / 100 + 32;
        data_file_name[7] = i / 10 + 32;
        data_file_name[8] = i % 10 + 32;

        if (!SD.exists(data_file_name)) {
            data_file = SD.open(data_file_name, FILE_WRITE);
            break;
        }
    }
    if (data_file) {
        log("Done.");
    } else {
        log_error("Failed.");
    }

    // PRINT DATA FILE CSV HEADERS
    data_file.println("Timestamp,Temperature");
    data_file.flush();

    // SET UP LCD
    if (DISPLAY_ENABLED) {
        lcd = new LiquidCrystal(LCD_PIN_RS, LCD_PIN_EN, LCD_PIN_DB4,
            LCD_PIN_DB5, LCD_PIN_DB6, LCD_PIN_DB7);
        lcd->begin(LCD_COLUMNS, LCD_ROWS);
    }

    // Finished everything!
    now = rtc.now();
    update_formatted_timestamp();
    log("Data logger started at ", false);
    log(formatted_timestamp, false);
    log(". Software version: ", false);
    log(VERSION);
}

void loop() {
    if (timer == READING_INTERVAL) {
        timer = 0;
        take_reading();
        save_reading_to_card();
    }

    timer++;
    delay(1000);
}
