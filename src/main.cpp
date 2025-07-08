#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <FastLED.h>
#include "config.h"

#define BTC_API_URL "https://pro-api.coingecko.com/api/v3/simple/price?ids=bitcoin&vs_currencies=usd&include_24hr_change=true"

// Forward declarations
void connectToWiFi();
void fetchBTCPrice();

// LED Array
CRGB leds[NUM_LEDS];

// Global variables
unsigned long lastUpdate = 0;
float currentBTCPrice = 0.0;
float btc24hChange = 0.0;
bool wifiConnected = false;

void setup() {
    Serial.begin(115200);
    Serial.println("ESP32 LED Matrix BTC Ticker Starting...");
    
    // Initialize FastLED
    FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS).setCorrection(TypicalLEDStrip);
    FastLED.setBrightness(BRIGHTNESS);
    
    // Clear all LEDs
    fill_solid(leds, NUM_LEDS, CRGB::Black);
    FastLED.show();
    
    // Connect to WiFi
    connectToWiFi();
    
    Serial.println("Setup complete!");
}

void loop() {
    // Check WiFi connection
    if (WiFi.status() != WL_CONNECTED) {
        wifiConnected = false;
        Serial.println("WiFi disconnected, attempting to reconnect...");
        connectToWiFi();
        return;
    }
    
    // Update BTC price at specified interval
    if (millis() - lastUpdate > UPDATE_INTERVAL) {
        fetchBTCPrice();
        lastUpdate = millis();
    }
    
    delay(1);
}

void connectToWiFi() {
    Serial.print("Connecting to WiFi");
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30) {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        wifiConnected = true;
        Serial.println();
        Serial.print("WiFi connected! IP address: ");
        Serial.println(WiFi.localIP());
        
        // Flash green to indicate WiFi connection
        fill_solid(leds, NUM_LEDS, CRGB::Green);
        FastLED.show();
        delay(500);
        fill_solid(leds, NUM_LEDS, CRGB::Black);
        FastLED.show();
    } else {
        Serial.println();
        Serial.println("Failed to connect to WiFi");
        
        // Flash red to indicate WiFi failure
        fill_solid(leds, NUM_LEDS, CRGB::Red);
        FastLED.show();
        delay(500);
        fill_solid(leds, NUM_LEDS, CRGB::Black);
        FastLED.show();
    }
}

void fetchBTCPrice() {
    if (!wifiConnected) {
        Serial.println("WiFi not connected, cannot fetch BTC price");
        return;
    }
    
    HTTPClient http;
    http.begin(BTC_API_URL);
    
    // Add CoinGecko Pro API key to header (recommended method)
    http.addHeader("x-cg-pro-api-key", COINGECKO_API_KEY);
    
    Serial.println("Fetching BTC price...");
    int httpResponseCode = http.GET();
    
    if (httpResponseCode == 200) {
        String payload = http.getString();
        
        // Parse JSON response from CoinGecko
        DynamicJsonDocument doc(1024);
        deserializeJson(doc, payload);
        
        // Extract BTC price and 24h change from CoinGecko format
        if (doc["bitcoin"]["usd"] && doc["bitcoin"]["usd_24h_change"]) {
            currentBTCPrice = doc["bitcoin"]["usd"];
            btc24hChange = doc["bitcoin"]["usd_24h_change"];
            
            // Round to nearest cent (like original roundToNearest function)
            currentBTCPrice = round(currentBTCPrice * 100.0) / 100.0;
            btc24hChange = round(btc24hChange * 100.0) / 100.0;
            
            Serial.printf("BTC price: $%.2f USD\n", currentBTCPrice);
            Serial.printf("24h change: %+.2f%%\n", btc24hChange);
            
            // Color coding like original (green for positive, red for negative)
            CRGB changeColor = (btc24hChange >= 0) ? CRGB::Green : CRGB::Red;
            
            // Flash with appropriate color to indicate successful price fetch
            fill_solid(leds, NUM_LEDS, changeColor);
            FastLED.show();
            delay(200);
            fill_solid(leds, NUM_LEDS, CRGB::Black);
            FastLED.show();
            
        } else {
            Serial.println("Error parsing BTC data from CoinGecko JSON response");
        }
    } else {
        Serial.printf("HTTP GET failed, error: %d\n", httpResponseCode);
        
        // Flash orange to indicate HTTP error
        fill_solid(leds, NUM_LEDS, CRGB::Orange);
        FastLED.show();
        delay(200);
        fill_solid(leds, NUM_LEDS, CRGB::Black);
        FastLED.show();
    }
    
    http.end();
}
