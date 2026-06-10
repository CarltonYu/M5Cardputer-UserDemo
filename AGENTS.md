# Cardputer ADV User Demo

User demo firmware for the M5Stack Cardputer ADV (ESP32-S3). This is an ESP-IDF project that demonstrates the device's capabilities through a multi-app graphical launcher.

## Technology Stack

- **Framework**: [ESP-IDF v5.4.2](https://docs.espressif.com/projects/esp-idf/en/v5.4.2/esp32s3/index.html)
- **Target**: ESP32-S3 (Xtensa dual-core, 240 MHz, 8 MB flash)
- **Languages**: C++ (primary), C
- **Build System**: CMake + ESP-IDF component system
- **RTOS**: FreeRTOS (1000 Hz tick rate)
- **Graphics**: M5GFX + M5Unified (LGFX-based 2D graphics library)
- **App Framework**: Mooncake v2.2.0 (lightweight app lifecycle manager)
- **Logging**: mooncake_log (header-only, bundled in components/mooncake_log)
- **UI Toolkit**: smooth_ui_toolkit (animation, easing, transition utilities)
- **USB Stack**: TinyUSB (HID device via `espressif/esp_tinyusb`)
- **Wireless**: ESP-NOW, Wi-Fi (station), BLE (NimBLE HID keyboard)
- **LoRa**: RadioLib (via `jgromes/radiolib` component)
- **Settings**: NVS wrapper (`hal/utils/settings`)
- **Audio**: M5Unified speaker/mic APIs + custom SFX helpers

## Project Structure

```
├── CMakeLists.txt              # Root project CMake (project: cardputer-adv)
├── sdkconfig.defaults          # Default ESP-IDF config (8 MB flash, custom partition table, BLE enabled)
├── partitions.csv              # Custom flash partition table
├── dependencies.lock           # Locked managed component versions
├── fetch_repos.py              # Dependency fetcher for Git-based components
├── repos.json                  # Git repo URLs, paths, and pinned branches/tags
├── .clang-format               # Code formatting rules (Google-based)
├── main/                       # Main firmware source
│   ├── main.cpp                # Entry point (app_main)
│   ├── CMakeLists.txt          # Main component build rules
│   ├── idf_component.yml       # Managed dependencies (esp-now, esp_tinyusb, radiolib)
│   ├── Kconfig.projbuild       # Project Kconfig (BLE HID role: Media/Keyboard/Mouse)
│   ├── assets/                 # Embedded assets (fonts, images)
│   ├── hal/                    # Hardware Abstraction Layer
│   │   ├── hal.h / hal.cpp     # Singleton HAL (GetHAL())
│   │   ├── hal_config.h        # Pin definitions (keyboard, IR, SPI, SD, GPS, LoRa)
│   │   ├── keyboard/           # TCA8418 matrix keyboard driver
│   │   ├── cap_lora868/        # LoRa 868 cap (RadioLib wrapper)
│   │   └── utils/              # HAL utilities
│   │       ├── settings/       # NVS key-value settings wrapper
│   │       ├── adafruit_tca8418/
│   │       ├── ble_hid_device/ # BLE HID keyboard helper (NimBLE)
│   │       ├── ir_nec/         # IR NEC RMT encoder/helper
│   │       └── tusb_hid_device/# TinyUSB HID keyboard helper
│   └── apps/                   # Applications
│       ├── apps.h              # Aggregated app headers
│       ├── app_launcher/       # System launcher (menu, system bar, keyboard bar, boot anim)
│       ├── app_wifi_scan/      # Wi-Fi scanner
│       ├── app_record/         # Audio recorder
│       ├── app_chat/           # ESP-NOW chat
│       ├── app_remote/         # IR remote (NEC)
│       ├── app_repl/           # PikaPython REPL
│       ├── app_set_wifi/       # Wi-Fi configuration
│       ├── app_clock/          # Clock / network time display
│       ├── app_keyboard/       # BLE & USB HID keyboard
│       ├── app_imu/            # IMU sensor display
│       ├── app_sdcard/         # SD card info
│       ├── app_lora_chat/      # LoRa chat (requires LoRa 868 cap)
│       ├── app_gps/            # GPS display (requires GPS cap)
│       ├── app_stringir_toolkit/ # String-based IR toolkit
│       ├── app_dummy/          # Placeholder / template app
│       └── utils/              # Shared app utilities
│           ├── audio/          # Tone/melody/SFX helpers
│           ├── theme.h         # Color/theme definitions
│           └── common.h        # Common macros and helpers
└── components/                 # ESP-IDF components (fetched via fetch_repos.py)
    ├── M5GFX/                  # Graphics library (v0.2.15)
    ├── M5Unified/              # Unified M5 device abstraction (v0.2.10)
    ├── mooncake/               # App framework (v2.2.0)
    ├── mooncake_log/           # Logging + Signal/Slot (v1.3.0)
    └── smooth_ui_toolkit/      # UI animation toolkit (v2.4.0)
```

## Fetching Dependencies

Before building, fetch the Git-based components declared in `repos.json`:

```bash
python3 ./fetch_repos.py
```

This clones or updates the repositories in `components/` and checks out the pinned branches. The `repos.json` pins:

- `M5GFX` → `0.2.15`
- `M5Unified` → `0.2.10`
- `mooncake` → `v2.2.0`
- `mooncake_log` → `v1.3.0`
- `smooth_ui_toolkit` → `v2.4.0`

Managed components (from the Espressif Component Registry) are resolved automatically by `idf.py` using `main/idf_component.yml` and `dependencies.lock`.

## Build and Flash Commands

Prerequisite: ESP-IDF v5.4.2 environment must be set up and `IDF_PATH` available.

```bash
# Build
idf.py build

# Flash and monitor (adjust port as needed)
idf.py -p /dev/ttyACM0 flash monitor
```

## Runtime Architecture

1. **Entry Point**: `main/main.cpp::app_main()`
2. **Logger Setup**: `mclog::set_level(mclog::level_debug)` with Unix millisecond timestamps.
3. **HAL Initialization**: `GetHAL().init()` sets up:
   - M5Unified (`M5.begin()`)
   - Display, canvas sprites (204×109 main canvas + system bar + keyboard bar)
   - I2C scan
   - TCA8418 keyboard
   - NVS settings (`Settings` wrapper)
   - SPI bus (for SD card)
4. **UI HAL Binding**: `ui_hal::on_delay` and `ui_hal::on_get_tick` are wired to HAL for `smooth_ui_toolkit`.
5. **App Installation**: Apps are installed via `GetMooncake().installApp(std::make_unique<AppName>())`.
6. **Main Loop**: `GetHAL().feedTheDog()` (FreeRTOS yield), `GetHAL().update()` (M5 + keyboard + LoRa), `GetMooncake().update()` (app lifecycle).

### App Lifecycle (Mooncake v2)

Each app inherits from `mooncake::AppAbility` and implements lifecycle hooks:

- `onCreate()` — allocate resources
- `onOpen()` — UI setup, play open animation
- `onRunning()` — main loop logic, input handling, rendering
- `onClose()` — cleanup before backgrounding
- `onDestroy()` — final teardown

The `Launcher` app is installed first and runs as the background shell. It renders:
- A smooth-scrolling icon menu
- A system bar (Wi-Fi status, battery, time)
- A keyboard status bar (Caps Lock, Fn, Ctrl, Alt, Opt)

## Code Style Guidelines

The project uses `.clang-format` with these key rules:

- **Base style**: Google
- **Indent**: 4 spaces
- **Column limit**: 120
- **Braces**: Custom (functions break after definition; classes/enums/namespaces inline)
- **Pointer alignment**: Left (`int* ptr`)
- **Namespace indentation**: None
- **Bin packing**: enabled for arguments and parameters
- **Include sorting**: disabled (`SortIncludes: false`)

Run formatting with:
```bash
clang-format -i main/**/*.cpp main/**/*.h main/**/*.cc
```

## App Development Pattern

To add a new app:

1. Create a directory under `main/apps/app_yourname/`.
2. Provide icon asset pairs (`assets/yourname_big.h`, `assets/yourname_small.h`) as `const uint16_t` image arrays.
3. Implement a class inheriting from `mooncake::AppAbility`:
   ```cpp
   class AppYourName : public mooncake::AppAbility {
   public:
       void onOpen() override;
       void onRunning() override;
       void onClose() override;
   };
   ```
4. Include the header in `main/apps/apps.h`.
5. Install the app in `main/main.cpp`:
   ```cpp
   GetMooncake().installApp(std::make_unique<AppYourName>());
   ```
6. Access HAL inside lifecycle methods via `GetHAL()`.

### Common Patterns

Canvas shortcuts (used inside apps):
```cpp
Hal& hal = GetHAL();
hal.canvas.fillScreen(THEME_COLOR_BG);
hal.pushCanvas();
```

Open/close animations:
```cpp
#include <smooth_ui_toolkit.h>
// Use smooth_ui_toolkit transitions or custom sprite-based wipes
```

Keyboard input:
```cpp
auto& kb = GetHAL().keyboard;
auto& latest = kb.getLatestKeyEvent();
// latest.state, latest.keyCode, latest.isModifier, latest.keyName
```

## Testing Strategy

There is no formal unit test runner. Testing is manual / integration-based:

- Build and flash the firmware to a Cardputer ADV device.
- Use `sdkconfig.defaults` to ensure consistent base configuration.
- Apps can be temporarily disabled in `main.cpp` by commenting out their `installApp` line.
- The `app_dummy` placeholder can be used as a starting point for prototyping.

## Key Configuration

### Partition Table (`partitions.csv`)
| Name     | Type | SubType | Offset  | Size   |
|----------|------|---------|---------|--------|
| nvs      | data | nvs     | 0x9000  | 0x4000 |
| otadata  | data | ota     | 0xd000  | 0x2000 |
| phy_init | data | phy     | 0xf000  | 0x1000 |
| factory  | app  | factory | 0x10000 | 4M     |

### Default SDK Config (`sdkconfig.defaults`)
- Target: `esp32s3`
- Flash size: 8 MB
- CPU frequency: 240 MHz
- Custom partition table enabled
- Console: USB Serial/JTAG
- BLE: NimBLE enabled, HID keyboard role (`CONFIG_EXAMPLE_KBD_ENABLE=y`)
- BT device name: `CardputerADV Keyboard`
- FreeRTOS tick rate: 1000 Hz
- Main task stack: 4096 bytes
- TinyUSB HID count: 1

### Project Kconfig (`Kconfig.projbuild`)
- BLE HID role selection: Media Device / Keyboard / Mouse

## Dependencies

### Managed (Component Registry)
- `espressif/esp-now` ^2.5.2 — ESP-NOW wireless peer-to-peer
- `espressif/esp_tinyusb` ^1.1 — TinyUSB wrapper
- `jgromes/radiolib` ^7.2.1 — LoRa transceiver library

### Git-based (fetched via `fetch_repos.py`)
- `M5GFX` — Low-level graphics
- `M5Unified` — Unified M5 device API (display, power, IMU, speaker, mic)
- `mooncake` — App ability/lifecycle framework
- `mooncake_log` — Logging + signal/slot system (`mclog::Signal`, `mclog::tagInfo`)
- `smooth_ui_toolkit` — Smooth UI animations and transitions

## Security Considerations

- The firmware runs on bare metal with full hardware access. There is no memory protection between apps.
- Apps share the same address space and global HAL instance.
- Wi-Fi credentials are stored in NVS; encryption at rest is not enabled by default.
- ESP-NOW traffic is not encrypted by default in this demo codebase.
- USB HID presents the device as a generic input peripheral.
- BLE pairing uses Secure Simple Pairing (`CONFIG_BT_NIMBLE_SM_LVL=2`).
- No secure boot or flash encryption configuration is present in the default `sdkconfig.defaults`.

## Hardware Features Exposed

- 240×320 LCD via M5GFX (sprites for canvas layers)
- Physical matrix keyboard (TCA8418) with Fn/Ctrl/Alt/Opt modifiers
- I2S microphone and speaker (via M5Unified codec)
- SD card interface (SPI, FAT filesystem mounted at `/sdcard`)
- Battery level via M5Unified Power API
- Home button (`M5.BtnA`)
- IR TX (NEC via RMT, GPIO 44)
- USB HID keyboard/mouse (TinyUSB)
- BLE HID keyboard (NimBLE)
- Wi-Fi station + SNTP time sync
- ESP-NOW messaging
- IMU (BMI270 / SH200Q via M5Unified)
- LoRa 868 cap (SX1262 via RadioLib, SPI)
- GPS cap (UART, TX=GPIO13, RX=GPIO15)

## Acknowledgments

The project references and bundles code from:
- Adafruit TCA8418 driver
- M5Unified / M5GFX
- PikaPython (REPL app)
- RadioLib
- raylib (3D rendering in IMU app)
- TinyGPSPlus
- mooncake / mooncake_log / smooth_ui_toolkit
- esp32s3-keyboard
- xiaozhi-esp32
