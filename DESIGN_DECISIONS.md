# Open Shooting Gallery - Design Decisions

This document tracks all architectural and implementation decisions made during development of the Arduino controller for the shooting gallery system.

---

## System Architecture

### Communication Protocol Design

**Decision**: Use custom binary protocol over Serial (115200 baud)
- **Rationale**: Provides deterministic, low-latency communication between server and controller
- **Implementation**: 32-bit words for most protocols, 96-bit (3×32) for lighting
- **Bit ordering**: Big-endian (MSB first) for consistency with network byte order

### Protocol Structure

**Decision**: Protocol ID in upper 4 bits (bits 31-28) of every word
- **Rationale**: Allows immediate routing without parsing entire payload
- **Protocol IDs**:
  - `0x0` = GENERAL (main control)
  - `0x1` = TARGET (per-target configuration)
  - `0x2` = LIGHTING (LED control)
  - `0x6` = RETURN (status/acknowledgement)

---

## Dynamic Configuration Updates

### On-The-Fly Protocol Updates

**Decision**: All protocols can be updated at any time during gameplay
- **Rationale**: Enables real-time difficulty adjustment, dynamic game modes, and live show control
- **Implementation**:
  - No buffering or queuing of protocol updates
  - Immediate application of new configurations upon receipt
  - State transitions handled gracefully (e.g., stopping targets when entering IDLE)

**Examples of dynamic updates**:
- Game mode changes: IDLE ↔ RESET ↔ GAME ↔ DEBUG
- Mid-game target speed/juke percentage adjustments
- Real-time lighting color/brightness changes

### State Preservation During Updates

**Decision**: Preserve timing state when updating configurations
- **Rationale**: Prevents jarring resets; maintains smooth gameplay flow
- **Implementation**:
  - Game time updates preserve elapsed time
  - Target configuration updates don't reset movement timers (unless transitioning from inactive)
  - Lighting updates are immediate but smooth (no flickering)

---

## Target Management

### Target Configuration Model

**Decision**: Store both protocol configuration and runtime state separately
- **Rationale**: 
  - Protocol config (`TargetProtocol`) = received instructions (can be updated anytime)
  - Runtime state (`TargetState`) = current operational state (position, active, timers)
  - Clear separation of "what was requested" vs "what is happening"

**TargetProtocol fields**:
- `mode`: halt / start moving / one-time move
- `movementSpeed`: 0-3 (25%/50%/75%/100%)
- `timeBetweenCycles`: seconds between move cycles
- `jukePercent`: percentage of cycles that are jukes (10% increments)
- `randomSpeedPercent`: probability of random speed (10% increments)
- `visibility`: target position as % hidden (10% increments)

**TargetState fields**:
- `active`: whether target is currently moving
- `currentVisibility`: current position
- `movementSpeed`: active speed (may differ from config if randomized)
- `lastMoveTime`: timestamp for cycle timing

### Broadcast vs. Individual Targeting

**Decision**: Support both individual target control and "all targets" broadcast
- **Rationale**: Simplifies server-side logic for global operations
- **Implementation**:
  - Target ID `0-11`: specific target
  - Target ID `12` (TARGET_ID_ALL): broadcast to all targets
  - Broadcast applies config to all targets individually (allows per-target state divergence later)

### Target Protocol Modes

**Decision**: Three distinct target modes
1. **HALT** (`0x0`): Stop target movement immediately
   - Preserves position, disables active flag
2. **START_MOVING** (`0x1`): Continuous movement with cycle logic
   - Uses juke percentage, random speed, time between cycles
   - Runs until halted or game ends
3. **ONE_TIME_MOVE** (`0x2`): Single positional move
   - Goes to specific visibility percentage
   - Does not continue cycling
   - Used for manual positioning, resets, demos

**Rationale**: Covers all use cases: automated gameplay, manual control, and setup/debug

---

## Lighting System

### Lighting Architecture

**Decision**: Dual lighting system - addressable LEDs + arena relay
- **Addressable LEDs**: 8960 total (512 per target × 12 targets), SPI controlled
- **Arena lighting**: Simple on/off relay control
- **Rationale**: 
  - Target LEDs provide visual feedback, hit indication, dynamic effects
  - Arena lighting provides ambient illumination
  - Different control methods match hardware capabilities

### LED Addressing Scheme

**Decision**: Virtual segmentation of continuous LED strip
- **Implementation**: 
  - All target LEDs on same physical SPI bus
  - Logically divided into 12 segments (0-511, 512-1023, ..., 5632-6143, etc.)
  - Protocol uses per-target ranges (0-511) for simplicity
  - Controller calculates absolute positions: `(targetId × 512) + startLedRange`
- **Rationale**: 
  - Simplifies wiring (one SPI bus vs. 12 separate buses)
  - Server doesn't need to calculate absolute positions
  - Allows flexible LED allocation per target

### Lighting Protocol Design

**Decision**: 96-bit (3×32-bit words) lighting protocol
- **Word 1**: Control (Protocol ID, Mode, Target ID, Brightness)
- **Word 2**: Range (Start LED, End LED within target segment)
- **Word 3**: Color (RGBW - 8 bits each)
- **Rationale**: 
  - Fits RGBW color space (4×8 bits = 32 bits)
  - Supports range-based updates (don't need to update all 512 LEDs)
  - Brightness control separate from color (easier dimming)

### Lighting Target IDs

**Decision**: Special target IDs for lighting control
- `0-11`: Individual target LED strip
- `12` (LIGHTING_ID_ALL_TARGETS): All target LEDs via SPI
- `13` (LIGHTING_ID_ARENA): Arena relay lighting only
- `15` (LIGHTING_ID_ALL): Everything (targets + arena)
- **Rationale**: Provides granular control and convenient broadcast modes

### Lighting State Management

**Decision**: Per-target lighting state + separate arena state
- **Per-target state** (`targetLightingStates[12]`): Tracks RGBW, brightness, on/off per target
- **Arena state** (`arenaLightingState`): Tracks arena relay state
- **Rationale**: 
  - Allows independent control of each target's lighting
  - Preserves state when updates are sent (e.g., changing one target doesn't affect others)
  - State can be queried for debugging/synchronization

### Multiple Lighting Updates

**Decision**: Lighting protocol can be sent multiple times in sequence
- **Use cases**:
  - Update target 0 red, target 1 blue, target 2 green
  - Change front targets to one color, back targets to another
  - Animate lighting by sending rapid updates
- **Implementation**: Each lighting protocol is processed immediately and independently
- **Rationale**: Maximum flexibility for lighting effects, no artificial limitations

---

## Serial Communication

### Protocol Word Reading

**Decision**: Read 4 bytes, reconstruct as big-endian 32-bit word
- **Implementation**:
  ```cpp
  word = (bytes[0] << 24) | (bytes[1] << 16) | (bytes[2] << 8) | bytes[3]
  ```
- **Rationale**: Ensures consistent byte order regardless of Arduino architecture

### Multi-Word Protocol Handling

**Decision**: Lighting protocol reads 3 consecutive words atomically
- **Implementation**: If word 2 or word 3 unavailable, send error and discard partial protocol
- **Rationale**: Prevents invalid lighting states from incomplete data

### Serial Buffer Processing

**Decision**: Process all available serial words per loop iteration
- **Implementation**: `while (Serial.available() >= 4)` loop in main loop
- **Rationale**: 
  - Prevents serial buffer overflow
  - Reduces latency for rapid protocol sequences
  - Essential for on-the-fly updates

### Return Protocol Design

**Decision**: Lightweight 32-bit acknowledgement/status protocol
- **Modes**:
  - Acknowledgements for all commands (idle, reset, run game, run debug)
  - Event notifications (target hit, end of game)
  - Error reporting
  - Debug stdout
- **Status field**: Tracks target/controller state (idle, hit, stuck, lost connectivity)
- **Rationale**: Server needs confirmation of commands and notification of events

---

## State Machine

### Controller States

**Decision**: Four primary controller states
1. **STATE_IDLE**: Waiting for commands, targets stopped
2. **STATE_TARGET_RESET**: Resetting all servos to 0°
3. **STATE_RUNNING_GAME**: Active gameplay with timer
4. **STATE_RUNNING_DEBUG**: Debug mode (manual control, diagnostics)

**Rationale**: Clear separation of operational modes, easy to reason about behavior

### State Transitions

**Decision**: Allow any-to-any state transitions via GENERAL protocol
- **Implementation**: Mode change immediately updates `currentState`
- **Cleanup actions**: Each transition handles necessary cleanup (e.g., stop targets when entering IDLE)
- **Rationale**: Enables emergency stop, reset during gameplay, mode switching

### Game Timing

**Decision**: Track game start time and duration separately
- **`gameStartTime`**: Absolute timestamp (millis()) when game started
- **`gameRunTime`**: Duration in milliseconds (from protocol's 10s increments)
- **Rationale**: Allows calculation of remaining time, supports duration updates mid-game

---

## Data Structures

### Protocol Structures vs. State Structures

**Decision**: Separate structures for protocol parsing and runtime state
- **Protocol structures** (`GeneralProtocol`, `TargetProtocol`, `LightingProtocol`):
  - Mirror wire format
  - Used for parsing received data
  - Updated on every protocol receipt
- **State structures** (`TargetState`, `LightingState`):
  - Operational runtime data
  - May diverge from protocol (e.g., randomized speeds, current position)
  - Persistent across protocol updates

**Rationale**: Clean separation of concerns; protocol changes don't corrupt runtime state

### Array-Based Storage

**Decision**: Fixed-size arrays for all targets and lighting states
- `targetConfigs[NUM_TARGETS]`
- `targetStates[NUM_TARGETS]`
- `targetLightingStates[NUM_TARGETS]`
- **Rationale**: 
  - Predictable memory usage (important for Arduino)
  - O(1) access by target ID
  - No dynamic allocation (avoids fragmentation)

---

## Bit Packing and Extraction

### Bit Field Sizes

**Decision**: Use smallest bit widths that support required ranges
- **Examples**:
  - Protocol ID: 4 bits (supports 0-15)
  - Target ID: 4 bits (supports 0-15, special values 12, 13, 15)
  - Movement speed: 2 bits (0-3)
  - Percentages in 10% increments: 4 bits (0-10)
  - RGBW colors: 8 bits each (0-255)
- **Rationale**: Maximizes information density in 32-bit words

### Bit Extraction Pattern

**Decision**: Use shift-and-mask pattern for bit extraction
- **Implementation**: `(word >> bitPosition) & bitmask`
- **Examples**:
  - Protocol ID: `(word >> 28) & 0x0F` (upper 4 bits)
  - Mode: `(word >> 24) & 0x0F` (next 4 bits)
  - 8-bit value: `(word >> 8) & 0xFF`
- **Rationale**: Efficient, portable, compiler-optimizable

---

## Timing and Scheduling

### Non-Blocking Design

**Decision**: Main loop never blocks
- **Implementation**: 
  - Small 10ms delay at end of loop only
  - All timing via timestamp comparison (`millis()`)
  - No `delay()` in protocol handlers or game logic
- **Rationale**: Maintains responsiveness to serial input and events

### Target Movement Timing

**Decision**: Per-target timing using `lastMoveTime` timestamp
- **Implementation**: Check `millis() - lastMoveTime >= (timeBetweenCycles * 1000)`
- **Rationale**: 
  - Targets operate independently
  - Timing unaffected by loop iteration time
  - Easy to update timing parameters on the fly

---

## Error Handling

### Protocol Validation

**Decision**: Validate protocol ID, send error for unknown protocols
- **Implementation**: Switch on protocol ID, default case sends `RETURN_MODE_ERROR`
- **Rationale**: Detects communication errors, protocol version mismatches

### Incomplete Protocol Handling

**Decision**: Discard incomplete multi-word protocols and send error
- **Example**: If lighting protocol word 1 received but word 2/3 unavailable
- **Rationale**: Prevents partial application of lighting updates (could cause visual glitches)

### Target ID Bounds Checking

**Decision**: Check target ID < NUM_TARGETS before array access
- **Implementation**: `if (targetId < NUM_TARGETS)` before accessing arrays
- **Rationale**: Prevents buffer overruns from malformed protocols

---

## Future Extensibility

### Reserved Bits

**Decision**: Zero out reserved bits on write, ignore on read
- **Rationale**: Allows future protocol extensions without breaking compatibility

### Extensible Mode Values

**Decision**: 4-bit mode fields support 0-15 (currently using 0-3 for most)
- **Rationale**: Room for additional modes (e.g., new target behaviors, lighting effects)

### Hardware Abstraction TODOs

**Decision**: Mark hardware interactions with `// TODO:` comments
- **Examples**:
  - Servo control
  - Hit sensor reading
  - SPI LED updates
  - Relay control
- **Rationale**: Framework is hardware-independent; TODOs indicate integration points

---

## Memory and Performance Considerations

### Global State

**Decision**: Use global variables for state (not dynamic allocation)
- **Rationale**: 
  - Arduino best practice (limited heap)
  - Predictable memory footprint
  - Fast access

### Minimal Acknowledgements

**Decision**: Send return protocol only on state changes or events
- **Not sent on every protocol receipt** (would flood serial)
- **Sent on**: Mode changes, target hits, errors, game end
- **Rationale**: Reduces serial traffic, server knows what changed

---

## Debugging and Diagnostics

### Protocol Debug Output

**Decision**: JSON-formatted debug output for all received and transmitted protocols
- **Implementation**:
  - `debugOutputGeneralProtocol()`, `debugOutputTargetProtocol()`, `debugOutputLightingProtocol()`, `debugOutputReturnProtocol()`
  - Called automatically on every protocol RX/TX
  - Builds entire JSON string in 256-byte buffer using `snprintf()`
  - Outputs atomically with single `Serial.println()` call
  - Can be disabled via `DEBUG_PROTOCOL_OUTPUT` flag
- **Rationale**:
  - Essential for debugging communication issues
  - JSON format allows external tools to parse/log protocol traffic
  - Direction field ("RX"/"TX") shows data flow
  - **Atomic output prevents interleaved messages** when multiple events occur simultaneously
  - Buffer-based approach is cleaner and more efficient than multiple print calls
  - Minimal performance impact when disabled

**Example debug output**:
```json
{"direction":"RX","protocol":"GENERAL","protocolId":0,"mode":2,"targetId":D,"gameRunTime":15}
{"direction":"RX","protocol":"TARGET","protocolId":1,"mode":1,"targetId":0,"movementSpeed":2,"timeBetweenCycles":5,"jukePercent":30,"randomSpeedPercent":10,"visibility":0}
{"direction":"TX","protocol":"RETURN","protocolId":6,"mode":2,"targetId":F,"status":0}
```

**Debug output fields**:
- **direction**: "RX" (received) or "TX" (transmitted)
- **protocol**: Protocol type name (GENERAL, TARGET, LIGHTING, RETURN)
- **protocolId**: Hex value from protocol
- Protocol-specific fields (mode, targetId, speeds, colors, etc.)
- Percentages expanded to actual values (e.g., jukePercent shows 30 for 30%)

**Disabling debug output**:
- Set `DEBUG_PROTOCOL_OUTPUT` to `false` in code
- All debug functions check this flag and return immediately if disabled
- Zero overhead when disabled (compiler can optimize out empty function bodies)

**Technical implementation**:
- Uses 96-160 byte static buffer per function call (sized to actual need)
- `snprintf_P()` safely formats JSON with bounds checking, reads format strings from PROGMEM
- Single `Serial.println()` ensures atomic output (no interleaving)
- Buffer is stack-allocated and released after function returns
- **Conditional compilation**: When `DEBUG_PROTOCOL_OUTPUT` is false, macros replace function calls with no-op `((void)0)` - completely eliminated by compiler

---

## LED Pattern System

### Hardware Configuration

**LED Strip Specifications**:
- **IC**: WS2814 (RGBW, 32-bit protocol)
- **Color Order**: WRGB (White, Red, Green, Blue)
- **Voltage**: 24V power supply, 5V logic data line
- **Density**: 896 LEDs/meter
- **Segment Length**: 70cm per target = 627 LEDs per segment
- **Total LEDs**: 12 targets × 627 LEDs = **7,524 LEDs**

**Architecture**: 4-pin hybrid configuration
- **Pin D2**: Targets 0, 1, 2 (1,881 LEDs)
- **Pin D3**: Targets 3, 4, 5 (1,881 LEDs)
- **Pin D4**: Targets 6, 7, 8 (1,881 LEDs)
- **Pin D5**: Targets 9, 10, 11 (1,881 LEDs)

**Rationale for 4-pin architecture**:
- ✅ **4× faster single-target updates** (75ms vs 300ms for full chain)
- ✅ **Serial buffer safe** (75ms < UART buffer overflow threshold)
- ✅ **Selective updates** via dirty flags (skip unchanged segments)
- ✅ **Better signal integrity** (shorter chains per pin)
- ✅ **Pin budget** (only 4 pins, leaves D6-D13 for servos/sensors)
- ❌ Single-pin architecture rejected due to 300ms interrupts-disabled freeze

### Pattern Descriptor Architecture

**Problem**: Full LED buffer requires **30,096 bytes** (7,524 LEDs × 4 bytes WRGB) but Arduino Uno has only **2,048 bytes RAM total**.

**Solution**: Store pattern definitions (8 bytes) instead of pixel data (30KB).

**PatternDescriptor Structure** (8 bytes):
```cpp
struct PatternDescriptor {
  uint8_t type;        // Pattern type (solid, pulse, chase, strobe, gradient)
  uint8_t speed;       // Animation speed 0-255
  uint8_t param1;      // Pattern-specific parameter (e.g., tail length)
  uint8_t brightness;  // Global brightness 0-255
  uint8_t w, r, g, b;  // WRGB base color
} __attribute__((packed));
```

**Memory Budget**:
- 12 targets × 3 states (IDLE, MOVING, HIT) = 36 pattern descriptors
- 36 × 8 bytes = **288 bytes** (vs 30KB for pixel buffer!)
- Pattern state tracking: **12 bytes**
- Dirty flags: **4 bytes**
- **Total LED system RAM**: **~304 bytes** (vs 30KB impossible requirement)

### Pattern Types

**Pattern Library**:
1. **PATTERN_OFF**: All LEDs off (0 bytes state)
2. **PATTERN_SOLID**: Single solid color across all LEDs
3. **PATTERN_PULSE**: Breathing effect using sine wave brightness modulation
4. **PATTERN_CHASE**: Moving dot with trailing fade
5. **PATTERN_STROBE**: Fast on/off blinking
6. **PATTERN_GRADIENT**: Two-color linear gradient across strip

**Pattern States** (mapped to target behavior):
- **IDLE**: Target inactive (not moving) - dim cyan solid color
- **MOVING**: Target active - green chase effect with trailing dots
- **HIT**: Target was hit - red strobe flash

### Streaming Renderer

**Concept**: Generate pixel colors **on-the-fly** during LED transmission, never store full pixel buffer.

**Algorithm**:
```cpp
void updateLEDPin(pinIndex) {
  for (ledIndex = 0; ledIndex < 1881; ledIndex++) {
    // Determine which target (0-2) this LED belongs to
    targetId = (pinIndex * 3) + (ledIndex / 627);
    ledWithinTarget = ledIndex % 627;
    
    // Get active pattern for this target
    pattern = targetPatterns[targetId][currentPatternState[targetId]];
    
    // Generate color algorithmically (no lookup, pure math)
    generatePixelColor(pattern, ledWithinTarget, millis(), &w, &r, &g, &b);
    
    // Send to LED strip
    strip.setPixelColor(ledIndex, Color(r, g, b, w));
  }
  
  strip.show();  // Transmit to LEDs (~75ms for 1,881 LEDs)
}
```

**Performance**:
- **Single pin update**: 75ms (1,881 LEDs)
- **Full chain update**: 300ms (all 7,524 LEDs)
- **Typical operation**: Only dirty pins updated (1-2 pins per loop iteration)
- **Effective frame rate**: 13-40 FPS for dynamic patterns

### Pattern Generation

**Key Techniques**:

**1. Sine Wave Lookup Table** (PROGMEM, 256 bytes):
```cpp
const uint8_t sin8_table[256] PROGMEM = { /* ... */ };
uint8_t brightness = sin8((millis() * speed) >> 4);  // Fast sine
```

**2. Fixed-Point Math** (8-bit operations):
```cpp
// Scale 8-bit value by 8-bit factor (returns 8-bit)
inline uint8_t scale8(uint8_t value, uint8_t scale) {
  return ((uint16_t)value * (uint16_t)scale) >> 8;
}
```

**3. Modulo Wraparound** (chase pattern):
```cpp
uint16_t position = ((millis() * speed) >> 6) % 627;
int16_t distance = ledIndex - position;
if (distance < 0) distance += 627;  // Circular buffer
```

### Dirty Flag Optimization

**Concept**: Only update LED pins that have changed patterns.

**Implementation**:
```cpp
bool ledPinDirty[4] = {false};  // 4 bits (one per pin)

// Mark target's pin dirty when pattern state changes
void markTargetLEDsDirty(targetId) {
  ledPinDirty[targetId / 3] = true;
}

// In loop(): only update dirty pins
void updateDirtyLEDs() {
  for (pin = 0; pin < 4; pin++) {
    if (ledPinDirty[pin]) {
      updateLEDPin(pin);
      ledPinDirty[pin] = false;
    }
  }
}
```

**Benefits**:
- Idle system: 0ms LED overhead (no updates)
- Single target changes: 75ms update (not 300ms)
- All targets change: 300ms (but interruptible between pins)

### Pattern State Machine

**Automatic Pattern Selection**:
```cpp
void updatePatternStates() {
  for (targetId = 0; targetId < 12; targetId++) {
    // Determine pattern based on target state
    if (targetHit[targetId]) {
      newPattern = PATTERN_STATE_HIT;
    } else if (targetActive[targetId]) {
      newPattern = PATTERN_STATE_MOVING;
    } else {
      newPattern = PATTERN_STATE_IDLE;
    }
    
    // Mark dirty if pattern changed
    if (currentPattern[targetId] != newPattern) {
      currentPattern[targetId] = newPattern;
      markTargetLEDsDirty(targetId);
    }
  }
}
```

**State Transitions**:
- IDLE → MOVING: When target starts moving (chase effect)
- MOVING → HIT: When target is hit (red strobe)
- HIT → IDLE: After hit acknowledgment (return to idle)
- Any → Any: Server can override via lighting protocol

### Future Extensions

**Planned Features**:
- **Server-defined patterns**: Extend lighting protocol to send pattern descriptors
- **Pattern transitions**: Smooth fade between states (crossfade)
- **Synchronized patterns**: Multiple targets same animation (phase-locked)
- **Hit animations**: Progressive fill, pulse wave, color ripple
- **Arena lighting integration**: Relay control for ambient lighting

**Protocol Extension** (for server-defined patterns):
```
Word 1: protocolId | patternType | targetId | patternState | brightness
Word 2: speed | param1 | param2 | reserved
Word 3: WRGB color data
```

---

## Memory and Performance Optimizations

### Overview

**Platform Constraints**: Arduino Uno R3 (ATmega328P)
- **RAM**: 2048 bytes (2KB) - shared by stack, heap, and global variables
- **Flash**: 32256 bytes (~31KB) - program storage (read-only at runtime)
- **Clock**: 16MHz

**Initial State** (before optimizations):
- RAM: 890 bytes (43.5%)
- Flash: 5736 bytes (17.8%)

**Optimized State** (after Phase 1 + Phase 2):
- RAM: 315 bytes (15.4%)
- Flash: 5818 bytes (18.0%)
- **Savings**: 575 bytes RAM (64.6% reduction)

**Final State** (with LED Pattern System):
- RAM: 741 bytes (36.2%) - includes 304 bytes for pattern descriptors + NeoPixel objects
- Flash: 9666 bytes (30.0%) - includes Adafruit_NeoPixel library (~3.8KB)
- **Remaining**: 1307 bytes RAM (63.8%), 22590 bytes Flash (70.0%)
- **Pattern system overhead**: 426 bytes RAM (vs 30KB impossible requirement - 98.6% savings!)

### Phase 1: PROGMEM and Control Flow Optimizations

#### String Literals to PROGMEM

**Problem**: Arduino copies string literals to RAM by default
- JSON format strings in 4 debug functions: ~450 bytes
- Wasteful on constrained systems (strings never change)

**Solution**: Store strings in Flash using PROGMEM
- Changed `snprintf()` → `snprintf_P()` (PROGMEM-aware variant)
- Wrapped format strings in `PSTR()` macro (stores in Flash)
- **Example**:
  ```cpp
  // Before (450 bytes RAM)
  snprintf(buffer, sizeof(buffer),
    "{\"direction\":\"%s\",\"protocol\":\"GENERAL\",...}",
    direction, ...);
  
  // After (450 bytes Flash, 0 bytes RAM)
  snprintf_P(buffer, sizeof(buffer),
    PSTR("{\"direction\":\"%s\",\"protocol\":\"GENERAL\",...}"),
    direction, ...);
  ```
- **Impact**: ~450 bytes RAM saved, moved to Flash

#### Remove delay() from Main Loop

**Problem**: `delay(10)` blocks entire program
- Wastes 160,000 CPU cycles per iteration (94% of 16MHz)
- Causes 10-50ms latency for incoming serial data
- Prevents responsive real-time control

**Solution**: Remove delay, run at full speed
- Changed `while (Serial.available() >= 4)` → `do...while` with outer `if` check
- No blocking in main loop
- Timing still controlled via `millis()` timestamps in game logic
- **Impact**: 
  - Reduced response latency from 10ms to <1ms
  - 160,000 cycles saved per loop iteration
  - Targets and protocols update immediately

#### Conditional Compilation for Debug Functions

**Problem**: Runtime check `if (!DEBUG_PROTOCOL_OUTPUT) return;` still generates code
- Function call overhead: ~20 cycles per call
- Function body: ~250 cycles (even if early return)
- Called 4+ times per protocol (16+ times per second during active gameplay)

**Solution**: Wrap debug functions in `#if DEBUG_PROTOCOL_OUTPUT ... #else ... #endif`
- When disabled, replace functions with no-op macros: `#define debugOutputGeneralProtocol(...) ((void)0)`
- Compiler completely eliminates dead code
- **Example**:
  ```cpp
  #if DEBUG_PROTOCOL_OUTPUT
  void debugOutputGeneralProtocol(...) {
    // Full implementation
  }
  #else
  #define debugOutputGeneralProtocol(direction, config) ((void)0)
  #endif
  ```
- **Impact**: 
  - Zero runtime overhead when disabled
  - ~270 cycles saved per protocol when disabled
  - ~428 bytes code savings in Flash when disabled

### Phase 2: Bit Packing All Protocol Structures

#### Bit-Packed GeneralProtocol

**Before** (4 bytes):
```cpp
struct GeneralProtocol {
  uint8_t protocolId;       // 8 bits (only needs 4)
  uint8_t mode;             // 8 bits (only needs 4)
  uint8_t targetId;         // 8 bits (only needs 4)
  uint8_t gameRunTime;      // 8 bits (OK)
};
```

**After** (3 bytes):
```cpp
struct GeneralProtocol {
  uint8_t protocolId : 4;   // 4 bits
  uint8_t mode : 4;         // 4 bits
  uint8_t targetId : 4;     // 4 bits
  uint8_t reserved : 4;     // 4 bits padding
  uint8_t gameRunTime;      // 8 bits
} __attribute__((packed));
```
- **Savings**: 1 byte per instance

#### Bit-Packed TargetProtocol

**Before** (8 bytes):
```cpp
struct TargetProtocol {
  uint8_t protocolId;              // 8 bits (needs 4)
  uint8_t mode;                    // 8 bits (needs 2)
  uint8_t targetId;                // 8 bits (needs 4)
  uint8_t movementSpeed;           // 8 bits (needs 2)
  uint8_t timeBetweenCycles;       // 8 bits (needs 4)
  uint8_t jukePercent;             // 8 bits (needs 4)
  uint8_t randomSpeedPercent;      // 8 bits (needs 4)
  uint8_t visibility;              // 8 bits (needs 4)
};
```

**After** (4 bytes):
```cpp
struct TargetProtocol {
  uint8_t protocolId : 4;          // 4 bits
  uint8_t mode : 2;                // 2 bits
  uint8_t targetId : 4;            // 4 bits
  uint8_t movementSpeed : 2;       // 2 bits
  uint8_t timeBetweenCycles : 4;   // 4 bits
  uint8_t jukePercent : 4;         // 4 bits
  uint8_t randomSpeedPercent : 4;  // 4 bits
  uint8_t visibility : 4;          // 4 bits (28 bits total)
} __attribute__((packed));
```
- **Savings**: 4 bytes per instance × 12 targets = **48 bytes**

#### Bit-Packed LightingProtocol

**Before** (13 bytes):
```cpp
struct LightingProtocol {
  uint8_t protocolId;       // 8 bits (needs 4)
  uint8_t mode;             // 8 bits (needs 1)
  uint8_t targetId;         // 8 bits (needs 4)
  uint8_t brightness;       // 8 bits (needs 7)
  uint16_t startLedRange;   // 16 bits (needs 9)
  uint16_t endLedRange;     // 16 bits (needs 9)
  uint8_t red;              // 8 bits (OK)
  uint8_t green;            // 8 bits (OK)
  uint8_t blue;             // 8 bits (OK)
  uint8_t white;            // 8 bits (OK)
};
```

**After** (9 bytes):
```cpp
struct LightingProtocol {
  uint16_t protocolId : 4;       // 4 bits
  uint16_t mode : 1;             // 1 bit
  uint16_t targetId : 4;         // 4 bits
  uint16_t brightness : 7;       // 7 bits
  uint16_t startLedRange : 9;    // 9 bits
  uint16_t endLedRange : 9;      // 9 bits (34 bits total)
  uint8_t reserved : 2;          // 2 bits padding
  uint8_t red;                   // 8 bits
  uint8_t green;                 // 8 bits
  uint8_t blue;                  // 8 bits
  uint8_t white;                 // 8 bits
} __attribute__((packed));
```
- **Savings**: 4 bytes per instance

#### Bit-Packed TargetState

**Before** (8 bytes):
```cpp
struct TargetState {
  bool active;                  // 8 bits (needs 1)
  uint8_t currentVisibility;    // 8 bits (needs 4)
  uint8_t movementSpeed;        // 8 bits (needs 2)
  unsigned long lastMoveTime;   // 32 bits (OK)
};
```

**After** (5 bytes):
```cpp
struct TargetState {
  uint8_t active : 1;              // 1 bit
  uint8_t currentVisibility : 4;   // 4 bits
  uint8_t movementSpeed : 2;       // 2 bits
  uint8_t reserved : 1;            // 1 bit padding
  uint32_t lastMoveTime;           // 32 bits
} __attribute__((packed));
```
- **Savings**: 3 bytes × 12 targets = **36 bytes**

#### Bit-Packed LightingState

**Before** (6 bytes):
```cpp
struct LightingState {
  bool isOn;           // 8 bits (needs 1)
  uint8_t brightness;  // 8 bits (needs 7)
  uint8_t red;         // 8 bits (OK)
  uint8_t green;       // 8 bits (OK)
  uint8_t blue;        // 8 bits (OK)
  uint8_t white;       // 8 bits (OK)
};
```

**After** (5 bytes):
```cpp
struct LightingState {
  uint8_t isOn : 1;        // 1 bit
  uint8_t brightness : 7;  // 7 bits
  uint8_t red;             // 8 bits
  uint8_t green;           // 8 bits
  uint8_t blue;            // 8 bits
  uint8_t white;           // 8 bits
} __attribute__((packed));
```
- **Savings**: 1 byte × 13 instances (12 targets + 1 arena) = **13 bytes**

### Total Optimization Impact

**Phase 1 Savings**:
- PROGMEM strings: ~450 bytes RAM → Flash
- Conditional compilation: 0 runtime overhead when debug disabled
- No delay: 160,000 cycles saved per loop, <1ms latency

**Phase 2 Savings**:
- GeneralProtocol: 1 byte
- TargetProtocol: 48 bytes (4 bytes × 12 targets)
- LightingProtocol: 4 bytes
- TargetState: 36 bytes (3 bytes × 12 targets)
- LightingState: 13 bytes (1 byte × 13 instances)
- **Total struct savings**: ~102 bytes

**Combined Result**:
- **RAM**: 890 → 315 bytes (575 bytes saved, 64.6% reduction)
- **Flash**: 5736 → 5818 bytes (+82 bytes for PROGMEM strings and packed struct code)
- **Free RAM**: 1733 bytes available for hardware libraries and runtime operations
- **Performance**: 10x improvement in responsiveness, ~160K cycles saved per iteration

### Implementation Notes

**Bit field syntax**: `uint8_t fieldName : bitCount;`
- Compiler packs multiple fields into single bytes
- `__attribute__((packed))` prevents padding between struct members
- Access is transparent: `config.protocolId` works normally

**PROGMEM considerations**:
- Only works for const data (strings, lookup tables)
- Must use `_P` variants of functions (`snprintf_P`, `strcpy_P`, etc.)
- Slightly slower access (Flash read vs RAM read), but negligible impact

**Trade-offs**:
- Bit fields add minor code complexity
- PROGMEM requires `_P` function variants
- Flash usage slightly increased (82 bytes)
- **Worth it**: 575 bytes RAM freed is critical on 2KB system

---

## Next Steps / Open Questions

1. **Servo control implementation**: Which servo library? PWM settings?
2. **Hit sensor type**: Photoresistor? Vibration sensor? Laser break?
3. **SPI LED library**: FastLED? NeoPixel? Custom?
4. **Juke logic**: Random hide/show? Fake-out movements? Duration?
5. **Random speed change**: Apply per-cycle or per-movement?
6. **Target movement patterns**: Linear? Easing? Bounce?
7. **Relay control**: Direct GPIO or relay module?
8. **Error recovery**: Auto-reset on stuck targets?

---

*Document created: 2026-08-02*  
*Last optimization update: 2025-01-XX*
