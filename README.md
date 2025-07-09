# ESP32 LED Matrix BTC Ticker

A PlatformIO project that displays Bitcoin price updates on a 32x16 LED matrix using FastLED library.

## Hardware Requirements

- ESP32 Development Board
- 32x16 WS2812B LED Matrix (512 LEDs total)
- Power supply (5V, adequate for your LED count)
- Jumper wires

## Wiring

- LED Matrix Data Pin → ESP32 GPIO 5
- LED Matrix 5V → External 5V Power Supply
- LED Matrix GND → ESP32 GND + Power Supply GND

## Software Setup

### Prerequisites

1. Install [PlatformIO](https://platformio.org/install)
2. Install [VS Code](https://code.visualstudio.com/) with PlatformIO extension (recommended)

### Configuration

1. **Copy the configuration template:**
   ```bash
   cp include/config.h.template include/config.h
   ```

2. **Edit `include/config.h`** and update with your actual credentials:
   ```cpp
   #define WIFI_SSID "YOUR_WIFI_SSID"
   #define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
   #define COINGECKO_API_KEY "YOUR_COINGECKO_API_KEY"
   ```

   **Note:** `config.h` is in `.gitignore` to keep your credentials private.

### Building and Uploading

1. Clone/navigate to this repository
2. Open the project in VS Code with PlatformIO, or use command line:

#### Using VS Code + PlatformIO
- Open the project folder in VS Code
- Click the PlatformIO icon in the sidebar
- Click "Build" to compile
- Connect your ESP32 via USB
- Click "Upload" to flash the firmware
- Click "Monitor" to view serial output

#### Using Command Line
```bash
# Navigate to project directory
cd esp-led-btc

# Install dependencies and build
pio run

# Upload to ESP32 (make sure it's connected via USB)
pio run --target upload

# Monitor serial output
pio device monitor --baud 115200
```

## How It Works

1. **Startup**: ESP32 connects to WiFi and initializes the LED matrix
2. **Status Indicators**: 
   - Green flash: WiFi connected successfully
   - Red flash: WiFi connection failed
   - Blue flash: BTC price fetched successfully
   - Orange flash: HTTP request failed
3. **Price Updates**: Fetches BTC price from CoinDesk API every 30 seconds
4. **Console Output**: Displays current BTC price and timestamp in serial monitor

## Serial Monitor Output

You should see output like:
```
ESP32 LED Matrix BTC Ticker Starting...
Connecting to WiFi........
WiFi connected! IP address: 192.168.1.100
Setup complete!
Fetching BTC price...
BTC Price: $43,250.75 USD
Last Updated: Dec 8, 2023 19:30:00 UTC
```

## Text Rendering

The project now includes text rendering capabilities using the FastLED_NeoMatrix library:

### Text Functions with Dynamic Wrapping

```cpp
// Basic text function (no wrapping)
void printText(x, y, text, fontSize=1, color=white);

// Centered text function (no wrapping)
void printTextCentered(centerX, centerY, text, fontSize=1, color=white);

// Scrolling text function (with wrapping for animation)
void printScrollingText(y, text, scrollOffset, fontSize=1, color=white);
```

**Wrapping Control:**
- `printText()` and `printTextCentered()`: **No wrapping** (precise positioning)
- `printScrollingText()`: **Wrapping enabled** (for continuous scrolling animation)

**Example Usage:**
```cpp
// Static display - no wrapping
printTextCentered(16, 8, "$43,250");

// Animated ticker - uses wrapping for smooth scrolling
int scrollPos = -50;  // Start off-screen
printScrollingText(8, "BTC $43,250.75 +2.3% 24h", scrollPos);
scrollPos++;  // Move right each frame
```

### Color Examples

```cpp
// Using matrix color helper (recommended)
uint16_t red = matrix->Color(255, 0, 0);
uint16_t green = matrix->Color(0, 255, 0);
uint16_t blue = matrix->Color(0, 0, 255);
uint16_t white = matrix->Color(255, 255, 255);

// Display examples
printText(0, 0, "BTC", red);           // Top-left corner
printText(5, 8, "$43,250", green);    // Centered position
```

### Matrix Layout

- **Size**: 32x16 pixels
- **Origin**: Top-left (0,0)
- **Layout**: Row-major with zigzag wiring
- **Text wrapping**: Disabled by default

## Troubleshooting

- **No WiFi connection**: Check SSID/password in `config.h`
- **No LED response**: Verify wiring and power supply
- **Compilation errors**: Ensure PlatformIO dependencies are installed
- **Upload fails**: Check ESP32 is in bootloader mode and correct port is selected

## Next Steps

This initial version logs BTC prices to the console. Future enhancements will include:
- Display BTC price as scrolling text on the LED matrix
- Price change indicators (color coding)
- Historical price graphs
- Multiple cryptocurrency support
