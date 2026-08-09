# Dual Board Architecture: Uno R3 + ESP32

> **Hardware update (2026-08-09)**: The lighting controller board changed from a TTGO LoRa32 v1 to a **stock ESP32 dev board** (no LoRa, no OLED), and the LED pinout changed from 4 shared pins (3 targets/pin) to **12 dedicated pins (1 target/pin)**. Sections below have been updated to match.

## System Overview

**Current Architecture:**
```
Raspberry Pi (SERVER)
    ↕ USB Serial (115200 baud)
Arduino Uno R3 (MAIN CONTROLLER)
    ↕ Hardware Serial (TX0/RX0, 115200 baud)
ESP32 Dev Board (LIGHTING CONTROLLER)
    ↕ 12× GPIO (one per target) → Level Shifter(s) → WS2814 LEDs
```

---

## Question 1: Does an ESP32 Address Earlier LED Issues?

### ✅ **YES - It's the Perfect Solution!**

### Problem vs Solution Comparison:

| Issue | Arduino Uno R3 | ESP32 Dev Board | Solved? |
|-------|----------------|----------------|---------|
| **RAM for 30KB LED buffers** | 2 KB (impossible) | 520 KB (5.8% usage) | ✅ YES |
| **CPU speed for 12 strips** | 16 MHz (struggling) | 240 MHz dual-core (effortless) | ✅ YES |
| **Multiple high-speed outputs** | Limited | 12+ GPIO pins available | ✅ YES |
| **Flash for pattern code** | 32 KB (tight) | 4 MB (plenty) | ✅ YES |
| **Cost** | $25 | $8-15 (cheaper, no LoRa/OLED needed) | ✅ BONUS |

### Memory Analysis:
```
LED Buffer Requirements:
- 7,536 LEDs × 4 bytes (RGBW) = 30,144 bytes
- Pattern descriptors: 288 bytes
- LED states: ~100 bytes
- Code overhead: ~5,000 bytes
TOTAL: ~35 KB

ESP32:
- 520 KB RAM available
- 35 KB / 520 KB = 6.7% RAM usage
- 93.3% FREE for future expansion!
```

**Verdict**: The ESP32 completely solves the RAM crisis and is actually cheaper than alternatives!

---

## Question 2: What Changes Are Needed?

### A. Hardware Configuration Flag

**Already implemented in main.cpp:**
```cpp
// Line 10 in main.cpp
#define ENABLE_LED_SYSTEM false  // For Uno R3
#define ENABLE_LED_SYSTEM true   // For ESP32 LoRa32
```

### B. New Build Mode: Lighting Controller

**Add a new compile-time mode:**
```cpp
// ============================================================================
// BOARD CONFIGURATION
// ============================================================================
// Compile for different hardware targets

#define MODE_MAIN_CONTROLLER 1    // Arduino Uno R3: Servos, game logic, serial passthrough
#define MODE_LIGHTING_CONTROLLER 2 // ESP32 LoRa32: LED control only

// Set active mode:
#define BOARD_MODE MODE_MAIN_CONTROLLER  // Change for each target
```

### C. Code Changes Summary

#### 1. **Serial Configuration (Platform-Specific)**

**Arduino Uno R3** (Main Controller):
```cpp
#if BOARD_MODE == MODE_MAIN_CONTROLLER
  // UART0 for server communication (USB)
  Serial.begin(115200);  
  
  // UART0 TX/RX (D0/D1) for lighting controller
  // Problem: UART0 is also used for USB!
  // Solution: Use SoftwareSerial on different pins
  #include <SoftwareSerial.h>
  SoftwareSerial LightingSerial(8, 9); // RX, TX to ESP32
  LightingSerial.begin(115200);
#endif
```

**ESP32** (Lighting Controller):
```cpp
#if BOARD_MODE == MODE_LIGHTING_CONTROLLER
  // UART0 for debug output (USB)
  Serial.begin(115200);
  
  // UART2 for main controller communication
  Serial2.begin(115200, SERIAL_8N1, 16, 17); // RX=GPIO16, TX=GPIO17
#endif
```

#### 2. **Pin Definitions (Platform-Specific)**

```cpp
// LED pins - one dedicated GPIO per target (ESP32 dev board)
const uint8_t LED_PINS[NUM_LED_PINS] = {
  4, 13, 14, 18, 19, 21, 22, 23, 25, 26, 27, 32  // Targets 0-11
};

// Servo pins (Uno R3 only)
#if BOARD_MODE == MODE_MAIN_CONTROLLER
  // I2C for PWM servo shield (A4/A5)
  Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver();
#endif
```

#### 3. **Protocol Handling Logic**

**Main Controller (Uno R3):**
- Receive ALL protocols from server
- Process GENERAL and TARGET protocols locally
- Forward LIGHTING protocols to ESP32 via serial

**Lighting Controller (ESP32):**
- Receive LIGHTING protocols from Uno R3
- Process LED patterns and update strips
- Send status back to Uno R3 (optional)

#### 4. **Main Controller Forwarding Logic**

```cpp
#if BOARD_MODE == MODE_MAIN_CONTROLLER

void processSerialData() {
  uint32_t word;
  
  if (readSerialWord(word)) {
    uint8_t protocolId = (word >> 28) & 0x0F;
    
    switch (protocolId) {
      case PROTOCOL_GENERAL:
        // Process locally
        parseGeneralProtocol(word, generalConfig);
        handleGeneralProtocol();
        break;
        
      case PROTOCOL_TARGET:
        // Process locally
        parseTargetProtocol(word, config);
        handleTargetProtocol(config.targetId);
        break;
        
      case PROTOCOL_LIGHTING:
        // Forward to lighting controller
        uint32_t word2, word3;
        if (readSerialWord(word2) && readSerialWord(word3)) {
          // Send all 3 words to the ESP32
          LightingSerial.write((uint8_t*)&word, 4);
          LightingSerial.write((uint8_t*)&word2, 4);
          LightingSerial.write((uint8_t*)&word3, 4);
        }
        break;
    }
  }
}

#endif
```

#### 5. **Lighting Controller Processing Logic (ESP32)**

```cpp
#if BOARD_MODE == MODE_LIGHTING_CONTROLLER

void loop() {
  // Receive LIGHTING protocols from Uno R3 via Serial2
  if (Serial2.available() >= 12) {  // 3 words × 4 bytes
    uint32_t word1, word2, word3;
    
    Serial2.readBytes((uint8_t*)&word1, 4);
    Serial2.readBytes((uint8_t*)&word2, 4);
    Serial2.readBytes((uint8_t*)&word3, 4);
    
    LightingProtocol config;
    parseLightingProtocol(word1, word2, word3, config);
    handleLightingProtocol(config);
  }
  
  // Update LED patterns
  updatePatternStates();
  updateDirtyLEDs();
}

#endif
```

### D. PlatformIO Configuration Updates

**Current platformio.ini:**
```ini
[env:esp32-lighting]
platform = espressif32
board = esp32dev
upload_port = COM11
monitor_port = COM11
framework = arduino
lib_deps =
    adafruit/Adafruit NeoPixel@^1.12.0
```

### E. Build Flags for Easy Switching

**Current approach:**
```ini
[env:uno-main]
platform = atmelavr
board = uno
build_flags = 
    -DBOARD_MODE=1  # MODE_MAIN_CONTROLLER

[env:esp32-lighting]
platform = espressif32
board = esp32dev
build_flags = 
    -DBOARD_MODE=2  # MODE_LIGHTING_CONTROLLER
```

---

## Question 3: Serial Wiring Between Boards

### ⚠️ CRITICAL ISSUE: Voltage Level Mismatch

**Problem:**
- Arduino Uno R3 outputs **5V logic** (TX)
- ESP32 expects **3.3V input** (RX = GPIO16)
- **Direct connection WILL DAMAGE the ESP32!**

**Solution: Voltage Divider for Uno TX → ESP32 RX**

### Physical Wiring Diagram

```
┌─────────────────────┐           ┌─────────────────────┐
│  Arduino Uno R3     │           │  ESP32     │
│  (Main Controller)  │           │  (Lighting)         │
├─────────────────────┤           ├─────────────────────┤
│                     │           │                     │
│  D8 (TX) ──────────┼──────┬────┼──► GPIO16 (RX2)     │
│                     │      │    │                     │
│                     │      R1   │                     │
│                     │      │    │                     │
│  D9 (RX) ◄─────────┼──────┼────┼──── GPIO17 (TX2)    │
│                     │      │    │                     │
│  GND ──────────────┼──────┼────┼──── GND             │
│                     │      │    │                     │
└─────────────────────┘      R2   └─────────────────────┘
                             │
                            GND

R1 = 1kΩ (between Uno TX and voltage divider midpoint)
R2 = 2kΩ (between midpoint and GND)
Voltage at ESP32 RX = 5V × (2kΩ / 3kΩ) = 3.3V ✓
```

### Detailed Connection Table

| Arduino Uno R3 | Component | ESP32 | Signal Direction |
|----------------|-----------|----------------|------------------|
| **D8** (SoftwareSerial TX) | → 1kΩ resistor → | **GPIO16** (UART2 RX) | Uno → ESP32 |
| (midpoint of divider) | → 2kΩ resistor → GND | - | Voltage division |
| **D9** (SoftwareSerial RX) | ← Direct connection ← | **GPIO17** (UART2 TX) | Uno ← ESP32 |
| **GND** | ─────────────────────── | **GND** | Common ground |
| **5V** | (NOT connected to ESP32) | - | Keep separate! |

**Why D8/D9?**
- Uno's D0/D1 (UART0) are used for USB communication with server
- D8/D9 are free and support SoftwareSerial
- Can use any free digital pins on Uno

### Voltage Divider Calculation

```
Uno TX = 5V
ESP32 RX expects ≤ 3.3V

R1 = 1kΩ (top resistor)
R2 = 2kΩ (bottom resistor to GND)

Voltage at ESP32 RX = 5V × (R2 / (R1 + R2))
                     = 5V × (2kΩ / 3kΩ)
                     = 5V × 0.667
                     = 3.33V ✓ SAFE
```

### ESP32 TX → Uno RX: No Level Shift Needed!

**Good news:** ESP32's 3.3V output is sufficient for Uno's 5V RX input.
- Uno RX recognizes HIGH at ≥2.0V (typ)
- ESP32 TX outputs 3.3V
- **3.3V > 2.0V** → Works reliably! ✓

### Alternative: Logic Level Converter Module

**If you prefer a cleaner solution:**
- Use a bi-directional logic level converter (e.g., BOB-12009)
- Cost: ~$3
- No resistor calculations needed
- Supports multiple channels (useful for future expansion)

```
┌──────────────┐           ┌──────────────┐
│ Uno R3 (5V)  │           │ ESP32 (3.3V) │
├──────────────┤           ├──────────────┤
│ D8 (TX) ─────┼─► HV1     │              │
│              │   ⇅  LV1 ◄┼───── GPIO16  │
│ D9 (RX) ◄────┼─── HV2    │              │
│              │   ⇅  LV2 ─┼────► GPIO17  │
│ 5V ──────────┼─► HV      │              │
│ GND ─────────┼─► GND ────┼───── GND     │
│              │   LV ◄────┼───── 3.3V    │
└──────────────┘           └──────────────┘
```

---

## LED Data Signal: ESP32 → WS2814

### ⚠️ SECOND CRITICAL ISSUE: LED Logic Level

**Problem:**
- ESP32 GPIO outputs **3.3V logic**
- WS2814 requires **≥3.5V logic** (0.7 × 5V supply)
- **Will not work reliably without level shifting!**

### Solution: 3× BSS138 4-Channel Level Shifter Modules

**In use for this build:** 3× generic BSS138 4-channel bi-directional logic level converter modules (the same breakout style used for the Uno↔ESP32 UART link) — 3 modules × 4 channels = 12 channels, an exact fit for the 12 LED pins.
- Converts 3.3V ↔ 5V (bi-directional, though only ESP32→LED direction is used here)
- Each module has its own `LV`/`HV` power rails and 4 channel pairs (`LV1-4` / `HV1-4`)
- Cost: ~$1-2/module, ~$3-6 total

### Wiring Diagram: ESP32 → BSS138 Modules → LEDs

```
┌──────────────────┐      ┌──────────────┐      ┌────────────────┐
│  ESP32           │      │  BSS138 #1   │      │  WS2814 LEDs   │
├──────────────────┤      ├──────────────┤      ├────────────────┤
│ GPIO4  ─────────►│─────►│ LV1     HV1 ─│─────►│ DI (Target 0)  │
│ GPIO13 ─────────►│─────►│ LV2     HV2 ─│─────►│ DI (Target 1)  │
│ GPIO14 ─────────►│─────►│ LV3     HV3 ─│─────►│ DI (Target 2)  │
│ GPIO18 ─────────►│─────►│ LV4     HV4 ─│─────►│ DI (Target 3)  │
│                  │      │              │      │                │
│                  │      ┌──────────────┐      │                │
│                  │      │  BSS138 #2   │      │                │
│ GPIO19 ─────────►│─────►│ LV1     HV1 ─│─────►│ DI (Target 4)  │
│ GPIO21 ─────────►│─────►│ LV2     HV2 ─│─────►│ DI (Target 5)  │
│ GPIO22 ─────────►│─────►│ LV3     HV3 ─│─────►│ DI (Target 6)  │
│ GPIO23 ─────────►│─────►│ LV4     HV4 ─│─────►│ DI (Target 7)  │
│                  │      │              │      │                │
│                  │      ┌──────────────┐      │                │
│                  │      │  BSS138 #3   │      │                │
│ GPIO25 ─────────►│─────►│ LV1     HV1 ─│─────►│ DI (Target 8)  │
│ GPIO26 ─────────►│─────►│ LV2     HV2 ─│─────►│ DI (Target 9)  │
│ GPIO27 ─────────►│─────►│ LV3     HV3 ─│─────►│ DI (Target 10) │
│ GPIO32 ─────────►│─────►│ LV4     HV4 ─│─────►│ DI (Target 11) │
│                  │      │              │      │                │
│ 3.3V ───────────►│─────►│ LV (all 3 modules)  │                │
│ GND ─────────────│──────│ GND     HV ◄─┼──────┼─── +5V*        │
└──────────────────┘      └──────────────┘      └────────────────┘

* 5V from separate power supply (NOT from ESP32!)
  Your 24V supply should have a 5V regulator for WS2814 logic
```

### BSS138 Module Configuration (×3)

| Pin | Function | Connect To |
|-----|----------|------------|
| **LV1-4** (per module) | 3.3V inputs | ESP32 GPIO 4,13,14,18 (module 1), 19,21,22,23 (module 2), 25,26,27,32 (module 3) |
| **HV1-4** (per module) | 5V outputs | WS2814 DI pins (via 330Ω) |
| **LV** | Low voltage rail | ESP32 3.3V |
| **HV** | High voltage rail | 5V from LED power supply |
| **GND** | Ground | Common ground — shared across all 3 modules, the ESP32, and the LED power supply |

---

## Complete System Wiring Summary

### Power Distribution

```
┌─────────────────┐
│ 24V Power Supply│
├─────────────────┤
│ +24V ──┬────────┼──► WS2814 LED strips (VDD)
│        │        │
│        └───┬────┼──► 5V Regulator (e.g., LM7805 or buck converter)
│            │    │         │
│            │    │         └──► BSS138 HV rails (×3) + WS2814 logic
│            │    │
│            │    │
│ GND ───────┴────┼──► Common ground (all boards + LEDs)
└─────────────────┘

┌─────────────────┐
│ USB Power       │
├─────────────────┤
│ +5V ────────────┼──► Arduino Uno R3 (via USB)
│ GND ────────────┼──► Common ground
└─────────────────┘

┌─────────────────┐
│ USB Power       │
├─────────────────┤
│ +5V ────────────┼──► ESP32 (via USB)
│ GND ────────────┼──► Common ground
└─────────────────┘
```

### Signal Connections

**Server ↔ Uno R3:**
- USB cable (5V power + Serial communication)

**Uno R3 ↔ ESP32:**
- Uno D8 → 1kΩ → (midpoint) → 2kΩ → GND (voltage divider to 3.3V) → ESP32 GPIO16
- Uno D9 ← direct wire ← ESP32 GPIO17
- GND ← common ← GND

**ESP32 → LEDs** (one pin per target, `LED_PINS[]` in `main.cpp`):
- GPIO4, 13, 14, 18 → BSS138 #1 (LV1-4) → HV1-4 → 330Ω each → LED Targets 0-3 DI
- GPIO19, 21, 22, 23 → BSS138 #2 (LV1-4) → HV1-4 → 330Ω each → LED Targets 4-7 DI
- GPIO25, 26, 27, 32 → BSS138 #3 (LV1-4) → HV1-4 → 330Ω each → LED Targets 8-11 DI

---

## Implementation Checklist

### Hardware
- [ ] Purchase ESP32 dev board (~$8-15)
- [x] 3× BSS138 4-channel level shifter modules (~$3-6 total) — on hand
- [ ] Gather resistors: 2× 1kΩ, 2× 2kΩ (Uno↔ESP32 UART), 12× 330Ω (LED data lines)
- [ ] 5V voltage regulator (if not already in LED power system)
- [ ] Breadboard or PCB for level shifter circuit
- [ ] Jumper wires

### Software
- [ ] Add `BOARD_MODE` compile-time flag to main.cpp
- [ ] Implement SoftwareSerial on Uno R3 (D8/D9)
- [ ] Configure UART2 on ESP32 (GPIO16/17)
- [ ] Add protocol forwarding logic to Uno
- [ ] Test lighting protocol reception on ESP32
- [ ] Update platformio.ini with NeoPixel library for ESP32
- [ ] Create build configurations for each board

### Testing
- [ ] Test Uno ↔ ESP32 serial communication (echo test)
- [ ] Test voltage levels with multimeter (3.3V at ESP32 RX)
- [ ] Test single LED strip with ESP32 + level shifter
- [ ] Test all 12 LED strips simultaneously
- [ ] Test full system: Server → Uno → ESP32 → LEDs

---

## Benefits of This Architecture

✅ **Modular Design**: Replace/upgrade boards independently  
✅ **Uno R3 Focus**: Handles servos and game logic without RAM constraints  
✅ **ESP32 Focus**: Dedicated LED controller with ample RAM and enough free GPIOs for a dedicated pin per target  
✅ **Cost Effective**: ESP32 is cheaper than Arduino Mega/Teensy, and a stock dev board is cheaper than the TTGO LoRa32 it replaced  
✅ **Single Codebase**: Same code compiles for both targets with flags  
✅ **Easy Debugging**: Each board can be tested independently  

---

## Potential Future Enhancements

1. **WiFi Integration**: ESP32 can add web interface for LED control
2. **Bluetooth**: Control LEDs from mobile app
3. **SD Card Logging**: Log game events (ESP32 can add SD card module)
4. **External module for LoRa/OLED**: The stock ESP32 dev board has no on-board LoRa or OLED — either could still be added via external breakout modules if needed, but would need to share the now fully-committed 12 LED GPIOs + UART2, so re-evaluate pin budget first

---

*Document created: 2026-08-04*  
*Updated 2026-08-09: TTGO LoRa32 v1 replaced with stock ESP32 dev board; LED pinout changed from 4 shared pins to 12 dedicated pins (one per target)*  
*Architecture: Uno R3 (Main) + ESP32 (Lighting)*  
*Project: Open Shooting Gallery Dual Board System*
