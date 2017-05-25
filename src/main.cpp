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
#define NUM_SAMPLES             20      // Samples get averaged to reduce noise.
#define SAMPLE_DELAY            10      // Milliseconds between samples.
#define READING_INTERVAL        60      // Seconds between readings.


// Hardware Settings
// - The data logging shield uses A4, A5, and digital pins 10, 11, 12, and 13.

#define NUM_THERMISTORS         4

#define THERMISTOR_1_PIN        0       // Analog pin
#define THERMISTOR_2_PIN        1       // Analog pin
#define THERMISTOR_3_PIN        2       // Analog pin
#define THERMISTOR_4_PIN        3       // Analog pin

const uint8_t thermistor_pins[NUM_THERMISTORS] = {
    THERMISTOR_1_PIN,
    THERMISTOR_2_PIN,
    THERMISTOR_3_PIN,
    THERMISTOR_4_PIN
};

#define THERMISTOR_SERIES_RES   10000
#define THERMISTOR_RES_NOM      10000   // Nominal resistance, R0.
#define THERMISTOR_B_COEFF      3950    // Beta coefficient of the thermistor.
#define THERMISTOR_TEMP_NOM     25      // Nominal temperature of R0.

#define BUTTON_1_PIN            2
#define BUTTON_2_PIN            3
#define SD_CARD_PIN             10
#define RTC_PIN_1               A4      // Analog pin
#define RTC_PIN_2               A5      // Analog pin
#define LCD_PIN_RS              4
#define LCD_PIN_EN              5
#define LCD_PIN_DB4             6
#define LCD_PIN_DB5             7
#define LCD_PIN_DB6             8
#define LCD_PIN_DB7             9

#define LCD_ROWS                2
#define LCD_COLUMNS             16

#define RTC_TYPE                RTC_DS1307


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
char temperature_string[4][8];
double latest_resistance[4];
double latest_temperature[4];

/*
    DISPLAY MODES (ALL WITH LOGGING)
    0: Idle
    1: Information (RAM free)
    2: RTC Editor
*/
uint8_t display_mode = 0;

uint16_t i, z; // 16-bit iterator
uint8_t timer = 0; // Counts seconds
uint32_t milli_timer = 0; // Counts time taken to do a loop
uint32_t uptime = 0;
uint8_t cursor = 0; // Maximum: 31 (second row, last column)

bool button_1 = false;
bool button_2 = false;


// Utility Methods

// Determine amount of free RAM.
// - Retrieved 2017-05-19 (https://playground.arduino.cc/Code/AvailableMemory)
int freeRAM() {
    extern int __heap_start, *__brkval;
    int v;
    return (int) &v - (__brkval == 0 ? (int) &__heap_start : (int) __brkval);
}

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
        }
    }
}

// Flush various logging buffers.
void log_flush() {
    if (SERIAL_LOGGING) {
        Serial.flush();
    }
    if (FILE_LOGGING) {
        log_file.flush();
    }
}

// Log an error message. Uses standard log method, then hangs forever.
void log_error(const char* msg, bool with_newline = true) {
    log(msg, with_newline);
    log_flush();
    while (true); // Loop forever
}

// Update the LCD to display latest values for the set display mode.
void update_display() {
    if (DISPLAY_ENABLED && lcd) {
        lcd->clear();
        cursor = 0;

        switch (display_mode) {
            case 1:     // Information
                lcd->print("Free RAM: ");
                lcd->print(freeRAM(), 10);
                lcd->noBlink();
                break;
            case 2:     // RTC Editor
                lcd->print("TBD");
                lcd->setCursor(0, 0);
                lcd->blink();
                break;
            case 0:     // Idle
            default:
                lcd->noBlink();
                break;
        }
    }
}

// Switch the display mode, triggering a display update.
void switch_display_mode(uint8_t m) {
    display_mode = m % 3;
    update_display();
}


// Data Methods

// Update the global formatted timestamp string with the contents of 'now'.
void update_formatted_timestamp() {
    sprintf(formatted_timestamp, "%04u-%02u-%02uT%02u:%02u:%02u", now.year(),
        now.month(), now.day(), now.hour(), now.minute(), now.second());
}

double resistance_to_temperature(double resistance) {
    // Formula: T = 1/(1/B * ln(R/R_0) + (1/T0)) - 273.15 (celcius)
    return 1 / ((log(resistance / THERMISTOR_RES_NOM) / THERMISTOR_B_COEFF) + 1
        / (THERMISTOR_TEMP_NOM + 273.15)) - 273.15;
}

void take_reading(uint8_t t) {
    now = rtc.now();

    latest_resistance[t] = 0;

    for (i = 0; i < NUM_SAMPLES; i++) {
        latest_resistance[t] += (double) analogRead(thermistor_pins[t]);
        delay(SAMPLE_DELAY);
    }

    // Formulas: R = sr / (1023 / mean_of_samples - 1)
    //           sr = thermistor series resistance

    latest_resistance[t] = THERMISTOR_SERIES_RES
        / (1023 / (latest_resistance[t] / NUM_SAMPLES) - 1); // Resistance
    latest_temperature[t] = resistance_to_temperature(latest_resistance[t]);

    // TODO: Error calculations
}

void save_reading_to_card() {
    if (data_file) {
        update_formatted_timestamp();
        for (i = 0; i < NUM_THERMISTORS; i++) {
            dtostrf(latest_temperature[i], 5, 2, temperature_string[i]);
        }

        log("Took reading: ", false);
        log(formatted_timestamp, false); log(",", false);
        log(temperature_string[0], false); log(",", false);
        log(temperature_string[1], false); log(",", false);
        log(temperature_string[2], false); log(",", false);
        log(temperature_string[3]);
        log_flush();

        data_file.print(formatted_timestamp); data_file.print(",");
        data_file.print(temperature_string[0]); data_file.print(",");
        data_file.print(temperature_string[1]); data_file.print(",");
        data_file.print(temperature_string[2]); data_file.print(",");
        data_file.println(temperature_string[3]);
        data_file.flush();
    }
}


// Main Methods

void setup() {
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

            // Need to add 48 to get ASCII number characters.
            log_file_name[4] = i / 100 + 48;
            log_file_name[5] = i / 10 % 10 + 48;
            log_file_name[6] = i % 10 + 48;

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
    log("Initializing RTC... ", false);
    Wire.begin();
    if (!rtc.begin()) {
        log_error("Failed.");
    }
    log("Done.");

    // INPUT RTC TIME
    if (SERIAL_LOGGING) {
        uint16_t year;
        uint8_t month, day, hour, minute, second;

        Serial.print("Change clock? (y/n) ");
        while (Serial.available() < 1);
        Serial.println();
        if (Serial.read() == 'y') {
            Serial.println();
            Serial.print("Enter Year: ");
            while (Serial.available() < 4);
            year += (Serial.read() - 48) * 1000;
            year += (Serial.read() - 48) * 100;
            year += (Serial.read() - 48) * 10;
            year += (Serial.read() - 48);
            Serial.println(year);

            Serial.print("Enter Month: ");
            while (Serial.available() < 2);
            month += (Serial.read() - 48) * 10;
            month += (Serial.read() - 48);
            Serial.println(month);

            Serial.print("Enter Day: ");
            while (Serial.available() < 2);
            day += (Serial.read() - 48) * 10;
            day += (Serial.read() - 48);
            Serial.println(day);

            Serial.print("Enter Hour: ");
            while (Serial.available() < 2);
            hour += (Serial.read() - 48) * 10;
            hour += (Serial.read() - 48);
            Serial.println(hour);

            Serial.print("Enter Minute: ");
            while (Serial.available() < 2);
            minute += (Serial.read() - 48) * 10;
            minute += (Serial.read() - 48);
            Serial.println(minute);

            Serial.print("Enter Second: ");
            while (Serial.available() < 2);
            second += (Serial.read() - 48) * 10;
            second += (Serial.read() - 48);
            Serial.println(second);

            rtc.adjust(DateTime(year, month, day, hour, minute, second));
        }
    }

    // SET UP DATA FILE
    log("Creating data file... ", false);
    char data_file_name[] = "dat_000.csv";
    for (i = 0; i < MAX_DATA_FILES; i++) {
        // Increment until we can find a data file slot.

        // Need to add 48 to get ASCII digit characters.
        data_file_name[4] = i / 100 + 48;
        data_file_name[5] = i / 10 % 10 + 48;
        data_file_name[6] = i % 10 + 48;

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
    data_file.println("Timestamp,Temp1,Temp2,Temp3,Temp4");
    data_file.flush();

    // SET UP LCD
    if (DISPLAY_ENABLED) {
        lcd = new LiquidCrystal(LCD_PIN_RS, LCD_PIN_EN, LCD_PIN_DB4,
            LCD_PIN_DB5, LCD_PIN_DB6, LCD_PIN_DB7);
        lcd->begin(LCD_COLUMNS, LCD_ROWS);

        update_display();
    }

    // SET UP BUTTONS
    pinMode(BUTTON_1_PIN, INPUT);
    pinMode(BUTTON_2_PIN, INPUT);

    // Finished everything!
    now = rtc.now();
    update_formatted_timestamp();
    log("Data logger started at ", false);
    log(formatted_timestamp, false);
    log(". Software version: ", false);
    log(VERSION);
    log_flush();
}

void loop() {
    // Time the loop to make sure it runs rounded to the nearest second.
    milli_timer = millis();
    if (timer >= READING_INTERVAL) {
        timer = 0;
        for (z = 0; z < NUM_THERMISTORS; z++) { // Loop through all thermistors
            take_reading(z);
        }
        save_reading_to_card();
    }

    button_1 = digitalRead(BUTTON_1_PIN);
    button_2 = digitalRead(BUTTON_2_PIN);

    if (button_1 && button_2) {
        switch_display_mode(++display_mode);
    } else if (button_1) {

    } else if (button_2) {
        cursor = (cursor + 1) % 32;
        lcd->setCursor(cursor % 16, cursor > 15 ? 1 : 0);
    }

    milli_timer = millis() - milli_timer;
    while (milli_timer >= 1000) {
        // Prevent an integer overflow error by making sure milli_timer < 1000
        timer++; // An extra second has occurred - don't let it slip away!
        milli_timer -= 1000;
    }
    timer++;
    uptime++;
    delay(1000 - milli_timer); // (Ideally) 1 second between loops
}
