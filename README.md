# GarageDoor 🏠🚗

**Turn your dumb garage door into a smart one** — controllable from Alexa, Apple Home, Google Home, or any Matter-compatible app. No cloud subscriptions, no proprietary hubs, just an ESP32 and a relay.

This project uses the **Matter** protocol to expose your garage door as a smart home device. A relay wired across your existing wall-button terminals simulates a button press on command. The ESP32 handles WiFi, Matter commissioning, and state tracking — all in a single `.ino` file.

## How It Works

Your garage door opener's wall button works by shorting two terminals together. This project connects a relay across those same terminals, letting the ESP32 "press" the button electronically. Matter takes care of the rest — discovery, pairing, and integration with your smart home ecosystem.

The door's position is estimated by timing (default: 12 seconds for a full open/close cycle). The onboard RGB LED reflects the current position — bright white when open, off when closed.

> **Future improvement:** A magnetic rotation encoder on the chain sprocket could provide real position feedback. Contributions welcome!

## Hardware

### Parts

| Part | Description | Link |
|------|-------------|------|
| **ESP32-S3 Supermini** | Microcontroller board (most ESP32 variants will work) | [Amazon](https://a.co/d/0guFfRhl) |
| **5V Relay Module** | Triggers from 3.3V logic, switches the garage button circuit | [Amazon](https://www.amazon.com/dp/B07874KSLY) |
| **USB Power Adapter** | Powers the ESP32 near the garage door opener | Any USB-C adapter |
| **Wire** | Hookup wire for connecting relay to the wall-button terminals | 22–18 AWG |

### Wiring

```
ESP32 GPIO 13  ──────►  Relay IN
ESP32 3.3V     ──────►  Relay VCC (triggers at 3.3V despite being a "5V" module)
ESP32 GND      ──────►  Relay GND

Relay COM      ──────►  Garage wall-button terminal 1
Relay NO       ──────►  Garage wall-button terminal 2
```

> **Note:** Use the **NO** (Normally Open) terminal on the relay. This way, the circuit is open at rest and closes (simulating a button press) when the relay activates.

> **⚠️ Safety:** Only connect the relay to the **low-voltage wall-button terminals** on your garage door opener — never to mains power. These terminals are typically 12–24V DC.

### Installation

Mount the ESP32 and relay near your garage door opener on the ceiling. Power the ESP32 with a USB adapter. Run two wires from the relay to the same terminals your wall button connects to (in parallel with the existing button — it will still work normally).

## Software Setup

### Prerequisites

- **Arduino IDE** (2.x recommended)
- **Arduino ESP32 Board Support** — version **3.3.6**
  - In Arduino IDE, go to *File → Preferences → Additional Board Manager URLs* and add:
    ```
    https://espressif.github.io/arduino-esp32/package_esp32_index.json
    ```
  - Then go to *Tools → Board → Boards Manager*, search for `esp32`, and install version **3.3.6**

### Configuration

1. **Create your `Secrets.h`** file with your WiFi credentials:
   ```cpp
   #define WIFI_SSID "your-wifi-name"
   #define WIFI_PASS "your-wifi-password"
   ```

2. **Adjust timing** (optional) — in `GarageDoor.ino`, change `DOOR_MOVE_SECONDS` to match how long your garage door takes to fully open or close:
   ```cpp
   const uint8_t DOOR_MOVE_SECONDS = 12;  // seconds for full open/close
   ```

3. **Select your board** in Arduino IDE:
   - *Tools → Board → esp32 → ESP32S3 Dev Module* (or your specific variant)
   - *Tools → USB CDC On Boot → Enabled* (for Serial Monitor output on Supermini)

4. **Upload** the sketch to your ESP32.

### Matter Commissioning

After uploading and powering on:

1. Open the **Serial Monitor** (115200 baud) to see the pairing code and QR code URL.
2. In your Matter controller app, add a new device:
   - **Amazon Alexa:** Alexa app → Devices → Add Device → Other → Matter
   - **Apple Home:** Home app → Add Accessory → scan the QR code
   - **Google Home:** Google Home app → Devices → Add → Matter-enabled device
3. Enter the **manual pairing code** or scan the **QR code** shown in the Serial Monitor.
4. The device appears as a window covering (this is the closest Matter device type to a garage door — it maps open/close commands perfectly).

> **Tip:** The device shows up as a "window covering" or "roller shade" in your smart home app. You can rename it to "Garage Door" after pairing.

## Usage

Once commissioned:

- **"Alexa, open the garage door"** — opens the door
- **"Alexa, close the garage door"** — closes the door
- Use any Matter-compatible app to control it
- The RGB LED on the ESP32 shows the door position (bright = open, dim/off = closed)
- The wall button continues to work normally — the relay is wired in parallel

## Technical Details

- **Matter Device Type:** `WindowCovering` (RollerShade Exterior) — the closest standard type to a garage door
- **State Persistence:** Door position is stored in ESP32's NVP (Preferences) and survives reboots
- **Relay Timing:** The relay activates for 250ms to simulate a button press
- **Position Estimation:** Linear interpolation over `DOOR_MOVE_SECONDS` — no physical sensor
- **LED Feedback:** RGB LED brightness maps inversely to lift percentage (0% lift = fully open = bright)

## Project Structure

```
GarageDoor/
├── GarageDoor.ino   # Main sketch — all logic in one file
└── Secrets.h        # WiFi credentials (not committed — create your own)
```

## License

MIT License — Copyright (c) 2026 Frank A. Krueger. See [LICENSE](LICENSE) for details.
