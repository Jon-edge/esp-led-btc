# 🚀 ESP32 LED Matrix BTC Ticker

A PlatformIO project that displays Bitcoin price updates on a 32x16 LED matrix using FastLED, NeoMatrix, and more.

## 🛠️ Hardware Requirements

- ESP32 Development Board
- 32x16 WS2812B LED Matrix (512 LEDs total)
- Power supply (5V, adequate for your LED count)
- Jumper wires

## 🔌 Wiring (WROOM ESP32)

- LED Matrix Data Pin → ESP32 GPIO 5
- LED Matrix 5V → External 5V Power Supply (optional, but recommended)
- LED Matrix GND → ESP32 GND + Power Supply GND

## 💻 Software Setup

### Tooling/Extensions

#### Windsurf, VS Code

1. Install [PlatformIO](https://platformio.org/install)
2. Install [VS Code](https://code.visualstudio.com/) with PlatformIO extension
   (recommended)

#### Cursor

1. Cursor currently is not licensed by Microsoft to use their cpptools extension. Workaround is to install an older version: 
https://github.com/microsoft/vscode-cpptools/releases/download/v1.24.4/cpptools-macOS-arm64.vsix
2. PlatformIO is also not listed in the Open VSX Registry (non-Microsoft Extensions Marketplace). Manually download the PlatformIO vsix extension file: 
https://marketplace.visualstudio.com/_apis/public/gallery/publishers/platformio/vsextensions/platformio-ide/3.3.4/vspackage?targetPlatform=darwin-arm64
3. Manually install these `.vsix` extensions (cpptools first) with Command Palette: `Extensions: Install from VSIX`

### ⚙️ Configuration

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

### 🔨 Building and Uploading

1. Clone/navigate to this repository
2. PlatformIO should automatically detect and initialize the project. If not, PIO Home->Open the directory.

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

## ⚡ How It Works

1. **Startup**: ESP32 connects to WiFi and initializes the LED matrix
2. **Price Updates**: Asyncronously fetches BTC price from CoinDesk API (using timer interrupts), calculates deltas from OHLC
3. **Drawing**: Main loop, using a variety of LED libs

## 📺 Serial Monitor Output

You should see output along the lines of:
```
ESP32 LED Matrix BTC Ticker Starting...
Connecting to WiFi........
WiFi connected! IP address: 192.168.1.100
Setup complete!
Fetching BTC price...
BTC Price: $XXXXX USD
Last Updated: Dec 8, 2023 19:30:00 UTC
```

### Matrix Layout

- **Size**: 32x16 pixels
- **Origin**: Bottom-right
- **Layout**: Column-major with zigzag wiring

## 🔧 Troubleshooting

- **No WiFi connection**: Check SSID/password in `config.h`
- **No LED response**: Verify wiring and power supply
- **Compilation errors**: Ensure PlatformIO dependencies are installed
- **Upload fails**: Check ESP32 is in bootloader mode and correct port is selected

## 🗺️ Roadmap:

1. **Non-blocking async HTTP fetch** ✅
2. OTA (Over the Air) update support
3. Text: Restyle, add more animations + fonts
4. Animated background effects
5. Chart (line or bar)