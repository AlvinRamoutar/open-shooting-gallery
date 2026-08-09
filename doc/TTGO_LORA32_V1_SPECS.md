# TTGO LoRa32 V1 Technical Specifications

> ⚠️ **SUPERSEDED (2026-08-09)**: The lighting controller no longer uses this board. Hardware was swapped for a **stock ESP32 dev board** (no LoRa, no OLED), and the LED pinout moved from 4 shared pins (3 targets/pin) to **12 dedicated pins (1 target/pin)**. This entire document describes the TTGO's pin conflicts (LoRa SPI, OLED I2C) that **no longer apply** — a stock ESP32 has far more free GPIOs since there's no radio or display to share them with. Kept for historical reference only. See [DUAL_BOARD_ARCHITECTURE.md](DUAL_BOARD_ARCHITECTURE.md) for the current pinout and `main.cpp`'s `LED_PINS[]` array for the authoritative pin list.

## Hardware Overview

### Core Specifications
| Component | Specification |
|-----------|---------------|
| **MCU** | ESP32 (Xtensa dual-core 32-bit LX6) |
| **Clock Speed** | Up to 240 MHz (adjustable) |
| **SRAM** | 520 KB |
| **Flash** | 4 MB |
| **ROM** | 448 KB |
| **GPIO Pins** | 36 total (many reserved for peripherals) |
| **ADC** | 12-bit, 18 channels |
| **DAC** | 8-bit, 2 channels |
| **PWM** | 16 channels |

### LED Project Compatibility ✅

**RAM Analysis:**
- **Required for LED buffers**: 30,096 bytes (7,524 LEDs × 4 bytes)
- **Available SRAM**: 520,000 bytes
- **Buffer usage**: 5.8% of total RAM
- **Verdict**: More than sufficient! Can store full pixel buffers with plenty of headroom

**Comparison to Arduino Uno R3:**
- Uno R3: 2 KB RAM (impossible to fit 30KB buffers)
- TTGO: 520 KB RAM (**260× more memory**)

---

## Serial/UART Capabilities

### Hardware UARTs
ESP32 has **3 independent hardware UARTs**:

| UART | Default Pins | Typical Use | Available for Project |
|------|--------------|-------------|----------------------|
| **UART0** | TX: GPIO1, RX: GPIO3 | USB/Serial Monitor | ⚠️ Used for programming |
| **UART1** | TX: GPIO10, RX: GPIO9 | Flash Memory | ❌ Reserved |
| **UART2** | TX: GPIO17, RX: GPIO16 | User Application | ✅ **Recommended** |

**Pin Remapping:** ESP32 UARTs can be remapped to almost any GPIO pin using:
```cpp
Serial2.begin(115200, SERIAL_8N1, RX_PIN, TX_PIN);
```

### Recommended UART Configuration for Uno R3 Communication

**UART2 (Recommended):**
- **RX**: GPIO16 (receive from Uno TX)
- **TX**: GPIO17 (transmit to Uno RX)
- **Baud Rate**: 115200 (matches your current setup)
- **Reason**: GPIO16/17 have no conflicts with LoRa or OLED

**Alternative UART Pins (if needed):**
- Any GPIO except: 6-11 (flash), 34-39 (input-only), pins used by LoRa/OLED

---

## GPIO Pins for NeoPixel/WS2814 LED Control

### Recommended 4-Pin Configuration

**Best pins for high-speed LED data output:**
| Pin | Usage | Conflicts | Speed Rating |
|-----|-------|-----------|--------------|
| **GPIO12** | LED Data 1 (Targets 0,1,2) | None | ✅ Excellent |
| **GPIO13** | LED Data 2 (Targets 3,4,5) | None | ✅ Excellent |
| **GPIO14** | LED Data 3 (Targets 6,7,8) | None | ✅ Excellent |
| **GPIO27** | LED Data 4 (Targets 9,10,11) | None | ✅ Excellent |

**Why these pins:**
- ✅ Support high-speed output (800kHz WS2814 timing)
- ✅ No conflicts with LoRa, OLED, or UART2
- ✅ Adjacent numbering (easy to remember)
- ✅ Can source sufficient current for logic signals
- ✅ Have internal pull-up/pull-down options

### Alternative LED Pins (if needed)
- GPIO21, GPIO22 (I2C capable, but can be GPIO)
- GPIO32, GPIO33 (if LoRa DIO interrupts not needed)
- GPIO2 (has LED on some modules, avoid if present)

### Pins to AVOID for LED Control
| Pin(s) | Reason |
|--------|--------|
| GPIO0 | Boot mode, has button |
| GPIO4, GPIO15, GPIO16 | OLED (SDA, SCL, RST) |
| GPIO5, GPIO18, GPIO19, GPIO23 | LoRa SPI (SCK, MISO, MOSI, CS) |
| GPIO6-GPIO11 | Connected to flash memory |
| GPIO26 | LoRa reset |
| GPIO33 | LoRa DIO0 (critical interrupt) |
| GPIO32 | LoRa DIO1 (optional interrupt) |
| GPIO35 | LoRa DIO2, input-only |
| GPIO34, GPIO36, GPIO39 | Input-only, no pull-ups |

---

## Built-in Peripherals (TTGO LoRa32 V1)

### 1. LoRa Radio (SX1276/1278)

**Pin Assignment:**
| Signal | GPIO | Function |
|--------|------|----------|
| SCK | GPIO5 | SPI Clock |
| MISO | GPIO19 | SPI Data In |
| MOSI | GPIO27 | SPI Data Out |
| NSS/CS | GPIO18 | Chip Select |
| RST | GPIO14 | Reset |
| DIO0 | GPIO26 | Interrupt (RX done) |
| DIO1 | GPIO33 | Interrupt (RX timeout, FHSS) |
| DIO2 | GPIO32 | Interrupt (FH change) |

**For LED project:**
- If LoRa is **not needed**: Can reclaim GPIO18, GPIO26, GPIO32, GPIO33 for LEDs
- If LoRa is **needed**: Avoid these pins entirely

### 2. OLED Display (0.96" SSD1306, 128×64)

**Pin Assignment:**
| Signal | GPIO | Function |
|--------|------|----------|
| SDA | GPIO4 | I2C Data |
| SCL | GPIO15 | I2C Clock |
| RST | GPIO16 | Display Reset |

**Conflict Warning:** ⚠️
- GPIO16 is shared between OLED RST and recommended UART2 RX
- **Solutions:**
  1. **Don't use OLED** → GPIO16 free for UART2 RX (simplest)
  2. Use software reset for OLED → GPIO16 free for UART2
  3. Move UART2 RX to another pin (e.g., GPIO22)
  4. Use UART2 TX-only + different RX pin

**Recommendation:** Disable OLED for LED project (not critical for functionality)

### 3. Other Peripherals
- **Button**: GPIO0 (boot button, active LOW)
- **Blue LED**: GPIO25 (can disable in software if needed)
- **Battery Connector**: For 3.7V LiPo (JST 1.25mm, 2-pin)
- **USB-to-Serial**: CP2104 or CH9102 (depending on revision)

---

## Power Requirements

### Input Power
| Parameter | Specification |
|-----------|---------------|
| **USB Input** | 5V via micro-USB |
| **Battery Input** | 3.7-4.2V LiPo via JST connector |
| **Charging** | Built-in LiPo charger (when USB connected) |
| **Logic Level** | 3.3V (internal regulator from 5V or battery) |

### GPIO Current Capabilities
| Parameter | Value | Notes |
|-----------|-------|-------|
| **Per GPIO** | 40 mA max, 28 mA recommended | For driving loads |
| **Source/Sink** | ~12 mA safe continuous | For LED data signals |
| **Total GPIO** | 200 mA maximum across all pins | Cumulative limit |

### LED Strip Logic Requirements
- **WS2814 Logic High**: Minimum 0.7 × VDD
  - If WS2814 runs on 5V: Needs ≥3.5V logic high
  - **Problem**: ESP32 outputs 3.3V logic ⚠️
  
**Level Shifting Required:**
- **Option 1**: 74HCT245 or 74AHCT125 (3.3V → 5V level shifter)
- **Option 2**: BSS138 MOSFET + resistors (simple, cheap)
- **Option 3**: Some WS2812/WS2814 work with 3.3V if first LED is close
- **Option 4**: Use 330Ω resistor + short wire (may work, not guaranteed)

### Power Consumption Estimate
| Component | Current Draw |
|-----------|--------------|
| ESP32 (active, WiFi off) | ~80-160 mA |
| ESP32 (active, WiFi on) | ~200-300 mA |
| LoRa (transmit) | ~120 mA |
| LoRa (receive) | ~12 mA |
| OLED display | ~20 mA |
| **Total (worst case)** | ~440 mA |

**LED Power (separate supply):**
- 7,524 LEDs × 60 mA (max white) = 451.4 A
- Your 24V power supply handles LED power
- TTGO only needs to drive logic signals (<50 mA total)

---

## Pin Conflict Analysis

### Critical Conflicts

**GPIO16 Conflict:**
- Used by OLED RST
- Also recommended for UART2 RX
- **Resolution**: Disable OLED → GPIO16 free

### Pin Availability Summary

| Pin Category | GPIO Pins | Available for LEDs? |
|--------------|-----------|---------------------|
| **Recommended for LEDs** | 12, 13, 14, 27 | ✅ Yes |
| **Alternative for LEDs** | 21, 22, 32, 33 | ⚠️ Yes (if LoRa DIO not needed) |
| **Reserved (LoRa SPI)** | 5, 18, 19, 23, 26 | ❌ No |
| **Reserved (OLED)** | 4, 15, 16 | ❌ No (unless OLED disabled) |
| **Reserved (Flash)** | 6, 7, 8, 9, 10, 11 | ❌ Never use |
| **Input Only** | 34, 35, 36, 39 | ❌ No (can't output) |
| **Boot Mode** | 0, 2 | ⚠️ Avoid (can interfere with boot) |

---

## Recommended Pinout for Shooting Gallery Project

### Configuration A: LED Control Only (No LoRa/OLED)

```cpp
// UART Communication with Arduino Uno R3
#define UART_RX_PIN 16    // Receive from Uno TX
#define UART_TX_PIN 17    // Transmit to Uno RX

// LED Data Pins (WS2814)
#define LED_PIN_0 12      // Targets 0,1,2  (1,881 LEDs)
#define LED_PIN_1 13      // Targets 3,4,5  (1,881 LEDs)
#define LED_PIN_2 14      // Targets 6,7,8  (1,881 LEDs)
#define LED_PIN_3 27      // Targets 9,10,11 (1,881 LEDs)

// Initialization
Serial2.begin(115200, SERIAL_8N1, UART_RX_PIN, UART_TX_PIN);

Adafruit_NeoPixel strip0(1881, LED_PIN_0, NEO_WRGB + NEO_KHZ800);
Adafruit_NeoPixel strip1(1881, LED_PIN_1, NEO_WRGB + NEO_KHZ800);
Adafruit_NeoPixel strip2(1881, LED_PIN_2, NEO_WRGB + NEO_KHZ800);
Adafruit_NeoPixel strip3(1881, LED_PIN_3, NEO_WRGB + NEO_KHZ800);
```

### Configuration B: LED + LoRa (No OLED)

```cpp
// UART Communication
#define UART_RX_PIN 22    // Moved from GPIO16 (OLED RST free)
#define UART_TX_PIN 17

// LED Data Pins (avoiding LoRa pins)
#define LED_PIN_0 12      // Safe
#define LED_PIN_1 13      // Safe
#define LED_PIN_2 14      // Safe (may conflict with LoRa RST)
#define LED_PIN_3 27      // Safe

// LoRa Pins (SX1276/1278)
// SCK=5, MISO=19, MOSI=27, CS=18, RST=14, DIO0=26, DIO1=33, DIO2=32
```

⚠️ **Note**: GPIO14 conflict between LED and LoRa RST. If using LoRa, swap LED_PIN_2 to GPIO21.

### Configuration C: LED + OLED (No LoRa)

```cpp
// UART Communication (moved to avoid OLED GPIO16)
#define UART_RX_PIN 22    
#define UART_TX_PIN 17

// LED Data Pins
#define LED_PIN_0 12      
#define LED_PIN_1 13      
#define LED_PIN_2 14      
#define LED_PIN_3 27      

// OLED Pins (I2C)
// SDA=4, SCL=15, RST=16
```

---

## Level Shifting Consideration

### Problem Statement
- **ESP32 Logic Output**: 3.3V
- **WS2814 Logic Input**: Expects 0.7 × VDD = 3.5V (if VDD = 5V)
- **Voltage Gap**: 0.2V shortfall

### Solution Options

**Option 1: Level Shifter IC (Recommended)**
```
ESP32 GPIO → 74HCT245 → WS2814 Data In
(3.3V)       (3.3V→5V)   (5V)
```
- **Pros**: Reliable, handles all 4 pins, fast switching
- **Cons**: Requires extra chip (~$1)

**Option 2: MOSFET Shifter**
```
ESP32 GPIO → BSS138 + 10kΩ resistors → WS2814
```
- **Pros**: Cheap, simple, per-pin
- **Cons**: Need 4 MOSFETs

**Option 3: Try Direct (Risky)**
- Add 330Ω resistor + keep wire short (<1 meter)
- Some WS2812/WS2814 accept 3.3V logic
- Test thoroughly before deployment

**Option 4: Reduce LED Supply Voltage**
- Power LEDs with 3.3V instead of 5V
- Reduces brightness but simplifies logic levels
- Not practical for 24V LED strips

---

## Comparison Table: Uno R3 vs TTGO LoRa32 V1

| Feature | Arduino Uno R3 | TTGO LoRa32 V1 | Improvement |
|---------|----------------|----------------|-------------|
| **CPU** | 8-bit AVR, 16 MHz | 32-bit dual-core, 240 MHz | **15× faster** |
| **RAM** | 2 KB | 520 KB | **260× more** |
| **Flash** | 32 KB | 4 MB | **128× more** |
| **LED Buffer Fit** | ❌ No (needs 30KB) | ✅ Yes (5.8% usage) | **Solved!** |
| **Hardware UARTs** | 1 | 3 | More flexible |
| **GPIO Speed** | 16 MHz | 240 MHz | Faster bit-banging |
| **Logic Level** | 5V | 3.3V | ⚠️ Needs level shifter |
| **Price** | ~$25 | ~$15-20 | Cheaper |
| **Extras** | None | LoRa, OLED, WiFi, BT | Bonus features |

---

## Final Recommendations

### ✅ TTGO LoRa32 V1 is IDEAL for this project

**Why:**
1. **RAM**: 520 KB easily holds 30KB LED buffers (Uno's 2KB couldn't)
2. **Speed**: 240 MHz handles 4 simultaneous LED strips without issue
3. **UARTs**: UART2 available for Uno communication
4. **GPIO**: 4+ pins available for LED data with no conflicts

### Recommended Pinout (Final)
```cpp
// Communication with Uno R3
Serial2.begin(115200, SERIAL_8N1, 16, 17);  // RX=16, TX=17

// LED Strips (with level shifters)
#define LED_STRIP_0  12   // Targets 0,1,2
#define LED_STRIP_1  13   // Targets 3,4,5
#define LED_STRIP_2  14   // Targets 6,7,8
#define LED_STRIP_3  27   // Targets 9,10,11

// Don't use OLED (frees GPIO16 for UART)
// Don't use LoRa (simplifies design, frees pins)
```

### Hardware Changes Needed
1. **Add 74HCT245 level shifter** between ESP32 and LED strips (3.3V→5V)
2. Connect ESP32 UART2 to Uno UART0 (cross TX/RX)
3. Common ground between ESP32, Uno, and LED power supply

### Advantages Over Uno R3
- ✅ Solves RAM crisis (can store full LED buffers)
- ✅ Faster processing (smoother animations)
- ✅ More UARTs (cleaner serial routing)
- ✅ Future-proof (WiFi/BT/LoRa if needed later)
- ✅ More affordable

### Next Steps
1. Update `platformio.ini` with correct board config (already done ✓)
2. Port LED code to ESP32 (change pin numbers, add level shifter)
3. Test with small LED segment first
4. Implement full 4-strip control
5. Verify serial communication with Uno at 115200 baud
