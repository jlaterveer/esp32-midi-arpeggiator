# ESP32 MIDI Arpeggiator

A flexible, real-time MIDI arpeggiator for ESP32 with rotary encoder input, pattern control, and live MIDI chord capturing.

## 🔧 Features

- Real-time MIDI input/output at 31250 baud
- Chord capture with latch and clear functionality
- Multiple arpeggio patterns (UP, DOWN, TRIANGLE, SINE, SQUARE, RANDOM)
- Adjustable parameters:
  - BPM
  - Note length
  - Velocity
  - Octave spread
  - Pattern
  - Resolution (notes per beat)
  - Note repeat
  - Transpose
  - Velocity dynamics toggle
- RGB LED feedback and mode indication
- Rotary encoder with push-button for parameter changes

## ⚙️ Hardware

- **Board**: ESP32 Dev Module
- **MIDI IN**: `GPIO4` (Serial1 RX)
- **MIDI OUT**: `GPIO5` (Serial2 TX)
- **Encoder Pins**:
  - CLK: `GPIO7`
  - DT: `GPIO8`
  - SW (Button): `GPIO9`
- **Clear Button**: `GPIO2`
- **Built-in RGB LED**: `GPIO21` (Neopixel-style WS2812)

> ⚠️ **Note:** Adjust pin numbers in `main.cpp` if your wiring differs.

## 🛠️ Getting Started

### Prerequisites

- [VSCode](https://code.visualstudio.com/)
- [PlatformIO Extension](https://platformio.org/platformio-ide)
- [GitHub CLI (optional)](https://cli.github.com/)

### Build & Upload

1. Clone the repo or copy this project.
2. Open it in VSCode with PlatformIO.
3. Click **Build** or **Upload** using the bottom toolbar.

```bash
# For command-line PlatformIO:
pio run --target upload