# Dual Board Architecture: Uno R3 + TTGO LoRa32 v1

## System Overview

**New Architecture:**
```
Raspberry Pi (SERVER)
    ↕ USB Serial (115200 baud)
Arduino Uno R3 (MAIN CONTROLLER)
    ↕ Hardware Serial (TX0/RX0, 115200 baud)
TTGO LoRa32 v1 (LIGHTING CONTROLLER)
    ↕ GPIO12/13/14/27 → Level Shifter → WS2814 LEDs
```

---

## Question 1: Does TTGO LoRa32 v1 Address Earlier LED Issues?

### ✅ **YES - It's the Perfect Solution!**

### Problem vs Solution Comparison:

| Issue | Arduino Uno R3 | TTGO LoRa32 v1 | Solved? |
|-------|----------------|----------------|---------|
| **RAM for 30KB LED buffers** | 2 KB (impossible) | 520 KB (5.8% usage) | ✅ YES |
| **CPU speed for 4 strips** | 16 MHz (struggling) | 240 MHz dual-core (effortless) | ✅ YES |
| **Multiple high-speed outputs** | Limited | 4+ GPIO pins available | ✅ YES |
| **Flash for pattern code** | 32 KB (tight) | 4 MB (plenty) | ✅ YES |
| **Cost** | $25 | $15-20 (cheaper!) | ✅ BONUS |

### Memory Analysis:
```
LED Buffer Requirements:
- 7,524 LEDs × 4 bytes (RGBW) = 30,096 bytes
- Pattern descriptors: 288 bytes
- LED states: ~100 bytes
- Code overhead: ~5,000 bytes
TOTAL: ~35 KB

TTGO LoRa32 v1:
- 520 KB RAM available
- 35 KB / 520 KB = 6.7% RAM usage
- 93.3% FREE for future expansion!
```

**Verdict**: The TTGO LoRa32 v1 completely solves the RAM crisis and is actually cheaper than alternatives!

---

## Question 2: What Changes Are Needed?

### A. Hardware Configuration Flag

**Already implemented in main.cpp:**
```cpp
// Line 10 in main.cpp
#define ENABLE_LED_SYSTEM false  // For Uno R3
#define ENABLE_LED_SYSTEM true   // For TTGO LoRa32
```

### B. New Build Mode: Lighting Controller

**Add a new compile-time mode:**
```cpp
// ============================================================================
// BOARD CONFIGURATION
// ============================================================================
// Compile for different hardware targets

#define MODE_MAIN_CONTROLLER 1    // Arduino Uno R3: Servos, game logic, serial passthrough
#define MODE_LIGHTING_CONTROLLER 2 // TTGO LoRa32: LED control only

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
  SoftwareSerial LightingSerial(8, 9); // RX, TX to TTGO
  LightingSerial.begin(115200);
#endif
```

**TTGO LoRa32 v1** (Lighting Controller):
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
// LED pins
#if defined(ESP32)
  // TTGO LoRa32 v1
  #define LED_PIN_0 12  // Targets 0,1,2
  #define LED_PIN_1 13  // Targets 3,4,5
  #define LED_PIN_2 14  // Targets 6,7,8
  #define LED_PIN_3 27  // Targets 9,10,11
#else
  // Arduino Uno R3 (if ever needed)
  #define LED_PIN_0 2
  #define LED_PIN_1 3
  #define LED_PIN_2 4
  #define LED_PIN_3 5
#endif

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
- Forward LIGHTING protocols to TTGO via serial

**Lighting Controller (TTGO):**
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
          // Send all 3 words to TTGO
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

#### 5. **Lighting Controller Processing Logic**

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

**Already added in platformio.ini:**
```ini
[env:ttgo-lora32-v1]
platform = espressif32
board = ttgo-lora32-v1
upload_port = COM11
monitor_port = COM11
framework = arduino
lib_deps =
    adafruit/Adafruit NeoPixel@^1.12.0  # ADD THIS
    # Remove OLED/LoRa libs if not needed
```

### E. Build Flags for Easy Switching

**Recommended approach:**
```ini
[env:uno-main]
platform = atmelavr
board = uno
build_flags = 
    -DBOARD_MODE=1  # MODE_MAIN_CONTROLLER
    -DENABLE_LED_SYSTEM=false

[env:ttgo-lighting]
platform = espressif32
board = ttgo-lora32-v1
build_flags = 
    -DBOARD_MODE=2  # MODE_LIGHTING_CONTROLLER
    -DENABLE_LED_SYSTEM=true
```

---

## Question 3: Serial Wiring Between Boards

### ⚠️ CRITICAL ISSUE: Voltage Level Mismatch

**Problem:**
- Arduino Uno R3 outputs **5V logic** (TX)
- TTGO LoRa32 v1 expects **3.3V input** (RX = GPIO16)
- **Direct connection WILL DAMAGE the ESP32!**

**Solution: Voltage Divider for Uno TX → ESP32 RX**

### Physical Wiring Diagram

```
┌─────────────────────┐           ┌─────────────────────┐
│  Arduino Uno R3     │           │  TTGO LoRa32 v1     │
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

| Arduino Uno R3 | Component | TTGO LoRa32 v1 | Signal Direction |
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

### Solution: 74HCT245 Level Shifter

**Recommended IC:** 74HCT245 (octal bus transceiver)
- Converts 3.3V → 5V
- Fast enough for 800kHz WS2814 timing
- Handles 8 channels (only need 4)
- Cost: ~$1

### Wiring Diagram: ESP32 → Level Shifter → LEDs

```
┌──────────────────┐      ┌──────────────┐      ┌────────────┐
│  TTGO LoRa32 v1  │      │  74HCT245    │      │  WS2814    │
├──────────────────┤      ├──────────────┤      │  LEDs      │
│                  │      │              │      │            │
│ GPIO12 ─────────►┼─────►│ A1      B1 ─┼─────►│ DI (Strip 0)│
│ GPIO13 ─────────►┼─────►│ A2      B2 ─┼─────►│ DI (Strip 1)│
│ GPIO14 ─────────►┼─────►│ A3      B3 ─┼─────►│ DI (Strip 2)│
│ GPIO27 ─────────►┼─────►│ A4      B4 ─┼─────►│ DI (Strip 3)│
│                  │      │              │      │            │
│ 3.3V ───────────►┼─────►│ VCC (A side)│      │            │
│ GND ─────────────┼─────►│ GND         │◄─────┼─── GND     │
│                  │      │ VCC (B side)│◄─────┼─── +5V*    │
│                  │      │ DIR → VCC   │      │            │
│                  │      │ /OE → GND   │      │            │
└──────────────────┘      └──────────────┘      └────────────┘

* 5V from separate power supply (NOT from ESP32!)
  Your 24V supply should have a 5V regulator for WS2814 logic
```

### 74HCT245 Pin Configuration

| Pin | Function | Connect To |
|-----|----------|------------|
| **A1-A4** | 3.3V inputs | ESP32 GPIO12/13/14/27 |
| **B1-B4** | 5V outputs | WS2814 DI pins (via 330Ω) |
| **DIR** | Direction | VCC (5V) - A→B direction |
| **/OE** | Output Enable | GND - always enabled |
| **VCC (A)** | Low voltage | ESP32 3.3V |
| **VCC (B)** | High voltage | 5V from LED power supply |
| **GND** | Ground | Common ground |

### Alternative: Per-Channel Level Shift

If you don't want to add a 74HCT245, you can try:

**Option 1: 330Ω Resistor + Short Wire (may work)**
- Some WS2814 strips accept 3.3V if first LED is very close
- Add 330Ω resistor between ESP32 GPIO and LED DI
- Keep wire <6 inches
- **Success rate: ~60%** (not guaranteed)

**Option 2: BSS138 MOSFET Shifter (per channel)**
- More reliable than resistor-only
- Requires 4 MOSFETs + resistors
- Cost: ~$2 total
- More complex wiring

**Recommendation:** Use 74HCT245 for reliability!

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
│            │    │         └──► 74HCT245 VCC(B) + WS2814 logic
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
│ +5V ────────────┼──► TTGO LoRa32 v1 (via USB)
│ GND ────────────┼──► Common ground
└─────────────────┘
```

### Signal Connections

**Server ↔ Uno R3:**
- USB cable (5V power + Serial communication)

**Uno R3 ↔ TTGO LoRa32 v1:**
- Uno D8 → 1kΩ → (midpoint) → 2kΩ → GND (voltage divider to 3.3V) → TTGO GPIO16
- Uno D9 ← direct wire ← TTGO GPIO17
- GND ← common ← GND

**TTGO LoRa32 v1 → LEDs:**
- GPIO12 → 74HCT245 A1 → B1 → 330Ω → LED Strip 0 DI
- GPIO13 → 74HCT245 A2 → B2 → 330Ω → LED Strip 1 DI
- GPIO14 → 74HCT245 A3 → B3 → 330Ω → LED Strip 2 DI
- GPIO27 → 74HCT245 A4 → B4 → 330Ω → LED Strip 3 DI

---

## Implementation Checklist

### Hardware
- [ ] Purchase TTGO LoRa32 v1 board (~$15-20)
- [ ] Purchase 74HCT245 level shifter IC (~$1)
- [ ] Gather resistors: 2× 1kΩ, 2× 2kΩ, 4× 330Ω
- [ ] 5V voltage regulator (if not already in LED power system)
- [ ] Breadboard or PCB for level shifter circuit
- [ ] Jumper wires

### Software
- [ ] Add `BOARD_MODE` compile-time flag to main.cpp
- [ ] Implement SoftwareSerial on Uno R3 (D8/D9)
- [ ] Configure UART2 on TTGO (GPIO16/17)
- [ ] Add protocol forwarding logic to Uno
- [ ] Test lighting protocol reception on TTGO
- [ ] Update platformio.ini with NeoPixel library for TTGO
- [ ] Create build configurations for each board

### Testing
- [ ] Test Uno ↔ TTGO serial communication (echo test)
- [ ] Test voltage levels with multimeter (3.3V at ESP32 RX)
- [ ] Test single LED strip with TTGO + level shifter
- [ ] Test all 4 LED strips simultaneously
- [ ] Test full system: Server → Uno → TTGO → LEDs

---

## Benefits of This Architecture

✅ **Modular Design**: Replace/upgrade boards independently  
✅ **Uno R3 Focus**: Handles servos and game logic without RAM constraints  
✅ **TTGO Focus**: Dedicated LED controller with ample RAM  
✅ **Cost Effective**: TTGO LoRa32 v1 is cheaper than Arduino Mega/Teensy  
✅ **Future Expansion**: LoRa radio available for wireless features  
✅ **Single Codebase**: Same code compiles for both targets with flags  
✅ **Easy Debugging**: Each board can be tested independently  

---

## Potential Future Enhancements

1. **LoRa Remote Control**: Use built-in LoRa for wireless game control
2. **OLED Status Display**: Show LED status, target states on OLED
3. **WiFi Integration**: ESP32 can add web interface for LED control
4. **Bluetooth**: Control LEDs from mobile app
5. **SD Card Logging**: Log game events (TTGO can add SD card module)

---

*Document created: 2026-08-04*  
*Architecture: Uno R3 (Main) + TTGO LoRa32 v1 (Lighting)*  
*Project: Open Shooting Gallery Dual Board System*
