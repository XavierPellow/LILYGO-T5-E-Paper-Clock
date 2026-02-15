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
#include <esp_wifi.h>

// CONSTS
// const char* ssid       = "Dah0use";
// const char* password   = "Eto nash durackij wpA kluchik";

struct WifiCreds
{
  const char *ssid;
  const char *password;
};
const WifiCreds wifi_creds[] = {
    {"McDonkeys_2_4", "mdqfyephjb"},
    {"Dah0use", "Eto nash durackij wpA kluchik"},
};
// TODO: Have the program loop through them until it finds one it can connect to.
const char *ssid = "McDonkeys_2_4";
const char *password = "mdqfyephjb";

const char *ntpServer1 = "au.pool.ntp.org";
const char *ntpServer2 = "pool.ntp.org";

const char *time_zone = "AEST-10AEDT,M10.1.0,M4.1.0/3"; // Melbourne Time Zone

bool got_time_adjustment = false;
const uint64_t time_update_interval_us = 24 * 3600 * 1000000; // Update time every day

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
    ClearDisplay();
  }

  void ClearDisplay()
  {
    epd_clear_area_cycles({0, 0, EPD_WIDTH, EPD_HEIGHT}, 2, 20);
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

  // Draws the time centered on the display, using the provided tm struct.
  void DrawTimeCentered(tm &timeinfo)
  {
    // Create buffer for time string
    char timestr[64];                                       // adjust size as needed
    strftime(timestr, sizeof(timestr), "%H:%M", &timeinfo); // Succinct time

    memset(framebuffer, 255, EPD_WIDTH * EPD_HEIGHT / 2);

    DrawText(timestr);
    Update();
  }

  // Updates the display with the current contents of the framebuffer.
  // - Clears the display and redraws the framebuffer.
  void Update()
  {
    ClearDisplay();
    delay(100); // small delay to ensure clear area is processed before drawing
    epd_draw_image({0, 0, EPD_WIDTH, EPD_HEIGHT}, framebuffer, DrawMode_t::BLACK_ON_WHITE);
  }

protected:
  uint8_t *framebuffer;
  const uint32_t buf_width = EPD_WIDTH;
  const uint32_t buf_height = EPD_HEIGHT;
  const uint32_t y_offset = -24;     // How much to move all text (-'ve ^, +'ve v)
  const uint32_t colon_offset = -24; // How much to move a colon (-'ve ^, +'ve v)

  // Font info
  const uint32_t NUM_CHARS = 95;         // From 0x20 to 0x7E inclusive
  const sFONT &font = Font288;           // From FontReg288.h
  const Rect_t *bboxes = Font288_bboxes; // From FontReg288.h
  const int bytes_per_char = sizeof(Font288_Table) / NUM_CHARS;
} Display;

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
  sntp_servermode_dhcp(1);
  // esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);

  configTzTime(time_zone, ntpServer1, ntpServer2); // also calls _init()
}

void setupWiFi()
{
  Serial.printf("Connecting to %s ", ssid);
  WiFi.begin(ssid, password);
  if (WiFi.waitForConnectResult() != WL_CONNECTED)
  {
    Serial.println("Failed to connect via Wi-Fi!");
  }
  else
  {
    Serial.println("Connected!");
  }
}

// Connects to Wi-Fi, gets the time from NTP, then disconnects from Wi-Fi again to save power.
// - Returns OK if time was successfully obtained, TIMEOUT if we failed to get the time within the retry limit, or FAIL if we failed to connect to Wi-Fi.
// - Note: We can't use async callbacks as we want to sleep while waiting for the time, so we have to block and poll for the time instead.
Error updateTime()
{
  // Connect via Wi-Fi and NTP
  WiFi.begin(ssid, password);
  if (WiFi.waitForConnectResult() != WL_CONNECTED)
  {
    Serial.println("Failed to connect via Wi-Fi!");
    return Error::FAIL;
  }
  esp_sntp_init();

  // Get time
  time_t now = 0;
  struct tm timeinfo = {0};
  int retry = 0;
  const int retry_count = 10;

  while (timeinfo.tm_year < (2020 - 1900) && ++retry < retry_count)
  {
    delay(1000);
    time(&now);
    localtime_r(&now, &timeinfo);
  }

  // Disconnect from NTP and Wi-Fi to save power
  esp_sntp_stop();
  WiFi.disconnect(true);

  return (timeinfo.tm_year >= (2020 - 1900)) ? Error::OK : Error::TIMEOUT;
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
  // Get time
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo))
  { // Write into time struct using the ESP's
    // local RTC time?
    Serial.println("No time available (yet)");
    return;
  }

  // Update time when necessary
  static int next_time_update_us = 0;
  const auto current_time_us = esp_timer_get_time();
  if (current_time_us > next_time_update_us)
  {
    const auto err = updateTime();

    if (err != Error::OK)
    {
      Serial.printf("Failed to update time! Error code: %d\n", (int)err);
    }
    else
    {
      Serial.println("Time updated successfully!");
    }
    next_time_update_us = current_time_us + time_update_interval_us;
  }

  Display.DrawTimeCentered(timeinfo);

  // Sleep until just after the next minute.
  // +3: Adding a few seconds of buffer to make sure we wake up after
  // the minute changes, not before.
  const uint64_t wait_time_us = (60 - timeinfo.tm_sec + 3) * 1000000;
  esp_sleep_enable_timer_wakeup(wait_time_us);
  delay(1000); // ensure logs go through TODO: Remove

  esp_light_sleep_start(); // This pauses the CPU until the timer wakes it up,
                           // resuming from here when it wakes up.
}
