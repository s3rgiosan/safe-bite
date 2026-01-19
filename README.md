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

## Hardware

- [M5StickC Plus2](https://shop.m5stack.com/products/m5stickc-plus2-esp32-mini-iot-development-kit) (~€20)

## Costs

| Item | Cost |
|------|------|
| Hardware | €20-25 (one-time) |
| OpenAI API | ~€3/month |

## Blog Series

I'm documenting the build process. Follow along at [s3rgiosan.dev](https://s3rgiosan.dev).

## Acknowledgments

- Inspired by [this LinkedIn post](https://www.linkedin.com/posts/organised_i-made-my-8-year-old-son-who-doesnt-have-ugcPost-7414307168881094656-CRCv/)
- FODMAP data based on [Monash University guidelines](https://www.monashfodmap.com/)

## Links

- [CH34x USB driver for macOS](https://github.com/WCHSoftGroup/ch34xser_macos) — Required for M5StickC Plus2 serial connection
- [arduinohw](https://github.com/organised/arduinohw) — The project that inspired Safe Bite

## License

MIT
