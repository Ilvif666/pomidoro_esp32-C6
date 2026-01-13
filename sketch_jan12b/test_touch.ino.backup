// Simple touch test using AXS5106L driver on Waveshare ESP32-C6-LCD-1.47
// Shows raw touch coordinates as circles and prints them to Serial.
//
// Display: Arduino_GFX + Arduino_HWSPI (same pins as pomodoro sketch)
// Touch:  AXS5106L via I2C (TP_SCL = GPIO19, TP_SDA = GPIO18, TP_INT = GPIO20, TP_RST = GPIO21)

#include <Arduino_GFX_Library.h>
#include <Wire.h>
#include "esp_lcd_touch_axs5106l.h"

#define GFX_BL 23
#define ROTATION 0

// Simple RGB565 color defines
const uint16_t BLACK = 0x0000;
const uint16_t WHITE = 0xFFFF;
const uint16_t GREEN = 0x07E0;

// Display pins (official Waveshare mapping) - same as рабочий пример
Arduino_DataBus *bus = new Arduino_HWSPI(15 /* DC */, 14 /* CS */, 1 /* SCK */, 2 /* MOSI */);
Arduino_GFX *gfx = new Arduino_ST7789(
  bus, 22 /* RST */, 0 /* rotation */, false /* IPS */,
  172 /* width */, 320 /* height */,
  34 /*col_offset1*/, 0 /*row_offset1*/,
  34 /*col_offset2*/, 0 /*row_offset2*/);

// Low-level LCD init (скопировано из рабочего демо)
void lcd_reg_init(void) {
  static const uint8_t init_operations[] = {
    BEGIN_WRITE,
    WRITE_COMMAND_8, 0x11,
    END_WRITE,
    DELAY, 120,

    BEGIN_WRITE,
    WRITE_C8_D16, 0xDF, 0x98, 0x53,
    WRITE_C8_D8, 0xB2, 0x23,

    WRITE_COMMAND_8, 0xB7,
    WRITE_BYTES, 4,
    0x00, 0x47, 0x00, 0x6F,

    WRITE_COMMAND_8, 0xBB,
    WRITE_BYTES, 6,
    0x1C, 0x1A, 0x55, 0x73, 0x63, 0xF0,

    WRITE_C8_D16, 0xC0, 0x44, 0xA4,
    WRITE_C8_D8, 0xC1, 0x16,

    WRITE_COMMAND_8, 0xC3,
    WRITE_BYTES, 8,
    0x7D, 0x07, 0x14, 0x06, 0xCF, 0x71, 0x72, 0x77,

    WRITE_COMMAND_8, 0xC4,
    WRITE_BYTES, 12,
    0x00, 0x00, 0xA0, 0x79, 0x0B, 0x0A, 0x16, 0x79, 0x0B, 0x0A, 0x16, 0x82,

    WRITE_COMMAND_8, 0xC8,
    WRITE_BYTES, 32,
    0x3F, 0x32, 0x29, 0x29, 0x27, 0x2B, 0x27, 0x28, 0x28, 0x26, 0x25, 0x17, 0x12, 0x0D, 0x04, 0x00,
    0x3F, 0x32, 0x29, 0x29, 0x27, 0x2B, 0x27, 0x28, 0x28, 0x26, 0x25, 0x17, 0x12, 0x0D, 0x04, 0x00,

    WRITE_COMMAND_8, 0xD0,
    WRITE_BYTES, 5,
    0x04, 0x06, 0x6B, 0x0F, 0x00,

    WRITE_C8_D16, 0xD7, 0x00, 0x30,
    WRITE_C8_D8, 0xE6, 0x14,
    WRITE_C8_D8, 0xDE, 0x01,

    WRITE_COMMAND_8, 0xB7,
    WRITE_BYTES, 5,
    0x03, 0x13, 0xEF, 0x35, 0x35,

    WRITE_COMMAND_8, 0xC1,
    WRITE_BYTES, 3,
    0x14, 0x15, 0xC0,

    WRITE_C8_D16, 0xC2, 0x06, 0x3A,
    WRITE_C8_D16, 0xC4, 0x72, 0x12,
    WRITE_C8_D8, 0xBE, 0x00,
    WRITE_C8_D8, 0xDE, 0x02,

    WRITE_COMMAND_8, 0xE5,
    WRITE_BYTES, 3,
    0x00, 0x02, 0x00,

    WRITE_COMMAND_8, 0xE5,
    WRITE_BYTES, 3,
    0x01, 0x02, 0x00,

    WRITE_C8_D8, 0xDE, 0x00,
    WRITE_C8_D8, 0x35, 0x00,
    WRITE_C8_D8, 0x3A, 0x05,

    WRITE_COMMAND_8, 0x2A,
    WRITE_BYTES, 4,
    0x00, 0x22, 0x00, 0xCD,

    WRITE_COMMAND_8, 0x2B,
    WRITE_BYTES, 4,
    0x00, 0x00, 0x01, 0x3F,

    WRITE_C8_D8, 0xDE, 0x02,

    WRITE_COMMAND_8, 0xE5,
    WRITE_BYTES, 3,
    0x00, 0x02, 0x00,

    WRITE_C8_D8, 0xDE, 0x00,
    WRITE_C8_D8, 0x36, 0x00,
    WRITE_COMMAND_8, 0x21,
    END_WRITE,

    DELAY, 10,

    BEGIN_WRITE,
    WRITE_COMMAND_8, 0x29,
    END_WRITE
  };
  bus->batchOperation(init_operations, sizeof(init_operations));
}

// Touch pins from official Waveshare LVGL example (04_lvgl_arduino_v8)
// Touch_I2C_SDA = 18, Touch_I2C_SCL = 19, Touch_RST = 20, Touch_INT = 21
static const int TP_SDA = 18;
static const int TP_SCL = 19;
static const int TP_RST = 20;
static const int TP_INT = 21;

touch_data_t touch_points;

void setup() {
  Serial.begin(115200);
  Serial.println("AXS5106L touch test starting...");

  // Init display
  if (!gfx->begin()) {
    Serial.println("gfx->begin() failed!");
  }
  lcd_reg_init(); // reuse same LCD init from main sketch
  gfx->setRotation(ROTATION);
  gfx->fillScreen(BLACK);

#ifdef GFX_BL
  pinMode(GFX_BL, OUTPUT);
  digitalWrite(GFX_BL, HIGH);
#endif

  // Init I2C for touch (same as official demo - no frequency specified)
  Serial.println("Initializing I2C for touch...");
  Wire.begin(TP_SDA, TP_SCL);
  delay(100);
  
  // Check I2C connection by scanning
  Serial.println("Scanning I2C bus...");
  byte error, address;
  int nDevices = 0;
  for(address = 1; address < 127; address++ ) {
    Wire.beginTransmission(address);
    error = Wire.endTransmission();
    if (error == 0) {
      Serial.print("I2C device found at address 0x");
      if (address<16) Serial.print("0");
      Serial.print(address,HEX);
      Serial.println("  !");
      nDevices++;
    }
  }
  if (nDevices == 0) {
    Serial.println("No I2C devices found!");
  } else {
    Serial.print("Found ");
    Serial.print(nDevices);
    Serial.println(" device(s)");
  }

  // Init touch driver (use gfx->getRotation() like official demo)
  Serial.println("Initializing touch controller...");
  Serial.print("TP_RST="); Serial.print(TP_RST);
  Serial.print(" TP_INT="); Serial.print(TP_INT);
  Serial.print(" rotation="); Serial.print(gfx->getRotation());
  Serial.print(" width="); Serial.print(gfx->width());
  Serial.print(" height="); Serial.println(gfx->height());
  
  bsp_touch_init(&Wire, TP_RST, TP_INT, gfx->getRotation(), gfx->width(), gfx->height());
  Serial.println("Touch init complete. Tap screen to test.");
  
  // Test if we can read touch interrupt pin
  pinMode(TP_INT, INPUT_PULLUP);
  Serial.print("TP_INT pin state: ");
  Serial.println(digitalRead(TP_INT));

  gfx->setTextColor(WHITE);
  gfx->setCursor(5, 5);
  gfx->println("Touch test: tap screen");
  gfx->setCursor(5, 20);
  gfx->print("I2C devices: ");
  gfx->println(nDevices);
  
  // Test drawing - draw a green circle at fixed position to verify drawing works
  gfx->fillCircle(86, 160, 15, GREEN);  // Center of screen (172/2, 320/2)
  gfx->setCursor(5, 35);
  gfx->setTextColor(GREEN);
  gfx->println("Test circle");
  Serial.println("Test green circle drawn at center (86, 160)");
}

void loop() {
  static bool lastIntState = HIGH;
  
  // Check interrupt pin state - read touch immediately when it goes LOW
  bool currentIntState = digitalRead(TP_INT);
  
  // If interrupt pin just went LOW (touch detected), read data immediately
  if (currentIntState == LOW && lastIntState == HIGH) {
    // Touch just started - read immediately
    delayMicroseconds(100); // Tiny delay for controller to prepare data
    // Read touch data directly from I2C (bypassing bsp_touch_read which has flag issues)
    Wire.beginTransmission(0x63);
    Wire.write(0x01);  // AXS5106L_TOUCH_DATA_REG
    if (Wire.endTransmission() == 0) {
      uint8_t bytesRead = Wire.requestFrom(0x63, 14);
      if (bytesRead >= 14) {
        uint8_t data[14];
        Wire.readBytes(data, 14);
        
        uint8_t touch_num = data[1];
        if (touch_num > 0 && touch_num <= 5) {
          // Parse coordinates manually (same as bsp_touch_read does)
          touch_points.touch_num = touch_num;
          for (uint8_t i = 0; i < touch_num; i++) {
            touch_points.coords[i].x = ((uint16_t)(data[2+i*6] & 0x0f)) << 8;
            touch_points.coords[i].x |= data[3+i*6];
            touch_points.coords[i].y = (((uint16_t)(data[4+i*6] & 0x0f)) << 8);
            touch_points.coords[i].y |= data[5+i*6];
            
            // Apply rotation transformation (same as bsp_touch_get_coordinates)
            uint16_t x = touch_points.coords[i].x;
            uint16_t y = touch_points.coords[i].y;
            switch (gfx->getRotation()) {
              case 1:
                touch_points.coords[i].y = x;
                touch_points.coords[i].x = y;
                break;
              case 2:
                touch_points.coords[i].x = x;
                touch_points.coords[i].y = gfx->height() - 1 - y;
                break;
              case 3:
                touch_points.coords[i].y = gfx->height() - 1 - x;
                touch_points.coords[i].x = gfx->width() - 1 - y;
                break;
              default:
                touch_points.coords[i].x = gfx->width() - 1 - x;
                touch_points.coords[i].y = y;
                break;
            }
          }
        } else {
          touch_points.touch_num = 0;
        }
      }
    }
  } else if (currentIntState == HIGH) {
    // Touch released
    touch_points.touch_num = 0;
  } else if (currentIntState == LOW && lastIntState == LOW) {
    // Touch still active - read again for continuous tracking
    Wire.beginTransmission(0x63);
    Wire.write(0x01);
    if (Wire.endTransmission() == 0) {
      uint8_t bytesRead = Wire.requestFrom(0x63, 14);
      if (bytesRead >= 14) {
        uint8_t data[14];
        Wire.readBytes(data, 14);
        uint8_t touch_num = data[1];
        if (touch_num > 0 && touch_num <= 5) {
          touch_points.touch_num = touch_num;
          for (uint8_t i = 0; i < touch_num; i++) {
            touch_points.coords[i].x = ((uint16_t)(data[2+i*6] & 0x0f)) << 8;
            touch_points.coords[i].x |= data[3+i*6];
            touch_points.coords[i].y = (((uint16_t)(data[4+i*6] & 0x0f)) << 8);
            touch_points.coords[i].y |= data[5+i*6];
            
            uint16_t x = touch_points.coords[i].x;
            uint16_t y = touch_points.coords[i].y;
            switch (gfx->getRotation()) {
              case 1:
                touch_points.coords[i].y = x;
                touch_points.coords[i].x = y;
                break;
              case 2:
                touch_points.coords[i].x = x;
                touch_points.coords[i].y = gfx->height() - 1 - y;
                break;
              case 3:
                touch_points.coords[i].y = gfx->height() - 1 - x;
                touch_points.coords[i].x = gfx->width() - 1 - y;
                break;
              default:
                touch_points.coords[i].x = gfx->width() - 1 - x;
                touch_points.coords[i].y = y;
                break;
            }
          }
        } else {
          touch_points.touch_num = 0;
        }
      }
    }
  }
  
  lastIntState = currentIntState;

  // Removed debug I2C test to reduce Serial lag

  // Use our directly read touch_points if touch_num > 0, otherwise try bsp_touch_get_coordinates
  bool hasTouch = (touch_points.touch_num > 0);
  
  if (!hasTouch) {
    hasTouch = bsp_touch_get_coordinates(&touch_points);
  }
  
  if (hasTouch) {
    // Removed Serial output to reduce lag - touch is working!
    
    for (uint8_t i = 0; i < touch_points.touch_num; i++) {
      uint16_t x = touch_points.coords[i].x;
      uint16_t y = touch_points.coords[i].y;

      // Use coordinates as-is, clamp to screen bounds
      int16_t drawX = x;
      int16_t drawY = y;
      
      if (drawX < 0) drawX = 0;
      if (drawX >= gfx->width()) drawX = gfx->width() - 1;
      if (drawY < 0) drawY = 0;
      if (drawY >= gfx->height()) drawY = gfx->height() - 1;

      // Draw only circle (removed squares and debug output to reduce lag)
      gfx->fillCircle(drawX, drawY, 12, GREEN);
    }
  }

  delay(5); // Reduced delay for faster response
}

