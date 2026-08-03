#include <Arduino.h>
#include <Adafruit_NeoPixel.h>

// ============================================================================
// SHOOTING GALLERY CONTROLLER
// ============================================================================
// This controller manages a 12-target shooting gallery system.
// 
// OPERATION:
// - Controller starts in IDLE state
// - Server sends protocol payloads over serial (115200 baud)
// - All protocols can be updated ON THE FLY during gameplay
// - New configurations immediately override previous settings
//
// PROTOCOL SEQUENCE:
// - GENERAL protocol (mandatory, sets mode and game time)
// - TARGET protocols (up to 12, one per target)
// - LIGHTING protocols (optional, for LED control)
//
// DYNAMIC UPDATES:
// - General mode can change at any time (IDLE ↔ RESET ↔ GAME ↔ DEBUG)
// - Target configs can be updated mid-game (speed, juke %, visibility, etc.)
// - Lighting can be changed on the fly
// ============================================================================

// ============================================================================
// PROTOCOL CONSTANTS
// ============================================================================

// Protocol IDs
#define PROTOCOL_GENERAL  0x0
#define PROTOCOL_TARGET   0x1
#define PROTOCOL_LIGHTING 0x2
#define PROTOCOL_RETURN   0x6

// General Protocol - Modes
#define GENERAL_MODE_IDLE         0x0
#define GENERAL_MODE_TARGET_RESET 0x1
#define GENERAL_MODE_RUN_GAME     0x2
#define GENERAL_MODE_RUN_DEBUG    0x3

// Target Protocol - Modes
#define TARGET_MODE_HALT          0x0
#define TARGET_MODE_START_MOVING  0x1
#define TARGET_MODE_ONE_TIME_MOVE 0x2

// Return Protocol - Modes
#define RETURN_MODE_IDLE               0x0
#define RETURN_MODE_ACK_TARGET_RESET   0x1
#define RETURN_MODE_ACK_RUN_GAME       0x2
#define RETURN_MODE_ACK_RUN_DEBUG      0x3
#define RETURN_MODE_TARGET_HIT         0x4
#define RETURN_MODE_END_OF_GAME        0x5
#define RETURN_MODE_STDOUT             0x9
#define RETURN_MODE_ERROR              0xA

// Return Protocol - Status
#define STATUS_IDLE                    0x0
#define STATUS_HIT                     0x1
#define STATUS_STUCK                   0x2
#define STATUS_LOST_CONNECTIVITY       0x3
#define STATUS_LOST_SERVER             0x4

// Lighting Protocol - Modes
#define LIGHTING_MODE_OFF 0x0
#define LIGHTING_MODE_ON  0x1

// Debug Configuration
#define DEBUG_PROTOCOL_OUTPUT true  // Set to false to disable protocol debug output

// Lighting Protocol - Target IDs
#define LIGHTING_ID_ALL_TARGETS 12  // All target LEDs via SPI
#define LIGHTING_ID_ARENA       13  // Arena lighting via relay
#define LIGHTING_ID_ALL         15  // ALL lighting (targets + arena)

// System Constants
#define NUM_TARGETS 12
#define TARGET_ID_ALL 13
#define TARGET_ID_CONTROLLER 15

// LED System Constants (WS2814 WRGB, 24V, 5V logic)
#define LEDS_PER_TARGET 627       // 70cm @ 896 LEDs/m = 627 LEDs
#define TOTAL_LEDS 7524           // 627 LEDs × 12 targets
#define TARGETS_PER_PIN 3         // 4-pin architecture: 3 targets per data pin
#define NUM_LED_PINS 4            // D2, D3, D4, D5
#define LEDS_PER_PIN 1881         // 627 × 3 = 1,881 LEDs per pin

// LED Pin Assignments (4-pin hybrid architecture)
#define LED_PIN_0 2               // Targets 0, 1, 2
#define LED_PIN_1 3               // Targets 3, 4, 5
#define LED_PIN_2 4               // Targets 6, 7, 8
#define LED_PIN_3 5               // Targets 9, 10, 11

// ============================================================================
// DATA STRUCTURES
// ============================================================================

struct GeneralProtocol {
  uint8_t protocolId : 4;   // 0-15 (4 bits)
  uint8_t mode : 4;         // 0-15 (4 bits)
  uint8_t targetId : 4;     // 0-15 (4 bits)
  uint8_t reserved : 4;     // Padding (4 bits)
  uint8_t gameRunTime;      // 0-255 in 10s increments (8 bits)
} __attribute__((packed));

struct TargetProtocol {
  uint8_t protocolId : 4;          // 0-15 (4 bits)
  uint8_t mode : 2;                // 0-2 (2 bits)
  uint8_t targetId : 4;            // 0-13 (4 bits)
  uint8_t movementSpeed : 2;       // 0-3 (2 bits)
  uint8_t timeBetweenCycles : 4;   // 0-15 seconds (4 bits)
  uint8_t jukePercent : 4;         // 0-10 in 10% increments (4 bits)
  uint8_t randomSpeedPercent : 4;  // 0-10 in 10% increments (4 bits)
  uint8_t visibility : 4;          // 0-10 % hidden in 10% increments (4 bits)
} __attribute__((packed));

struct TargetState {
  uint8_t active : 1;              // Boolean (1 bit)
  uint8_t currentVisibility : 4;   // 0-10 (4 bits)
  uint8_t movementSpeed : 2;       // 0-3 (2 bits)
  uint8_t reserved : 1;            // Padding (1 bit)
  uint32_t lastMoveTime;           // Milliseconds (32 bits)
} __attribute__((packed));

struct LightingProtocol {
  uint16_t protocolId : 4;       // 0-15 (4 bits)
  uint16_t mode : 1;             // 0-1 (1 bit)
  uint16_t targetId : 4;         // 0-15 (4 bits)
  uint16_t brightness : 7;       // 0-100 (7 bits)
  uint16_t startLedRange : 9;    // 0-511 (9 bits)
  uint16_t endLedRange : 9;      // 0-511 (9 bits)
  uint8_t reserved : 2;          // Padding (2 bits)
  uint8_t red;                   // 0-255 (8 bits)
  uint8_t green;                 // 0-255 (8 bits)
  uint8_t blue;                  // 0-255 (8 bits)
  uint8_t white;                 // 0-255 (8 bits)
} __attribute__((packed));

struct LightingState {
  uint8_t isOn : 1;        // Boolean (1 bit)
  uint8_t brightness : 7;  // 0-100 (7 bits)
  uint8_t red;             // 0-255 (8 bits)
  uint8_t green;           // 0-255 (8 bits)
  uint8_t blue;            // 0-255 (8 bits)
  uint8_t white;           // 0-255 (8 bits)
} __attribute__((packed));

// ============================================================================
// LED PATTERN SYSTEM
// ============================================================================
// Memory-efficient pattern descriptor architecture for WS2814 WRGB LEDs.
// Stores pattern definitions (8 bytes) instead of pixel data (30KB impossible).
// Generates colors algorithmically during transmission.
// ============================================================================

// Pattern Types
enum PatternType : uint8_t {
  PATTERN_OFF = 0,        // All LEDs off
  PATTERN_SOLID = 1,      // Single solid color
  PATTERN_PULSE = 2,      // Breathing effect (sine wave brightness)
  PATTERN_CHASE = 3,      // Moving dot with trail
  PATTERN_STROBE = 4,     // Fast blink
  PATTERN_GRADIENT = 5    // Two-color linear gradient
};

// Pattern State (maps to target behavior)
enum PatternState : uint8_t {
  PATTERN_STATE_IDLE = 0,    // Target idle (not moving)
  PATTERN_STATE_MOVING = 1,  // Target active/moving
  PATTERN_STATE_HIT = 2      // Target was hit
};

// Pattern Descriptor (8 bytes) - defines how pattern should look
struct PatternDescriptor {
  uint8_t type;           // PatternType (1 byte)
  uint8_t speed;          // Animation speed 0-255 (1 byte)
  uint8_t param1;         // Pattern-specific parameter (1 byte)
  uint8_t brightness;     // Global brightness 0-255 (1 byte)
  uint8_t w, r, g, b;     // WRGB color (4 bytes)
} __attribute__((packed));  // Total: 8 bytes

// ============================================================================
// GLOBAL STATE
// ============================================================================

enum ControllerState {
  STATE_IDLE,
  STATE_TARGET_RESET,
  STATE_RUNNING_GAME,
  STATE_RUNNING_DEBUG
};

ControllerState currentState = STATE_IDLE;
GeneralProtocol generalConfig;
TargetProtocol targetConfigs[NUM_TARGETS];
TargetState targetStates[NUM_TARGETS];
LightingState targetLightingStates[NUM_TARGETS];  // Per-target LED states
LightingState arenaLightingState;                  // Arena relay lighting state
unsigned long gameStartTime = 0;
unsigned long gameRunTime = 0;  // in milliseconds

// LED Pattern System State
PatternDescriptor targetPatterns[NUM_TARGETS][3];  // 12 targets × 3 states = 36 patterns (288 bytes)
PatternState currentPatternState[NUM_TARGETS];     // Current active pattern per target (12 bytes)
bool ledPinDirty[NUM_LED_PINS] = {false};          // Dirty flags for 4 pins (4 bytes)

// TEMPORARY: Test with small LED count due to RAM constraints
// Full system: 1881 LEDs × 4 bytes = 7.5KB per strip = 30KB total (Arduino has 2KB!)
// Need streaming approach or external controller for full 7524 LED system
#define TEST_LEDS_PER_PIN 10  // Just 10 LEDs for testing

// NeoPixel Strip Objects (4 strips, one per data pin)
// Protocol: NEO_WRGB + NEO_KHZ800 (verified with WS2814 LEDs)
Adafruit_NeoPixel ledStrips[NUM_LED_PINS] = {
  Adafruit_NeoPixel(TEST_LEDS_PER_PIN, LED_PIN_0, NEO_WRGB + NEO_KHZ800),  // Targets 0,1,2
  Adafruit_NeoPixel(TEST_LEDS_PER_PIN, LED_PIN_1, NEO_WRGB + NEO_KHZ800),  // Targets 3,4,5
  Adafruit_NeoPixel(TEST_LEDS_PER_PIN, LED_PIN_2, NEO_WRGB + NEO_KHZ800),  // Targets 6,7,8
  Adafruit_NeoPixel(TEST_LEDS_PER_PIN, LED_PIN_3, NEO_WRGB + NEO_KHZ800)   // Targets 9,10,11
};

// ============================================================================
// PROTOCOL PARSING
// ============================================================================

void parseGeneralProtocol(uint32_t word, GeneralProtocol &config) {
  config.protocolId = (word >> 28) & 0x0F;
  config.mode = (word >> 24) & 0x0F;
  config.targetId = (word >> 20) & 0x0F;
  config.gameRunTime = (word >> 8) & 0xFF;
}

void parseTargetProtocol(uint32_t word, TargetProtocol &config) {
  config.protocolId = (word >> 28) & 0x0F;
  config.mode = (word >> 24) & 0x0F;
  config.targetId = (word >> 20) & 0x0F;
  config.movementSpeed = (word >> 18) & 0x03;
  config.timeBetweenCycles = (word >> 12) & 0x0F;
  config.jukePercent = (word >> 8) & 0x0F;
  config.randomSpeedPercent = (word >> 4) & 0x0F;
  config.visibility = word & 0x0F;
}

void parseLightingProtocol(uint32_t word1, uint32_t word2, uint32_t word3, LightingProtocol &config) {
  // Word 1: Protocol ID, Mode, Target ID, Brightness
  config.protocolId = (word1 >> 28) & 0x0F;
  config.mode = (word1 >> 24) & 0x0F;
  config.targetId = (word1 >> 20) & 0x0F;
  config.brightness = (word1 >> 8) & 0xFF;
  
  // Word 2: Start LED Range, End LED Range
  config.startLedRange = (word2 >> 16) & 0xFFFF;
  config.endLedRange = word2 & 0xFFFF;
  
  // Word 3: RGBW colors
  config.red = (word3 >> 24) & 0xFF;
  config.green = (word3 >> 16) & 0xFF;
  config.blue = (word3 >> 8) & 0xFF;
  config.white = word3 & 0xFF;
}

// ============================================================================
// FORWARD DECLARATIONS
// ============================================================================

void handleGeneralProtocol();
void handleTargetProtocol(uint8_t targetId);
void handleLightingProtocol(LightingProtocol &config);
void updateTarget(uint8_t targetId);
void checkTargetHits();
void updateGameState();

// ============================================================================
// DEBUG OUTPUT (JSON FORMAT)
// ============================================================================
// These functions output received (RX) and transmitted (TX) protocols as JSON
// strings to Serial for debugging and monitoring. Each function:
// - Checks DEBUG_PROTOCOL_OUTPUT flag (zero overhead when disabled)
// - Builds entire JSON string in buffer first (prevents interleaved output)
// - Outputs atomically with single Serial.println()
// - Expands percentages to actual values (e.g., 3 -> 30%)
//
// Example output:
// {"direction":"RX","protocol":"TARGET","protocolId":1,"mode":1,"targetId":0,...}
// ============================================================================

#if DEBUG_PROTOCOL_OUTPUT

void debugOutputGeneralProtocol(const char* direction, GeneralProtocol &config) {
  char buffer[128];
  snprintf_P(buffer, sizeof(buffer),
    PSTR("{\"direction\":\"%s\",\"protocol\":\"GENERAL\",\"protocolId\":%X,\"mode\":%X,\"targetId\":%X,\"gameRunTime\":%u}"),
    direction, config.protocolId, config.mode, config.targetId, config.gameRunTime);
  Serial.println(buffer);
}

void debugOutputTargetProtocol(const char* direction, TargetProtocol &config) {
  char buffer[160];
  snprintf_P(buffer, sizeof(buffer),
    PSTR("{\"direction\":\"%s\",\"protocol\":\"TARGET\",\"protocolId\":%X,\"mode\":%X,\"targetId\":%u,\"movementSpeed\":%u,\"timeBetweenCycles\":%u,\"jukePercent\":%u,\"randomSpeedPercent\":%u,\"visibility\":%u}"),
    direction, config.protocolId, config.mode, config.targetId, 
    config.movementSpeed, config.timeBetweenCycles, 
    config.jukePercent * 10, config.randomSpeedPercent * 10, config.visibility * 10);
  Serial.println(buffer);
}

void debugOutputLightingProtocol(const char* direction, LightingProtocol &config) {
  char buffer[140];
  snprintf_P(buffer, sizeof(buffer),
    PSTR("{\"direction\":\"%s\",\"protocol\":\"LIGHTING\",\"protocolId\":%X,\"mode\":%X,\"targetId\":%u,\"brightness\":%u,\"ledRange\":[%u,%u],\"rgbw\":[%u,%u,%u,%u]}"),
    direction, config.protocolId, config.mode, config.targetId, config.brightness,
    config.startLedRange, config.endLedRange,
    config.red, config.green, config.blue, config.white);
  Serial.println(buffer);
}

void debugOutputReturnProtocol(const char* direction, uint8_t mode, uint8_t targetId, uint8_t status) {
  char buffer[96];
  snprintf_P(buffer, sizeof(buffer),
    PSTR("{\"direction\":\"%s\",\"protocol\":\"RETURN\",\"protocolId\":%X,\"mode\":%X,\"targetId\":%u,\"status\":%X}"),
    direction, PROTOCOL_RETURN, mode, targetId, status);
  Serial.println(buffer);
}

#else

// When debug is disabled, macros compile to nothing (zero overhead)
#define debugOutputGeneralProtocol(direction, config) ((void)0)
#define debugOutputTargetProtocol(direction, config) ((void)0)
#define debugOutputLightingProtocol(direction, config) ((void)0)
#define debugOutputReturnProtocol(direction, mode, targetId, status) ((void)0)

#endif

// ============================================================================
// PROTOCOL TRANSMISSION
// ============================================================================

uint32_t buildReturnWord(uint8_t mode, uint8_t targetId, uint8_t status) {
  uint32_t word = 0;
  word |= (uint32_t)PROTOCOL_RETURN << 28;
  word |= (uint32_t)mode << 24;
  word |= (uint32_t)targetId << 20;
  word |= (uint32_t)status << 18;
  return word;
}

void sendReturnProtocol(uint8_t mode, uint8_t targetId, uint8_t status) {
  uint32_t word = buildReturnWord(mode, targetId, status);
  
  // Debug output before sending
  debugOutputReturnProtocol("TX", mode, targetId, status);
  
  Serial.write((uint8_t*)&word, sizeof(word));
}

// ============================================================================
// SERIAL COMMUNICATION
// ============================================================================

bool readSerialWord(uint32_t &word) {
  if (Serial.available() >= 4) {
    uint8_t bytes[4];
    Serial.readBytes(bytes, 4);
    
    // Reconstruct 32-bit word (big-endian)
    word = ((uint32_t)bytes[0] << 24) |
           ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) |
           ((uint32_t)bytes[3]);
    return true;
  }
  return false;
}

void processSerialData() {
  uint32_t word;
  
  // Process all available protocol words
  // NOTE: All protocols can be updated on the fly - new configurations
  // override previous settings immediately, even during active gameplay
  
  if (readSerialWord(word)) {
    uint8_t protocolId = (word >> 28) & 0x0F;
    
    switch (protocolId) {
      case PROTOCOL_GENERAL:
        parseGeneralProtocol(word, generalConfig);
        debugOutputGeneralProtocol("RX", generalConfig);  // Debug output
        handleGeneralProtocol();  // Immediately update state
        break;
        
      case PROTOCOL_TARGET:
        {
          TargetProtocol config;
          parseTargetProtocol(word, config);
          debugOutputTargetProtocol("RX", config);  // Debug output
          
          if (config.targetId < NUM_TARGETS) {
            targetConfigs[config.targetId] = config;
            handleTargetProtocol(config.targetId);  // Immediately apply
          } else if (config.targetId == TARGET_ID_ALL) {
            // Apply to all targets immediately
            for (int i = 0; i < NUM_TARGETS; i++) {
              TargetProtocol targetConfig = config;
              targetConfig.targetId = i;
              targetConfigs[i] = targetConfig;
              handleTargetProtocol(i);
            }
          }
        }
        break;
        
      case PROTOCOL_LIGHTING:
        {
          // Lighting protocol uses 3 words (96 bits total)
          // Read the next 2 words to complete the lighting protocol
          uint32_t word2, word3;
          if (readSerialWord(word2) && readSerialWord(word3)) {
            LightingProtocol config;
            parseLightingProtocol(word, word2, word3, config);
            debugOutputLightingProtocol("RX", config);  // Debug output
            handleLightingProtocol(config);  // Immediately apply
          } else {
            // Incomplete lighting protocol received
            sendReturnProtocol(RETURN_MODE_ERROR, TARGET_ID_CONTROLLER, STATUS_IDLE);
          }
        }
        break;
        
      default:
        // Unknown protocol
        sendReturnProtocol(RETURN_MODE_ERROR, TARGET_ID_CONTROLLER, STATUS_IDLE);
        break;
    }
  }
}

// ============================================================================
// PROTOCOL HANDLERS
// ============================================================================

void handleGeneralProtocol() {
  ControllerState previousState = currentState;
  
  switch (generalConfig.mode) {
    case GENERAL_MODE_IDLE:
      currentState = STATE_IDLE;
      // Stop all active targets when entering idle
      if (previousState != STATE_IDLE) {
        for (int i = 0; i < NUM_TARGETS; i++) {
          targetStates[i].active = false;
        }
      }
      sendReturnProtocol(RETURN_MODE_IDLE, TARGET_ID_CONTROLLER, STATUS_IDLE);
      break;
      
    case GENERAL_MODE_TARGET_RESET:
      currentState = STATE_TARGET_RESET;
      // TODO: Reset all servos to 0°
      for (int i = 0; i < NUM_TARGETS; i++) {
        targetStates[i].active = false;
        targetStates[i].currentVisibility = 0;
      }
      sendReturnProtocol(RETURN_MODE_ACK_TARGET_RESET, TARGET_ID_CONTROLLER, STATUS_IDLE);
      break;
      
    case GENERAL_MODE_RUN_GAME:
      // Allow runtime updates
      if (previousState != STATE_RUNNING_GAME) {
        // First time entering game mode - start timer
        gameStartTime = millis();
      } else if (generalConfig.gameRunTime != (gameRunTime / 10000UL)) {
        // Game time was updated on the fly - adjust
        unsigned long elapsed = millis() - gameStartTime;
        gameStartTime = millis() - elapsed;  // Preserve elapsed time
      }
      currentState = STATE_RUNNING_GAME;
      gameRunTime = generalConfig.gameRunTime * 10000UL;  // Convert to milliseconds
      sendReturnProtocol(RETURN_MODE_ACK_RUN_GAME, TARGET_ID_CONTROLLER, STATUS_IDLE);
      break;
      
    case GENERAL_MODE_RUN_DEBUG:
      currentState = STATE_RUNNING_DEBUG;
      sendReturnProtocol(RETURN_MODE_ACK_RUN_DEBUG, TARGET_ID_CONTROLLER, STATUS_IDLE);
      break;
  }
}

void handleTargetProtocol(uint8_t targetId) {
  TargetProtocol &config = targetConfigs[targetId];
  TargetState &state = targetStates[targetId];
  
  switch (config.mode) {
    case TARGET_MODE_HALT:
      state.active = false;
      // TODO: Stop servo movement
      break;
      
    case TARGET_MODE_START_MOVING:
      // Update configuration even if already active
      state.movementSpeed = config.movementSpeed;
      
      // If transitioning from inactive to active, reset timer
      if (!state.active) {
        state.lastMoveTime = millis();
      }
      
      state.active = true;
      // TODO: Apply updated movement configuration
      break;
      
    case TARGET_MODE_ONE_TIME_MOVE:
      // Immediately move to specified visibility position
      state.currentVisibility = config.visibility;
      state.active = false;  // One-time move doesn't continue
      // TODO: Move servo to target position (config.visibility * 10% hidden)
      break;
  }
}

void handleLightingProtocol(LightingProtocol &config) {
  // Lighting can be updated on the fly at any time
  
  if (config.targetId < NUM_TARGETS) {
    // Individual target LED control
    LightingState &state = targetLightingStates[config.targetId];
    state.isOn = (config.mode == LIGHTING_MODE_ON);
    state.brightness = config.brightness;
    state.red = config.red;
    state.green = config.green;
    state.blue = config.blue;
    state.white = config.white;
    
    // TODO: Update LED strip for this target
    // - Calculate absolute LED positions: (targetId * LEDS_PER_TARGET) + startLedRange
    // - Apply RGBW color to range [startLedRange, endLedRange]
    // - Apply brightness scaling
    // - Update SPI LED controller
    
  } else if (config.targetId == LIGHTING_ID_ALL_TARGETS) {
    // All target LEDs via SPI
    for (int i = 0; i < NUM_TARGETS; i++) {
      LightingState &state = targetLightingStates[i];
      state.isOn = (config.mode == LIGHTING_MODE_ON);
      state.brightness = config.brightness;
      state.red = config.red;
      state.green = config.green;
      state.blue = config.blue;
      state.white = config.white;
    }
    // TODO: Update all target LED strips
    
  } else if (config.targetId == LIGHTING_ID_ARENA) {
    // Arena lighting via relay
    arenaLightingState.isOn = (config.mode == LIGHTING_MODE_ON);
    arenaLightingState.brightness = config.brightness;
    // TODO: Control arena relay (on/off only, RGBW not applicable)
    
  } else if (config.targetId == LIGHTING_ID_ALL) {
    // ALL lighting (targets + arena)
    for (int i = 0; i < NUM_TARGETS; i++) {
      LightingState &state = targetLightingStates[i];
      state.isOn = (config.mode == LIGHTING_MODE_ON);
      state.brightness = config.brightness;
      state.red = config.red;
      state.green = config.green;
      state.blue = config.blue;
      state.white = config.white;
    }
    arenaLightingState.isOn = (config.mode == LIGHTING_MODE_ON);
    arenaLightingState.brightness = config.brightness;
    // TODO: Update all LED strips and arena relay
  }
}

// ============================================================================
// GAME LOGIC
// ============================================================================

void updateGameState() {
  switch (currentState) {
    case STATE_IDLE:
      // Do nothing
      break;
      
    case STATE_TARGET_RESET:
      // TODO: Monitor reset completion
      break;
      
    case STATE_RUNNING_GAME:
      {
        unsigned long elapsed = millis() - gameStartTime;
        
        // Check if game time expired
        if (elapsed >= gameRunTime) {
          currentState = STATE_IDLE;
          sendReturnProtocol(RETURN_MODE_END_OF_GAME, TARGET_ID_CONTROLLER, STATUS_IDLE);
        }
        
        // TODO: Update active targets
        for (int i = 0; i < NUM_TARGETS; i++) {
          if (targetStates[i].active) {
            updateTarget(i);
          }
        }
      }
      break;
      
    case STATE_RUNNING_DEBUG:
      // TODO: Debug mode logic
      break;
  }
}

void updateTarget(uint8_t targetId) {
  // NOTE: Always use latest targetConfigs - config can be updated on the fly
  TargetProtocol &config = targetConfigs[targetId];
  TargetState &state = targetStates[targetId];
  
  unsigned long currentTime = millis();
  unsigned long timeSinceLastMove = currentTime - state.lastMoveTime;
  
  // Check if it's time for next move cycle
  if (timeSinceLastMove >= (config.timeBetweenCycles * 1000UL)) {
    state.lastMoveTime = currentTime;
    
    // TODO: Implement target movement logic
    // - Determine if this cycle is a juke (based on config.jukePercent)
    // - Check if random speed change applies (based on config.randomSpeedPercent)
    // - Calculate movement speed (config.movementSpeed or random 0-3)
    // - Update servo position based on visibility target
    // - Apply transition animation
  }
}

void checkTargetHits() {
  // TODO: Monitor hit sensors
  // When target is hit:
  // sendReturnProtocol(RETURN_MODE_TARGET_HIT, targetId, STATUS_HIT);
}

// ============================================================================
// LED PATTERN GENERATION
// ============================================================================
// These functions generate WRGB pixel colors on-the-fly during LED updates.
// No RAM buffering - colors calculated mathematically from pattern descriptors.
// ============================================================================

// Fast 8-bit sine approximation (lookup table in PROGMEM)
static const uint8_t PROGMEM sin8_table[256] = {
  128,131,134,137,140,143,146,149,152,155,158,162,165,167,170,173,
  176,179,182,185,188,190,193,196,198,201,203,206,208,211,213,215,
  218,220,222,224,226,228,230,232,234,235,237,238,240,241,243,244,
  245,246,248,249,250,250,251,252,253,253,254,254,254,255,255,255,
  255,255,255,255,254,254,254,253,253,252,251,250,250,249,248,246,
  245,244,243,241,240,238,237,235,234,232,230,228,226,224,222,220,
  218,215,213,211,208,206,203,201,198,196,193,190,188,185,182,179,
  176,173,170,167,165,162,158,155,152,149,146,143,140,137,134,131,
  128,124,121,118,115,112,109,106,103,100,97,93,90,88,85,82,
  79,76,73,70,67,65,62,59,57,54,52,49,47,44,42,40,
  37,35,33,31,29,27,25,23,21,20,18,17,15,14,12,11,
  10,9,7,6,5,5,4,3,2,2,1,1,1,0,0,0,
  0,0,0,0,1,1,1,2,2,3,4,5,5,6,7,9,
  10,11,12,14,15,17,18,20,21,23,25,27,29,31,33,35,
  37,40,42,44,47,49,52,54,57,59,62,65,67,70,73,76,
  79,82,85,88,90,93,97,100,103,106,109,112,115,118,121,124
};

inline uint8_t sin8(uint8_t theta) {
  return pgm_read_byte(&sin8_table[theta]);
}

// Scale 8-bit value by 8-bit scale factor (returns 8-bit result)
inline uint8_t scale8(uint8_t value, uint8_t scale) {
  return ((uint16_t)value * (uint16_t)scale) >> 8;
}

// Generate single pixel color from pattern descriptor
void generatePixelColor(const PatternDescriptor &pattern, uint16_t ledIndex, uint16_t numLeds,
                        uint32_t timestamp, uint8_t &w, uint8_t &r, uint8_t &g, uint8_t &b) {
  switch (pattern.type) {
    case PATTERN_OFF:
      w = r = g = b = 0;
      break;
      
    case PATTERN_SOLID:
      // Simple solid color - same for all LEDs
      w = scale8(pattern.w, pattern.brightness);
      r = scale8(pattern.r, pattern.brightness);
      g = scale8(pattern.g, pattern.brightness);
      b = scale8(pattern.b, pattern.brightness);
      break;
      
    case PATTERN_PULSE:
      // Breathing effect - sine wave modulates brightness
      {
        uint8_t phase = ((timestamp * pattern.speed) >> 4) & 0xFF;
        uint8_t pulseBrightness = sin8(phase);
        uint8_t scaledBrightness = scale8(pattern.brightness, pulseBrightness);
        w = scale8(pattern.w, scaledBrightness);
        r = scale8(pattern.r, scaledBrightness);
        g = scale8(pattern.g, scaledBrightness);
        b = scale8(pattern.b, scaledBrightness);
      }
      break;
      
    case PATTERN_CHASE:
      // Moving dot with trailing fade
      {
        uint16_t position = ((timestamp * pattern.speed) >> 6) % numLeds;  // Use actual LED count
        int16_t tailLength = pattern.param1;  // Tail length (0-255)
        if (tailLength > (int16_t)(numLeds / 2)) tailLength = numLeds / 2;  // Limit tail
        
        int16_t distance = (int16_t)ledIndex - (int16_t)position;
        if (distance < 0) distance += numLeds;  // Handle wraparound
        
        if (distance < tailLength) {
          // In tail - fade from 100% to 0%
          uint8_t fade = 255 - ((distance * 255) / tailLength);
          uint8_t scaledBrightness = scale8(pattern.brightness, fade);
          w = scale8(pattern.w, scaledBrightness);
          r = scale8(pattern.r, scaledBrightness);
          g = scale8(pattern.g, scaledBrightness);
          b = scale8(pattern.b, scaledBrightness);
        } else {
          w = r = g = b = 0;  // Outside chase
        }
      }
      break;
      
    case PATTERN_STROBE:
      // Fast on/off blinking
      {
        uint8_t strobeRate = pattern.param1 ? pattern.param1 : 10;  // Blinks per second (default 10)
        uint32_t period = 1000 / strobeRate;  // Period in ms
        bool isOn = ((timestamp % period) < (period / 2));  // 50% duty cycle
        
        if (isOn) {
          w = scale8(pattern.w, pattern.brightness);
          r = scale8(pattern.r, pattern.brightness);
          g = scale8(pattern.g, pattern.brightness);
          b = scale8(pattern.b, pattern.brightness);
        } else {
          w = r = g = b = 0;
        }
      }
      break;
      
    case PATTERN_GRADIENT:
      // Linear two-color gradient across LED strip
      // param1 stores second color in upper bits (simplified - use base color + param for variation)
      {
        uint8_t ratio = numLeds > 1 ? (ledIndex * 255UL) / (numLeds - 1) : 0;
        // Gradient from base color to dimmed version
        uint8_t fade = 255 - ratio;
        w = scale8(scale8(pattern.w, fade), pattern.brightness);
        r = scale8(scale8(pattern.r, fade), pattern.brightness);
        g = scale8(scale8(pattern.g, fade), pattern.brightness);
        b = scale8(scale8(pattern.b, fade), pattern.brightness);
      }
      break;
      
    default:
      w = r = g = b = 0;
  }
}

// Update single LED pin with patterns for its 3 targets
void updateLEDPin(uint8_t pinIndex) {
  if (pinIndex >= NUM_LED_PINS) return;
  
  Adafruit_NeoPixel &strip = ledStrips[pinIndex];
  uint32_t timestamp = millis();
  uint16_t numLeds = strip.numPixels();  // Use actual LED count
  
  // Each pin controls 3 targets
  uint8_t startTarget = pinIndex * TARGETS_PER_PIN;
  
  for (uint16_t ledIndex = 0; ledIndex < numLeds; ledIndex++) {
    // For testing with limited LEDs, just show pattern from first target on this pin
    uint8_t globalTarget = startTarget;  // Just use first target for now
    uint16_t ledWithinTarget = ledIndex % LEDS_PER_TARGET;
    
    // Get active pattern for this target
    PatternState state = currentPatternState[globalTarget];
    const PatternDescriptor &pattern = targetPatterns[globalTarget][state];
    
    // Generate color
    uint8_t w, r, g, b;
    generatePixelColor(pattern, ledWithinTarget, numLeds, timestamp, w, r, g, b);
    
    // Set pixel - Color(R, G, B, W) - library handles WRGB byte reordering
    strip.setPixelColor(ledIndex, strip.Color(r, g, b, w));
  }
  
  strip.show();
  ledPinDirty[pinIndex] = false;
}

// Update all LED pins that have dirty flags set
void updateDirtyLEDs() {
  for (uint8_t pin = 0; pin < NUM_LED_PINS; pin++) {
    if (ledPinDirty[pin]) {
      updateLEDPin(pin);
    }
  }
}

// Mark a target's LED pin as needing update
void markTargetLEDsDirty(uint8_t targetId) {
  if (targetId >= NUM_TARGETS) return;
  uint8_t pinIndex = targetId / TARGETS_PER_PIN;
  ledPinDirty[pinIndex] = true;
}

// Update pattern states based on target activity
void updatePatternStates() {
  for (uint8_t targetId = 0; targetId < NUM_TARGETS; targetId++) {
    PatternState oldState = currentPatternState[targetId];
    PatternState newState;
    
    // Determine pattern based on target state
    // TODO: Add hit detection flag to TargetState
    // For now, use active flag to switch between IDLE and MOVING
    if (targetStates[targetId].active) {
      newState = PATTERN_STATE_MOVING;
    } else {
      newState = PATTERN_STATE_IDLE;
    }
    
    // If state changed, mark LEDs dirty
    if (oldState != newState) {
      currentPatternState[targetId] = newState;
      markTargetLEDsDirty(targetId);
    }
  }
}

// Initialize default patterns (called from setup())
void initializeDefaultPatterns() {
  // IDLE pattern: Dim cyan solid color
  for (uint8_t i = 0; i < NUM_TARGETS; i++) {
    targetPatterns[i][PATTERN_STATE_IDLE] = {
      .type = PATTERN_SOLID,
      .speed = 0,
      .param1 = 0,
      .brightness = 64,   // 25% brightness
      .w = 0,             // No white
      .r = 0,
      .g = 64,
      .b = 128            // Cyan
    };
  }
  
  // MOVING pattern: Green chase effect
  for (uint8_t i = 0; i < NUM_TARGETS; i++) {
    targetPatterns[i][PATTERN_STATE_MOVING] = {
      .type = PATTERN_CHASE,
      .speed = 8,         // Medium speed
      .param1 = 3,        // Tail length (short for testing with 10 LEDs)
      .brightness = 255,  // Full brightness
      .w = 0,
      .r = 0,
      .g = 255,           // Green
      .b = 0
    };
  }
  
  // HIT pattern: Red strobe
  for (uint8_t i = 0; i < NUM_TARGETS; i++) {
    targetPatterns[i][PATTERN_STATE_HIT] = {
      .type = PATTERN_STROBE,
      .speed = 10,        // Fast
      .param1 = 10,       // 10 blinks per second
      .brightness = 255,
      .w = 0,
      .r = 255,           // Red
      .g = 0,
      .b = 0
    };
  }
  
  // Initialize all targets to IDLE pattern
  for (uint8_t i = 0; i < NUM_TARGETS; i++) {
    currentPatternState[i] = PATTERN_STATE_IDLE;
  }
  
  // Mark all pins dirty for initial update
  for (uint8_t pin = 0; pin < NUM_LED_PINS; pin++) {
    ledPinDirty[pin] = true;
  }
}

// ============================================================================
// ARDUINO SETUP & LOOP
// ============================================================================

void setup() {
  // Initialize serial communication
  Serial.begin(115200);
  
  // Initialize target states
  for (int i = 0; i < NUM_TARGETS; i++) {
    targetStates[i].active = false;
    targetStates[i].currentVisibility = 0;
    targetStates[i].movementSpeed = 0;
    targetStates[i].lastMoveTime = 0;
    
    // Initialize lighting states
    targetLightingStates[i].isOn = false;
    targetLightingStates[i].brightness = 0;
    targetLightingStates[i].red = 0;
    targetLightingStates[i].green = 0;
    targetLightingStates[i].blue = 0;
    targetLightingStates[i].white = 0;
  }
  
  // Initialize arena lighting
  arenaLightingState.isOn = false;
  arenaLightingState.brightness = 0;
  arenaLightingState.red = 0;
  arenaLightingState.green = 0;
  arenaLightingState.blue = 0;
  arenaLightingState.white = 0;
  
  // Initialize LED strips (WS2814 WRGB)
  Serial.print(F("Initializing "));
  Serial.print(NUM_LED_PINS);
  Serial.print(F(" LED strips with "));
  Serial.print(TEST_LEDS_PER_PIN);
  Serial.println(F(" LEDs each..."));
  
  for (uint8_t pin = 0; pin < NUM_LED_PINS; pin++) {
    ledStrips[pin].begin();
    ledStrips[pin].setBrightness(128);  // 50% brightness
    ledStrips[pin].show();  // Initialize all pixels to 'off'
    Serial.print(F("  Strip "));
    Serial.print(pin);
    Serial.print(F(": "));
    Serial.print(ledStrips[pin].numPixels());
    Serial.println(F(" LEDs"));
  }
  Serial.println(F("LED strips initialized"));
  
  // Initialize LED patterns
  initializeDefaultPatterns();
  
  Serial.println(F("LED system initialized (NEO_WRGB + NEO_KHZ800)"));
  
  // ============================================================================
  // TEMPORARY: PATTERN TEST - Cycle through all 3 target states
  // ============================================================================
  Serial.println(F(""));
  Serial.println(F("=== PATTERN TEST MODE ==="));
  Serial.println(F("Testing with 10 LEDs on pin D2"));
  
  // Simple direct test first - bypass pattern system
  Serial.println(F("\n[DIRECT TEST] First 3 LEDs: red, green, white"));
  // Color() takes (R, G, B, W) - library handles WRGB byte reordering
  ledStrips[0].clear();
  ledStrips[0].setPixelColor(0, ledStrips[0].Color(255, 0, 0, 0));  // Red
  ledStrips[0].setPixelColor(1, ledStrips[0].Color(0, 255, 0, 0));  // Green
  ledStrips[0].setPixelColor(2, ledStrips[0].Color(0, 0, 0, 255));  // White
  ledStrips[0].show();
  Serial.println(F("Showing for 3 seconds..."));
  delay(3000);
  ledStrips[0].clear();
  ledStrips[0].show();
  
  Serial.println(F("\n[PATTERN TEST] Cycling states..."));
  
  // Test pattern cycling - show all 3 states for target 0
  for (int cycle = 0; cycle < 2; cycle++) {
    // IDLE state (cyan solid)
    Serial.print(F("\nCycle ")); Serial.print(cycle + 1);
    Serial.println(F(": IDLE (cyan solid)"));
    currentPatternState[0] = PATTERN_STATE_IDLE;
    markTargetLEDsDirty(0);
    updateDirtyLEDs();
    delay(3000);
    
    // MOVING state (green chase)
    Serial.println(F("State: MOVING (green chase)"));
    currentPatternState[0] = PATTERN_STATE_MOVING;
    markTargetLEDsDirty(0);
    for (int i = 0; i < 30; i++) {  // Run chase for 3 seconds
      updateDirtyLEDs();
      delay(100);
    }
    
    // HIT state (red strobe)
    Serial.println(F("State: HIT (red strobe)"));
    currentPatternState[0] = PATTERN_STATE_HIT;
    markTargetLEDsDirty(0);
    for (int i = 0; i < 30; i++) {  // Run strobe for 3 seconds
      updateDirtyLEDs();
      delay(100);
    }
  }
  
  // Reset to IDLE
  Serial.println(F("\nResetting to IDLE"));
  currentPatternState[0] = PATTERN_STATE_IDLE;
  markTargetLEDsDirty(0);
  updateDirtyLEDs();
  
  Serial.println(F("Pattern test complete"));
  // ============================================================================
  // END OF TEMPORARY TEST
  // ============================================================================
  
  Serial.println(F("Ready for operation"));
  // ============================================================================
  // END TEMPORARY DIAGNOSTIC
  // ============================================================================
  
  // TODO: Initialize servos, sensors, and other hardware
  
  // Set initial state
  currentState = STATE_IDLE;
  
  // Send startup signal
  sendReturnProtocol(RETURN_MODE_IDLE, TARGET_ID_CONTROLLER, STATUS_IDLE);
}

void loop() {
  // Process all incoming serial data (may be multiple protocol words)
  // This allows rapid updates from server to be applied immediately
  if (Serial.available() >= 4) {
    do {
      processSerialData();
    } while (Serial.available() >= 4);
  }
  
  // Update game state
  updateGameState();
  
  // Check for target hits
  checkTargetHits();
  
  // Update LED pattern states based on target activity
  updatePatternStates();
  
  // ============================================================================
  // TEMPORARY: LED PATTERN TEST - DISABLED FOR DIAGNOSTICS
  // ============================================================================
  /*
  // Cycle through pattern states every 3 seconds to test all patterns
  static uint32_t lastPatternChange = 0;
  static uint8_t testPatternIndex = 0;
  
  if (millis() - lastPatternChange > 3000) {
    lastPatternChange = millis();
    
    // Cycle through pattern states: IDLE -> MOVING -> HIT -> IDLE...
    PatternState testState = (PatternState)(testPatternIndex % 3);
    
    // Apply test pattern to first 3 targets (visible on Pin D2)
    for (uint8_t i = 0; i < 3; i++) {
      currentPatternState[i] = testState;
      markTargetLEDsDirty(i);
    }
    
    testPatternIndex++;
    
    // Debug output
    #if DEBUG_PROTOCOL_OUTPUT
    const char* stateNames[] = {"IDLE", "MOVING", "HIT"};
    Serial.print("LED TEST: Pattern changed to ");
    Serial.println(stateNames[testState]);
    #endif
  }
  */
  // ============================================================================
  // END TEMPORARY TEST
  // ============================================================================
  
  // Update LEDs (only dirty pins are refreshed)
  updateDirtyLEDs();
  
  // No delay - run at full speed for maximum responsiveness
}