// A little test program to mess around with functions, and understand how the
// libraries work.

// COMPILE TIME ERRORS
#ifndef BOARD_HAS_PSRAM
#error "Please enable PSRAM, Arduino IDE -> tools -> PSRAM -> OPI !!!"
#endif

// LIBRARIES
// Epaper
#include "epd_driver.h"
#include "fonts/FontReg288.h"
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

bool got_time_adjustment = false;
uint8_t *framebuffer;

// Font info
const sFONT &font = Font288; // From FontReg288.h
const int bytes_per_char = sizeof(Font288_Table) / 95;
// 95 characters in the font file (from 0x20 to 0x7E)

void copyCharToBuffer(char c, size_t x, size_t y, size_t buf_width,
                      size_t buf_height, uint8_t *buffer) {
  if (c < 32) {
    return; // unsupported glyph
  }

  const size_t char_index = (size_t)(c - 32);

  const size_t font_bytes_per_row = (font.width + 7) >> 3;
  const size_t fb_bytes_per_row = (buf_width + 1) >> 1;

  const uint8_t *glyph = &font.table[char_index * bytes_per_char];

  for (size_t row = 0; row < font.height; row++) {
    size_t dst_y = y + row;
    if (dst_y >= buf_height) {
      break; // vertical clip
    }

    const uint8_t *font_row = glyph + row * font_bytes_per_row;

    uint8_t *fb_row = buffer + dst_y * fb_bytes_per_row;

    size_t col = 0;

    for (size_t b = 0; b < font_bytes_per_row; b++) {
      uint8_t bits = font_row[b];

      // Up to 8 pixels per font byte
      for (size_t i = 0; i < 8 && col < font.width; i++, col++) {
        size_t dst_x = x + col;
        if (dst_x >= buf_width) {
          bits <<= 1;
          continue; // horizontal clip
        }

        if (bits & 0x80) {
          size_t fb_index = dst_x >> 1;
          uint8_t mask = (dst_x & 1) ? 0x07 : 0x70;
          fb_row[fb_index] &= ~mask;
        }

        bits <<= 1;
      }
    }
  }
}

void copyStringToBuffer(const char *str, size_t x, size_t y, size_t buf_width,
                        size_t buf_height, uint8_t *buffer) {
  while (*str) {
    copyCharToBuffer(*str, x, y, buf_width, buf_height, buffer);
    x += font.width;
    str++;
  }
}

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

  Rect_t area = {
      .x = 50,
      .y = 50,
      .width = font.width * 5,
      .height = font.height,
  };

  copyStringToBuffer(timestr, 0, 0, font.width * 5, font.height, framebuffer);
  epd_draw_image(area, framebuffer, DrawMode_t::BLACK_ON_WHITE);
}

// Callback function (get's called when time adjusts via NTP)
void timeavailable(struct timeval *t) { got_time_adjustment = true; }

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
  if (got_time_adjustment) {
    Serial.println("Got time adjustment from NTP!");
    printLocalTime();
    WiFi.disconnect(true);
    Serial.println("WiFi Disconnected!");
    got_time_adjustment = false;
  }

  delay(60000);
  printLocalTime(); // it will take some time to sync time :)

  // Serial.println("Starting light sleep");
  // esp_light_sleep_start();
  // Serial.println("Exit light sleep");
}
