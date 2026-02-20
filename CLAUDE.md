# Safe Bite — Claude Context

## Project

Portable voice-activated dietary assistant for FODMAP/gluten restrictions, running on an **M5StickC Plus2** (ESP32-PICO-V3-02). Two modes:
- **Online**: voice → Mistral Voxtral (STT) → Mistral Small → display result
- **Offline**: button navigation through a local JSON food database

## Hardware

- **Device**: M5StickC Plus2 (ESP32-PICO-V3-02)
- **Display**: 1.14" TFT, **135×240 pixels** (portrait orientation in use)
- **Mic**: PDM SPM1423
- **Buttons**: M5 (main) and B (side/cancel)
- **Storage**: 4MB Flash, LittleFS partition for `data/`

## Build System

**PlatformIO** (`platformio.ini`). Key targets:

| Command | Action |
|---|---|
| `pio run` | Compile firmware |
| `pio run --target upload` | Flash firmware to device |
| `pio run --target uploadfs` | Upload `data/` to LittleFS (foods.json) |
| `pio device monitor` | Serial monitor at 115200 baud |

Firmware and filesystem are **independent** — only re-flash what changed.

## Key Files

```
src/main.cpp              # Main loop, state machine, UI rendering
src/audio_manager.cpp     # Recording, WAV buffer, recording screen
src/wifi_manager.cpp      # Non-blocking WiFi, status indicator
include/audio_manager.h
include/wifi_manager.h
include/language.h        # Bilingual string system (EN/PT)
include/config.h          # WiFi + API keys — gitignored, never commit
include/config.example.h  # Template for config.h
data/foods.json           # 176 foods across 8 categories, uploaded to LittleFS
```

## Architecture Notes

### Language system
Strings are `const char*` arrays indexed `[EN, PT]`. Use the `STR(arr)` macro everywhere — never hardcode UI strings.

### Audio recording
- Sample rate: **8000 Hz**, duration: **6s**, buffer: **48000 samples** (16-bit PCM → WAV)
- Chunk size: 240 samples (M5 mic recommended)
- Countdown display is keyed to **`samplesRecorded`**, not wall-clock time — so it stays in sync with actual completion even when `mic.record()` calls fail

### Recording screen rendering
Partial updates only — the screen is broken into zones (REC dot, waveform bars, seconds counter) and each is redrawn independently to prevent flickering. Full redraw only on screen entry.

### WiFi
Non-blocking connection — never stalls the main loop. Status shown as a small circle in the top-right corner (green = connected, yellow blinking = connecting, red = disconnected).

## Display Layout (recording screen)

```
[● REC]              [wifi]   ← row 0-24
[  waveform bars  ]           ← rows 52-100
[              Xs ]           ← rows 108-124  (countdown, yellow)
[B: Cancel        ]           ← row 125       (hint, dark grey)
```

"Recording" text was removed — the blinking REC dot is sufficient.

## Food Database

`data/foods.json` — 176 foods, 8 categories (Vegetables, Fruits, Snacks, Grains, Proteins, Dairy, Condiments, Drinks). Edit this file and run `pio run --target uploadfs` to push updates to the device.

## config.h (never commit)

```cpp
#define WIFI_SSID "..."
#define WIFI_PASSWORD "..."
#define MISTRAL_API_KEY "..."
```

Copy from `include/config.example.h`. Already in `.gitignore`.
