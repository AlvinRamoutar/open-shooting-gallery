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

*Document updated: 2026-08-02*
