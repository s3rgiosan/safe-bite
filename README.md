# Safe Bite 🍎

A portable voice-activated dietary assistant for people with FODMAP and gluten restrictions.

## What is this?

Safe Bite is a pocket-sized device that helps people make safe food choices. They ask "Can I eat apples?" and instantly see:

```txt
FODMAP: HIGH 🔴
GLUTEN: NO 🟢
```

## Features

- **Voice queries** — Ask about any food in English or Portuguese
- **Offline mode** — Works without WiFi using a local database of 176 foods
- **Color-coded answers** — Red (avoid), Yellow (small portions), Green (safe)
- **Kid-friendly** — Simple interface with just 2 buttons
- **WiFi connectivity** — Non-blocking connection with visual status indicator
- **Voice recording** — 6-second PDM microphone capture for voice search

## Hardware

- [M5StickC Plus2](https://shop.m5stack.com/products/m5stickc-plus2-esp32-mini-iot-development-kit) (~€20)

## How it works

**With WiFi:**

1. Press button and speak
2. Audio sent to Mistral Voxtral (speech-to-text)
3. Query sent to Mistral Small
4. Result displayed on screen

**Without WiFi:**

1. Navigate categories with buttons
2. Select food from list
3. Result displayed from local database

## Setup

1. Clone this repository
2. Install [PlatformIO](https://platformio.org/install/cli)
3. Copy `include/config.example.h` to `include/config.h` and fill in your credentials:

```cpp
// include/config.h
#define WIFI_SSID "your_network"
#define WIFI_PASSWORD "your_password"
#define MISTRAL_API_KEY "..."
```

## Building & Flashing

All commands use [PlatformIO CLI](https://platformio.org/install/cli).

**Compile the firmware:**

```sh
pio run
```

**Upload firmware to the device:**

```sh
pio run --target upload
```

**Build and upload the food database (LittleFS filesystem):**

The `data/foods.json` file is stored on the device's LittleFS partition. Upload it separately from the firmware:

```sh
pio run --target uploadfs
```

> Run this whenever you edit `data/foods.json`. The device and firmware uploads are independent — you only need to re-flash what changed.

**Full flash (firmware + filesystem):**

```sh
pio run --target upload && pio run --target uploadfs
```

**Monitor serial output:**

```sh
pio device monitor
```

Default baud rate is `115200` as set in `platformio.ini`.

## WiFi Connection

The device connects to WiFi in the background without blocking the UI:

- **Green circle** — Connected and ready for voice search
- **Yellow blinking** — Connecting...
- **Red outline** — Disconnected (will auto-retry)

If no `config.h` is present or WiFi fails, the device works in offline mode with the local food database.

## Voice Search

When WiFi is connected, a "Voice Search" option appears at the top of the categories menu:

1. Select "Voice Search" and press M5 button
2. Speak your food query (6-second recording)
3. Visual countdown shows recording progress
4. Audio is captured as 8kHz mono WAV and sent to Mistral Voxtral for transcription

The PDM microphone (SPM1423) captures speech at 8000 Hz sample rate with 16-bit depth.

## Costs

| Item | Cost |
|------|------|
| Hardware | €20-25 (one-time) |
| Mistral API | ~€1/month |

## Blog Series

I'm documenting the build process:

1. [Building Safe Bite: Why I'm Making This](https://s3rgiosan.com/building-safe-bite-why-im-making-this) — The personal story behind this project
2. [Building Safe Bite: Offline Mode](https://s3rgiosan.com/building-safe-bite-offline-mode/) — Local database and menu navigation

## Acknowledgments

- Inspired by [this LinkedIn post](https://www.linkedin.com/posts/organised_i-made-my-8-year-old-son-who-doesnt-have-ugcPost-7414307168881094656-CRCv/)
- FODMAP data based on [Monash University guidelines](https://www.monashfodmap.com/)

## Links

- [CH34x USB driver for macOS](https://github.com/WCHSoftGroup/ch34xser_macos) — Required for M5StickC Plus2 serial connection
- [arduinohw](https://github.com/organised/arduinohw) — The project that inspired Safe Bite

## License

MIT
