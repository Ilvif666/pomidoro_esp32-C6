// Arduino_GFX-based Pomodoro timer for Waveshare ESP32-C6-LCD-1.47
// Uses the golden "R" in a circle as the splash / stopped screen.
// Integrated with working AXS5106L touch controller

#include <Arduino_GFX_Library.h>
#include <Wire.h>
#include "FreeSansBold24pt7b.h"  // Smooth font for the logo and titles
#include "esp_lcd_touch_axs5106l.h"

// Backlight pin (official: GPIO23 = LCD_BL)
#define GFX_BL 23

// Rotation (0 = portrait, like official demo)
#define ROTATION 0

// Official pins from ESP32-C6-Touch-LCD-1.47 scheme:
// LCD_CLK = GPIO1, LCD_DIN = GPIO2, LCD_CS = GPIO14, LCD_DC = GPIO15, LCD_RST = GPIO22
Arduino_DataBus *bus = new Arduino_HWSPI(15 /* DC */, 14 /* CS */, 1 /* SCK */, 2 /* MOSI */);

Arduino_GFX *gfx = new Arduino_ST7789(
  bus, 22 /* RST */, 0 /* rotation */, false /* IPS */,
  172 /* width */, 320 /* height */,
  34 /*col_offset1*/, 0 /*uint8_t row_offset1*/,
  34 /*col_offset2*/, 0 /*row_offset2*/);

// --- Low-level LCD init from Waveshare demo (unchanged) ---
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

// --- Color scheme (minimalistic black & gold + blue for rest) ---
const uint16_t COLOR_BLACK = 0x0000;
const uint16_t COLOR_GOLD  = 0xFCE0; // tuned golden
const uint16_t COLOR_BLUE  = 0x001F;

// --- Pomodoro logic ---
enum TimerState {
  STOPPED,
  RUNNING,
  PAUSED
};

const unsigned long POMODORO_DURATION = 25UL * 60UL * 1000UL; // 25 minutes
const unsigned long FLASH_DURATION    = 500;                  // ms
const unsigned long LONG_PRESS_MS     = 1000;                 // long press

// Touch pins from official Waveshare LVGL example
static const int TP_SDA = 18;
static const int TP_SCL = 19;
static const int TP_RST = 20;
static const int TP_INT = 21;

TimerState currentState = STOPPED;
unsigned long startTime = 0;
unsigned long pausedTime = 0;
unsigned long elapsedBeforePause = 0;
bool isWorkSession = true;
bool flashActive = false;
unsigned long flashStartTime = 0;
uint16_t flashColor = COLOR_GOLD;

// Touch detection variables
touch_data_t touch_points;
bool touchPressed = false;
unsigned long touchStartTime = 0;
bool longPressDetected = false;
static bool lastIntState = HIGH;
// Debounce variables for TP_INT
static unsigned long lastTpIntLowTime = 0;
static const unsigned long TP_INT_DEBOUNCE_MS = 200;  // Ignore brief HIGH pulses
// Protection against short tap after timer start
unsigned long timerStartTime = 0;
const unsigned long SHORT_TAP_BLOCK_MS = 1500;  // Block short taps for 1.5s after timer start

// --- Helper: draw golden "R" splash (used as stopped screen) ---
void drawSplash() {
  gfx->fillScreen(COLOR_BLACK);

  uint16_t golden = COLOR_GOLD;
  int16_t centerX = gfx->width() / 2;
  int16_t centerY = gfx->height() / 2;
  int16_t radius = 70;
  int16_t borderWidth = 5;

  for (int16_t i = 0; i < borderWidth; i++) {
    gfx->drawCircle(centerX, centerY, radius - i, golden);
  }

  gfx->setFont(&FreeSansBold24pt7b);
  gfx->setTextColor(golden);
  gfx->setTextSize(2, 2, 0);
  gfx->setCursor(centerX - 33, centerY + 30);
  gfx->print("R");
}

// --- Helper: centered text using getTextBounds ---
void drawCenteredText(const char *txt, int16_t cx, int16_t cy, uint16_t color, uint8_t size) {
  int16_t x1, y1;
  uint16_t w, h;
  gfx->setFont(nullptr);
  gfx->setTextSize(size, size, 0);
  gfx->getTextBounds(txt, 0, 0, &x1, &y1, &w, &h);
  int16_t x = cx - (int16_t)w / 2;
  int16_t y = cy + (int16_t)h / 2;
  gfx->setCursor(x, y);
  gfx->setTextColor(color);
  gfx->print(txt);
}

// --- Pomodoro control functions ---
void triggerFlash(uint16_t color) {
  flashActive = true;
  flashColor = color;
  flashStartTime = millis();
}

void displayStoppedState(); // forward
void drawTimer();           // forward
void drawProgressCircle(float progress, int centerX, int centerY, int radius);

void startTimer() {
  currentState = RUNNING;
  isWorkSession = true;
  startTime = millis();
  timerStartTime = millis();  // Track when timer started for short tap protection
  elapsedBeforePause = 0;
  triggerFlash(COLOR_GOLD);
}

void pauseTimer() {
  if (currentState == RUNNING) {
    currentState = PAUSED;
    pausedTime = millis();
    elapsedBeforePause = millis() - startTime;
  }
}

void resumeTimer() {
  if (currentState == PAUSED) {
    currentState = RUNNING;
    startTime = millis() - elapsedBeforePause;
  }
}

void stopTimer() {
  currentState = STOPPED;
  displayStoppedState();
}

void updateTimer() {
  if (currentState == RUNNING) {
    unsigned long elapsed = millis() - startTime;
    if (elapsed >= POMODORO_DURATION) {
      if (isWorkSession) {
        isWorkSession = false;
        triggerFlash(COLOR_BLUE);
        startTime = millis();
      } else {
        isWorkSession = true;
        triggerFlash(COLOR_GOLD);
        startTime = millis();
      }
    }
  }
}

// Read touch data directly from I2C (working method from test)
void readTouchData() {
  bool currentIntState = digitalRead(TP_INT);
  
  if (currentIntState == LOW && lastIntState == HIGH) {
    // Touch just started - read immediately
    delayMicroseconds(100);
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
  } else if (currentIntState == HIGH && lastIntState == HIGH) {
    // Only reset if we\'re sure touch is released (both current and last are HIGH)
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
          // touch_num is 0 in data, but TP_INT is still LOW - don't reset yet
          // Keep previous touch_num value to maintain touch state
        }
      } else {
        // I2C read failed, but TP_INT is still LOW - keep touch state
      }
    }
  }
  
  lastIntState = currentIntState;
}

void handleTouchInput() {
  // Read TP_INT with debouncing to filter out noise
  bool tpIntLow = (digitalRead(TP_INT) == LOW);
  static unsigned long lastTpIntHighTime = 0;
  
  if (tpIntLow) {
    lastTpIntLowTime = millis();
    // Reset HIGH time when we see LOW again
    lastTpIntHighTime = 0;
  } else {
    // TP_INT is HIGH - track when it went HIGH
    if (lastTpIntHighTime == 0) {
      lastTpIntHighTime = millis();
    }
  }
  
  // Consider touch active if:
  // 1. TP_INT is currently LOW, OR
  // 2. TP_INT was LOW recently (within debounce window), OR  
  // 3. TP_INT has been HIGH for less than debounce time (might be noise)
  bool currentlyTouched = tpIntLow || 
                          (millis() - lastTpIntLowTime < TP_INT_DEBOUNCE_MS) ||
                          (lastTpIntHighTime > 0 && (millis() - lastTpIntHighTime < TP_INT_DEBOUNCE_MS));
  
  // Read touch data for coordinates (but don't use it for state detection)
  readTouchData();

  if (currentlyTouched && !touchPressed) {
    Serial.println(">>> TOUCH PRESSED <<<");
    touchPressed = true;
    touchStartTime = millis();
    longPressDetected = false;
  } else if (!currentlyTouched && touchPressed) {
    unsigned long touchDuration = millis() - touchStartTime;
    Serial.print(">>> TOUCH RELEASED after ");
    Serial.print(touchDuration);
    Serial.println(" ms <<<");

    // Only process short tap if long press wasn't already handled
    // Reduced threshold from 50ms to 10ms for faster response
    // Also block short taps for a short period after timer start to prevent accidental pause
    unsigned long timeSinceStart = (timerStartTime > 0) ? (millis() - timerStartTime) : SHORT_TAP_BLOCK_MS + 1;
    bool blockShortTap = (timeSinceStart < SHORT_TAP_BLOCK_MS);
    
    if (!longPressDetected && touchDuration > 10 && !blockShortTap) {
      Serial.println("*** SHORT TAP detected! ***");
      if (currentState == RUNNING) {
        pauseTimer();
      } else if (currentState == PAUSED) {
        resumeTimer();
      }
    } else if (longPressDetected) {
      Serial.println("*** LONG PRESS was already handled ***");
    } else if (blockShortTap) {
      Serial.println("*** SHORT TAP blocked (too soon after timer start) ***");
    }
    touchPressed = false;
    longPressDetected = false;

  } else if (touchPressed) {
    unsigned long elapsed = millis() - touchStartTime;
    
    // Check for long press (works every time, not just first)
    if (elapsed > LONG_PRESS_MS && !longPressDetected) {
      longPressDetected = true;
      Serial.print("*** LONG PRESS detected! (");
      Serial.print(elapsed);
      Serial.println(" ms) ***");
      // Execute long press action immediately
      if (currentState == STOPPED) {
        Serial.println("-> Starting timer");
        startTimer();
      } else {
        Serial.println("-> Stopping timer");
        stopTimer();
      }
      // Reset start time to allow detecting next long press while still holding
      touchStartTime = millis();
      longPressDetected = false;  // Reset to allow next long press detection
    } else if (!longPressDetected) {
      // Debug: show progress towards long press
      static unsigned long lastProgress = 0;
      if (millis() - lastProgress > 200) {
        Serial.print("Holding... ");
        Serial.print(elapsed);
        Serial.print(" / ");
        Serial.print(LONG_PRESS_MS);
        Serial.println(" ms");
        lastProgress = millis();
      }
    }
  }
  static unsigned long lastDebug = 0;
  if (millis() - lastDebug > 2000) {
    Serial.print(digitalRead(TP_INT));
    Serial.print(touch_points.touch_num);
    Serial.println(touchPressed);
    lastDebug = millis();
  }
}

void updateDisplay() {
  if (flashActive) {
    if (millis() - flashStartTime < FLASH_DURATION) {
      gfx->fillScreen(flashColor);
    } else {
      flashActive = false;
      if (currentState == STOPPED) {
        displayStoppedState();
      } else {
        drawTimer();
      }
    }
    return;
  }

  if (currentState == STOPPED) {
    return;
  } else {
    // Only redraw if time changed (prevent flickering)
    static unsigned long lastDisplayedSeconds = 999;
    unsigned long elapsed = (currentState == RUNNING) ? (millis() - startTime) : elapsedBeforePause;
    unsigned long currentSeconds = (elapsed / 1000UL) % 60UL;
    
    if (currentSeconds != lastDisplayedSeconds || lastDisplayedSeconds == 999) {
      drawTimer();
      lastDisplayedSeconds = currentSeconds;
    }
  }
}

void drawTimer() {
  gfx->fillScreen(COLOR_BLACK);

  unsigned long elapsed = 0;
  if (currentState == RUNNING) {
    elapsed = millis() - startTime;
  } else if (currentState == PAUSED) {
    elapsed = elapsedBeforePause;
  }

  unsigned long remaining = (elapsed >= POMODORO_DURATION) ? 0 : (POMODORO_DURATION - elapsed);
  unsigned long minutes = remaining / 60000UL;
  unsigned long seconds = (remaining % 60000UL) / 1000UL;

  char timeStr[6];
  sprintf(timeStr, "%02lu:%02lu", minutes, seconds);

  float progress = (float)elapsed / (float)POMODORO_DURATION;
  if (progress < 0) progress = 0;
  if (progress > 1) progress = 1;
  
  // Draw circle first, then time inside it
  int centerX = gfx->width() / 2;
  int centerY = gfx->height() / 2;
  int radius = 70;  // Bigger circle
  
  drawProgressCircle(progress, centerX, centerY, radius);
  
  // Draw time inside the circle (moved up a bit)
  drawCenteredText(timeStr, centerX, centerY - 20, COLOR_GOLD, 3);

  const char *statusTxt = nullptr;
  uint16_t statusColor = COLOR_GOLD;
  if (currentState == PAUSED) {
    statusTxt = "PAUSED";
  } else if (isWorkSession) {
    statusTxt = "WORK";
  } else {
    statusTxt = "REST";
    statusColor = COLOR_BLUE;
  }
  drawCenteredText(statusTxt, gfx->width() / 2, gfx->height() - 30, statusColor, 2);  // Bigger text (size 2)
}

void drawProgressCircle(float progress, int centerX, int centerY, int radius) {
  // Draw circle border (thicker)
  for (int i = 0; i < 5; i++) {
    gfx->drawCircle(centerX, centerY, radius - i, COLOR_GOLD);
  }

  if (progress > 0 && progress <= 1.0f) {
    const int segments = 64;  // More segments for smoother circle
    for (int i = 0; i < segments * progress; i++) {
      float angle = (i * 2.0f * PI) / segments - PI / 2.0f;
      int x = centerX + radius * cosf(angle);
      int y = centerY + radius * sinf(angle);
      gfx->drawPixel(x, y, COLOR_GOLD);
    }
  }
}

void displayStoppedState() {
  drawSplash();
}

// --- Arduino setup / loop ---
void setup(void) {
  Serial.begin(115200);
  Serial.println("Pomodoro Timer (Arduino_GFX) starting...");

  if (!gfx->begin()) {
    Serial.println("gfx->begin() failed!");
  }

  lcd_reg_init();
  gfx->setRotation(ROTATION);
  gfx->fillScreen(COLOR_BLACK);

#ifdef GFX_BL
  pinMode(GFX_BL, OUTPUT);
  digitalWrite(GFX_BL, HIGH);
  Serial.println("Initializing I2C for touch...");
#endif

  // Init I2C for touch
  Serial.println("Initializing touch controller...");
  Wire.begin(TP_SDA, TP_SCL);
  delay(100);
  Serial.print("TP_INT pin state after init: ");
  Serial.println(digitalRead(TP_INT));
  Serial.println("Touch init complete. Ready for input.");
  
  // Init touch driver
  bsp_touch_init(&Wire, TP_RST, TP_INT, gfx->getRotation(), gfx->width(), gfx->height());
  pinMode(TP_INT, INPUT_PULLUP);

  displayStoppedState();
}

void loop() {
  handleTouchInput();
  updateTimer();
  updateDisplay();
  delay(5);
}