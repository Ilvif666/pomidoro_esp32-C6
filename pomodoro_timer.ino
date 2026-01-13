#include <TFT_eSPI.h>

// Display configuration
TFT_eSPI tft = TFT_eSPI();

// Color definitions - Minimalistic black and gold theme
#define COLOR_BLACK    0x0000
#define COLOR_GOLD     0xFEA0  // Golden color
#define COLOR_BLUE     0x001F  // Blue for rest
#define COLOR_DARK     0x0000  // Dark background

// Timer states
enum TimerState {
  STOPPED,
  RUNNING,
  PAUSED
};

// Timer configuration
const unsigned long POMODORO_DURATION = 25 * 60 * 1000; // 25 minutes in milliseconds
const unsigned long FLASH_DURATION = 500; // Flash duration in ms
const unsigned long LONG_PRESS_DURATION = 1000; // Long press threshold in ms

// Touch configuration
// Note: Adjust these based on your Waveshare ESP32-C6 board
// Many Waveshare displays use I2C touch controllers (FT6236, etc.)
// This implementation uses a simple GPIO approach - you may need to adapt
// for I2C touch controllers by using Wire library and touch controller library
#define TOUCH_PIN 9  // Touch pin - may need to change for your board
#define TOUCH_THRESHOLD 40  // Touch sensitivity threshold (for analog touch)
// Alternative: If using I2C touch, uncomment and configure:
// #define USE_I2C_TOUCH
// #define TOUCH_I2C_ADDR 0x38  // Common I2C address for touch controllers

// Global variables
TimerState currentState = STOPPED;
unsigned long startTime = 0;
unsigned long pausedTime = 0;
unsigned long elapsedBeforePause = 0;
bool isWorkSession = true;
bool flashActive = false;
unsigned long flashStartTime = 0;
uint16_t flashColor = COLOR_GOLD;

// Touch detection variables
bool touchPressed = false;
unsigned long touchStartTime = 0;
bool longPressDetected = false;

void setup() {
  Serial.begin(115200);
  Serial.println("Pomodoro Timer Starting...");

  // Initialize display
  tft.init();
  tft.setRotation(1); // Portrait mode (adjust if needed)
  tft.fillScreen(COLOR_BLACK);
  
  // Configure touch pin
  // For analog touch:
  pinMode(TOUCH_PIN, INPUT);
  // For ESP32-C6 capacitive touch, you might use:
  // touchAttachInterrupt(TOUCH_PIN, touchCallback, TOUCH_THRESHOLD);
  // For I2C touch, initialize Wire library here
  
  // Initial display - stopped state
  displayStoppedState();
  
  Serial.println("Pomodoro Timer Ready");
}

void loop() {
  handleTouchInput();
  updateTimer();
  updateDisplay();
  delay(10); // Small delay to prevent excessive CPU usage
}

void handleTouchInput() {
  // Touch detection - adapt this based on your touch controller
  // Method 1: Simple GPIO/analog touch (current implementation)
  int touchValue = analogRead(TOUCH_PIN);
  bool currentlyTouched = (touchValue < TOUCH_THRESHOLD);
  
  // Method 2: For I2C touch controllers, you would use something like:
  // bool currentlyTouched = checkI2CTouch(); // Implement based on your touch IC
  
  // Method 3: For capacitive touch pins on ESP32-C6:
  // bool currentlyTouched = (touchRead(TOUCH_PIN) < TOUCH_THRESHOLD);
  
  if (currentlyTouched && !touchPressed) {
    // Touch just started
    touchPressed = true;
    touchStartTime = millis();
    longPressDetected = false;
  } else if (!currentlyTouched && touchPressed) {
    // Touch released
    unsigned long touchDuration = millis() - touchStartTime;
    
    if (longPressDetected) {
      // Long press action - Start/Stop
      if (currentState == STOPPED) {
        startTimer();
      } else {
        stopTimer();
      }
    } else if (touchDuration > 50) {
      // Short tap - Pause/Resume
      if (currentState == RUNNING) {
        pauseTimer();
      } else if (currentState == PAUSED) {
        resumeTimer();
      }
    }
    
    touchPressed = false;
    longPressDetected = false;
  } else if (touchPressed && !longPressDetected) {
    // Check for long press
    if (millis() - touchStartTime > LONG_PRESS_DURATION) {
      longPressDetected = true;
    }
  }
}

void startTimer() {
  currentState = RUNNING;
  isWorkSession = true;
  startTime = millis();
  elapsedBeforePause = 0;
  
  // Flash golden for work session start
  triggerFlash(COLOR_GOLD);
  
  Serial.println("Timer started - Work session");
}

void pauseTimer() {
  if (currentState == RUNNING) {
    currentState = PAUSED;
    pausedTime = millis();
    elapsedBeforePause = millis() - startTime;
    Serial.println("Timer paused");
  }
}

void resumeTimer() {
  if (currentState == PAUSED) {
    currentState = RUNNING;
    startTime = millis() - elapsedBeforePause;
    Serial.println("Timer resumed");
  }
}

void stopTimer() {
  currentState = STOPPED;
  displayStoppedState();
  Serial.println("Timer stopped");
}

void updateTimer() {
  if (currentState == RUNNING) {
    unsigned long elapsed = millis() - startTime;
    
    if (elapsed >= POMODORO_DURATION) {
      // Timer completed
      if (isWorkSession) {
        // Work session finished - start break
        isWorkSession = false;
        triggerFlash(COLOR_BLUE);
        startTime = millis(); // Reset for break (if you want breaks, otherwise restart work)
        Serial.println("Work session completed - Break started");
      } else {
        // Break finished - restart work
        isWorkSession = true;
        triggerFlash(COLOR_GOLD);
        startTime = millis();
        Serial.println("Break completed - Work session started");
      }
    }
  }
}

void triggerFlash(uint16_t color) {
  flashActive = true;
  flashColor = color;
  flashStartTime = millis();
}

void updateDisplay() {
  // Handle flash effect
  if (flashActive) {
    if (millis() - flashStartTime < FLASH_DURATION) {
      tft.fillScreen(flashColor);
    } else {
      flashActive = false;
      // Redraw the timer display after flash
      if (currentState == STOPPED) {
        displayStoppedState();
      } else {
        drawTimer();
      }
    }
    return;
  }
  
  // Update display based on state
  if (currentState == STOPPED) {
    // Stopped state is drawn once, no need to redraw constantly
    return;
  } else {
    drawTimer();
  }
}

void drawTimer() {
  tft.fillScreen(COLOR_BLACK);
  
  unsigned long elapsed = 0;
  if (currentState == RUNNING) {
    elapsed = millis() - startTime;
  } else if (currentState == PAUSED) {
    elapsed = elapsedBeforePause;
  }
  
  unsigned long remaining = POMODORO_DURATION - elapsed;
  if (remaining > POMODORO_DURATION) remaining = 0; // Prevent underflow
  
  // Calculate minutes and seconds
  unsigned long minutes = remaining / 60000;
  unsigned long seconds = (remaining % 60000) / 1000;
  
  // Draw timer in minimalistic style
  tft.setTextColor(COLOR_GOLD, COLOR_BLACK);
  tft.setTextDatum(MC_DATUM); // Middle center alignment
  
  // Display time in MM:SS format
  char timeStr[6];
  sprintf(timeStr, "%02lu:%02lu", minutes, seconds);
  
  // Large timer display
  tft.setTextSize(4);
  tft.drawString(timeStr, tft.width() / 2, tft.height() / 2 - 20, 1);
  
  // Draw progress circle (optional minimalistic indicator)
  drawProgressCircle((float)elapsed / POMODORO_DURATION);
  
  // Status indicator
  tft.setTextSize(1);
  if (currentState == PAUSED) {
    tft.setTextColor(COLOR_GOLD, COLOR_BLACK);
    tft.drawString("PAUSED", tft.width() / 2, tft.height() - 30, 1);
  } else if (isWorkSession) {
    tft.setTextColor(COLOR_GOLD, COLOR_BLACK);
    tft.drawString("WORK", tft.width() / 2, tft.height() - 30, 1);
  } else {
    tft.setTextColor(COLOR_BLUE, COLOR_BLACK);
    tft.drawString("REST", tft.width() / 2, tft.height() - 30, 1);
  }
}

void drawProgressCircle(float progress) {
  int centerX = tft.width() / 2;
  int centerY = tft.height() / 2 + 40;
  int radius = 30;
  
  // Draw background circle
  tft.drawCircle(centerX, centerY, radius, COLOR_GOLD);
  
  // Draw progress arc (simplified - draws segments)
  if (progress > 0 && progress <= 1.0) {
    int segments = 32;
    for (int i = 0; i < segments * progress; i++) {
      float angle = (i * 2.0 * PI) / segments - PI / 2; // Start from top
      int x = centerX + radius * cos(angle);
      int y = centerY + radius * sin(angle);
      tft.drawPixel(x, y, COLOR_GOLD);
    }
  }
}

void displayStoppedState() {
  tft.fillScreen(COLOR_BLACK);
  
  int centerX = tft.width() / 2;
  int centerY = tft.height() / 2;
  int radius = 50;
  
  // Draw golden circle
  tft.drawCircle(centerX, centerY, radius, COLOR_GOLD);
  tft.drawCircle(centerX, centerY, radius - 1, COLOR_GOLD);
  
  // Draw "R" letter in golden color
  tft.setTextColor(COLOR_GOLD, COLOR_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(6);
  tft.drawString("R", centerX, centerY, 1);
}
