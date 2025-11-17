// A little test program to mess around with functions, and understand how the libraries work.

// COMPILE TIME ERRORS
#ifndef BOARD_HAS_PSRAM
#error "Please enable PSRAM, Arduino IDE -> tools -> PSRAM -> OPI !!!"
#endif

// LIBRARIES
#include <Arduino.h> // Not sure yet if this is necessary, will check back later
// Epaper
#include "epd_driver.h"
#include "utilities.h"
#include "firasans.h"

// NTP
#include <WiFi.h>
#include "time.h"
#include "sntp.h"

// CONSTS
// const char* ssid       = "Dah0use";
// const char* password   = "Eto nash durackij wpA kluchik";

const char *ssid = "McDonkeys";
const char *password = "mdqfyephjb";

const char *ntpServer1 = "au.pool.ntp.org";
const char *ntpServer2 = "pool.ntp.org";
const long gmtOffset_sec = 3600 * 10;
const int daylightOffset_sec = 3600;

const char *time_zone = "AEST-10AEDT,M10.1.0,M4.1.0/3"; // Melbourne Time Zone
const GFXfont *font = &FiraSans;
// Based on font_properties_default()
const FontProperties font_props{
    .fg_color = 0,
    .bg_color = 15,
    .fallback_glyph = 0,
    .flags = 0,
};

bool need_to_disconnect = false;

// VARS
// Starting Cursor Position
int32_t cursor_x = 20;
int32_t cursor_y = 160;

// Frame buffer
uint8_t *framebuffer;

// Starting Time
// inital_hour = &timeinfo.tm_hour;
// inital_min = &timeinfo.tm_min;
// inital_sec = &timeinfo.tm_sec;

// FUNCTIONS
// Callback function (get's called when time adjusts via NTP)
void timeavailable(struct timeval *t)
{
    Serial.println("Got time adjustment from NTP!");
    printLocalTime();
    need_to_disconnect = true;
}

// Prints the time saved on the ESP's RTC
void printLocalTime()
{
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo))
    { // Write into time struct using the ESP's local RTC time?
        Serial.println("No time available (yet)");
        return;
    }

    // Print to serial
    Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");

    // Clear display
    epd_clear();
delay(100); // Wait a bit for the display to clear

    // Create buffer for time string
    char buffer[64]; // adjust size as needed
    // strftime(buffer, sizeof(buffer), "%A, %B %d %Y %H:%M:%S", &timeinfo);
    strftime(buffer, sizeof(buffer), "%H:%M", &timeinfo); // Succinct time format

    // Find the text's center position
    int32_t base_pos = 0;
    int32_t x = 0, y = 0, w = 0, h = 0;

    get_text_bounds(font, buffer, &base_pos, &base_pos, &x, &y, &w, &h, &font_props);
    Serial.println("Text bounds:");
    Serial.printf("x: %d, y: %d, w: %d, h: %d\n", x, y, w, h);

    cursor_x = (EPD_WIDTH - w) / 2;
    cursor_y = (EPD_HEIGHT) / 2;

    // Print to display
    writeln(font, buffer, &cursor_x, &cursor_y, NULL); // Note: writeln will update cursor_x and cursor_y
}

void setup()
{
    // Serial Init
    Serial.begin(115200);
    while (!Serial)
        ;

    // Epaper Display Init
    epd_init();

    // Allocate memory for frame buffer
    framebuffer = (uint8_t *)ps_calloc(sizeof(uint8_t), EPD_WIDTH * EPD_HEIGHT / 2);
    if (!framebuffer)
    {
        Serial.println("alloc memory failed !!!");
        while (1)
            ;
    }
    memset(framebuffer, 0xFF, EPD_WIDTH * EPD_HEIGHT / 2);

    // Reset Display
    epd_poweron();
    epd_clear();

    // NTP Init
    sntp_set_time_sync_notification_cb(timeavailable); // set notification call-back function
    sntp_servermode_dhcp(1);                           // (optional)
    // configTime(gmtOffset_sec, daylightOffset_sec, ntpServer1, ntpServer2); // Manual daylight savings offset
    configTzTime(time_zone, ntpServer1, ntpServer2); // (Hopefully) Automatic daylight savings offsetting

    // Connect to WiFi
    Serial.printf("Connecting to %s ", ssid);
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");
    }
    Serial.println(" CONNECTED");

    // Get and print time for the first time
    printLocalTime();

    // Calculate and await the initial syncing interval

    // Done
    Serial.println("Setup Complete!");
}

void loop()
{
    if (need_to_disconnect)
    {
        WiFi.disconnect(true);
        Serial.println("WiFi Disconnected!");
        need_to_disconnect = false;
    }

    delay(60000);
    printLocalTime(); // it will take some time to sync time :)

    // Serial.println("Starting light sleep");
    // esp_light_sleep_start();
    // Serial.println("Exit light sleep");
}
