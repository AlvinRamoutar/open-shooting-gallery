# Serial Protocol Examples

This document provides example serial data for controlling the shooting gallery system, with a focus on LED lighting sequences.

---

## Protocol Overview

All commands are **32-bit words** sent as **4 bytes** in **big-endian** order at **115200 baud**.

**Byte Order**: `[Byte3 Byte2 Byte1 Byte0]` where Byte3 is MSB

---

## Lighting Protocol (Type 0x2)

The lighting protocol consists of **3 consecutive 32-bit words** (12 bytes total, 96 bits):

```
Word 1: [Type=0x2][TargetID][StateIdx][PatternType]
Word 2: [Speed][Param1][Brightness][Reserved]
Word 3: [White][Red][Green][Blue]
```

### Pattern Types
- `0x00` = OFF (all LEDs dark)
- `0x01` = SOLID (constant color)
- `0x02` = PULSE (breathing effect)
- `0x03` = CHASE (moving tail)
- `0x04` = STROBE (flashing)
- `0x05` = GRADIENT (color transition)

### State Indices
- `0` = IDLE state
- `1` = MOVING state  
- `2` = HIT state

---

## Example 1: Set Target 0 IDLE to Cyan Solid

**Effect**: Target 0 shows solid cyan when idle (default pattern)

**Pattern**: Solid cyan (G=64, B=128), brightness 64

### Hex Bytes (12 bytes):
```
02 00 00 01  40 00 40 00  00 00 40 80
```

### Breakdown:
```
Word 1: 0x02000001
  - Type: 0x02 (Lighting)
  - Target: 0x00 (Target 0)
  - State: 0x00 (IDLE)
  - Pattern: 0x01 (SOLID)

Word 2: 0x40004000
  - Speed: 0x40 (64, not used for SOLID)
  - Param1: 0x00 (not used for SOLID)
  - Brightness: 0x40 (64 = 25%)
  - Reserved: 0x00

Word 3: 0x00004080
  - White: 0x00 (0)
  - Red: 0x00 (0)
  - Green: 0x40 (64)
  - Blue: 0x80 (128)
```

### Python Code:
```python
import serial

ser = serial.Serial('COM3', 115200)

# Lighting protocol: Target 0, IDLE state, SOLID cyan
data = bytes([
    0x02, 0x00, 0x00, 0x01,  # Word 1: Type, Target, State, Pattern
    0x40, 0x00, 0x40, 0x00,  # Word 2: Speed, Param1, Brightness, Reserved
    0x00, 0x00, 0x40, 0x80   # Word 3: W, R, G, B
])
ser.write(data)
```

---

## Example 2: Set Target 3 MOVING to Green Chase

**Effect**: Target 3 shows green moving chase when target is moving

**Pattern**: Green chase, speed 8, tail length 20, brightness 128

### Hex Bytes (12 bytes):
```
02 03 01 03  08 14 80 00  00 00 FF 00
```

### Breakdown:
```
Word 1: 0x02030103
  - Type: 0x02 (Lighting)
  - Target: 0x03 (Target 3)
  - State: 0x01 (MOVING)
  - Pattern: 0x03 (CHASE)

Word 2: 0x08148000
  - Speed: 0x08 (8, chase moves 8 pixels/frame)
  - Param1: 0x14 (20, tail length)
  - Brightness: 0x80 (128 = 50%)
  - Reserved: 0x00

Word 3: 0x0000FF00
  - White: 0x00 (0)
  - Red: 0x00 (0)
  - Green: 0xFF (255, full green)
  - Blue: 0x00 (0)
```

### Python Code:
```python
# Lighting protocol: Target 3, MOVING state, GREEN chase
data = bytes([
    0x02, 0x03, 0x01, 0x03,  # Word 1
    0x08, 0x14, 0x80, 0x00,  # Word 2
    0x00, 0x00, 0xFF, 0x00   # Word 3
])
ser.write(data)
```

---

## Example 3: Set Target 7 HIT to Red Strobe

**Effect**: Target 7 shows red strobe when hit

**Pattern**: Red strobe, 5 Hz (200ms period), brightness 255

### Hex Bytes (12 bytes):
```
02 07 02 04  05 00 FF 00  00 FF 00 00
```

### Breakdown:
```
Word 1: 0x02070204
  - Type: 0x02 (Lighting)
  - Target: 0x07 (Target 7)
  - State: 0x02 (HIT)
  - Pattern: 0x04 (STROBE)

Word 2: 0x0500FF00
  - Speed: 0x05 (5 Hz strobe frequency)
  - Param1: 0x00 (not used for STROBE)
  - Brightness: 0xFF (255 = 100%)
  - Reserved: 0x00

Word 3: 0x00FF0000
  - White: 0x00 (0)
  - Red: 0xFF (255, full red)
  - Green: 0x00 (0)
  - Blue: 0x00 (0)
```

### Python Code:
```python
# Lighting protocol: Target 7, HIT state, RED strobe
data = bytes([
    0x02, 0x07, 0x02, 0x04,  # Word 1
    0x05, 0x00, 0xFF, 0x00,  # Word 2
    0x00, 0xFF, 0x00, 0x00   # Word 3
])
ser.write(data)
```

---

## Example 4: Set All Targets to White Pulse (Breathing)

**Effect**: All 12 targets show white breathing effect

**Pattern**: White pulse, speed 4, brightness 200

### Hex Bytes (12 bytes):
```
02 0D 00 02  04 00 C8 00  FF 00 00 00
```

### Breakdown:
```
Word 1: 0x020D0002
  - Type: 0x02 (Lighting)
  - Target: 0x0D (13 = ALL targets)
  - State: 0x00 (IDLE state)
  - Pattern: 0x02 (PULSE)

Word 2: 0x0400C800
  - Speed: 0x04 (4, pulse speed)
  - Param1: 0x00 (not used for PULSE)
  - Brightness: 0xC8 (200 = 78%)
  - Reserved: 0x00

Word 3: 0xFF000000
  - White: 0xFF (255, full white)
  - Red: 0x00 (0)
  - Green: 0x00 (0)
  - Blue: 0x00 (0)
```

### Python Code:
```python
# Lighting protocol: ALL targets, IDLE state, WHITE pulse
data = bytes([
    0x02, 0x0D, 0x00, 0x02,  # Word 1
    0x04, 0x00, 0xC8, 0x00,  # Word 2
    0xFF, 0x00, 0x00, 0x00   # Word 3
])
ser.write(data)
```

---

## Example 5: Turn Off Target 5 LEDs

**Effect**: Turn off all LEDs on Target 5

**Pattern**: OFF (all parameters ignored except pattern type)

### Hex Bytes (12 bytes):
```
02 05 00 00  00 00 00 00  00 00 00 00
```

### Breakdown:
```
Word 1: 0x02050000
  - Type: 0x02 (Lighting)
  - Target: 0x05 (Target 5)
  - State: 0x00 (IDLE)
  - Pattern: 0x00 (OFF)

Word 2: 0x00000000 (all zeros, not used)
Word 3: 0x00000000 (all zeros, not used)
```

### Python Code:
```python
# Lighting protocol: Target 5, OFF
data = bytes([
    0x02, 0x05, 0x00, 0x00,  # Word 1
    0x00, 0x00, 0x00, 0x00,  # Word 2
    0x00, 0x00, 0x00, 0x00   # Word 3
])
ser.write(data)
```

---

## Example 6: Rainbow Gradient (Target 11)

**Effect**: Target 11 shows rainbow gradient

**Pattern**: Gradient from red to blue, speed 2, brightness 180

### Hex Bytes (12 bytes):
```
02 0B 00 05  02 00 B4 00  00 FF 00 FF
```

### Breakdown:
```
Word 1: 0x020B0005
  - Type: 0x02 (Lighting)
  - Target: 0x0B (11, Target 11)
  - State: 0x00 (IDLE)
  - Pattern: 0x05 (GRADIENT)

Word 2: 0x0200B400
  - Speed: 0x02 (2, slow gradient)
  - Param1: 0x00 (not used)
  - Brightness: 0xB4 (180 = 70%)
  - Reserved: 0x00

Word 3: 0x00FF00FF
  - White: 0x00 (0)
  - Red: 0xFF (255, start color)
  - Green: 0x00 (0)
  - Blue: 0xFF (255, end color creates red→blue gradient)
```

### Python Code:
```python
# Lighting protocol: Target 11, GRADIENT red to blue
data = bytes([
    0x02, 0x0B, 0x00, 0x05,  # Word 1
    0x02, 0x00, 0xB4, 0x00,  # Word 2
    0x00, 0xFF, 0x00, 0xFF   # Word 3
])
ser.write(data)
```

---

## Complete Python Script: Lighting Sequence Demo

```python
import serial
import time

# Open serial port
ser = serial.Serial('COM3', 115200, timeout=1)
time.sleep(2)  # Wait for Arduino to reset

def send_lighting_pattern(target_id, state_idx, pattern_type, speed, param1, brightness, w, r, g, b):
    """Send a lighting protocol command (3 words = 12 bytes)"""
    data = bytes([
        0x02, target_id, state_idx, pattern_type,  # Word 1
        speed, param1, brightness, 0x00,           # Word 2
        w, r, g, b                                 # Word 3
    ])
    ser.write(data)
    print(f"Sent: Target {target_id}, State {state_idx}, Pattern {pattern_type}")
    time.sleep(0.1)  # Small delay between commands

# Demo sequence
print("LED Lighting Sequence Demo")

# 1. Set all targets to idle (cyan solid)
print("\n1. All targets IDLE - cyan solid")
send_lighting_pattern(
    target_id=13,      # 13 = ALL
    state_idx=0,       # IDLE
    pattern_type=1,    # SOLID
    speed=0,
    param1=0,
    brightness=64,
    w=0, r=0, g=64, b=128
)
time.sleep(3)

# 2. Target 0 starts moving (green chase)
print("\n2. Target 0 MOVING - green chase")
send_lighting_pattern(
    target_id=0,
    state_idx=1,       # MOVING
    pattern_type=3,    # CHASE
    speed=8,
    param1=20,         # Tail length
    brightness=128,
    w=0, r=0, g=255, b=0
)
time.sleep(3)

# 3. Target 0 gets hit (red strobe)
print("\n3. Target 0 HIT - red strobe")
send_lighting_pattern(
    target_id=0,
    state_idx=2,       # HIT
    pattern_type=4,    # STROBE
    speed=5,           # 5 Hz
    param1=0,
    brightness=255,
    w=0, r=255, g=0, b=0
)
time.sleep(3)

# 4. Target 0 back to idle
print("\n4. Target 0 back to IDLE")
send_lighting_pattern(
    target_id=0,
    state_idx=0,       # IDLE
    pattern_type=1,    # SOLID
    speed=0,
    param1=0,
    brightness=64,
    w=0, r=0, g=64, b=128
)
time.sleep(2)

# 5. Multiple targets moving
print("\n5. Targets 2, 5, 8 MOVING")
for target in [2, 5, 8]:
    send_lighting_pattern(
        target_id=target,
        state_idx=1,       # MOVING
        pattern_type=3,    # CHASE
        speed=8,
        param1=20,
        brightness=128,
        w=0, r=0, g=255, b=0
    )
time.sleep(3)

# 6. All targets white pulse
print("\n6. All targets white pulse (breathing)")
send_lighting_pattern(
    target_id=13,      # ALL
    state_idx=0,
    pattern_type=2,    # PULSE
    speed=4,
    param1=0,
    brightness=200,
    w=255, r=0, g=0, b=0
)
time.sleep(5)

# 7. Turn off all LEDs
print("\n7. All LEDs OFF")
send_lighting_pattern(
    target_id=13,      # ALL
    state_idx=0,
    pattern_type=0,    # OFF
    speed=0,
    param1=0,
    brightness=0,
    w=0, r=0, g=0, b=0
)

print("\nDemo complete!")
ser.close()
```

---

## General Protocol (Game Control)

Control game mode and timing:

### Start Active Game (60 second round)
```
00 01 00 3C  (Type=0x00, Mode=0x01 ACTIVE, Reserved=0x00, Duration=60)
```

### Return to Idle
```
00 00 00 00  (Type=0x00, Mode=0x00 IDLE, Reserved=0x00, Duration=0)
```

---

## Target Protocol (Servo/Sensor Config)

Configure individual target servos:

### Set Target 3 to position 90°, speed 50
```
01 03 5A 32  (Type=0x01, TargetID=3, Position=90, Speed=50)
```

---

## Tips for Testing

1. **Use a serial monitor** to verify commands are being received
2. **Send multiple lighting commands** to see smooth transitions
3. **Test pattern parameters** to find your preferred speeds/brightness
4. **Combine with game state** - send GENERAL protocol to start game, then LIGHTING to animate targets
5. **Check return protocol** - Arduino echoes back status via RETURN protocol (Type 0x06)

---

*For full protocol specification, see `.instructions.md`*
