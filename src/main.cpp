// Arduino_GFX-based Pomodoro timer for Waveshare ESP32-C6-LCD-1.47
// Uses the golden "R" in a circle as the splash / stopped screen.
// Integrated with working AXS5106L touch controller

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <Wire.h>
#include <math.h>
#include <Preferences.h>  // For NVS (Non-Volatile Storage) to save settings
#include <FastIMU.h>      // For QMI8658 IMU auto-rotation
#include "FreeSansBold24pt7b.h"  // Smooth font for the logo and titles
#include "esp_lcd_touch_axs5106l.h"

// WiFi and Telegram includes
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>

// Preferences for persistent storage
Preferences preferences;

// ==================== WiFi & Telegram Configuration ====================
// WiFi credentials from platformio.ini build flags
#ifndef WIFI_SSID
  #define WIFI_SSID "YOUR_WIFI_SSID"
#endif
#ifndef WIFI_PASSWORD
  #define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
#endif
#ifndef TELEGRAM_BOT_TOKEN
  #define TELEGRAM_BOT_TOKEN ""
#endif
#ifndef TELEGRAM_CHAT_ID
  #define TELEGRAM_CHAT_ID ""
#endif

// WiFi and Telegram state
bool wifiConnected = false;
bool telegramConfigured = false;

// Use build flags for bot token and chat_id
const char* botToken = TELEGRAM_BOT_TOKEN;
const char* chatId = TELEGRAM_CHAT_ID;

// WiFi client for Telegram
WiFiClientSecure telegramClient;
UniversalTelegramBot* bot = nullptr;

// Telegram bot polling interval
const unsigned long BOT_CHECK_INTERVAL = 5000;  // Check every 5 seconds (was 2s)

// FreeRTOS task handle for Telegram
TaskHandle_t telegramTaskHandle = nullptr;

// Thread-safe command queue from Telegram to main loop
volatile bool telegramCmdStart = false;
volatile bool telegramCmdPause = false;
volatile bool telegramCmdResume = false;
volatile bool telegramCmdStop = false;
volatile bool telegramCmdMode = false;

// Outgoing message queue (main loop -> telegram task)
#define MSG_QUEUE_SIZE 3
QueueHandle_t telegramMsgQueue = nullptr;
struct TelegramMsg {
  char text[128];
};

// Last queued message to prevent duplicates
char lastQueuedMessage[128] = "";
unsigned long lastSentTime = 0;
const unsigned long SEND_COOLDOWN = 3000;  // 3 second cooldown between sends

// Mutex for telegram operations
SemaphoreHandle_t telegramMutex = nullptr;

// IMU (QMI8658) for auto-rotation - shares I2C bus with touch
#define IMU_ADDRESS 0x6B  // QMI8658 default I2C address
QMI8658 imu;
calData imuCalibration = { 0 };  // Calibration data
AccelData accelData;  // Accelerometer data
bool imuInitialized = false;

// Auto-rotation variables
uint8_t currentRotation = 0;  // Current display rotation (0-3)
unsigned long lastRotationCheck = 0;
const unsigned long ROTATION_CHECK_INTERVAL = 2000;  // Check every 2 seconds (was 500ms)
const float ROTATION_THRESHOLD = 0.5;  // Threshold in g for rotation detection

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
const uint16_t COLOR_RED   = 0xF800;
const uint16_t COLOR_COFFEE = 0x8200; // Brown/coffee color
const uint16_t COLOR_CYAN   = 0x07FF;
const uint16_t COLOR_MAGENTA = 0xF81F;
const uint16_t COLOR_YELLOW = 0xFFE0;
const uint16_t COLOR_ORANGE = 0xFBF9; // Pink (rgb(255,192,203))
const uint16_t COLOR_PURPLE = 0x780F; // Purple
const uint16_t COLOR_PINK = 0xFC1F;   // Pink
const uint16_t COLOR_TEAL = 0x07EF;   // Teal
const uint16_t COLOR_LIME = 0x87E0;   // Lime green
const uint16_t COLOR_INDIGO = 0x4810; // Indigo
const uint16_t COLOR_CORAL = 0xFA00;  // International Orange (#ff4f00, rgb(255,79,0))
const uint16_t COLOR_LAVENDER = 0x9C1F; // Lavender
const uint16_t COLOR_EMERALD = 0x07D0; // Emerald green
const uint16_t COLOR_WHITE = 0xFFFF;  // White
const uint16_t COLOR_DARK_BLUE = 0x000F; // Dark blue
const uint16_t COLOR_OLIVE = 0x8400;   // Olive green
const uint16_t COLOR_TURQUOISE = 0x04FF; // Turquoise (more contrast than teal)
const uint16_t COLOR_VIOLET = 0x901A;  // Violet (more contrast than purple)
const uint16_t COLOR_SALMON = 0xFC60;  // Salmon
const uint16_t COLOR_MINT = 0x87FF;    // Mint green (lighter than cyan)
const uint16_t COLOR_NAVY = 0x0010;    // Navy blue

// Global palette array for color picker
const uint16_t paletteColors[] = {
  COLOR_RED,         // 0 - Red
  COLOR_ORANGE,      // 1 - Orange/Pink
  COLOR_CORAL,       // 2 - International Orange
  COLOR_YELLOW,      // 3 - Yellow
  COLOR_LIME,        // 4 - Lime green
  COLOR_GREEN,       // 5 - Green
  COLOR_MINT,        // 6 - Mint green
  COLOR_CYAN,        // 7 - Cyan
  COLOR_TURQUOISE,   // 8 - Turquoise
  COLOR_BLUE,        // 9 - Blue
  COLOR_DARK_BLUE,   // 10 - Dark blue
  COLOR_NAVY,        // 11 - Navy blue
  COLOR_INDIGO,      // 12 - Indigo
  COLOR_VIOLET,      // 13 - Violet
  COLOR_PURPLE,      // 14 - Purple
  COLOR_MAGENTA,     // 15 - Magenta
  COLOR_GOLD,        // 16 - Golden
  COLOR_COFFEE       // 17 - Coffee/Brown
};
const int paletteSize = sizeof(paletteColors) / sizeof(paletteColors[0]);

// Function to invert a 16-bit RGB565 color
uint16_t invertColor(uint16_t color) {
  // RGB565: RRRRR GGGGGG BBBBB
  uint16_t r = (color >> 11) & 0x1F;  // 5 bits red
  uint16_t g = (color >> 5) & 0x3F;   // 6 bits green
  uint16_t b = color & 0x1F;          // 5 bits blue
  // Invert each channel
  r = 0x1F - r;
  g = 0x3F - g;
  b = 0x1F - b;
  return (r << 11) | (g << 5) | b;
}

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
// View mode: 0 = normal view, 1 = grid view (palette), 2 = color preview
uint8_t currentViewMode = 0;
bool gridViewActive = false;  // Kept for backward compatibility
// Selected color for work session (user-customizable)
uint16_t selectedWorkColor = COLOR_GOLD;  // Default work color
uint16_t tempPreviewColor = COLOR_GOLD;   // Temporary color for preview
int8_t tempSelectedColorIndex = -1;       // Temporary selection in grid (-1 = none)
// Grid parameters (needed for touch detection)
static int16_t gridCellWidth = 43;
static int16_t gridCellHeight = 43;
static int16_t gridStartX = 0;
static int16_t gridNumRows = 7;
static int16_t gridNumCols = 3;
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

// Home screen button bounds (for work/rest buttons on splash screen)
static int16_t workBtnLeft = 0;
static int16_t workBtnRight = 0;
static int16_t workBtnTop = 0;
static int16_t workBtnBottom = 0;
static bool workBtnValid = false;
static int16_t restBtnLeft = 0;
static int16_t restBtnRight = 0;
static int16_t restBtnTop = 0;
static int16_t restBtnBottom = 0;
static bool restBtnValid = false;

// Grid view button bounds (for X and ✓ buttons in bottom row)
static int16_t gridCancelBtnLeft = 0;
static int16_t gridCancelBtnRight = 0;
static int16_t gridCancelBtnTop = 0;
static int16_t gridCancelBtnBottom = 0;
static bool gridCancelBtnValid = false;
static int16_t gridConfirmBtnLeft = 0;
static int16_t gridConfirmBtnRight = 0;
static int16_t gridConfirmBtnTop = 0;
static int16_t gridConfirmBtnBottom = 0;
static bool gridConfirmBtnValid = false;

// Color preview button bounds (for confirm/cancel on color preview screen)
static int16_t previewCancelBtnLeft = 0;
static int16_t previewCancelBtnRight = 0;
static int16_t previewCancelBtnTop = 0;
static int16_t previewCancelBtnBottom = 0;
static bool previewCancelBtnValid = false;
static int16_t previewConfirmBtnLeft = 0;
static int16_t previewConfirmBtnRight = 0;
static int16_t previewConfirmBtnTop = 0;
static int16_t previewConfirmBtnBottom = 0;
static bool previewConfirmBtnValid = false;

// Gear button bounds (settings button on home screen)
static int16_t gearBtnLeft = 0;
static int16_t gearBtnRight = 0;
static int16_t gearBtnTop = 0;
static int16_t gearBtnBottom = 0;
static bool gearBtnValid = false;

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

// Extra touch padding for buttons (makes touch areas larger than visible buttons)
static const int16_t TOUCH_PADDING = 15;  // 15px extra on each side

// Forward declarations
void drawCenteredText(const char *txt, int16_t cx, int16_t cy, uint16_t color, uint8_t size);
void drawGearIcon(int16_t cx, int16_t cy, int16_t size, uint16_t color);
void drawColorPreview();
void displayStoppedState();
extern bool forceCircleRedraw;  // Force progress circle redraw

// Save selected color to NVS (persistent storage)
void saveSelectedColor() {
  preferences.begin("pomodoro", false);  // Open namespace in RW mode
  preferences.putUShort("workColor", selectedWorkColor);
  preferences.end();
  Serial.print("Saved color to NVS: 0x");
  Serial.println(selectedWorkColor, HEX);
}

// Load selected color from NVS (persistent storage)
void loadSelectedColor() {
  preferences.begin("pomodoro", true);  // Open namespace in read-only mode
  selectedWorkColor = preferences.getUShort("workColor", COLOR_GOLD);  // Default to gold
  preferences.end();
  Serial.print("Loaded color from NVS: 0x");
  Serial.println(selectedWorkColor, HEX);
}

// ==================== WiFi & Telegram Functions ====================

// Connect to WiFi
void connectWiFi() {
  Serial.println("Connecting to WiFi...");
  Serial.print("SSID: ");
  Serial.println(WIFI_SSID);
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    Serial.println();
    Serial.print("WiFi connected! IP: ");
    Serial.println(WiFi.localIP());
  } else {
    wifiConnected = false;
    Serial.println();
    Serial.println("WiFi connection failed!");
  }
}

// Initialize Telegram bot
void initTelegramBot() {
  // Check if bot token is configured
  telegramConfigured = (strlen(botToken) > 0 && strlen(chatId) > 0);
  
  if (!wifiConnected || !telegramConfigured) {
    Serial.println("Telegram not configured or WiFi not connected");
    return;
  }
  
  if (bot != nullptr) {
    delete bot;
  }
  
  telegramClient.setInsecure();  // Skip certificate verification
  telegramClient.setTimeout(10000);  // 10 second timeout to prevent retries
  bot = new UniversalTelegramBot(botToken, telegramClient);
  bot->waitForResponse = 5000;  // 5 second wait for response
  Serial.println("Telegram bot initialized");
  
  // Send startup message
  bot->sendMessage(chatId, "🍅 Pomodoro Timer connected!", "HTML");
}

// Queue message to Telegram (non-blocking)
void sendTelegramMessage(const String& message) {
  if (!wifiConnected || !telegramConfigured || telegramMsgQueue == nullptr) {
    return;
  }
  
  TelegramMsg msg;
  message.toCharArray(msg.text, sizeof(msg.text));
  
  if (xQueueSend(telegramMsgQueue, &msg, 0) == pdTRUE) {
    Serial.print("[TG] Queued: ");
    Serial.println(message);
  }
}

// Telegram task - sends queued messages in background
void telegramTask(void* parameter) {
  Serial.println("[TG TASK] Started");
  
  while (true) {
    // Send queued messages
    TelegramMsg outMsg;
    if (telegramMsgQueue != nullptr && xQueueReceive(telegramMsgQueue, &outMsg, 0) == pdTRUE) {
      if (bot != nullptr) {
        Serial.print("[TG TASK] Sending: ");
        Serial.println(outMsg.text);
        bot->sendMessage(chatId, outMsg.text, "HTML");
        Serial.println("[TG TASK] Done");
      }
    }
    
    // Check for incoming commands (less frequently)
    static unsigned long lastCheck = 0;
    if (millis() - lastCheck > BOT_CHECK_INTERVAL && bot != nullptr) {
      lastCheck = millis();
      int numNewMessages = bot->getUpdates(bot->last_message_received + 1);
      
      for (int i = 0; i < numNewMessages; i++) {
        String text = bot->messages[i].text;
        String from_id = bot->messages[i].chat_id;
        
        if (from_id != String(chatId)) continue;
        text.toLowerCase();
        
        Serial.print("[TG] Command: ");
        Serial.println(text);
        
        if (text == "/start" || text == "/help") {
          String msg = "🍅 <b>Pomodoro Timer</b>\n\n";
          msg += "/status - Current status\n";
          msg += "/work - Start work\n";
          msg += "/pause - Pause\n";
          msg += "/resume - Resume\n";
          msg += "/stop - Stop\n";
          msg += "/mode - Change mode";
          bot->sendMessage(chatId, msg, "HTML");
        }
        else if (text == "/work") {
          telegramCmdStart = true;
          bot->sendMessage(chatId, "🍅 Starting...", "HTML");
        }
        else if (text == "/pause") {
          telegramCmdPause = true;
          bot->sendMessage(chatId, "⏸ Pausing...", "HTML");
        }
        else if (text == "/resume") {
          telegramCmdResume = true;
          bot->sendMessage(chatId, "▶️ Resuming...", "HTML");
        }
        else if (text == "/stop") {
          telegramCmdStop = true;
          bot->sendMessage(chatId, "⏹ Stopping...", "HTML");
        }
        else if (text == "/mode") {
          telegramCmdMode = true;
          String modeStr;
          switch (currentMode) {
            case MODE_1_1: modeStr = "25/5"; break;
            case MODE_25_5: modeStr = "50/10"; break;
            case MODE_50_10: modeStr = "1/1"; break;
          }
          bot->sendMessage(chatId, "⏱ Mode: " + modeStr, "HTML");
        }
        else if (text == "/status") {
          String msg = "🍅 ";
          msg += (currentState == STOPPED) ? "Stopped" : 
                 (currentState == RUNNING) ? (isWorkSession ? "Working" : "Resting") : "Paused";
          msg += " | ";
          switch (currentMode) {
            case MODE_1_1: msg += "1/1"; break;
            case MODE_25_5: msg += "25/5"; break;
            case MODE_50_10: msg += "50/10"; break;
          }
          bot->sendMessage(chatId, msg, "HTML");
        }
      }
    }
    
    vTaskDelay(pdMS_TO_TICKS(100));  // Check queue frequently
  }
}

// Process Telegram commands in main loop (thread-safe)
void processTelegramCommands() {
  if (telegramCmdStart) {
    telegramCmdStart = false;
    if (currentState == STOPPED) {
      Serial.println("[TG CMD] Starting timer");
      currentState = RUNNING;
      isWorkSession = true;
      startTime = millis();
      timerStartTime = millis();
      elapsedBeforePause = 0;
      displayInitialized = false;
      forceCircleRedraw = true;
    }
  }
  if (telegramCmdPause) {
    telegramCmdPause = false;
    if (currentState == RUNNING) {
      Serial.println("[TG CMD] Pausing timer");
      currentState = PAUSED;
      pausedTime = millis();
      elapsedBeforePause = millis() - startTime;
      displayInitialized = false;
      forceCircleRedraw = true;
    }
  }
  if (telegramCmdResume) {
    telegramCmdResume = false;
    if (currentState == PAUSED) {
      Serial.println("[TG CMD] Resuming timer");
      currentState = RUNNING;
      startTime = millis() - elapsedBeforePause;
      displayInitialized = false;
      forceCircleRedraw = true;
    }
  }
  if (telegramCmdStop) {
    telegramCmdStop = false;
    if (currentState != STOPPED) {
      Serial.println("[TG CMD] Stopping timer");
      currentState = STOPPED;
      displayInitialized = false;
      displayStoppedState();
    }
  }
  if (telegramCmdMode) {
    telegramCmdMode = false;
    Serial.println("[TG CMD] Changing mode");
    switch (currentMode) {
      case MODE_1_1: currentMode = MODE_25_5; break;
      case MODE_25_5: currentMode = MODE_50_10; break;
      case MODE_50_10: currentMode = MODE_1_1; break;
    }
    displayInitialized = false;
    forceCircleRedraw = true;
  }
}

// Start Telegram task on separate core
void startTelegramTask() {
  if (!wifiConnected || !telegramConfigured) return;
  
  // Create mutex for thread-safe telegram operations
  telegramMutex = xSemaphoreCreateMutex();
  
  // Create message queue for outgoing messages
  telegramMsgQueue = xQueueCreate(MSG_QUEUE_SIZE, sizeof(TelegramMsg));
  
  // Create task with low priority (but not lowest)
  xTaskCreatePinnedToCore(
    telegramTask,           // Task function
    "TelegramTask",         // Task name
    8192,                   // Stack size
    NULL,                   // Parameters
    1,                      // Priority (low but runs)
    &telegramTaskHandle,    // Task handle
    0                       // Core 0
  );
  Serial.println("Telegram task created on core 0");
}

// --- Helper: draw golden "R" splash (used as stopped screen) ---
void drawSplash() {
  gfx->fillScreen(COLOR_BLACK);

  // Use selected work color for logo
  uint16_t workColor = selectedWorkColor;
  
  // Check if we're in landscape mode
  bool isLandscape = (currentRotation == 1 || currentRotation == 3);
  
  int16_t centerX = gfx->width() / 2;
  int16_t centerY = gfx->height() / 2;
  int16_t radius = 70;
  int16_t borderWidth = 5;

  for (int16_t i = 0; i < borderWidth; i++) {
    gfx->drawCircle(centerX, centerY, radius - i, workColor);
  }

  gfx->setFont(&FreeSansBold24pt7b);
  gfx->setTextColor(workColor);
  gfx->setTextSize(2, 2, 0);
  gfx->setCursor(centerX - 33, centerY + 30);
  gfx->print("R");
  
  // Draw gear icon (settings button)
  int16_t gearSize = 36;
  int16_t gearCenterX, gearCenterY;
  
  if (isLandscape) {
    // Landscape: gear button on the right side
    gearCenterX = gfx->width() - 40;
    gearCenterY = gfx->height() / 2;
  } else {
    // Portrait: gear button at the bottom center
    gearCenterX = gfx->width() / 2;
    gearCenterY = gfx->height() - 40;
  }
  
  // Set gear button bounds for touch detection
  int padding = 8;
  gearBtnLeft = gearCenterX - gearSize/2 - padding;
  gearBtnRight = gearCenterX + gearSize/2 + padding;
  gearBtnTop = gearCenterY - gearSize/2 - padding;
  gearBtnBottom = gearCenterY + gearSize/2 + padding;
  
  // Draw gear icon
  drawGearIcon(gearCenterX, gearCenterY, gearSize, workColor);
  gearBtnValid = true;
  
  // Disable old work/rest buttons
  workBtnValid = false;
  restBtnValid = false;
}

// --- Helper: draw grid view (3 columns, X rows with square cells) ---
void drawGrid() {
  gfx->fillScreen(COLOR_BLACK);
  
  int16_t screenWidth = gfx->width();   // 172
  int16_t screenHeight = gfx->height(); // 320
  
  // Save grid parameters to global variables for touch detection
  gridCellWidth = 43;
  gridCellHeight = 43;
  gridNumCols = 3;
  gridNumRows = screenHeight / gridCellHeight;  // 320 / 43 = 7 rows
  
  // Calculate total grid width (3 columns * 43px = 129px)
  int16_t gridWidth = gridNumCols * gridCellWidth;
  // Center the grid horizontally and save to global
  gridStartX = (screenWidth - gridWidth) / 2;
  
  // Grid lines color (black)
  uint16_t gridColor = COLOR_BLACK;
  
  // Calculate last row Y position (last row is for buttons, skip it)
  int16_t lastRowY = (gridNumRows - 1) * gridCellHeight;
  
  // Fill grid cells with palette colors and highlight selected
  int colorIndex = 0;
  for (int row = 0; row < gridNumRows - 1; row++) {
    for (int col = 0; col < gridNumCols; col++) {
      int16_t cellX = gridStartX + col * gridCellWidth;
      int16_t cellY = row * gridCellHeight;
      
      // Use palette color
      if (colorIndex < paletteSize) {
        uint16_t cellColor = paletteColors[colorIndex];
        
        // Fill the cell with color
        gfx->fillRect(cellX, cellY, gridCellWidth, gridCellHeight, cellColor);
        
        // Highlight selected cell with white border
        if (colorIndex == tempSelectedColorIndex) {
          // Draw thick white border (3 pixels)
          for (int i = 0; i < 3; i++) {
            gfx->drawRect(cellX + i, cellY + i, 
                          gridCellWidth - i * 2, gridCellHeight - i * 2, 
                          COLOR_WHITE);
          }
        }
      }
      
      colorIndex++;
      if (colorIndex >= paletteSize) break;
    }
    if (colorIndex >= paletteSize) break;
  }
  
  // Draw grid lines (black) on top of colored cells
  for (int col = 1; col < gridNumCols; col++) {
    int16_t x = gridStartX + col * gridCellWidth;
    gfx->drawFastVLine(x, 0, lastRowY, gridColor);
  }
  
  // Draw horizontal lines (row separators)
  for (int row = 1; row < gridNumRows - 1; row++) {
    int16_t y = row * gridCellHeight;
    if (y < screenHeight) {
      gfx->drawFastHLine(gridStartX, y, gridWidth, gridColor);
    }
  }
  
  // Draw bottom border line
  gfx->drawFastHLine(gridStartX, lastRowY, gridWidth, gridColor);
  
  // Draw buttons in bottom row: "X" on left, "V" (checkmark) on right, centered
  int16_t bottomRowY = lastRowY;
  int16_t bottomRowHeight = gridCellHeight;
  int16_t bottomRowCenterY = bottomRowY + bottomRowHeight / 2 + 15;
  
  // Calculate button size - try size 5 or 6 for bigger buttons
  const char *cancelTxt = "X";
  const char *confirmTxt = "V";  // Use "V" instead of "✓" for better compatibility
  
  int16_t x1, y1;
  uint16_t w1, h1, w2, h2;
  gfx->setFont(nullptr);
  uint8_t textSize = 5;  // Try size 5 for bigger buttons (can be 6 if needed)
  gfx->setTextSize(textSize, textSize, 0);
  
  // Get bounds for both texts to ensure same button size
  gfx->getTextBounds(cancelTxt, 0, 0, &x1, &y1, &w1, &h1);
  gfx->getTextBounds(confirmTxt, 0, 0, &x1, &y1, &w2, &h2);
  
  // Use maximum width and height for both buttons to make them same size
  uint16_t maxW = (w1 > w2) ? w1 : w2;
  uint16_t maxH = (h1 > h2) ? h1 : h2;
  
  // Calculate button size with padding
  int padding = 6;
  int16_t btnWidth = maxW + padding * 2;
  int16_t btnHeight = maxH + padding * 2;
  
  // Allow buttons to be larger - remove size restriction
  // Buttons can extend beyond row height if needed
  
  // Center buttons in the bottom row with space between them
  int16_t spaceBetween = 20;  // Space between buttons
  int16_t totalButtonsWidth = btnWidth * 2 + spaceBetween;
  int16_t buttonsStartX = gridStartX + (gridWidth - totalButtonsWidth) / 2;
  
  // Left button "X"
  int16_t cancelCenterX = buttonsStartX + btnWidth / 2;
  gridCancelBtnLeft   = buttonsStartX;
  gridCancelBtnRight  = buttonsStartX + btnWidth;
  gridCancelBtnTop    = bottomRowCenterY - btnHeight / 2;
  gridCancelBtnBottom = bottomRowCenterY + btnHeight / 2;
  
  // Draw border around cancel button (golden color)
  gfx->drawRect(gridCancelBtnLeft, gridCancelBtnTop,
                btnWidth,
                btnHeight,
                COLOR_GOLD);
  
  // Draw "X" text centered with slight offset (right and down) for better visual centering
  int16_t textOffsetX = 2;  // Move right a bit
  int16_t textOffsetY = 2;  // Move down a bit
  drawCenteredText(cancelTxt, cancelCenterX + textOffsetX, bottomRowCenterY + textOffsetY, COLOR_GOLD, textSize);
  gridCancelBtnValid = true;
  
  // Right button "V" (checkmark)
  int16_t confirmStartX = buttonsStartX + btnWidth + spaceBetween;
  int16_t confirmCenterX = confirmStartX + btnWidth / 2;
  gridConfirmBtnLeft   = confirmStartX;
  gridConfirmBtnRight  = confirmStartX + btnWidth;
  gridConfirmBtnTop    = bottomRowCenterY - btnHeight / 2;
  gridConfirmBtnBottom = bottomRowCenterY + btnHeight / 2;
  
  // Draw border around confirm button (golden color)
  gfx->drawRect(gridConfirmBtnLeft, gridConfirmBtnTop,
                btnWidth,
                btnHeight,
                COLOR_GOLD);
  
  // Draw "V" text centered with slight offset (right and down) for better visual centering
  drawCenteredText(confirmTxt, confirmCenterX + textOffsetX, bottomRowCenterY + textOffsetY, COLOR_GOLD, textSize);
  gridConfirmBtnValid = true;
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

// --- Helper: draw play icon (triangle) ---
void drawPlayIcon(int16_t cx, int16_t cy, int16_t size, uint16_t color) {
  // Draw a filled triangle pointing right
  int16_t halfH = size / 2;
  int16_t w = size * 3 / 4;
  // Triangle vertices: left-top, left-bottom, right-center
  gfx->fillTriangle(
    cx - w/2, cy - halfH,      // top-left
    cx - w/2, cy + halfH,      // bottom-left
    cx + w/2, cy,              // right center
    color
  );
}

// --- Helper: draw pause icon (two bars) ---
void drawPauseIcon(int16_t cx, int16_t cy, int16_t size, uint16_t color) {
  // Draw two vertical bars
  int16_t barWidth = size / 4;
  int16_t barHeight = size;
  int16_t gap = size / 4;
  // Left bar
  gfx->fillRect(cx - gap - barWidth, cy - barHeight/2, barWidth, barHeight, color);
  // Right bar  
  gfx->fillRect(cx + gap, cy - barHeight/2, barWidth, barHeight, color);
}

// --- Helper: draw gear icon (settings) ---
void drawGearIcon(int16_t cx, int16_t cy, int16_t size, uint16_t color) {
  // Draw a simple gear: outer circle with teeth + inner circle
  int16_t outerRadius = size / 2;
  int16_t innerRadius = size / 4;
  int16_t toothLength = size / 6;
  int numTeeth = 8;
  
  // Draw outer ring
  for (int16_t i = 0; i < 3; i++) {
    gfx->drawCircle(cx, cy, outerRadius - i, color);
  }
  
  // Draw inner circle (hollow center)
  gfx->fillCircle(cx, cy, innerRadius + 2, COLOR_BLACK);
  for (int16_t i = 0; i < 2; i++) {
    gfx->drawCircle(cx, cy, innerRadius - i, color);
  }
  
  // Draw teeth around the gear
  for (int i = 0; i < numTeeth; i++) {
    float angle = (i * 2.0f * PI) / numTeeth;
    int16_t x1 = cx + (outerRadius - 2) * cosf(angle);
    int16_t y1 = cy + (outerRadius - 2) * sinf(angle);
    int16_t x2 = cx + (outerRadius + toothLength) * cosf(angle);
    int16_t y2 = cy + (outerRadius + toothLength) * sinf(angle);
    // Draw thick tooth (3 lines)
    for (int t = -1; t <= 1; t++) {
      float perpAngle = angle + PI / 2;
      int16_t offsetX = t * cosf(perpAngle);
      int16_t offsetY = t * sinf(perpAngle);
      gfx->drawLine(x1 + offsetX, y1 + offsetY, x2 + offsetX, y2 + offsetY, color);
    }
  }
}

// --- Helper: draw color preview screen ---
void drawColorPreview() {
  gfx->fillScreen(COLOR_BLACK);
  
  uint16_t workColor = tempPreviewColor;
  uint16_t restColor = invertColor(tempPreviewColor);
  
  int16_t centerX = gfx->width() / 2;
  int16_t centerY = gfx->height() / 2;
  
  // Draw "WORK" label and color swatch at top
  int16_t workY = centerY - 60;
  drawCenteredText("WORK", centerX, workY - 30, workColor, 2);
  // Draw work color swatch (filled rectangle)
  int16_t swatchWidth = 80;
  int16_t swatchHeight = 40;
  gfx->fillRect(centerX - swatchWidth/2, workY - swatchHeight/2, swatchWidth, swatchHeight, workColor);
  gfx->drawRect(centerX - swatchWidth/2, workY - swatchHeight/2, swatchWidth, swatchHeight, COLOR_WHITE);
  
  // Draw "REST" label and color swatch at bottom
  int16_t restY = centerY + 60;
  drawCenteredText("REST", centerX, restY - 30, restColor, 2);
  // Draw rest color swatch (filled rectangle)
  gfx->fillRect(centerX - swatchWidth/2, restY - swatchHeight/2, swatchWidth, swatchHeight, restColor);
  gfx->drawRect(centerX - swatchWidth/2, restY - swatchHeight/2, swatchWidth, swatchHeight, COLOR_WHITE);
  
  // Draw X (cancel) and V (confirm) buttons at bottom
  int16_t btnY = gfx->height() - 40;
  int16_t btnSize = 30;
  int padding = 6;
  
  // Cancel button (X) on left
  int16_t cancelCenterX = gfx->width() / 4;
  previewCancelBtnLeft = cancelCenterX - btnSize/2 - padding;
  previewCancelBtnRight = cancelCenterX + btnSize/2 + padding;
  previewCancelBtnTop = btnY - btnSize/2 - padding;
  previewCancelBtnBottom = btnY + btnSize/2 + padding;
  gfx->drawRect(previewCancelBtnLeft, previewCancelBtnTop,
                previewCancelBtnRight - previewCancelBtnLeft,
                previewCancelBtnBottom - previewCancelBtnTop,
                COLOR_WHITE);
  drawCenteredText("X", cancelCenterX, btnY, COLOR_WHITE, 3);
  previewCancelBtnValid = true;
  
  // Confirm button (V) on right
  int16_t confirmCenterX = gfx->width() * 3 / 4;
  previewConfirmBtnLeft = confirmCenterX - btnSize/2 - padding;
  previewConfirmBtnRight = confirmCenterX + btnSize/2 + padding;
  previewConfirmBtnTop = btnY - btnSize/2 - padding;
  previewConfirmBtnBottom = btnY + btnSize/2 + padding;
  gfx->drawRect(previewConfirmBtnLeft, previewConfirmBtnTop,
                previewConfirmBtnRight - previewConfirmBtnLeft,
                previewConfirmBtnBottom - previewConfirmBtnTop,
                COLOR_WHITE);
  drawCenteredText("V", confirmCenterX, btnY, COLOR_WHITE, 3);
  previewConfirmBtnValid = true;
}

// --- Pomodoro control functions ---
// Last telegram send time to prevent duplicates
static unsigned long lastTgSendTime = 0;
const unsigned long TG_SEND_DEBOUNCE = 3000;  // 3 seconds

// Helper function to get current UI color based on work/rest session
uint16_t getCurrentUIColor() {
  if (isWorkSession) {
    return selectedWorkColor;  // Use user-selected color for work
  } else {
    return invertColor(selectedWorkColor);  // Inverted color for rest
  }
}

void displayStoppedState(); // forward
void drawTimer();           // forward
void drawProgressCircle(float progress, int centerX, int centerY, int radius, uint16_t color);
void drawCenteredText(const char *txt, int16_t cx, int16_t cy, uint16_t color, uint8_t size);  // forward
void updateDisplay();       // forward

void startTimer() {
  if (currentState == RUNNING) return;
  Serial.println("[TIMER] startTimer called");
  currentState = RUNNING;
  isWorkSession = true;
  startTime = millis();
  timerStartTime = millis();
  elapsedBeforePause = 0;
  displayInitialized = false;
  if (millis() - lastTgSendTime > TG_SEND_DEBOUNCE) {
    lastTgSendTime = millis();
    sendTelegramMessage("🍅 <b>Work started!</b>");
  }
}

void pauseTimer() {
  if (currentState != RUNNING) return;
  Serial.println("[TIMER] pauseTimer called");
  currentState = PAUSED;
  pausedTime = millis();
  elapsedBeforePause = millis() - startTime;
  if (millis() - lastTgSendTime > TG_SEND_DEBOUNCE) {
    lastTgSendTime = millis();
    sendTelegramMessage("⏸ <b>Timer paused</b>");
  }
}

void resumeTimer() {
  if (currentState != PAUSED) return;
  Serial.println("[TIMER] resumeTimer called");
  currentState = RUNNING;
  startTime = millis() - elapsedBeforePause;
  if (millis() - lastTgSendTime > TG_SEND_DEBOUNCE) {
    lastTgSendTime = millis();
    sendTelegramMessage("▶️ <b>Timer resumed</b>");
  }
}

void stopTimer() {
  if (currentState == STOPPED) return;
  Serial.println("[TIMER] stopTimer called");
  currentState = STOPPED;
  displayInitialized = false;
  if (millis() - lastTgSendTime > TG_SEND_DEBOUNCE) {
    lastTgSendTime = millis();
    sendTelegramMessage("⏹ <b>Timer stopped</b>");
  }
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
        // Send Telegram notification
        sendTelegramMessage("☕ <b>Rest time!</b> Take a break.");
      } else {
        isWorkSession = true;
        startTime = millis();
        displayInitialized = false;  // Force redraw to update colors
        // Send Telegram notification
        sendTelegramMessage("🍅 <b>Work time!</b> Focus on your task.");
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
            // Native touch panel is 172x320 (portrait)
            // Transform to rotated display coordinates
            switch (gfx->getRotation()) {
              case 0:  // Portrait normal
                touch_points.coords[i].x = 172 - 1 - x;
                touch_points.coords[i].y = y;
                break;
              case 1:  // Landscape right (320x172)
                touch_points.coords[i].x = y;
                touch_points.coords[i].y = 172 - 1 - x;
                break;
              case 2:  // Portrait upside down
                touch_points.coords[i].x = x;
                touch_points.coords[i].y = 320 - 1 - y;
                break;
              case 3:  // Landscape left (320x172)
                touch_points.coords[i].x = 320 - 1 - y;
                touch_points.coords[i].y = x;
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
            // Native touch panel is 172x320 (portrait)
            // Transform to rotated display coordinates
            switch (gfx->getRotation()) {
              case 0:  // Portrait normal
                touch_points.coords[i].x = 172 - 1 - x;
                touch_points.coords[i].y = y;
                break;
              case 1:  // Landscape right (320x172)
                touch_points.coords[i].x = y;
                touch_points.coords[i].y = 172 - 1 - x;
                break;
              case 2:  // Portrait upside down
                touch_points.coords[i].x = x;
                touch_points.coords[i].y = 320 - 1 - y;
                break;
              case 3:  // Landscape left (320x172)
                touch_points.coords[i].x = 320 - 1 - y;
                touch_points.coords[i].y = x;
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

      // Check for grid view buttons (X and ✓) when grid is active
      bool inGridCancelButton = false;
      bool inGridConfirmButton = false;
      int8_t tappedColorIndex = -1;  // Color cell tapped in grid (-1 = none)
      
      if (gridViewActive && lastTouchValid && tx >= 0 && ty >= 0) {
        // First check if tap is on a color cell
        int16_t lastRowY = (gridNumRows - 1) * gridCellHeight;
        if (ty < lastRowY) {
          // Calculate which cell was tapped
          int col = (tx - gridStartX) / gridCellWidth;
          int row = ty / gridCellHeight;
          if (col >= 0 && col < gridNumCols && tx >= gridStartX && tx < gridStartX + gridNumCols * gridCellWidth) {
            int colorIdx = row * gridNumCols + col;
            if (colorIdx >= 0 && colorIdx < paletteSize) {
              tappedColorIndex = colorIdx;
            }
          }
        }
        
        // Check cancel button - with extra touch padding
        if (gridCancelBtnValid) {
          if (tx >= gridCancelBtnLeft - TOUCH_PADDING && tx <= gridCancelBtnRight + TOUCH_PADDING &&
              ty >= gridCancelBtnTop - TOUCH_PADDING && ty <= gridCancelBtnBottom + TOUCH_PADDING) {
            inGridCancelButton = true;
          }
        }
        // Check confirm button - with extra touch padding
        if (gridConfirmBtnValid) {
          if (tx >= gridConfirmBtnLeft - TOUCH_PADDING && tx <= gridConfirmBtnRight + TOUCH_PADDING &&
              ty >= gridConfirmBtnTop - TOUCH_PADDING && ty <= gridConfirmBtnBottom + TOUCH_PADDING) {
            inGridConfirmButton = true;
          }
        }
      }

      // Check for home screen gear button (settings) when stopped - with extra touch padding
      bool inGearButton = false;
      if (currentState == STOPPED && currentViewMode == 0 && lastTouchValid && tx >= 0 && ty >= 0) {
        if (gearBtnValid) {
          if (tx >= gearBtnLeft - TOUCH_PADDING && tx <= gearBtnRight + TOUCH_PADDING &&
              ty >= gearBtnTop - TOUCH_PADDING && ty <= gearBtnBottom + TOUCH_PADDING) {
            inGearButton = true;
          }
        }
      }
      
      // Check for color preview buttons - with extra touch padding
      bool inPreviewCancelButton = false;
      bool inPreviewConfirmButton = false;
      if (currentViewMode == 2 && lastTouchValid && tx >= 0 && ty >= 0) {
        if (previewCancelBtnValid) {
          if (tx >= previewCancelBtnLeft - TOUCH_PADDING && tx <= previewCancelBtnRight + TOUCH_PADDING &&
              ty >= previewCancelBtnTop - TOUCH_PADDING && ty <= previewCancelBtnBottom + TOUCH_PADDING) {
            inPreviewCancelButton = true;
          }
        }
        if (previewConfirmBtnValid) {
          if (tx >= previewConfirmBtnLeft - TOUCH_PADDING && tx <= previewConfirmBtnRight + TOUCH_PADDING &&
              ty >= previewConfirmBtnTop - TOUCH_PADDING && ty <= previewConfirmBtnBottom + TOUCH_PADDING) {
            inPreviewConfirmButton = true;
          }
        }
      }

      // Check for mode button click first (with extra touch padding)
      bool inModeButton = false;
      if (modeBtnValid && lastTouchValid && tx >= 0 && ty >= 0) {
        if (tx >= modeBtnLeft - TOUCH_PADDING && tx <= modeBtnRight + TOUCH_PADDING &&
            ty >= modeBtnTop - TOUCH_PADDING && ty <= modeBtnBottom + TOUCH_PADDING) {
          inModeButton = true;
        }
      }
      
      // Check for status button click (with extra touch padding)
      bool inStatusButton = false;
      if (statusBtnValid && lastTouchValid && tx >= 0 && ty >= 0) {
        if (tx >= statusBtnLeft - TOUCH_PADDING && tx <= statusBtnRight + TOUCH_PADDING &&
            ty >= statusBtnTop - TOUCH_PADDING && ty <= statusBtnBottom + TOUCH_PADDING) {
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

      if (inGridCancelButton) {
        // X button clicked in grid view - return to home screen without saving
        Serial.println("*** GRID CANCEL (X) BUTTON CLICKED ***");
        tempSelectedColorIndex = -1;  // Clear temporary selection
        gridViewActive = false;
        currentViewMode = 0;  // Return to home
        displayStoppedState();  // Return to home screen
      } else if (inGridConfirmButton) {
        // ✓ button clicked in grid view - go to color preview
        Serial.println("*** GRID CONFIRM (✓) BUTTON CLICKED ***");
        if (tempSelectedColorIndex >= 0 && tempSelectedColorIndex < paletteSize) {
          tempPreviewColor = paletteColors[tempSelectedColorIndex];
          Serial.print("-> Preview color index: ");
          Serial.print(tempSelectedColorIndex);
          Serial.print(", color: 0x");
          Serial.println(tempPreviewColor, HEX);
        }
        tempSelectedColorIndex = -1;  // Clear temporary selection
        gridViewActive = false;
        currentViewMode = 2;  // Switch to color preview
        drawColorPreview();
      } else if (tappedColorIndex >= 0) {
        // Color cell tapped - select it
        Serial.print("*** COLOR CELL TAPPED: ");
        Serial.print(tappedColorIndex);
        Serial.print(" (0x");
        Serial.print(paletteColors[tappedColorIndex], HEX);
        Serial.println(") ***");
        tempSelectedColorIndex = tappedColorIndex;
        drawGrid();  // Redraw grid with new selection highlighted
      } else if (inPreviewCancelButton) {
        // X button clicked on color preview - return to home without saving
        Serial.println("*** PREVIEW CANCEL (X) BUTTON CLICKED ***");
        currentViewMode = 0;
        displayStoppedState();
      } else if (inPreviewConfirmButton) {
        // V button clicked on color preview - save color and return to home
        Serial.println("*** PREVIEW CONFIRM (V) BUTTON CLICKED ***");
        selectedWorkColor = tempPreviewColor;
        saveSelectedColor();  // Save to NVS for persistence
        Serial.print("-> Saved color: 0x");
        Serial.println(selectedWorkColor, HEX);
        currentViewMode = 0;
        displayStoppedState();
      } else if (inGearButton) {
        // Gear button clicked on home screen - show grid view (palette)
        Serial.println("*** GEAR BUTTON CLICKED ***");
        tempSelectedColorIndex = -1;  // Reset temporary selection
        gridViewActive = true;
        currentViewMode = 1;  // Grid/palette view
        drawGrid();
      } else if (inModeButton) {
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
    
    // Check for long press (only once per touch)
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
      // Don't reset - only one long press per touch
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
    // Check view mode: 0 = home, 1 = grid/palette, 2 = color preview
    if (currentViewMode == 0 && !gridViewActive) {
      // Normal stopped state is handled by displayStoppedState()
      return;
    }
    if (currentViewMode == 1 || gridViewActive) {
      // Grid view is active, no need to update (grid is static)
      return;
    }
    if (currentViewMode == 2) {
      // Color preview is active, no need to update (static)
      return;
    }
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
  
  // Check if we're in landscape mode (rotation 1 or 3)
  bool isLandscape = (currentRotation == 1 || currentRotation == 3);
  
  // Get current UI color based on work/rest session
  uint16_t uiColor = getCurrentUIColor();
  
  // Only redraw everything on first call or if state changed
  if (!displayInitialized) {
    gfx->fillScreen(COLOR_BLACK);
    drawProgressCircle(progress, centerX, centerY, radius, uiColor);
    displayInitialized = true;
    
    const char *statusTxt = nullptr;
    uint16_t statusColor = uiColor;
    bool useIcon = false;  // Use icon for pause/start
    bool isPauseIcon = false;
    
    // Status button: when running -> pause icon, when paused -> play icon
    if (currentState == PAUSED) {
      useIcon = true;
      isPauseIcon = false;  // Play icon
    } else if (currentState == RUNNING) {
      useIcon = true;
      isPauseIcon = true;   // Pause icon
    } else if (isWorkSession) {
      statusTxt = "work";
    } else {
      statusTxt = "rest";
    }
    
    // Calculate button size
    int16_t x1, y1;
    uint16_t w, h;
    int16_t iconSize = 24;  // Icon size
    
    if (useIcon) {
      w = iconSize + 8;
      h = iconSize;
    } else {
      gfx->setFont(nullptr);
      gfx->setTextSize(3, 3, 0);
      gfx->getTextBounds(statusTxt, 0, 0, &x1, &y1, &w, &h);
    }

    int padding = 6;
    int16_t statusCenterX, statusCenterY;
    
    if (isLandscape) {
      // Landscape: status button on the right side, vertically centered
      statusCenterX = gfx->width() - 35;
      statusCenterY = gfx->height() / 2;
      statusBtnLeft   = statusCenterX - (int16_t)w / 2 - padding;
      statusBtnRight  = statusCenterX + (int16_t)w / 2 + padding;
      statusBtnTop    = statusCenterY - (int16_t)h / 2 - padding;
      statusBtnBottom = statusCenterY + (int16_t)h / 2 + padding;
    } else {
      // Portrait: status button at the bottom center
      statusCenterX = gfx->width() / 2;
      statusCenterY = gfx->height() - 30;
      statusBtnLeft   = statusCenterX - (int16_t)w / 2 - padding;
      statusBtnRight  = statusCenterX + (int16_t)w / 2 + padding;
      statusBtnTop    = statusCenterY - (int16_t)h / 2 - padding;
      statusBtnBottom = statusCenterY + (int16_t)h / 2 + padding;
    }

    // Draw 1-pixel border around button
    gfx->drawRect(statusBtnLeft, statusBtnTop,
                  statusBtnRight - statusBtnLeft,
                  statusBtnBottom - statusBtnTop,
                  statusColor);

    // Draw icon or text centered inside the button
    int16_t btnCenterY = (statusBtnTop + statusBtnBottom) / 2;
    int16_t btnCenterX = (statusBtnLeft + statusBtnRight) / 2;
    if (useIcon) {
      if (isPauseIcon) {
        drawPauseIcon(btnCenterX, btnCenterY, iconSize, statusColor);
      } else {
        drawPlayIcon(btnCenterX, btnCenterY, iconSize, statusColor);
      }
    } else {
      drawCenteredText(statusTxt, btnCenterX, btnCenterY, statusColor, 3);
    }
    statusBtnValid = true;
    lastDisplayedState = currentState;  // Initialize state tracking
    
    // Draw mode button (left side in landscape, top center in portrait)
    const char *modeTxt = nullptr;
    switch (currentMode) {
      case MODE_1_1:  modeTxt = "1/1"; break;
      case MODE_25_5: modeTxt = "25/5"; break;
      case MODE_50_10: modeTxt = "50/10"; break;
      default: modeTxt = "25/5"; break;
    }
    
    gfx->setFont(nullptr);
    gfx->setTextSize(3, 3, 0);
    gfx->getTextBounds(modeTxt, 0, 0, &x1, &y1, &w, &h);
    
    padding = 4;
    int16_t modeCenterX, modeCenterY;
    
    if (isLandscape) {
      // Landscape: mode button on the left side, vertically centered
      modeCenterX = 35;
      modeCenterY = gfx->height() / 2;
      modeBtnLeft   = modeCenterX - (int16_t)w / 2 - padding;
      modeBtnRight  = modeCenterX + (int16_t)w / 2 + padding;
      modeBtnTop    = modeCenterY - (int16_t)h / 2 - padding;
      modeBtnBottom = modeCenterY + (int16_t)h / 2 + padding;
    } else {
      // Portrait: mode button at the top center
      int16_t topMargin = 24;
      modeCenterX = gfx->width() / 2;
      int16_t modeY = topMargin + (int16_t)h + padding;
      modeBtnLeft   = modeCenterX - (int16_t)w / 2 - padding;
      modeBtnRight  = modeCenterX + (int16_t)w / 2 + padding;
      modeBtnTop    = topMargin;
      modeBtnBottom = modeY + padding;
    }
    
    // Draw 1-pixel border around mode button
    gfx->drawRect(modeBtnLeft, modeBtnTop,
                  modeBtnRight - modeBtnLeft,
                  modeBtnBottom - modeBtnTop,
                  uiColor);
    
    // Draw mode text centered inside the button
    int16_t modeBtnCenterY = (modeBtnTop + modeBtnBottom) / 2;
    int16_t modeBtnCenterX = (modeBtnLeft + modeBtnRight) / 2;
    drawCenteredText(modeTxt, modeBtnCenterX, modeBtnCenterY, uiColor, 3);
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
    // Determine new status and color
    const char *statusTxt = nullptr;
    uint16_t statusColor = uiColor;  // Use current UI color (gold for work, blue for rest)
    bool useIcon = false;
    bool isPauseIcon = false;
    
    if (currentState == PAUSED) {
      useIcon = true;
      isPauseIcon = false;  // Play icon
    } else if (currentState == RUNNING) {
      useIcon = true;
      isPauseIcon = true;   // Pause icon
    } else if (isWorkSession) {
      statusTxt = "work";
    } else {
      statusTxt = "rest";
    }
    
    // Erase old status by drawing it in black (if we had one)
    if (lastDisplayedState != STOPPED) {
      // Erase old button area
      gfx->fillRect(statusBtnLeft, statusBtnTop,
                    statusBtnRight - statusBtnLeft,
                    statusBtnBottom - statusBtnTop,
                    COLOR_BLACK);
    }
    
    // Calculate button size
    int16_t x1, y1;
    uint16_t w, h;
    int16_t iconSize = 24;
    
    if (useIcon) {
      w = iconSize + 8;
      h = iconSize;
    } else {
      gfx->setFont(nullptr);
      gfx->setTextSize(3, 3, 0);
      gfx->getTextBounds(statusTxt, 0, 0, &x1, &y1, &w, &h);
    }

    int padding = 6;
    int16_t statusCenterX, statusCenterY;
    
    if (isLandscape) {
      // Landscape: status button on the right side
      statusCenterX = gfx->width() - 35;
      statusCenterY = gfx->height() / 2;
      statusBtnLeft   = statusCenterX - (int16_t)w / 2 - padding;
      statusBtnRight  = statusCenterX + (int16_t)w / 2 + padding;
      statusBtnTop    = statusCenterY - (int16_t)h / 2 - padding;
      statusBtnBottom = statusCenterY + (int16_t)h / 2 + padding;
    } else {
      // Portrait: status button at the bottom
      statusCenterX = gfx->width() / 2;
      statusCenterY = gfx->height() - 30;
      statusBtnLeft   = statusCenterX - (int16_t)w / 2 - padding;
      statusBtnRight  = statusCenterX + (int16_t)w / 2 + padding;
      statusBtnTop    = statusCenterY - (int16_t)h / 2 - padding;
      statusBtnBottom = statusCenterY + (int16_t)h / 2 + padding;
    }

    // Draw new border
    gfx->drawRect(statusBtnLeft, statusBtnTop,
                  statusBtnRight - statusBtnLeft,
                  statusBtnBottom - statusBtnTop,
                  statusColor);

    // Draw icon or text centered inside the button
    int16_t btnCenterY = (statusBtnTop + statusBtnBottom) / 2;
    int16_t btnCenterX = (statusBtnLeft + statusBtnRight) / 2;
    if (useIcon) {
      if (isPauseIcon) {
        drawPauseIcon(btnCenterX, btnCenterY, iconSize, statusColor);
      } else {
        drawPlayIcon(btnCenterX, btnCenterY, iconSize, statusColor);
      }
    } else {
      drawCenteredText(statusTxt, btnCenterX, btnCenterY, statusColor, 3);
    }
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
    if (modeBtnValid) {
      gfx->fillRect(modeBtnLeft, modeBtnTop,
                    modeBtnRight - modeBtnLeft,
                    modeBtnBottom - modeBtnTop,
                    COLOR_BLACK);
    }
    
    int16_t x1, y1;
    uint16_t w, h;
    gfx->setFont(nullptr);
    gfx->setTextSize(3, 3, 0);
    gfx->getTextBounds(modeTxt, 0, 0, &x1, &y1, &w, &h);
    
    int padding = 4;
    int16_t modeCenterX, modeCenterY;
    
    if (isLandscape) {
      // Landscape: mode button on the left side
      modeCenterX = 35;
      modeCenterY = gfx->height() / 2;
      modeBtnLeft   = modeCenterX - (int16_t)w / 2 - padding;
      modeBtnRight  = modeCenterX + (int16_t)w / 2 + padding;
      modeBtnTop    = modeCenterY - (int16_t)h / 2 - padding;
      modeBtnBottom = modeCenterY + (int16_t)h / 2 + padding;
    } else {
      // Portrait: mode button at the top center
      int16_t topMargin = 24;
      modeCenterX = gfx->width() / 2;
      int16_t modeY = topMargin + (int16_t)h + padding;
      modeBtnLeft   = modeCenterX - (int16_t)w / 2 - padding;
      modeBtnRight  = modeCenterX + (int16_t)w / 2 + padding;
      modeBtnTop    = topMargin;
      modeBtnBottom = modeY + padding;
    }
    
    // Draw new border
    gfx->drawRect(modeBtnLeft, modeBtnTop,
                  modeBtnRight - modeBtnLeft,
                  modeBtnBottom - modeBtnTop,
                  uiColor);
    
    // Draw text
    int16_t modeBtnCenterY = (modeBtnTop + modeBtnBottom) / 2;
    int16_t modeBtnCenterX = (modeBtnLeft + modeBtnRight) / 2;
    drawCenteredText(modeTxt, modeBtnCenterX, modeBtnCenterY, uiColor, 3);
    modeBtnValid = true;
    lastDisplayedMode = currentMode;
  }
}

// Flag to force progress circle redraw
bool forceCircleRedraw = false;

void drawProgressCircle(float progress, int centerX, int centerY, int radius, uint16_t color) {
  static float lastProgress = -1.0f;
  static bool circleDrawn = false;
  static uint16_t lastColor = COLOR_GOLD;
  int borderWidth = 5;
  
  // Force redraw on rotation change
  if (forceCircleRedraw) {
    circleDrawn = false;
    lastProgress = -1.0f;
    forceCircleRedraw = false;
  }
  
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

// --- Auto-rotation using IMU accelerometer ---
// Detect rotation based on accelerometer data (gravity direction)
uint8_t detectRotation() {
  if (!imuInitialized) return currentRotation;
  
  // IMU shares I2C bus with touch - no pin switching needed
  imu.update();
  imu.getAccel(&accelData);
  
  float ax = accelData.accelX;
  float ay = accelData.accelY;
  
  // Determine orientation based on which axis feels gravity
  // Portrait: Y-axis dominant, Landscape: X-axis dominant
  if (ay < -ROTATION_THRESHOLD) {
    return 0;  // Portrait normal (USB connector down)
  } else if (ay > ROTATION_THRESHOLD) {
    return 2;  // Portrait upside down (USB connector up)
  } else if (ax > ROTATION_THRESHOLD) {
    return 1;  // Landscape right
  } else if (ax < -ROTATION_THRESHOLD) {
    return 3;  // Landscape left
  }
  
  return currentRotation;  // Keep current if no clear orientation
}

// Apply new rotation to display and touch
void applyRotation(uint8_t newRotation) {
  if (newRotation == currentRotation) return;
  
  Serial.print("Rotation changed: ");
  Serial.print(currentRotation);
  Serial.print(" -> ");
  Serial.println(newRotation);
  
  currentRotation = newRotation;
  gfx->setRotation(currentRotation);
  
  // Re-initialize touch controller with new rotation
  bsp_touch_init(&Wire, TP_RST, TP_INT, gfx->getRotation(), gfx->width(), gfx->height());
  
  // Force full display refresh
  displayInitialized = false;
  forceCircleRedraw = true;  // Reset progress circle state
  memset(lastTimeStr, 0, sizeof(lastTimeStr));
  
  // Redraw current screen
  if (currentState == STOPPED && !gridViewActive) {
    drawSplash();
  } else if (gridViewActive) {
    drawGrid();
  } else {
    gfx->fillScreen(COLOR_BLACK);
    drawTimer();
  }
}

// Check and handle auto-rotation (called from loop)
void checkAutoRotation() {
  if (!imuInitialized) return;
  
  unsigned long now = millis();
  if (now - lastRotationCheck < ROTATION_CHECK_INTERVAL) return;
  lastRotationCheck = now;
  
  uint8_t newRotation = detectRotation();
  if (newRotation != currentRotation) {
    applyRotation(newRotation);
  }
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

  // Initialize IMU (QMI8658) for auto-rotation
  // IMU shares I2C bus with touch controller
  Serial.println("Initializing IMU (QMI8658)...");
  int imuErr = imu.init(imuCalibration, IMU_ADDRESS);
  if (imuErr != 0) {
    Serial.print("IMU init failed with error: ");
    Serial.println(imuErr);
    imuInitialized = false;
  } else {
    Serial.println("IMU initialized successfully!");
    imuInitialized = true;
  }

  // Load saved color from NVS
  loadSelectedColor();
  
  // Connect to WiFi
  connectWiFi();
  
  // Initialize Telegram bot
  initTelegramBot();
  
  // Start Telegram task on separate core
  startTelegramTask();

  displayStoppedState();
}

void loop() {
  // Handle touch FIRST - highest priority for responsiveness
  handleTouchInput();
  
  // Process commands from Telegram (non-blocking - just checks flags)
  processTelegramCommands();
  
  updateTimer();
  updateDisplay();
  checkAutoRotation();  // Check IMU for auto-rotation

  // Tap indicator disabled for better touch responsiveness
  // (was causing lag due to drawing overhead)

  delay(2);  // Reduced delay for faster loop
}