# LED Troubleshooting Guide

## Current Test (in main.cpp setup())

The diagnostic test will:
1. Send Serial messages describing each test
2. Light up **only the first 10 LEDs** (not all 1,881)
3. Test each color channel separately: Red → Green → Blue → White → All
4. Each test lasts 2 seconds

**Open Serial Monitor at 115200 baud** to see the test messages.

## Expected Behavior

If hardware is working:
- You should see 10 LEDs change colors every 2 seconds
- Serial Monitor shows "Test 1: RED", "Test 2: GREEN", etc.

## If Nothing Happens

### 1. Check Physical Connections

**Arduino Pin D2 → LED Strip DI (Data Input)**
- Verify wire is connected to D2 (digital pin 2)
- Check continuity with multimeter
- Try a different jumper wire

**Ground Connection**
- Arduino GND must connect to LED power supply GND (common ground)
- Check continuity between Arduino GND pin and LED strip GND
- This is CRITICAL - without common ground, data signal won't be interpreted correctly

**Power Supply**
- LED strip 24V power supply connected to +24V and GND
- LED strip also needs 5V logic on data line (Arduino outputs 5V, this is OK)
- Verify LED strip has power (check with multimeter: 24V between V+ and GND)

### 2. Try Different NeoPixel Configurations

Your strip might not be WS2814, or might have different timing. Edit line 211 in main.cpp:

**Current setting:**
```cpp
Adafruit_NeoPixel(LEDS_PER_PIN, LED_PIN_0, NEO_WRGB + NEO_KHZ800),
```

**Try these alternatives (one at a time):**

```cpp
// If it's actually WS2812B (GRB, not WRGB)
Adafruit_NeoPixel(LEDS_PER_PIN, LED_PIN_0, NEO_GRB + NEO_KHZ800),

// If it's SK6812 RGBW
Adafruit_NeoPixel(LEDS_PER_PIN, LED_PIN_0, NEO_GRBW + NEO_KHZ800),

// Try 400kHz instead of 800kHz
Adafruit_NeoPixel(LEDS_PER_PIN, LED_PIN_0, NEO_WRGB + NEO_KHZ400),

// WS2811 (older chips)
Adafruit_NeoPixel(LEDS_PER_PIN, LED_PIN_0, NEO_RGB + NEO_KHZ400),
```

### 3. Test with External NeoPixel Tester

Create a minimal test sketch (File → New):

```cpp
#include <Adafruit_NeoPixel.h>

#define PIN 2
#define NUMPIXELS 10

Adafruit_NeoPixel strip(NUMPIXELS, PIN, NEO_WRGB + NEO_KHZ800);

void setup() {
  Serial.begin(115200);
  Serial.println("Starting simple test...");
  
  strip.begin();
  strip.setBrightness(255);
  
  // Test: Set first LED to red
  strip.setPixelColor(0, strip.Color(255, 0, 0, 0));
  strip.show();
  
  Serial.println("First LED should be RED");
}

void loop() {
  delay(5000);
  Serial.println("Still running...");
}
```

If this doesn't work either, it's definitely a hardware/connection issue.

### 4. Hardware Diagnostics

**Signal Level Test:**
- Use multimeter in DC voltage mode
- Measure voltage on D2 pin with code running
- Should toggle between 0V and ~5V rapidly (might read ~2.5V average)
- If stuck at 0V or 5V, pin initialization failed

**LED Strip Polarity:**
- Verify data flows DI → DO (data input to data output)
- You should be connected to DI (start of strip), not DO (end of strip)
- Some strips have arrows showing data direction

**Data Line Resistance:**
- Try adding 330Ω resistor between Arduino D2 and LED strip DI
- This can help with signal integrity on long wires
- Place resistor close to Arduino

**Capacitor on Power:**
- Add 1000µF capacitor across LED strip +24V and GND (near strip)
- Helps smooth power fluctuations during LED updates

### 5. Verify LED Strip Type

**Check your actual LED strip datasheet:**
- Confirm it's actually WS2814 (not WS2812B, SK6812, APA102, etc.)
- Check voltage: WS2814 can run on 24V power but needs 5V logic
- Verify RGBW (4-channel) not RGB (3-channel)

**Common IC Types:**
- **WS2812B**: RGB (3-channel), 5V, 800kHz, GRB order
- **WS2814**: RGBW (4-channel), 24V power/5V logic, 800kHz, WRGB order
- **SK6812**: RGBW (4-channel), 5V, 800kHz, GRBW order
- **APA102**: RGB (3-channel), 5V, SPI (needs clock + data)

### 6. Simplify Test

If nothing works, try:
1. Use a single addressable LED (cut from strip)
2. Use 5V power instead of 24V temporarily
3. Use a different Arduino pin (try D6)
4. Test with a known-working LED strip

## Serial Monitor Output

You should see:
```
DIAGNOSTIC: Starting LED test on Pin D2...
Testing first 10 LEDs with different patterns:
Test 1: RED (R=255, G=0, B=0)
Test 2: GREEN (R=0, G=255, B=0)
Test 3: BLUE (R=0, G=0, B=255)
Test 4: WHITE channel (W=255)
Test 5: ALL CHANNELS (R=64, G=64, B=64, W=64)
Test 6: OFF (clearing all)
DIAGNOSTIC: Test complete...
```

If Serial Monitor shows this but LEDs don't light:
- **Hardware connection issue** (most likely)
- Wrong LED chip type
- LED strip is damaged

If Serial Monitor shows nothing:
- Wrong baud rate (should be 115200)
- Arduino not running uploaded code
- Serial port not connected
