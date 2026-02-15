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

// Error codes, based on ESP-IDF's esp_err_t
enum class Error
{
  FAIL = -1,
  OK = 0,
  NO_MEM = 0x101,
  INVALID_ARG = 0x102,
  INVALID_STATE = 0x103,
  INVALID_SIZE = 0x104,
  NOT_FOUND = 0x105,
  NOT_SUPPORTED = 0x106,
  TIMEOUT = 0x107,
  INVALID_RESPONSE = 0x108,
  INVALID_CRC = 0x109,
  INVALID_VERSION = 0x10a,
  INVALID_MAC = 0x10b,
  NOT_FINISHED = 0x10c,
  NOT_ALLOWED = 0x10d,
};

class EPDDisplay
{
public:
  void setup()
  {
    epd_init();

    // Allocate memory for frame buffer
    framebuffer =
        (uint8_t *)ps_calloc(sizeof(uint8_t), EPD_WIDTH * EPD_HEIGHT / 2);
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
  }

  // this is straight up vibe coded im ngl
  // Draws a single character onto the framebuffer at the specified position.
  Error DrawChar(char c, size_t x, size_t y)
  {
    if (c < 32)
    {
      return Error::INVALID_ARG; // unsupported glyph
    }

    const size_t char_index = (size_t)(c - 32);

    const size_t font_bytes_per_row = (font.width + 7) >> 3;
    const size_t fb_bytes_per_row = (buf_width + 1) >> 1;

    const uint8_t *glyph = &font.table[char_index * bytes_per_char];

    for (size_t row = 0; row < font.height; row++)
    {
      size_t dst_y = y + row;
      if (dst_y >= buf_height)
      {
        break; // vertical clip
      }

      const uint8_t *font_row = glyph + row * font_bytes_per_row;

      uint8_t *fb_row = framebuffer + dst_y * fb_bytes_per_row;

      size_t col = 0;

      for (size_t b = 0; b < font_bytes_per_row; b++)
      {
        uint8_t bits = font_row[b];

        // Up to 8 pixels per font byte
        for (size_t i = 0; i < 8 && col < font.width; i++, col++)
        {
          size_t dst_x = x + col;
          if (dst_x >= buf_width)
          {
            bits <<= 1;
            continue; // horizontal clip
          }

          if (bits & 0x80)
          {
            size_t fb_index = dst_x >> 1;
            uint8_t mask = (dst_x & 1) ? 0x07 : 0x70;
            fb_row[fb_index] &= ~mask;
          }

          bits <<= 1;
        }
      }
    }

    return Error::OK;
  }

  // Draws a string onto the framebuffer at the specified position.
  // - Uses bounding boxes to place characters more tightly together
  // - If x or y is 0, centers the text in that dimension
  void DrawText(const char *str, uint32_t x = -1, uint32_t y = -1, uint32_t inter_char_spacing = 20)
  {
    size_t char_count = 0;
    if (x == -1)
    {
      // Calculate total width of the text using bounding boxes
      uint32_t total_width = 0;
      for (const char *s = str; *s; s++)
      {
        if (*s >= 32 && *s < 127)
        {
          total_width += bboxes[*s - 32].width + inter_char_spacing;
        }
      }
      total_width -= inter_char_spacing; // Remove extra spacing after the last character
      x = (EPD_WIDTH - total_width) / 2;
    }
    y = y_offset +
        ((y == (uint32_t)-1)
             ? (EPD_HEIGHT - font.height) / 2
             : y);

    for (const char *s = str; *s; s++)
    {
      // Move colons up by colon_offset for better visual alignment
      const uint32_t used_y = (*s == ':')
                                  ? (y + colon_offset)
                                  : y;
      DrawChar(*s, x, used_y);

      // Move x for the next character, using the bounding box width for tighter spacing
      x += bboxes[*s - 32].width + inter_char_spacing;
    }
  }

  // Prints the time saved on the ESP's RTC, and also draws it on the display centered.
  void DrawTimeCentered(tm &timeinfo)
  {
    // Print to serial
    Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");

    // Create buffer for time string
    char timestr[64];                                       // adjust size as needed
    strftime(timestr, sizeof(timestr), "%H:%M", &timeinfo); // Succinct time

    memset(framebuffer, 255, EPD_WIDTH * EPD_HEIGHT / 2);

    DrawText(timestr);
    Update();
  }

  void Update()
  {
    epd_clear();
    delay(100); // small delay to ensure clear area is processed before drawing
    epd_draw_image({0, 0, EPD_WIDTH, EPD_HEIGHT}, framebuffer, DrawMode_t::BLACK_ON_WHITE);
  }

protected:
  uint8_t *framebuffer;
  uint32_t buf_width = EPD_WIDTH;
  uint32_t buf_height = EPD_HEIGHT;
  uint32_t y_offset = -24;     // How much to move all text (-'ve ^, +'ve v)
  uint32_t colon_offset = -24; // How much to move a colon (-'ve ^, +'ve v)

  // Font info
  const uint32_t NUM_CHARS = 95;         // From 0x20 to 0x7E inclusive
  const sFONT &font = Font288;           // From FontReg288.h
  const Rect_t *bboxes = Font288_bboxes; // From FontReg288.h
  const int bytes_per_char = sizeof(Font288_Table) / NUM_CHARS;
} Display;
// Callback function (get's called when time adjusts via NTP)
void timeavailable(struct timeval *t) { got_time_adjustment = true; }

// Initializes Serial and waits for a short period to ensure it's ready before
// proceeding.
void setupSerial(const int wait_time_ms = 1000)
{
  const int end_wait = millis() + wait_time_ms;
  Serial.begin(115200);
  while (!Serial && millis() < end_wait)
  {
    delay(10);
  }
}

void setupNTP()
{
  sntp_set_time_sync_notification_cb(
      timeavailable);      // set notification call-back function
  sntp_servermode_dhcp(1); // (optional)
  // configTime(gmtOffset_sec, daylightOffset_sec, ntpServer1, ntpServer2); //
  // Manual daylight savings offset
  configTzTime(time_zone, ntpServer1,
               ntpServer2); // (Hopefully) Automatic daylight savings offsetting
}

void setupWiFi()
{
  Serial.printf("Connecting to %s ", ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(200);
    Serial.print(".");
  }
  Serial.println(" CONNECTED");
}

void setup()
{
  setupSerial();
  delay(5000); // Guarentee we have a way of getting back in in case we sleep
               // forever...
  Serial.println("Setup start...");

  Display.setup();
  setupNTP();
  setupWiFi();

  // Get and print time for the first time
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo))
  {
    Display.DrawTimeCentered(timeinfo);
  }

  // Done
  Serial.println("Setup complete!");
}

void loop()
{
  if (got_time_adjustment)
  {
    Serial.println("Got time adjustment from NTP!");
    // printLocalTime();
    WiFi.disconnect(true);
    Serial.println("WiFi Disconnected!");
    got_time_adjustment = false;
  }

  // Get time
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo))
  { // Write into time struct using the ESP's
    // local RTC time?
    Serial.println("No time available (yet)");
    return;
  }

  Display.DrawTimeCentered(timeinfo);

  // Sleep until just after the next minute.
  // +3: Adding a few seconds of buffer to make sure we wake up after
  // the minute changes, not before.
  const uint64_t wait_time_us = (60 - timeinfo.tm_sec + 3) * 1000000;
  esp_sleep_enable_timer_wakeup(wait_time_us);
  esp_light_sleep_start(); // This pauses the CPU until the timer wakes it up,
                           // resuming from here when it wakes up.
}
