// Arduino_GFX-based Pomodoro timer for Waveshare ESP32-C6-LCD-1.47
// Uses the golden "R" in a circle as the splash / stopped screen.
// Integrated with working AXS5106L touch controller

#include <Arduino_GFX_Library.h>
#include <Wire.h>
#include <math.h>
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
const uint16_t COLOR_GREEN = 0x07E0;

// --- Pomodoro logic ---
enum TimerState {
  STOPPED,
  RUNNING,
  PAUSED
};

enum PomodoroMode {
  MODE_1_1,    // 1 minute work, 1 minute rest (for testing)
  MODE_25_5,   // 25 minutes work, 5 minutes rest (standard)
  MODE_50_10   // 50 minutes work, 10 minutes rest (extended)
};

const unsigned long POMODORO_DURATION_1 = 1UL * 60UL * 1000UL;   // 1 minute
const unsigned long POMODORO_DURATION_25 = 25UL * 60UL * 1000UL;  // 25 minutes
const unsigned long POMODORO_DURATION_50 = 50UL * 60UL * 1000UL; // 50 minutes
const unsigned long REST_DURATION_1 = 1UL * 60UL * 1000UL;        // 1 minute
const unsigned long REST_DURATION_5 = 5UL * 60UL * 1000UL;        // 5 minutes
const unsigned long REST_DURATION_10 = 10UL * 60UL * 1000UL;      // 10 minutes
const unsigned long FLASH_DURATION    = 500;                  // ms
const unsigned long LONG_PRESS_MS     = 1000;                 // long press

// Touch pins from official Waveshare LVGL example
static const int TP_SDA = 18;
static const int TP_SCL = 19;
static const int TP_RST = 20;
static const int TP_INT = 21;

TimerState currentState = STOPPED;
PomodoroMode currentMode = MODE_25_5;  // Default to standard 25/5 mode
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
// Display optimization - track what needs redrawing
static bool displayInitialized = false;
static char lastTimeStr[6] = "";
static TimerState lastDisplayedState = STOPPED;  // Track state changes for status update
// Time display mode: false = MM:SS, true = MM only
static bool showMinutesOnly = false;
static bool lastShowMinutesOnly = false;  // Track mode changes for redraw

// Status button bounds (for pause/start button at bottom)
static int16_t statusBtnLeft = 0;
static int16_t statusBtnRight = 0;
static int16_t statusBtnTop = 0;
static int16_t statusBtnBottom = 0;
static bool statusBtnValid = false;

// Mode button bounds (for mode switch button at top)
static int16_t modeBtnLeft = 0;
static int16_t modeBtnRight = 0;
static int16_t modeBtnTop = 0;
static int16_t modeBtnBottom = 0;
static bool modeBtnValid = false;
static PomodoroMode lastDisplayedMode = MODE_25_5;  // Track mode changes

// Last known touch position (for button hit-test & indicator)
static int16_t lastTouchX = 0;
static int16_t lastTouchY = 0;
static bool lastTouchValid = false;

// Tap indicator (small green circle) to show taps
static bool tapIndicatorActive = false;
static unsigned long tapIndicatorStart = 0;
static int16_t tapIndicatorX = 0;
static int16_t tapIndicatorY = 0;
const unsigned long TAP_INDICATOR_DURATION = 500;  // ms

// Tap indicator radius (used for drawing / visual size)
static const int TAP_RADIUS = 4;  // Twice smaller than before (was 8)

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
  // Center text vertically: cy is the desired center, y1 is offset from baseline (usually negative)
  // cursorY + y1 + h/2 = cy, so cursorY = cy - y1 - h/2
  int16_t y = cy - y1 - (int16_t)h / 2;
  gfx->setCursor(x, y);
  gfx->setTextColor(color);
  gfx->print(txt);
}

// --- Pomodoro control functions ---
// Helper function to get current UI color based on work/rest session
uint16_t getCurrentUIColor() {
  return isWorkSession ? COLOR_GOLD : COLOR_BLUE;
}

void displayStoppedState(); // forward
void drawTimer();           // forward
void drawProgressCircle(float progress, int centerX, int centerY, int radius, uint16_t color);

void startTimer() {
  currentState = RUNNING;
  isWorkSession = true;
  startTime = millis();
  timerStartTime = millis();  // Track when timer started for short tap protection
  elapsedBeforePause = 0;
  displayInitialized = false;  // Force full redraw on timer start
  // Removed flash effect - no yellow screen on start
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
  displayInitialized = false;  // Force full redraw on next display
  displayStoppedState();
}

// Helper function to get current duration based on mode
unsigned long getCurrentDuration() {
  if (isWorkSession) {
    switch (currentMode) {
      case MODE_1_1:  return POMODORO_DURATION_1;
      case MODE_25_5: return POMODORO_DURATION_25;
      case MODE_50_10: return POMODORO_DURATION_50;
      default: return POMODORO_DURATION_25;
    }
  } else {
    switch (currentMode) {
      case MODE_1_1:  return REST_DURATION_1;
      case MODE_25_5: return REST_DURATION_5;
      case MODE_50_10: return REST_DURATION_10;
      default: return REST_DURATION_5;
    }
  }
}

void updateTimer() {
  if (currentState == RUNNING) {
    unsigned long elapsed = millis() - startTime;
    unsigned long duration = getCurrentDuration();
    if (elapsed >= duration) {
      if (isWorkSession) {
        isWorkSession = false;
        startTime = millis();
        displayInitialized = false;  // Force redraw to update colors
      } else {
        isWorkSession = true;
        startTime = millis();
        displayInitialized = false;  // Force redraw to update colors
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

  // Capture last valid touch position whenever a touch is active
  if (touch_points.touch_num > 0) {
    lastTouchX = touch_points.coords[0].x;
    lastTouchY = touch_points.coords[0].y;
    lastTouchValid = true;
  }

  if (currentlyTouched && !touchPressed) {
    Serial.println(">>> TOUCH PRESSED <<<");
    touchPressed = true;
    touchStartTime = millis();
    longPressDetected = false;
    // (optional) could capture initial touch position here if needed later
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
      // Base position for this tap (use last valid touch, as TP_INT may already be HIGH)
      int16_t tx = lastTouchValid ? lastTouchX : -1;
      int16_t ty = lastTouchValid ? lastTouchY : -1;

      // Always draw tap indicator if we have a valid position
      if (lastTouchValid && tx >= 0 && ty >= 0) {
        tapIndicatorX = tx;
        tapIndicatorY = ty;
        tapIndicatorActive = true;
        tapIndicatorStart = millis();
      }

      // Check for mode button click first
      bool inModeButton = false;
      if (modeBtnValid && lastTouchValid && tx >= 0 && ty >= 0) {
        if (tx >= modeBtnLeft && tx <= modeBtnRight &&
            ty >= modeBtnTop  && ty <= modeBtnBottom) {
          inModeButton = true;
        }
      }
      
      // Check for status button click
      bool inStatusButton = false;
      if (statusBtnValid && lastTouchValid && tx >= 0 && ty >= 0) {
        if (tx >= statusBtnLeft && tx <= statusBtnRight &&
            ty >= statusBtnTop  && ty <= statusBtnBottom) {
          inStatusButton = true;
        }
      }
      
      // Check for tap inside the timer circle (to toggle MM:SS <-> MM display)
      bool inCircle = false;
      if (lastTouchValid && tx >= 0 && ty >= 0 && (currentState == RUNNING || currentState == PAUSED)) {
        int16_t centerX = gfx->width() / 2;
        int16_t centerY = gfx->height() / 2;
        int16_t radius = 70;
        int16_t dx = tx - centerX;
        int16_t dy = ty - centerY;
        int16_t distSquared = dx * dx + dy * dy;
        if (distSquared <= radius * radius) {
          inCircle = true;
        }
      }

      if (inModeButton) {
        // Cycle through modes: 1/1 -> 25/5 -> 50/10 -> 1/1
        Serial.println("*** MODE BUTTON CLICKED ***");
        PomodoroMode oldMode = currentMode;
        switch (currentMode) {
          case MODE_1_1:
            currentMode = MODE_25_5;
            Serial.println("-> Switched to 25/5 mode");
            break;
          case MODE_25_5:
            currentMode = MODE_50_10;
            Serial.println("-> Switched to 50/10 mode");
            break;
          case MODE_50_10:
            currentMode = MODE_1_1;
            Serial.println("-> Switched to 1/1 mode");
            break;
        }
        // Force immediate mode button update
        lastDisplayedMode = oldMode;
        updateDisplay();
      } else if (inCircle) {
        // Toggle time display mode (MM:SS <-> MM)
        Serial.println("*** CIRCLE TAPPED - TOGGLE TIME DISPLAY MODE ***");
        showMinutesOnly = !showMinutesOnly;
        Serial.print("-> Switched to ");
        Serial.println(showMinutesOnly ? "MM only" : "MM:SS");
        // Force immediate time display update
        lastShowMinutesOnly = !showMinutesOnly;  // Force redraw
        strcpy(lastTimeStr, "");  // Clear last time string to force redraw
        updateDisplay();
      } else if (inStatusButton && (currentState == RUNNING || currentState == PAUSED)) {
        Serial.println("*** STATUS BUTTON CLICKED ***");
        // Save old state before changing
        TimerState oldState = currentState;
        if (currentState == RUNNING) {
          pauseTimer();
        } else { // PAUSED
          resumeTimer();
        }
        // Force immediate button update by setting lastDisplayedState to old state
        // This ensures updateDisplay() will detect the change and redraw the button
        lastDisplayedState = oldState;
        // Force immediate display update
        updateDisplay();
      } else {
        // Tap outside button area — только индикатор
        Serial.println("*** SHORT TAP ignored (outside button) ***");
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
  if (currentState == STOPPED) {
    return;
  } else {
    // Update display exactly once per second for smooth timer
    static unsigned long lastDisplayUpdate = 0;
    unsigned long now = millis();
    
    // Update every 1000ms (1 second) exactly
    if (now - lastDisplayUpdate >= 1000) {
      drawTimer();
      lastDisplayUpdate = now;  // Use current time, not lastDisplayUpdate + 1000, to prevent drift
    }
  }
}

void drawTimer() {
  unsigned long elapsed = 0;
  if (currentState == RUNNING) {
    elapsed = millis() - startTime;
  } else if (currentState == PAUSED) {
    elapsed = elapsedBeforePause;
  }

  unsigned long duration = getCurrentDuration();
  unsigned long remaining = (elapsed >= duration) ? 0 : (duration - elapsed);
  unsigned long minutes = remaining / 60000UL;
  unsigned long seconds = (remaining % 60000UL) / 1000UL;

  // Format time string based on display mode
  char timeStr[10];
  if (showMinutesOnly) {
    sprintf(timeStr, "%02lu", minutes);  // MM only
  } else {
    sprintf(timeStr, "%02lu:%02lu", minutes, seconds);  // MM:SS
  }

  float progress = (float)elapsed / (float)duration;
  if (progress < 0) progress = 0;
  if (progress > 1) progress = 1;
  
  int centerX = gfx->width() / 2;
  int centerY = gfx->height() / 2;
  int radius = 70;
  
  // Get current UI color based on work/rest session
  uint16_t uiColor = getCurrentUIColor();
  
  // Only redraw everything on first call or if state changed
  if (!displayInitialized) {
    gfx->fillScreen(COLOR_BLACK);
    drawProgressCircle(progress, centerX, centerY, radius, uiColor);
    displayInitialized = true;
    
    const char *statusTxt = nullptr;
    uint16_t statusColor = uiColor;
    // Status button text: when running -> "pause", when paused -> "start"
    if (currentState == PAUSED) {
      statusTxt = "start";
    } else if (currentState == RUNNING) {
      statusTxt = "pause";
    } else if (isWorkSession) {
      statusTxt = "work";
    } else {
      statusTxt = "rest";
    }
    int16_t statusCenterX = gfx->width() / 2;
    int16_t statusY = gfx->height() - 30;

    // First compute text bounds to define the button rectangle
    int16_t x1, y1;
    uint16_t w, h;
    gfx->setFont(nullptr);
    gfx->setTextSize(3, 3, 0);
    gfx->getTextBounds(statusTxt, 0, 0, &x1, &y1, &w, &h);

    int padding = 6;
    statusBtnLeft   = statusCenterX - (int16_t)w / 2 - padding;
    statusBtnRight  = statusCenterX + (int16_t)w / 2 + padding;
    statusBtnTop    = statusY - (int16_t)h - padding;
    statusBtnBottom = statusY + padding;

    // Draw 1-pixel border around button
    gfx->drawRect(statusBtnLeft, statusBtnTop,
                  statusBtnRight - statusBtnLeft,
                  statusBtnBottom - statusBtnTop,
                  statusColor);

    // Now draw the status text centered inside the button rectangle
    int16_t btnCenterY = (statusBtnTop + statusBtnBottom) / 2;
    drawCenteredText(statusTxt, statusCenterX, btnCenterY, statusColor, 3);
    statusBtnValid = true;
    lastDisplayedState = currentState;  // Initialize state tracking
    
    // Draw mode button at the top
    const char *modeTxt = nullptr;
    switch (currentMode) {
      case MODE_1_1:  modeTxt = "1/1"; break;
      case MODE_25_5: modeTxt = "25/5"; break;
      case MODE_50_10: modeTxt = "50/10"; break;
      default: modeTxt = "25/5"; break;
    }
    int16_t modeCenterX = gfx->width() / 2;
    // Calculate modeY so that top margin equals bottom margin (24px)
    // statusBtnBottom = height() - 30 + 6 = height() - 24, so bottom margin = 24
    // modeBtnTop should be 24, so modeY = 24 + h + padding
    gfx->setFont(nullptr);
    gfx->setTextSize(3, 3, 0);
    gfx->getTextBounds(modeTxt, 0, 0, &x1, &y1, &w, &h);
    
    padding = 4;
    int16_t topMargin = 24;  // Same as bottom margin (height() - 30 + 6 = 24)
    int16_t modeY = topMargin + (int16_t)h + padding;  // Position so top margin = 24px
    modeBtnLeft   = modeCenterX - (int16_t)w / 2 - padding;
    modeBtnRight  = modeCenterX + (int16_t)w / 2 + padding;
    modeBtnTop    = topMargin;  // 24px from top
    modeBtnBottom = modeY + padding;
    
    // Draw 1-pixel border around mode button
    gfx->drawRect(modeBtnLeft, modeBtnTop,
                  modeBtnRight - modeBtnLeft,
                  modeBtnBottom - modeBtnTop,
                  uiColor);
    
    // Draw mode text centered inside the button
    int16_t modeBtnCenterY = (modeBtnTop + modeBtnBottom) / 2;
    drawCenteredText(modeTxt, modeCenterX, modeBtnCenterY, uiColor, 3);
    modeBtnValid = true;
    lastDisplayedMode = currentMode;
    
    // Draw initial time text
    uint8_t textSize = showMinutesOnly ? 5 : 3;
    drawCenteredText(timeStr, centerX, centerY, uiColor, textSize);
    strcpy(lastTimeStr, timeStr);
    lastShowMinutesOnly = showMinutesOnly;
  } else {
    // Update progress circle - update more frequently for smoother animation
    drawProgressCircle(progress, centerX, centerY, radius, uiColor);
  }
  
  // Update time text if it changed or display mode changed
  if (strcmp(timeStr, lastTimeStr) != 0 || showMinutesOnly != lastShowMinutesOnly) {
    // Clear the text area inside the circle by filling a rectangle
    // Circle radius is 70, so we fill a safe rectangle inside it (100x60 pixels)
    int16_t textAreaWidth = 100;
    int16_t textAreaHeight = 60;
    int16_t textAreaLeft = centerX - textAreaWidth / 2;
    int16_t textAreaTop = centerY - textAreaHeight / 2;
    gfx->fillRect(textAreaLeft, textAreaTop, textAreaWidth, textAreaHeight, COLOR_BLACK);
    
    // Draw new time with current UI color and appropriate size
    uint8_t textSize = showMinutesOnly ? 5 : 3;  // Larger text for MM only mode
    drawCenteredText(timeStr, centerX, centerY, uiColor, textSize);
    strcpy(lastTimeStr, timeStr);
    lastShowMinutesOnly = showMinutesOnly;
  }
  
  // Update status text if state changed
  if (currentState != lastDisplayedState) {
    // Determine new status text and color
    const char *statusTxt = nullptr;
    uint16_t statusColor = uiColor;  // Use current UI color (gold for work, blue for rest)
    if (currentState == PAUSED) {
      statusTxt = "start";
    } else if (currentState == RUNNING) {
      statusTxt = "pause";
    } else if (isWorkSession) {
      statusTxt = "work";
    } else {
      statusTxt = "rest";
    }
    
    // Erase old status by drawing it in black (if we had one)
    if (lastDisplayedState != STOPPED) {
      const char *oldStatusTxt = nullptr;
      if (lastDisplayedState == PAUSED) {
        oldStatusTxt = "start";
      } else if (lastDisplayedState == RUNNING) {
        oldStatusTxt = "pause";
      } else if (isWorkSession) {
        oldStatusTxt = "work";
      } else {
        oldStatusTxt = "rest";
      }
      drawCenteredText(oldStatusTxt, gfx->width() / 2, gfx->height() - 30, COLOR_BLACK, 3);
    }
    
    // Draw new status and update button bounds
    int16_t statusCenterX = gfx->width() / 2;
    int16_t statusY = gfx->height() - 30;

    int16_t x1, y1;
    uint16_t w, h;
    gfx->setFont(nullptr);
    gfx->setTextSize(3, 3, 0);
    gfx->getTextBounds(statusTxt, 0, 0, &x1, &y1, &w, &h);

    int padding = 6;
    statusBtnLeft   = statusCenterX - (int16_t)w / 2 - padding;
    statusBtnRight  = statusCenterX + (int16_t)w / 2 + padding;
    statusBtnTop    = statusY - (int16_t)h - padding;
    statusBtnBottom = statusY + padding;

    // Erase old button area completely
    gfx->fillRect(statusBtnLeft, statusBtnTop,
                  statusBtnRight - statusBtnLeft,
                  statusBtnBottom - statusBtnTop,
                  COLOR_BLACK);

    // Draw new border
    gfx->drawRect(statusBtnLeft, statusBtnTop,
                  statusBtnRight - statusBtnLeft,
                  statusBtnBottom - statusBtnTop,
                  statusColor);

    // Draw text centered inside the button (по вертикали середина кнопки)
    int16_t btnCenterY = (statusBtnTop + statusBtnBottom) / 2;
    drawCenteredText(statusTxt, statusCenterX, btnCenterY, statusColor, 3);
    statusBtnValid = true;
    lastDisplayedState = currentState;
  }
  
  // Update mode button if mode changed
  if (currentMode != lastDisplayedMode) {
    const char *modeTxt = nullptr;
    switch (currentMode) {
      case MODE_1_1:  modeTxt = "1/1"; break;
      case MODE_25_5: modeTxt = "25/5"; break;
      case MODE_50_10: modeTxt = "50/10"; break;
      default: modeTxt = "25/5"; break;
    }
    
    // Erase old mode button if it existed
    if (modeBtnValid && lastDisplayedMode != MODE_25_5) {
      const char *oldModeTxt = nullptr;
      switch (lastDisplayedMode) {
        case MODE_1_1:  oldModeTxt = "1/1"; break;
        case MODE_25_5: oldModeTxt = "25/5"; break;
        case MODE_50_10: oldModeTxt = "50/10"; break;
        default: oldModeTxt = "25/5"; break;
      }
      gfx->fillRect(modeBtnLeft, modeBtnTop,
                    modeBtnRight - modeBtnLeft,
                    modeBtnBottom - modeBtnTop,
                    COLOR_BLACK);
    }
    
    int16_t modeCenterX = gfx->width() / 2;
    // Calculate modeY so that top margin equals bottom margin (24px)
    int16_t x1, y1;
    uint16_t w, h;
    gfx->setFont(nullptr);
    gfx->setTextSize(3, 3, 0);
    gfx->getTextBounds(modeTxt, 0, 0, &x1, &y1, &w, &h);
    
    int padding = 4;
    int16_t topMargin = 24;  // Same as bottom margin (height() - 30 + 6 = 24)
    int16_t modeY = topMargin + (int16_t)h + padding;  // Position so top margin = 24px
    modeBtnLeft   = modeCenterX - (int16_t)w / 2 - padding;
    modeBtnRight  = modeCenterX + (int16_t)w / 2 + padding;
    modeBtnTop    = topMargin;  // 24px from top
    modeBtnBottom = modeY + padding;
    
    // Erase old button area
    gfx->fillRect(modeBtnLeft, modeBtnTop,
                  modeBtnRight - modeBtnLeft,
                  modeBtnBottom - modeBtnTop,
                  COLOR_BLACK);
    
    // Draw new border
    gfx->drawRect(modeBtnLeft, modeBtnTop,
                  modeBtnRight - modeBtnLeft,
                  modeBtnBottom - modeBtnTop,
                  uiColor);
    
    // Draw text
    int16_t modeBtnCenterY = (modeBtnTop + modeBtnBottom) / 2;
    drawCenteredText(modeTxt, modeCenterX, modeBtnCenterY, uiColor, 3);
    modeBtnValid = true;
    lastDisplayedMode = currentMode;
  }
}

void drawProgressCircle(float progress, int centerX, int centerY, int radius, uint16_t color) {
  static float lastProgress = -1.0f;
  static bool circleDrawn = false;
  static uint16_t lastColor = COLOR_GOLD;
  int borderWidth = 5;
  
  // Redraw full circle if color changed (work <-> rest transition)
  if (lastColor != color) {
    circleDrawn = false;
    lastProgress = -1.0f;  // Force full redraw
    lastColor = color;
  }
  
  // Only redraw full circle on first call or if progress reset (timer restarted)
  if (!circleDrawn || progress < lastProgress || lastProgress < 0) {
    // Draw the full circle border with current color
    for (int16_t i = 0; i < borderWidth; i++) {
      gfx->drawCircle(centerX, centerY, radius - i, color);
    }
    circleDrawn = true;
    if (progress < lastProgress || lastProgress < 0) {
      lastProgress = 0.0f;  // Reset on timer restart
    }
  }
  
  // Only erase the newly elapsed portion (smooth incremental update)
  if (progress > lastProgress && lastProgress >= 0) {
    // Calculate how many new segments to erase
    const int segments = 720;  // More segments for smoother progress (2 per degree)
    int lastSegmentsErased = (int)(segments * lastProgress);
    int currentSegmentsErased = (int)(segments * progress);
    
    // Erase only the newly elapsed portion to avoid flickering
    for (int i = lastSegmentsErased; i < currentSegmentsErased; i++) {
      float angle = (i * 2.0f * PI) / segments - PI / 2.0f;  // Start from top
      // Erase all border layers
      for (int thickness = 0; thickness < borderWidth; thickness++) {
        int currentRadius = radius - thickness;
        if (currentRadius < 0) continue;
        
        // Erase a small arc segment to catch all circle pixels
        for (float angleOffset = -0.015f; angleOffset <= 0.015f; angleOffset += 0.005f) {
          float currentAngle = angle + angleOffset;
          int x = centerX + currentRadius * cosf(currentAngle);
          int y = centerY + currentRadius * sinf(currentAngle);
          gfx->drawPixel(x, y, COLOR_BLACK);
          // Erase neighboring pixels to ensure complete erasure
          gfx->drawPixel(x + 1, y, COLOR_BLACK);
          gfx->drawPixel(x - 1, y, COLOR_BLACK);
          gfx->drawPixel(x, y + 1, COLOR_BLACK);
          gfx->drawPixel(x, y - 1, COLOR_BLACK);
        }
      }
    }
  }
  
  lastProgress = progress;
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

  // Tap indicator (green circle) - draw on top, fade out, then stop drawing
  // The next updateDisplay() will naturally restore the UI underneath
  if (tapIndicatorActive) {
    unsigned long dt = millis() - tapIndicatorStart;
    if (dt >= TAP_INDICATOR_DURATION) {
      // Expired - just stop drawing it, updateDisplay() will restore the UI naturally
      tapIndicatorActive = false;
    } else {
      // Draw green circle on top, fading out as time approaches TAP_INDICATOR_DURATION
      int16_t cx = tapIndicatorX;
      int16_t cy = tapIndicatorY;
      int16_t r = TAP_RADIUS;
      
      // Calculate fade-out alpha: 255 (full) at start, 0 (transparent) at end
      uint8_t alpha = 255 - (uint8_t)((dt * 255) / TAP_INDICATOR_DURATION);
      
      // Draw circle with fade-out (blend green with black)
      for (int16_t y = -r; y <= r; y++) {
        for (int16_t x = -r; x <= r; x++) {
          if (x * x + y * y <= r * r) {
            int16_t px = cx + x;
            int16_t py = cy + y;
            if (px < 0 || py < 0 || px >= gfx->width() || py >= gfx->height()) continue;
            
            // Blend green with black based on alpha for fade-out effect
            uint16_t fg = COLOR_GREEN;
            uint8_t fg_r = (fg >> 11) & 0x1F;
            uint8_t fg_g = (fg >> 5) & 0x3F;
            uint8_t fg_b = fg & 0x1F;
            
            // Black (will be replaced by UI on next updateDisplay)
            uint8_t bg_r = 0;
            uint8_t bg_g = 0;
            uint8_t bg_b = 0;
            
            // Alpha blend: out = fg * alpha + bg * (1 - alpha)
            uint8_t out_r = (uint8_t)((fg_r * alpha + bg_r * (255 - alpha)) / 255);
            uint8_t out_g = (uint8_t)((fg_g * alpha + bg_g * (255 - alpha)) / 255);
            uint8_t out_b = (uint8_t)((fg_b * alpha + bg_b * (255 - alpha)) / 255);
            
            uint16_t out = (out_r << 11) | (out_g << 5) | out_b;
            gfx->drawPixel(px, py, out);
          }
        }
      }
    }
  }

  delay(5);
}