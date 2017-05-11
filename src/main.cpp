/*******************************************************************************
 * Fake Frog                                                                   *
 * An Arduino-based project to build a frog-shaped temperature logger.         *
 * Author: David Lougheed. Copyright 2017.                                     *
 ******************************************************************************/

// Includes
#include <Arduino.h>
#include <Wire.h>
#include <SD.h>
#include <RTClib.h>


// Compile-Time Settings
#define SERIAL_LOGGING  1
#define FILE_LOGGING    1


// Compile-Time Constants
#define SD_CARD_PIN     10
#define RTC_PIN_1       4
#define RTC_PIN_2       5
#define MAX_LOG_FILES   1000
#define MAX_DATA_FILES  1000


// Globals
bool serial_logging_started = false;

File log_file;
File data_file;

RTC_PCF8523 rtc;

// To save memory, use global data point variables.
DateTime now;
char formatted_timestamp[] = "0000-00-00T00:00:00";
char* data_file_entry_buffer = (char*) malloc(sizeof(char) * 50);
unsigned int latest_temperature;

uint16_t i; // 16-bit iterator


// Utility Methods

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
        if (!log_file) {
            if (with_newline) {
                log_file.println(msg);
            } else {
                log_file.print(msg);
            }
        }
    }
}

void log_error(const char* msg, bool with_newline = true) {
    log(msg, with_newline);
    while (true); // Loop forever
}


// Data Methods

void update_formatted_timestamp() {
    sprintf(formatted_timestamp, "%u-%u-%uT%u:%u:%u", now.year(),
        now.month(), now.day(), now.hour(), now.minute(), now.second());
}

void take_reading() {
    now = rtc.now();
    // TODO: Take a temperature reading
}

void save_reading_to_card() {
    if (data_file) {
        update_formatted_timestamp();
        sprintf(data_file_entry_buffer, "%u,%s", latest_temperature,
            formatted_timestamp);
        data_file.println(data_file_entry_buffer);
    }
}


// Main Methods

void setup() {
    // TODO: Set up pins

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
        char log_file_name[] = "log_file_000.txt";
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
    char data_file_name[] = "data_file_000.csv";
    for (i = 0; i < MAX_DATA_FILES; i++) {
        // Increment until we can find a data file slot.

        // Need to add 32 to get ASCII number characters.
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

    // Finished everything!
    now = rtc.now();
    update_formatted_timestamp();
    log("Data logger started at ", false);
    log(formatted_timestamp);
}

void loop() {
    // TODO: (Optional) Exit sleep

    take_reading();
    save_reading_to_card();

    // TODO: (Optional) Enter sleep

    delay(1000);
}
