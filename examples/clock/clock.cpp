// A little test program to mess around with functions, and understand how the
// libraries work.

// COMPILE TIME ERRORS
#ifndef BOARD_HAS_PSRAM
#error "Please enable PSRAM, Arduino IDE -> tools -> PSRAM -> OPI !!!"
#endif

// LIBRARIES
#include <Arduino.h> // Not sure yet if this is necessary, will check back later

// Epaper
#include "epd_driver.h"
#include "fonts/FontReg36.h"
#include "utilities.h"

// NTP
#include "sntp.h"
#include "time.h"
#include <WiFi.h>

// CONSTS
// const char* ssid       = "Dah0use";
// const char* password   = "Eto nash durackij wpA kluchik";

const char *ssid = "McDonkeys_2_4";
const char *password = "mdqfyephjb";

const char *ntpServer1 = "au.pool.ntp.org";
const char *ntpServer2 = "pool.ntp.org";
const long gmtOffset_sec = 3600 * 10;
const int daylightOffset_sec = 3600;

const char *time_zone = "AEST-10AEDT,M10.1.0,M4.1.0/3"; // Melbourne Time Zone

bool need_to_disconnect = false;
uint8_t *framebuffer;

// Prints the time saved on the ESP's RTC
void printLocalTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) { // Write into time struct using the ESP's
                                  // local RTC time?
    Serial.println("No time available (yet)");
    return;
  }

  // Print to serial
  Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");

  // Clear display
  epd_clear();
  delay(100); // Wait a bit for the display to clear

  // Create buffer for time string
  char timestr[64]; // adjust size as needed
  strftime(timestr, sizeof(timestr), "%H:%M", &timeinfo); // Succinct time

  memset(framebuffer, 255, EPD_WIDTH * EPD_HEIGHT / 2);

  const int frame_size = 108;
  const int char_width = 22;
  const int char_height = 36;

  Rect_t area = {
      .x = 50,
      .y = 50,
      .width = char_width,
      .height = char_height,
  };

  // Copy an '!' to the buffer
  for (int row = 0; row < char_height; row++) {
    for (int col = 0; col < char_width; col++) {
      int char_index = 1; // '!' character index in the font
      int byte_index = (char_index * frame_size) +
                       (row * ((char_width + 7) / 8)) + (col / 8);
      int bit_index = 7 - (col % 8);
      uint8_t byte = Font36_Table[byte_index];
      uint8_t bit = (byte >> bit_index) & 0x01;
      if (bit) {
        int buffer_index = (row * ((char_width + 1) / 2)) + (col / 2);
        framebuffer[buffer_index] &=
            ~(0x80 >> (col % 2)); // Set the corresponding bit to 0 (black)
      }
    }
  }
  epd_draw_image(area, framebuffer, DrawMode_t::BLACK_ON_WHITE);
}

// Callback function (get's called when time adjusts via NTP)
void timeavailable(struct timeval *t) {
  Serial.println("Got time adjustment from NTP!");
  printLocalTime();
  need_to_disconnect = true;
}

void setup() {
  // Serial Init
  const int end_wait = millis() + 1000;
  Serial.begin(115200);
  while (!Serial && millis() < end_wait) {
    delay(10);
  }

  Serial.println("Starting up...");

  // Epaper Display Init
  epd_init();

  // Allocate memory for frame buffer
  framebuffer =
      (uint8_t *)ps_calloc(sizeof(uint8_t), EPD_WIDTH * EPD_HEIGHT / 2);
  if (!framebuffer) {
    Serial.println("alloc memory failed !!!");
    while (1)
      ;
  }
  memset(framebuffer, 0xFF, EPD_WIDTH * EPD_HEIGHT / 2);

  // Reset Display
  epd_poweron();
  epd_clear();

  // NTP Init
  sntp_set_time_sync_notification_cb(
      timeavailable);      // set notification call-back function
  sntp_servermode_dhcp(1); // (optional)
  // configTime(gmtOffset_sec, daylightOffset_sec, ntpServer1, ntpServer2); //
  // Manual daylight savings offset
  configTzTime(time_zone, ntpServer1,
               ntpServer2); // (Hopefully) Automatic daylight savings offsetting

  // Connect to WiFi
  Serial.printf("Connecting to %s ", ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(200);
    Serial.print(".");
  }
  Serial.println(" CONNECTED");

  // Get and print time for the first time
  printLocalTime();

  // Calculate and await the initial syncing interval

  // Done
  Serial.println("Setup Complete!");
}

void loop() {
  if (need_to_disconnect) {
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
