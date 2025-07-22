#include <Arduino.h>
#include <WiFi.h>
// Replace the problematic async library with standard HTTPClient
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <FastLED.h>
#include <FastLED_NeoMatrix.h>
#include <Adafruit_GFX.h>
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <esp_task_wdt.h>

// Built-in GFX fonts - RELIABLY AVAILABLE
#include <Fonts/TomThumb.h>  // 3x5 pixel font - numbers + letters (compact)

// Additional Adafruit built-in fonts (commented until needed)
// #include <Fonts/FreeMono9pt7b.h>      // 9pt monospace
// #include <Fonts/FreeMonoBold9pt7b.h>  // 9pt monospace bold
// #include <Fonts/Tiny3x3a2pt7b.h>      // 3x3 ultra tiny

// External robjen/GFX_fonts collection (temporarily disabled for reliable build)
// Will re-enable with proper include path resolution
// #include "Font3x5FixedNum.h"     // 3x5 - **NUMBERS ONLY**
// #include "Font5x7FixedMono.h"    // 5x7 - numbers + letters (monospace)

#include "config.h"

// WiFi Configuration defaults (can be overridden in config.h)
#ifndef WIFI_CONNECT_TIMEOUT
#define WIFI_CONNECT_TIMEOUT 15000   // 15 seconds per connection attempt
#endif

#ifndef WIFI_MAX_ATTEMPTS  
#define WIFI_MAX_ATTEMPTS 3          // 3 connection attempts
#endif

#ifndef WIFI_RECONNECT_INTERVAL
#define WIFI_RECONNECT_INTERVAL 10000 // 10 second base reconnect interval
#endif

#define BTC_API_URL "https://pro-api.coingecko.com/api/v3/simple/price?ids=bitcoin&vs_currencies=usd&include_24hr_change=true"

// Font selection enum for easy switching
enum FontType {
  FONT_BUILTIN,           // 6x8 built-in font (default)
  FONT_TOMTHUMB,          // 3x5 - numbers + letters (compact)
//   // NUMBERS ONLY fonts (ultra compact for price display)
//   FONT_2X5_NUM,           // 2x5 - **NUMBERS ONLY** (ultra compact)
//   FONT_3X5_NUM,           // 3x5 - **NUMBERS ONLY** (2 variants: rounder/squarer)
//   FONT_3X7_NUM,           // 3x7 - **NUMBERS ONLY** (rounder/squarer variants)
//   // FULL CHARACTER SET fonts (numbers + letters)
//   FONT_4X5_FIXED,         // 4x5 - numbers + letters (proportional, compact)
//   FONT_4X7_FIXED,         // 4x7 - numbers + letters (proportional)
//   FONT_5X5_FIXED,         // 5x5 - numbers + letters (proportional)
//   FONT_5X7_FIXED,         // 5x7 - numbers + letters (proportional)
//   FONT_5X7_MONO           // 5x7 - numbers + letters (monospace, best readability)
};

// Scroll state structure for independent scrolling text management
struct ScrollState {
    int16_t offset;
    unsigned long lastUpdate;
    unsigned long speed;
    
    ScrollState(int16_t startOffset = 32, unsigned long scrollSpeed = 100) 
        : offset(startOffset), lastUpdate(0), speed(scrollSpeed) {}
    
    bool shouldUpdate() {
        return (millis() - lastUpdate) > speed;
    }
    
    void update() {
        lastUpdate = millis();
        offset--;
    }
    
    void reset(int16_t resetOffset = 32) {
        offset = resetOffset;
    }
};

// Forward declarations
void connectToWiFi();
void setupOTA();
void setupWebServer();
void addToConsoleBuffer(const String& message);
void fetchBTCPriceTask(void *pvParameters);
void fetchOHLCDataTask(void *pvParameters);
void suspendHttpTasks();
void resumeHttpTasks();

// Text printing function declarations
void printText(int16_t x, int16_t y, const char* text, FontType fontType = FONT_BUILTIN, uint16_t color = 0xFFFF);
void printTextCentered(int16_t width, int16_t y, const char* text, FontType fontType = FONT_BUILTIN, uint16_t color = 0xFFFF);
void printScrollingText(int16_t y, const char* text, ScrollState& scrollState, FontType fontType = FONT_BUILTIN, uint16_t color = 0xFFFF);
void updateScrollingText(int16_t y, const char* text, ScrollState& scrollState, int16_t resetOffset, FontType fontType = FONT_BUILTIN, uint16_t color = 0xFFFF);
void updateMultiColorScrollingText(int16_t y, ScrollState& scrollState, FontType fontType);

// LED Array
CRGB leds[NUM_LEDS];
unsigned long lastUpdate = 0;

// Global variables for BTC price data
double currentBTCPrice = 0.0;
double btc24hChange = 0.0;
double btc1hChange = 0.0;
// double btc4hChange = 0.0;
double btc1dChange = 0.0;
bool wifiConnected = false;

// OTA state management
bool otaInProgress = false;
bool httpTasksSuspended = false;

// Request state management
bool priceRequestInProgress = false;
bool ohlcHourlyRequestInProgress = false;
bool ohlcDailyRequestInProgress = false;
unsigned long lastPriceRequestTime = 0;
unsigned long lastOHLCRequestTime = 0;
const unsigned long REQUEST_TIMEOUT = 10000;  // 10 seconds timeout for requests

// WiFi status management
unsigned long lastReconnectAttempt = 0;

// Scroll state instances for different text lines
ScrollState connectingScroll(0, 100);  // "Connecting..." - starts visible left, 100ms speed
ScrollState offlineScroll(32, 150);    // "Offline" - starts from right, 150ms speed (slower)
ScrollState changeScroll(0, 120);      // "24H: x.x%" - 120ms speed

// FastLED_NeoMatrix setup for 32x16 matrix
FastLED_NeoMatrix *matrix = new FastLED_NeoMatrix(leds, 32, 16, NEO_MATRIX_BOTTOM + NEO_MATRIX_RIGHT + NEO_MATRIX_COLUMNS + NEO_MATRIX_ZIGZAG);

// Task handles for non-blocking HTTP requests
TaskHandle_t priceTaskHandle = NULL;
TaskHandle_t ohlcTaskHandle = NULL;

// Mutex for thread-safe access to shared variables
SemaphoreHandle_t priceMutex;

// Web server for console monitoring
WebServer server(80);
String consoleBuffer = "";
const int MAX_CONSOLE_BUFFER = 8192;  // 8KB buffer for console output

void setup() {
    Serial.begin(115200);
    Serial.println("ESP32 LED Matrix BTC Ticker Starting...");
    
    // Initialize FastLED
    FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS).setCorrection(TypicalLEDStrip);
    FastLED.setBrightness(BRIGHTNESS);
    
    // Initialize matrix for text rendering
    matrix->begin();
    matrix->setBrightness(BRIGHTNESS);
    
    // Clear all LEDs
    fill_solid(leds, NUM_LEDS, CRGB::Black);
    FastLED.show();
    
    // Show "GM" during initialization
    uint16_t white = matrix->Color(255, 255, 255);
    printTextCentered(32, 8, "GM", FONT_BUILTIN, white);
    matrix->show();
    delay(1000);  // Show for 1 second
    
    // Create mutex for thread-safe access
    priceMutex = xSemaphoreCreateMutex();
    
    // Connect to WiFi
    connectToWiFi();
    
    // Setup OTA after WiFi connection
    if (WiFi.status() == WL_CONNECTED) {
        setupOTA();
        setupWebServer();
    }
    
    // Create tasks for non-blocking HTTP requests
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("Creating HTTP request tasks...");
        addToConsoleBuffer("Creating HTTP request tasks...");
        
        // Create price fetching task on Core 0
        xTaskCreatePinnedToCore(
            fetchBTCPriceTask,    // Task function
            "PriceTask",          // Task name
            8192,                 // Stack size
            NULL,                 // Parameters
            1,                    // Priority
            &priceTaskHandle,     // Task handle
            0                     // Core 0
        );
        
        // Create OHLC fetching task on Core 0
        xTaskCreatePinnedToCore(
            fetchOHLCDataTask,    // Task function
            "OHLCTask",           // Task name
            8192,                 // Stack size
            NULL,                 // Parameters
            1,                    // Priority
            &ohlcTaskHandle,      // Task handle
            0                     // Core 0
        );
        
        Serial.println("HTTP tasks created successfully!");
        addToConsoleBuffer("HTTP tasks created successfully!");
    }
    
    Serial.println("Setup complete!");
    addToConsoleBuffer("Setup complete!");
}

void loop() {
    // Handle OTA updates
    ArduinoOTA.handle();
    
    // Handle web server requests
    server.handleClient();
    
    // Don't clear entire screen - causes flicker
    
    // Check WiFi connection with enhanced monitoring
    if (WiFi.status() != WL_CONNECTED) {
        if (wifiConnected) {
            // Just lost connection - log the event
            Serial.printf("WiFi connection lost! Previous RSSI: %ddBm\n", WiFi.RSSI());
            addToConsoleBuffer("WiFi connection lost - attempting reconnect");
            wifiConnected = false;
        }
        
        // Show scrolling "Offline" text
        uint16_t red = matrix->Color(255, 0, 0);
        updateScrollingText(8, "Offline", offlineScroll, 32, FONT_BUILTIN, red);
        
        // Smarter reconnection with escalating intervals
        unsigned long timeSinceLastAttempt = millis() - lastReconnectAttempt;
        unsigned long reconnectInterval = WIFI_RECONNECT_INTERVAL;
        
        // Escalate reconnect attempts: 10s, 30s, 60s, then back to 10s
        static int reconnectAttempts = 0;
        if (reconnectAttempts >= 3) {
            reconnectInterval = 60000;  // 1 minute
        } else if (reconnectAttempts >= 1) {
            reconnectInterval = 30000;  // 30 seconds
        }
        
        if (timeSinceLastAttempt > reconnectInterval) {
            reconnectAttempts++;
            Serial.printf("WiFi offline for %lu seconds, reconnect attempt #%d\n", 
                         timeSinceLastAttempt / 1000, reconnectAttempts);
            
            // Reset offline scroll for next display cycle
            offlineScroll.reset(32);
            
            // Check if we can see our network before attempting connection
            WiFi.scanNetworks(true);  // Async scan
            delay(100);  // Brief delay for scan to start
            
            connectToWiFi();
            lastReconnectAttempt = millis();
            
            // Reset attempt counter on successful connection
            if (WiFi.status() == WL_CONNECTED) {
                reconnectAttempts = 0;
            }
        }
        
        delay(50);  // Small delay to prevent excessive CPU usage
        return;
    }
    
    // WiFi is connected
    if (!wifiConnected) {
        wifiConnected = true;
        // Clear display and show brief connection success
        fill_solid(leds, NUM_LEDS, CRGB::Black);
        uint16_t green = matrix->Color(0, 255, 0);
        printTextCentered(32, 8, "Connected", FONT_TOMTHUMB, green);
        matrix->show();
        delay(1000);  // Show success message briefly
        fill_solid(leds, NUM_LEDS, CRGB::Black);
        matrix->show();
    }
    
    // Display BTC ticker if we have price data
    if (currentBTCPrice > 0) {
        
        // Clear price area to prevent ghosting
        matrix->fillRect(0, 0, 32, 8, 0);  // Clear top price area
        
        // Display BTC price centered at top (white, whole number)
        char priceStr[16];
        sprintf(priceStr, "%.0f", currentBTCPrice);  // Whole number, no decimals
        uint16_t white = matrix->Color(255, 255, 255);
        printTextCentered(32, 7, priceStr, FONT_TOMTHUMB, white);
        
        // Display scrolling multi-timeframe changes at bottom (each interval color-coded)
        updateMultiColorScrollingText(14, changeScroll, FONT_TOMTHUMB);
    }

    matrix->show();
}

void connectToWiFi() {
    Serial.print("Connecting to WiFi SSID: ");
    Serial.println(WIFI_SSID);
    
    // Disconnect and clear any existing connection state
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    delay(100);
    WiFi.mode(WIFI_STA);
    delay(500);
    
    // Scan for networks and find the strongest matching SSID
    Serial.println("Scanning for networks...");
    int numNetworks = WiFi.scanNetworks();
    Serial.printf("Found %d networks:\n", numNetworks);
    
    int bestRSSI = -1000;
    int bestChannel = 0;
    String targetSSID = String(WIFI_SSID);
    bool targetFound = false;
    
    for (int i = 0; i < numNetworks; i++) {
        String ssid = WiFi.SSID(i);
        int rssi = WiFi.RSSI(i);
        String security = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? "OPEN" : "ENCRYPTED";
        
        Serial.printf("  %d: %s (%ddBm) %s", i, ssid.c_str(), rssi, security.c_str());
        
        // Check if this is our target SSID and if it's stronger than previous matches
        if (ssid == targetSSID) {
            targetFound = true;
            if (rssi > bestRSSI) {
                bestRSSI = rssi;
                bestChannel = WiFi.channel(i);
                Serial.print(" <- BEST MATCH");
            } else {
                Serial.print(" <- MATCH");
            }
        }
        Serial.println();
    }
    
    if (!targetFound) {
        Serial.printf("ERROR: Target SSID '%s' not found in scan!\n", WIFI_SSID);
        addToConsoleBuffer("ERROR: Target SSID not found in scan");
        return;
    }
    
    Serial.printf("Connecting to strongest '%s' signal: %ddBm (channel %d)\n", 
                  WIFI_SSID, bestRSSI, bestChannel);
    addToConsoleBuffer("Connecting to strongest signal: " + String(bestRSSI) + "dBm");
    
    // Reset scroll state for "Connecting..." animation (start from left visible)
    connectingScroll.reset(0);  // Start from left edge, visible immediately
    
    // Multiple connection attempts with different strategies
    for (int attempt = 1; attempt <= WIFI_MAX_ATTEMPTS; attempt++) {
        Serial.printf("\n=== Connection Attempt %d/%d ===\n", attempt, WIFI_MAX_ATTEMPTS);
        
        // Reset connecting animation to start from left for each attempt
        connectingScroll.reset(0);
        
        // Clean slate for each attempt
        WiFi.disconnect(true);
        delay(500);
        
        // Configure WiFi with optimal settings
        WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);  // Clear static IP
        WiFi.setAutoReconnect(true);
        WiFi.setHostname(DEVICE_HOSTNAME);
        
        // Start connection
        wl_status_t connectResult = WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
        Serial.printf("WiFi.begin() returned: %d\n", connectResult);
        
        // Wait for connection with detailed status monitoring
        unsigned long connectStart = millis();
        unsigned long lastStatusCheck = 0;
        int statusChecks = 0;
        
        while (WiFi.status() != WL_CONNECTED && (millis() - connectStart) < WIFI_CONNECT_TIMEOUT) {
            // Update connecting animation
            matrix->fillScreen(0);
            uint16_t yellow = matrix->Color(255, 255, 0);
            char connectMsg[32];
            sprintf(connectMsg, "Connecting %d/%d...", attempt, WIFI_MAX_ATTEMPTS);
            updateScrollingText(5, connectMsg, connectingScroll, 32, FONT_BUILTIN, yellow);
            
            delay(200);
            Serial.print(".");
            statusChecks++;
            
            // Detailed status every 2 seconds
            if (millis() - lastStatusCheck > 2000) {
                wl_status_t status = WiFi.status();
                Serial.printf("\nStatus check %d: %d (", statusChecks, status);
                
                switch(status) {
                    case WL_IDLE_STATUS: Serial.print("IDLE"); break;
                    case WL_NO_SSID_AVAIL: Serial.print("NO_SSID_AVAIL"); break;
                    case WL_SCAN_COMPLETED: Serial.print("SCAN_COMPLETED"); break;
                    case WL_CONNECTED: Serial.print("CONNECTED"); break;
                    case WL_CONNECT_FAILED: Serial.print("CONNECT_FAILED"); break;
                    case WL_CONNECTION_LOST: Serial.print("CONNECTION_LOST"); break;
                    case WL_DISCONNECTED: Serial.print("DISCONNECTED"); break;
                    default: Serial.print("UNKNOWN"); break;
                }
                Serial.printf(") RSSI: %ddBm\n", WiFi.RSSI());
                lastStatusCheck = millis();
                
                // Break early on certain failures
                if (status == WL_CONNECT_FAILED || status == WL_NO_SSID_AVAIL) {
                    Serial.println("Connection failed - trying next attempt");
                    break;
                }
            }
        }
        
        // Check final result
        if (WiFi.status() == WL_CONNECTED) {
            wifiConnected = true;
            Serial.printf("\n✓ WiFi connected successfully on attempt %d!\n", attempt);
            Serial.printf("IP address: %s\n", WiFi.localIP().toString().c_str());
            Serial.printf("Signal strength: %ddBm\n", WiFi.RSSI());
            Serial.printf("Channel: %d\n", WiFi.channel());
            
            addToConsoleBuffer("WiFi connected! IP: " + WiFi.localIP().toString() + 
                              " RSSI: " + String(WiFi.RSSI()) + "dBm");
            
            // Flash green to indicate WiFi connection
            fill_solid(leds, NUM_LEDS, CRGB::Green);
            FastLED.show();
            delay(500);
            fill_solid(leds, NUM_LEDS, CRGB::Black);
            FastLED.show();
            return;
        } else {
            wl_status_t finalStatus = WiFi.status();
            Serial.printf("\n✗ Attempt %d failed. Final status: %d\n", attempt, finalStatus);
            
            // Handle specific error cases
            if (finalStatus == WL_CONNECT_FAILED) {
                Serial.println("Authentication likely failed - check password");
                addToConsoleBuffer("Auth failed - check password (attempt " + String(attempt) + ")");
            } else if (finalStatus == WL_NO_SSID_AVAIL) {
                Serial.println("SSID not available - signal may be too weak");
                addToConsoleBuffer("SSID unavailable (attempt " + String(attempt) + ")");
            }
            
            // Wait before next attempt (exponential backoff)
            if (attempt < WIFI_MAX_ATTEMPTS) {
                int waitTime = attempt * 2;  // 2, 4, 6 seconds
                Serial.printf("Waiting %d seconds before next attempt...\n", waitTime);
                delay(waitTime * 1000);
            }
        }
    }
    
    // All attempts failed
    Serial.println("\n✗ All connection attempts failed!");
    addToConsoleBuffer("All WiFi connection attempts failed");
    
    // Flash red to indicate WiFi failure
    fill_solid(leds, NUM_LEDS, CRGB::Red);
    FastLED.show();
    delay(1000);
    fill_solid(leds, NUM_LEDS, CRGB::Black);
    FastLED.show();
}

void setupOTA() {
    // Set hostname for mDNS
    if (!MDNS.begin(DEVICE_HOSTNAME)) {
        Serial.println("Error setting up mDNS responder!");
        return;
    }
    Serial.println("mDNS responder started");
    
    // Configure OTA
    ArduinoOTA.setHostname(DEVICE_HOSTNAME);
    
    // Pre-flight checks for OTA readiness
    Serial.println("=== OTA PRE-FLIGHT CHECKS ===");
    Serial.printf("Total heap: %d bytes\n", ESP.getHeapSize());
    Serial.printf("Free heap: %d bytes\n", ESP.getFreeHeap());
    Serial.printf("Min free heap: %d bytes\n", ESP.getMinFreeHeap());
    Serial.printf("Max alloc heap: %d bytes\n", ESP.getMaxAllocHeap());
    Serial.printf("Flash size: %d bytes\n", ESP.getFlashChipSize());
    Serial.printf("Free sketch space: %d bytes\n", ESP.getFreeSketchSpace());
    
    // Verify we have adequate space for OTA
    if (ESP.getFreeSketchSpace() < 1000000) {
        Serial.println("WARNING: Insufficient flash space for OTA!");
        addToConsoleBuffer("WARNING: Insufficient flash space for OTA!");
    } else {
        Serial.println("Flash space check: PASSED");
        addToConsoleBuffer("Flash space check: PASSED");
    }
    
    // OTA event handlers with LED feedback and HTTP task management
    ArduinoOTA.onStart([]() {
        String type = (ArduinoOTA.getCommand() == U_FLASH) ? "sketch" : "filesystem";
        Serial.println("=== OTA UPDATE STARTING ===");
        Serial.println("Updating: " + type);
        addToConsoleBuffer("OTA UPDATE STARTING - Type: " + type);
        
        // Print memory and flash information for debugging
        Serial.printf("Free heap: %d bytes\n", ESP.getFreeHeap());
        Serial.printf("Free sketch space: %d bytes\n", ESP.getFreeSketchSpace());
        Serial.printf("Sketch size: %d bytes\n", ESP.getSketchSize());
        Serial.printf("Flash chip size: %d bytes\n", ESP.getFlashChipSize());
        Serial.printf("Flash chip speed: %d Hz\n", ESP.getFlashChipSpeed());
        
        // Check if we have enough space for OTA
        size_t freeSpace = ESP.getFreeSketchSpace();
        if (freeSpace < 1000000) {  // Less than 1MB free
            Serial.printf("WARNING: Low flash space for OTA: %d bytes\n", freeSpace);
        }
        
        addToConsoleBuffer("Memory check - Free heap: " + String(ESP.getFreeHeap()) + 
                          ", Flash space: " + String(ESP.getFreeSketchSpace()));
        
        // Critical: Suspend HTTP tasks to avoid conflicts
        otaInProgress = true;
        suspendHttpTasks();
        
        // Wait longer for tasks to fully suspend and memory to stabilize
        delay(500);
        
        // Extend watchdog timeout to prevent timeout during flash erase
        // Note: Don't fully disable watchdog as it can cause other issues
        esp_task_wdt_reset();  // Reset watchdog before long operation
        
        // Turn off ALL LEDs to reduce power consumption during flash operations
        fill_solid(leds, NUM_LEDS, CRGB::Black);
        FastLED.setBrightness(0);  // Minimize power draw
        FastLED.show();
        
        Serial.println("Watchdog disabled, LEDs off, tasks suspended");
        addToConsoleBuffer("OTA environment: watchdog disabled, LEDs off, tasks suspended");
        
        Serial.println("OTA environment prepared successfully");
        addToConsoleBuffer("OTA environment prepared successfully");
    });
    
    ArduinoOTA.onEnd([]() {
        Serial.println("\n=== OTA UPDATE COMPLETED ===");
        addToConsoleBuffer("OTA UPDATE COMPLETED SUCCESSFULLY");
        
        // Restore LED brightness and show success
        FastLED.setBrightness(BRIGHTNESS);
        fill_solid(leds, NUM_LEDS, CRGB::Green);
        FastLED.show();
        delay(1000);
        
        // Resume HTTP tasks
        otaInProgress = false;
        resumeHttpTasks();
        
        Serial.println("Device will restart in 2 seconds...");
        addToConsoleBuffer("Device will restart in 2 seconds...");
    });
    
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        static unsigned long lastProgressReport = 0;
        unsigned int percent = (progress / (total / 100));
        
        // Reset watchdog frequently during flash operations to prevent timeout
        esp_task_wdt_reset();
        
        // Report progress every 5% to monitor flash operations closely
        if (millis() - lastProgressReport > 500 || percent == 100) {
            Serial.printf("OTA Progress: %u%% (%u/%u bytes) - Flash: OK, WDT: Reset\n", percent, progress, total);
            addToConsoleBuffer("OTA Progress: " + String(percent) + "% - Flash: OK");
            lastProgressReport = millis();
            
            // Also log memory status during critical flash operations
            if (percent % 25 == 0) {  // Every 25%
                Serial.printf("Memory check - Free heap: %d bytes\n", ESP.getFreeHeap());
            }
        }
        
        // LEDs disabled during OTA to reduce power consumption
        // No LED updates to avoid interfering with flash operations
    });
    
    ArduinoOTA.onError([](ota_error_t error) {
        Serial.printf("=== OTA ERROR [%u] ===\n", error);
        String errorMsg = "OTA ERROR: ";
        
        if (error == OTA_AUTH_ERROR) {
            Serial.println("Authentication Failed");
            errorMsg += "Authentication Failed";
        } else if (error == OTA_BEGIN_ERROR) {
            Serial.println("Begin Failed");
            errorMsg += "Begin Failed";
        } else if (error == OTA_CONNECT_ERROR) {
            Serial.println("Connect Failed");
            errorMsg += "Connect Failed";
        } else if (error == OTA_RECEIVE_ERROR) {
            Serial.println("Receive Failed");
            errorMsg += "Receive Failed";
        } else if (error == OTA_END_ERROR) {
            Serial.println("End Failed");
            errorMsg += "End Failed";
        } else {
            Serial.println("Unknown Error");
            errorMsg += "Unknown Error";
        }
        
        addToConsoleBuffer(errorMsg);
        
        // Restore LED brightness and show error
        FastLED.setBrightness(BRIGHTNESS);
        fill_solid(leds, NUM_LEDS, CRGB::Red);
        FastLED.show();
        
        // Resume HTTP tasks on error
        otaInProgress = false;
        resumeHttpTasks();
        
        Serial.println("HTTP tasks resumed after OTA error");
        addToConsoleBuffer("HTTP tasks resumed after OTA error");
    });
    
    ArduinoOTA.begin();
    Serial.println("OTA Ready");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
}

void addToConsoleBuffer(const String& message) {
    // Add timestamp
    String timestampedMessage = "[" + String(millis()) + "] " + message + "\n";
    consoleBuffer += timestampedMessage;
    
    // Keep buffer size manageable
    if (consoleBuffer.length() > MAX_CONSOLE_BUFFER) {
        consoleBuffer = consoleBuffer.substring(consoleBuffer.length() - MAX_CONSOLE_BUFFER);
    }
}

void setupWebServer() {
    // Console monitoring page
    server.on("/", []() {
        String html = "<!DOCTYPE html><html><head><title>ESP32 BTC Ticker Console</title>";
        html += "<meta http-equiv='refresh' content='2'>";
        html += "<style>body{font-family:monospace;background:#000;color:#0f0;padding:20px;} ";
        html += "pre{white-space:pre-wrap;word-wrap:break-word;}</style></head>";
        html += "<body><h1>ESP32 BTC Ticker Console</h1>";
        html += "<p>Auto-refresh every 2 seconds | <a href='/clear'>Clear Buffer</a></p>";
        html += "<pre>" + consoleBuffer + "</pre>";
        html += "</body></html>";
        server.send(200, "text/html", html);
    });
    
    // API endpoint for just the console data
    server.on("/console", []() {
        server.send(200, "text/plain", consoleBuffer);
    });
    
    // Clear console buffer
    server.on("/clear", []() {
        consoleBuffer = "";
        addToConsoleBuffer("Console buffer cleared");
        server.sendHeader("Location", "/");
        server.send(302, "text/plain", "");
    });
    
    server.begin();
    Serial.println("Web server started on http://" + String(DEVICE_HOSTNAME) + ".local");
    addToConsoleBuffer("Web server started on http://" + String(DEVICE_HOSTNAME) + ".local");
}

// Task function for fetching BTC price (runs on separate core)
void fetchBTCPriceTask(void *pvParameters) {
    HTTPClient http;
    WiFiClientSecure client;
    client.setInsecure(); // Skip certificate verification for faster performance
    
    while (true) {
        // Skip HTTP requests during OTA to avoid conflicts
        if (wifiConnected && !priceRequestInProgress && !otaInProgress) {
            priceRequestInProgress = true;
            
            http.begin(client, BTC_API_URL);
            http.addHeader("x-cg-pro-api-key", COINGECKO_API_KEY);
            
            int httpCode = http.GET();
            
            if (httpCode == HTTP_CODE_OK) {
                String payload = http.getString();
                
                // Parse JSON response
                DynamicJsonDocument doc(1024);
                if (deserializeJson(doc, payload) == DeserializationError::Ok) {
                    if (doc["bitcoin"]["usd"] && doc["bitcoin"]["usd_24h_change"]) {
                        // Thread-safe update of shared variables
                        if (xSemaphoreTake(priceMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                            currentBTCPrice = doc["bitcoin"]["usd"];
                            btc24hChange = doc["bitcoin"]["usd_24h_change"];
                            
                            // Round to nearest cent
                            currentBTCPrice = round(currentBTCPrice * 100.0) / 100.0;
                            btc24hChange = round(btc24hChange * 100.0) / 100.0;
                            
                            Serial.printf("[TASK] BTC price: $%.2f USD, 24h: %+.2f%%\n", currentBTCPrice, btc24hChange);
                            addToConsoleBuffer("BTC price: $" + String(currentBTCPrice, 2) + " USD, 24h: " + String(btc24hChange, 2) + "%");
                            xSemaphoreGive(priceMutex);
                        }
                    }
                }
            } else {
                Serial.printf("[TASK] HTTP GET failed, error: %d\n", httpCode);
            }
            
            http.end();
            priceRequestInProgress = false;
        }
        
        // Wait 30 seconds before next request (adjust as needed)
        vTaskDelay(pdMS_TO_TICKS(UPDATE_INTERVAL));
    }
}

// Task function for fetching OHLC data
void fetchOHLCDataTask(void *pvParameters) {
    HTTPClient httpHourly, httpDaily;
    WiFiClientSecure client;
    client.setInsecure();
    
    while (true) {
        // Skip HTTP requests during OTA to avoid conflicts
        if (wifiConnected && !ohlcHourlyRequestInProgress && !otaInProgress) {
            ohlcHourlyRequestInProgress = true;
            
            // Fetch hourly data
            httpHourly.begin(client, "https://pro-api.coingecko.com/api/v3/coins/bitcoin/ohlc?vs_currency=usd&days=1&interval=hourly");
            httpHourly.addHeader("x-cg-pro-api-key", COINGECKO_API_KEY);
            
            int httpCode = httpHourly.GET();
            if (httpCode == HTTP_CODE_OK) {
                String payload = httpHourly.getString();
                
                DynamicJsonDocument doc(8192);
                if (deserializeJson(doc, payload) == DeserializationError::Ok) {
                    if (doc.is<JsonArray>() && doc.size() >= 1) {
                        JsonArray ohlcArray = doc.as<JsonArray>();
                        int dataPoints = ohlcArray.size();
                        
                        if (dataPoints >= 1) {
                            double price1hClose = ohlcArray[dataPoints - 1][4];
                            
                            if (xSemaphoreTake(priceMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                                btc1hChange = ((currentBTCPrice - price1hClose) / price1hClose) * 100.0;
                                                            Serial.printf("[TASK] 1h change: %+.2f%%\n", btc1hChange);
                            addToConsoleBuffer("1h change: " + String(btc1hChange, 2) + "%");
                            xSemaphoreGive(priceMutex);
                            }
                        }
                    }
                }
            }
            httpHourly.end();
            
            // Small delay between requests
            vTaskDelay(pdMS_TO_TICKS(2000));
            
            // Fetch daily data
            httpDaily.begin(client, "https://pro-api.coingecko.com/api/v3/coins/bitcoin/ohlc?vs_currency=usd&days=1&interval=daily");
            httpDaily.addHeader("x-cg-pro-api-key", COINGECKO_API_KEY);
            
            httpCode = httpDaily.GET();
            if (httpCode == HTTP_CODE_OK) {
                String payload = httpDaily.getString();
                
                DynamicJsonDocument doc(2048);
                if (deserializeJson(doc, payload) == DeserializationError::Ok) {
                    if (doc.is<JsonArray>() && doc.size() >= 1) {
                        JsonArray ohlcArray = doc.as<JsonArray>();
                        double dailyOpen = ohlcArray[0][1];
                        
                        if (xSemaphoreTake(priceMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                            btc1dChange = ((currentBTCPrice - dailyOpen) / dailyOpen) * 100.0;
                            Serial.printf("[TASK] 1d change: %+.2f%%\n", btc1dChange);
                            addToConsoleBuffer("1d change: " + String(btc1dChange, 2) + "%");
                            xSemaphoreGive(priceMutex);
                        }
                    }
                }
            }
            httpDaily.end();
            
            ohlcHourlyRequestInProgress = false;
        }
        
        // Wait 60 seconds before next OHLC request
        vTaskDelay(pdMS_TO_TICKS(UPDATE_INTERVAL));
    }
}

void setMatrixFont(FontType fontType) {
    switch(fontType) {
        case FONT_BUILTIN:
            matrix->setFont(); // Reset to built-in font
            matrix->setTextSize(1);
            break;
        case FONT_TOMTHUMB:
            matrix->setFont(&TomThumb);
            break;
        // // NUMBERS ONLY fonts (ultra compact for price display)
        // case FONT_2X5_NUM:
        //     matrix->setFont(&Font2x5FixedMonoNum);
        //     break;
        // case FONT_3X5_NUM:
        //     matrix->setFont(&Font3x5FixedNum);
        //     break;
        // case FONT_3X7_NUM:
        //     matrix->setFont(&Font3x7FixedNum);
        //     break;
        // // FULL CHARACTER SET fonts (numbers + letters)
        // case FONT_4X5_FIXED:
        //     matrix->setFont(&Font4x5Fixed);
        //     break;
        // case FONT_4X7_FIXED:
        //     matrix->setFont(&Font4x7Fixed);
        //     break;
        // case FONT_5X5_FIXED:
        //     matrix->setFont(&Font5x5Fixed);
        //     break;
        // case FONT_5X7_FIXED:
        //     matrix->setFont(&Font5x7Fixed);
        //     break;
        // case FONT_5X7_MONO:
        //     matrix->setFont(&Font5x7FixedMono);
        //     break;
        default:
            matrix->setFont(); // Default to built-in
            matrix->setTextSize(1);
            break;
    }
}

void updateMultiColorScrollingText(int16_t y, ScrollState& scrollState, FontType fontType) {
    // Only update if enough time has passed
    if (scrollState.shouldUpdate()) {
        setMatrixFont(fontType);
        
        // Clear only the scrolling text area (bottom line)
        matrix->fillRect(0, y-6, 32, 8, 0);  // Clear bottom text area
        
        // Format each timeframe with its value (now only 3 segments)
        char timeframes[3][16];
        sprintf(timeframes[0], "1H: %+.1f%%", btc1hChange);
        sprintf(timeframes[1], "1D: %+.1f%%", btc1dChange);
        sprintf(timeframes[2], "24H: %+.1f%%", btc24hChange);
        
        // Get colors for each timeframe based on sign (now only 3 colors)
        uint16_t colors[3];
        colors[0] = (btc1hChange >= 0) ? matrix->Color(0, 255, 0) : matrix->Color(255, 0, 0);
        colors[1] = (btc1dChange >= 0) ? matrix->Color(0, 255, 0) : matrix->Color(255, 0, 0);
        colors[2] = (btc24hChange >= 0) ? matrix->Color(0, 255, 0) : matrix->Color(255, 0, 0);
        
        // Calculate text bounds for each segment
        int16_t x1, y1;
        uint16_t segmentWidths[3], textHeight;
        int16_t totalWidth = 0;
        
        for (int i = 0; i < 3; i++) {
            matrix->getTextBounds(timeframes[i], 0, 0, &x1, &y1, &segmentWidths[i], &textHeight);
            totalWidth += segmentWidths[i];
            if (i < 2) totalWidth += 8; // Add spacing between segments (only between 1H-1D and 1D-24H)
        }
        
        // Draw each timeframe segment at its position
        int16_t currentX = scrollState.offset;
        for (int i = 0; i < 3; i++) {
            matrix->setTextColor(colors[i]);
            matrix->setCursor(currentX, y);
            matrix->print(timeframes[i]);
            currentX += segmentWidths[i] + 8; // Move to next segment position
        }
        
        scrollState.update();  // Move scroll position
        
        // Reset when entire multi-segment text has scrolled off-screen
        if (scrollState.offset < -totalWidth) {
            scrollState.reset(32);
        }
    }
}


void printText(int16_t x, int16_t y, const char* text, FontType fontType, uint16_t color) {
    setMatrixFont(fontType);
    matrix->setTextColor(color);
    matrix->setTextWrap(false);
    matrix->setCursor(x, y);
    matrix->print(text);
}

void printTextCentered(int16_t width, int16_t y, const char* text, FontType fontType, uint16_t color) {
    setMatrixFont(fontType);
    matrix->setTextColor(color);
    matrix->setTextWrap(false);
    
    // Calculate text width for centering
    int16_t x1, y1;
    uint16_t w, h;
    matrix->getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
    int16_t x = (width - w) / 2;
    
    matrix->setCursor(x, y);
    matrix->print(text);
}

// Number-specific printing functions (optimized for numeric display)
void printNumber(int16_t x, int16_t y, const char* number, FontType fontType, uint16_t color) {
    // Same as printText but semantically indicates number usage
    printText(x, y, number, fontType, color);
}

void printNumberCentered(int16_t width, int16_t y, const char* number, FontType fontType, uint16_t color) {
    // Same as printTextCentered but semantically indicates number usage
    printTextCentered(width, y, number, fontType, color);
}

void printScrollingText(int16_t y, const char* text, ScrollState& scrollState, FontType fontType, uint16_t color) {
    setMatrixFont(fontType);
    matrix->setTextWrap(false);   // Enable wrapping for scrolling
    matrix->setTextColor(color);
    matrix->setCursor(scrollState.offset, y);
    matrix->print(text);
}

void updateScrollingText(int16_t y, const char* text, ScrollState& scrollState, int16_t resetOffset, FontType fontType, uint16_t color) {
    // Only update if enough time has passed based on the scroll state's speed
    printScrollingText(y, text, scrollState, fontType, color);
    if (scrollState.shouldUpdate()) {
        
        scrollState.update();  // Move scroll position
        
        // Calculate text width for continuous scrolling
        setMatrixFont(fontType);
        int16_t x1, y1;
        uint16_t textWidth, textHeight;
        matrix->getTextBounds(text, 0, 0, &x1, &y1, &textWidth, &textHeight);
        
        // Reset when entire text has scrolled off-screen (continuous wrapping)
        if (scrollState.offset < -((int16_t)textWidth)) {
            scrollState.reset(32);  // Start from right edge again
        }
    }
}

// HTTP Task Management Functions for OTA Safety
void suspendHttpTasks() {
    Serial.println("Suspending HTTP tasks for OTA...");
    addToConsoleBuffer("Suspending HTTP tasks for OTA...");
    
    if (priceTaskHandle != NULL) {
        vTaskSuspend(priceTaskHandle);
        Serial.println("Price task suspended.");
        addToConsoleBuffer("Price task suspended.");
    }
    
    if (ohlcTaskHandle != NULL) {
        vTaskSuspend(ohlcTaskHandle);
        Serial.println("OHLC task suspended.");
        addToConsoleBuffer("OHLC task suspended.");
    }
    
    httpTasksSuspended = true;
    Serial.println("All HTTP tasks suspended successfully.");
    addToConsoleBuffer("All HTTP tasks suspended successfully.");
}

void resumeHttpTasks() {
    Serial.println("Resuming HTTP tasks after OTA...");
    addToConsoleBuffer("Resuming HTTP tasks after OTA...");
    
    if (priceTaskHandle != NULL) {
        vTaskResume(priceTaskHandle);
        Serial.println("Price task resumed.");
        addToConsoleBuffer("Price task resumed.");
    }
    
    if (ohlcTaskHandle != NULL) {
        vTaskResume(ohlcTaskHandle);
        Serial.println("OHLC task resumed.");
        addToConsoleBuffer("OHLC task resumed.");
    }
    
    httpTasksSuspended = false;
    Serial.println("All HTTP tasks resumed successfully.");
    addToConsoleBuffer("All HTTP tasks resumed successfully.");
}
