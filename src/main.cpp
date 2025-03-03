//Downlink command format:
//Byte 0: Command type
//   0x01: Turn relay(s) ON
//   0x02: Turn relay(s) OFF
//   0x03: Toggle relay(s) for duration
//
//For commands 0x01 and 0x02:
//Byte 1: Relay bitmap (bit 0 = relay1, bit 1 = relay2, etc.)
//
//For command 0x03 (Toggle for duration):
//Byte 1: Relay bitmap (bit 0 = relay1, bit 1 = relay2, etc.)
//Byte 2-3: Duration in seconds (LSB first)
//
//Examples:
//Turn on all relays:
// 01 FF
//Turn off all relays:
// 02 FF
//Turn on relay 1:
// 01 01
//Turn off relay 4:
// 02 08
//Toggle relay 4 for 4 seconds:
// 03 08 04 00
// Current relay configuration:
// Relay 1: 03 01 05 00 (Toggle relay 1 for 5 seconds)
// Relay 2: 03 02 05 00 (Toggle relay 2 for 5 seconds)
// Relay 3: 03 04 05 00 (Toggle relay 3 for 5 seconds)
// Relay 4: 03 08 05 00 (Toggle relay 4 for 5 seconds)
// Relay 5: 03 10 05 00 (Toggle relay 5 for 5 seconds)
// Relay 6: 03 20 05 00 (Toggle relay 6 for 5 seconds)
// Relay 7: 03 40 05 00 (Toggle relay 7 for 5 seconds)
// Relay 8: 03 80 05 00 (Toggle relay 8 for 5 seconds)


#include <Arduino.h>
#include "Relay.h"
#include "LoRaWANManager.h"
#include "secrets.h"

const LoRaWANBand_t region = US915;
const uint8_t subBand = 2;
LoRaWANManager lorawan(joinEui, devEui, nwkKey, appKey, region, subBand);

Relay relay1(36);
Relay relay2(35);
Relay relay3(34);
Relay relay4(33);
Relay relay5(47);
Relay relay6(48);
Relay relay7(26);
Relay relay8(21);

// Array of relay pointers for easier access
Relay* relays[] = {&relay1, &relay2, &relay3, &relay4, &relay5, &relay6, &relay7, &relay8};
const uint8_t NUM_RELAYS = 8;

// Timer variables for timed relay operation
struct RelayTimer {
  unsigned long startTime = 0;
  unsigned long duration = 0;
  unsigned long lastPrintTime = 0;  // Track last print time per relay
  bool active = false;
  bool initialState = false;  // Store the initial state to know what to return to
};

RelayTimer relayTimers[NUM_RELAYS];

// Buffer for downlink messages
uint8_t downlinkBuffer[256];
// Time tracking for downlink polling
unsigned long lastDownlinkCheck = 0;
const unsigned long DOWNLINK_CHECK_INTERVAL = 1;

// Replace the command deduplication variables
struct CommandDedupeInfo {
  uint32_t commandHash;
  unsigned long lastReceivedTime;
};

const unsigned long DEDUPE_WINDOW_MS = 30000; // 30 second dedupe window
CommandDedupeInfo lastCommand = {0, 0};

void processDownlinkMessage(uint8_t* payload, uint8_t size) {
  // Print received data for debugging
  Serial.print("Received downlink raw data: ");
  for (int i = 0; i < size && i < 20; i++) { // Print first 20 bytes maximum
    Serial.print(payload[i], HEX);
    Serial.print(" ");
  }
  if (size > 20) {
    Serial.print("... (total ");
    Serial.print(size);
    Serial.print(" bytes)");
  }
  Serial.println();
  
  // Check if we have at least one byte for the command
  if (size < 1) {
    Serial.println("Downlink too short, ignoring.");
    return;
  }
  
  // Calculate a hash of the command for deduplication
  uint32_t commandHash = 0;
  for (int i = 0; i < size && i < 4; i++) {
    commandHash = (commandHash << 8) | payload[i];
  }
  
  // Get current time for dedupe check
  unsigned long now = millis();
  
  // Check if this is a duplicate command within the time window
  if (commandHash == lastCommand.commandHash) {
    unsigned long timeSinceLastCommand = now - lastCommand.lastReceivedTime;
    if (timeSinceLastCommand < DEDUPE_WINDOW_MS) {
      Serial.print("Duplicate command received after ");
      Serial.print(timeSinceLastCommand);
      Serial.print("ms (window: ");
      Serial.print(DEDUPE_WINDOW_MS);
      Serial.println("ms), ignoring");
      return;
    }
    Serial.print("Command matches previous but window expired (");
    Serial.print(timeSinceLastCommand);
    Serial.println("ms), processing");
  }
  
  // Update the last command info
  lastCommand.commandHash = commandHash;
  lastCommand.lastReceivedTime = now;
  
  // Extract command type
  uint8_t command = payload[0];
  
  // Process command based on type
  if (command == 0x01) { // Turn relay(s) ON
    // Check if we have enough bytes for this command
    if (size < 2) {
      Serial.println("ON command incomplete");
      return;
    }
    
    uint8_t relayBitmap = payload[1];
    
    Serial.print("Turn ON command: relays=");
    Serial.println(relayBitmap, BIN);
    
    // Process for each relay
    for (uint8_t i = 0; i < NUM_RELAYS; i++) {
      if (relayBitmap & (1 << i)) {
        relays[i]->on();
        relayTimers[i].active = false; // Cancel any timers
        Serial.print("Relay ");
        Serial.print(i + 1);
        Serial.println(" ON");
      }
    }
  }
  else if (command == 0x02) { // Turn relay(s) OFF
    // Check if we have enough bytes for this command
    if (size < 2) {
      Serial.println("OFF command incomplete");
      return;
    }
    
    uint8_t relayBitmap = payload[1];
    
    Serial.print("Turn OFF command: relays=");
    Serial.println(relayBitmap, BIN);
    
    // Process for each relay
    for (uint8_t i = 0; i < NUM_RELAYS; i++) {
      if (relayBitmap & (1 << i)) {
        relays[i]->off();
        relayTimers[i].active = false; // Cancel any timers
        Serial.print("Relay ");
        Serial.print(i + 1);
        Serial.println(" OFF");
      }
    }
  }
  else if (command == 0x03) { // Toggle relay(s) for duration
    // Check if we have enough bytes for this command
    if (size < 4) {
      Serial.println("Toggle for duration command incomplete");
      return;
    }
    
    uint8_t relayBitmap = payload[1];
    // Duration in seconds (LSB first)
    unsigned long duration = ((unsigned long)payload[3] << 8) | payload[2];
    
    Serial.println("----------------------------------------");
    Serial.println("Processing Toggle Duration Command:");
    Serial.print("Raw bytes: ");
    for(int i = 0; i < 4; i++) {
      Serial.print("0x");
      Serial.print(payload[i], HEX);
      Serial.print(" ");
    }
    Serial.println();
    
    Serial.print("Duration calculation: LSB=0x");
    Serial.print(payload[2], HEX);
    Serial.print(", MSB=0x");
    Serial.print(payload[3], HEX);
    Serial.print(", Combined=0x");
    Serial.print(duration, HEX);
    Serial.print(" (");
    Serial.print(duration);
    Serial.println(" seconds)");
    
    Serial.print("Toggle for duration command: relays=0x");
    Serial.print(relayBitmap, HEX);
    Serial.print(" (binary ");
    Serial.print(relayBitmap, BIN);
    Serial.print("), duration=");
    Serial.print(duration);
    Serial.println(" seconds");
    
    // Process for each relay
    for (uint8_t i = 0; i < NUM_RELAYS; i++) {
      if (relayBitmap & (1 << i)) {
        // Check if timer is already active for this relay
        if (relayTimers[i].active) {
          Serial.print("Timer already active for relay ");
          Serial.print(i + 1);
          Serial.println(", ignoring duplicate command");
          continue;
        }
        
        // Store initial state before changing
        relayTimers[i].initialState = relays[i]->getState();
        Serial.print("Initial state of relay ");
        Serial.print(i + 1);
        Serial.print(": ");
        Serial.println(relayTimers[i].initialState ? "ON" : "OFF");
        
        // Set to opposite state instead of toggle
        if (relayTimers[i].initialState) {
          relays[i]->off();
        } else {
          relays[i]->on();
        }
        
        Serial.print("Changed relay ");
        Serial.print(i + 1);
        Serial.print(" to: ");
        Serial.println(relays[i]->getState() ? "ON" : "OFF");
        
        // Set up timer
        unsigned long now = millis();
        relayTimers[i].duration = duration * 1000; // Convert to milliseconds
        relayTimers[i].startTime = now;
        relayTimers[i].lastPrintTime = now;
        relayTimers[i].active = true;
        
        unsigned long endTime = now + relayTimers[i].duration;
        
        Serial.println("----------------------------------------");
        Serial.print("Timer started for relay ");
        Serial.print(i + 1);
        Serial.print(" for ");
        Serial.print(duration);
        Serial.println(" seconds");
        Serial.print("Start time: ");
        Serial.print(now);
        Serial.print(", End time: ");
        Serial.print(endTime);
        Serial.print(" (duration: ");
        Serial.print(relayTimers[i].duration);
        Serial.println("ms)");
        Serial.println("----------------------------------------");
      }
    }
  }
  else {
    Serial.print("Unknown command type: 0x");
    Serial.println(command, HEX);
  }
}

void clearDownlinkBuffer() {
  // Clear the downlink buffer by filling it with zeros
  memset(downlinkBuffer, 0, sizeof(downlinkBuffer));
  
  // Clear any pending downlinks in the LoRaWAN manager
  while (lorawan.receiveDownlink(downlinkBuffer, nullptr, nullptr)) {
    Serial.println("Cleared pending downlink");
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("ESP32 Relay Controller Starting...");
  
  if (!lorawan.begin()) {
    Serial.println("Failed to initialize LoRaWAN");
    return;
  }
  
  // Join network
  if (!lorawan.joinNetwork()) {
    Serial.println("Failed to join network");
    return;
  }
  
  Serial.println("Successfully joined LoRaWAN network");
}

void loop() {
  // Process any pending LoRaWAN events
  // Poll for downlink messages periodically
  unsigned long currentTime = millis();
  
  // Check for downlink messages every DOWNLINK_CHECK_INTERVAL milliseconds
  if (currentTime - lastDownlinkCheck > DOWNLINK_CHECK_INTERVAL) {
    size_t length = sizeof(downlinkBuffer);
    uint8_t port = 0;
    
    if (lorawan.receiveDownlink(downlinkBuffer, &length, &port)) {
      Serial.println("==========================================");
      Serial.print("Downlink received on port: ");
      Serial.print(port);
      Serial.print(", length: ");
      Serial.println(length);
      
      // Print the full received payload in HEX
      Serial.print("Full payload: ");
      for (size_t i = 0; i < length && i < 16; i++) {  // Print first 16 bytes
        Serial.print(downlinkBuffer[i], HEX);
        Serial.print(" ");
      }
      if (length > 16) Serial.print("...");  // Indicate truncation
      Serial.println();
      
      // Handle TTN's padded downlink format by determining the actual command length
      uint8_t actualSize = 0;
      bool validCommand = false;
      
      // Extract the command type and determine the expected message length
      if (length > 0) {
        uint8_t cmdType = downlinkBuffer[0];
        Serial.print("Command type: 0x");
        Serial.println(cmdType, HEX);
        
        if ((cmdType == 0x01 || cmdType == 0x02) && length >= 2) {
          // ON/OFF command (Command, Bitmap)
          actualSize = 2;
          validCommand = true;
          Serial.print("Relay control command. Relay bitmap: 0x");
          Serial.println(downlinkBuffer[1], HEX);
        } else if (cmdType == 0x03 && length >= 4) {
          // Toggle for duration (Command, Bitmap, Duration LSB, Duration MSB)
          actualSize = 4;
          validCommand = true;
          Serial.println("Toggle for duration command");
        } else {
          // Unknown command or incomplete payload
          actualSize = length > 10 ? 10 : length; // Limit to first 10 bytes for safety
          Serial.println("Unknown or incomplete command");
        }
        
        Serial.print("Processing command with actual size: ");
        Serial.println(actualSize);
        
        // Process only the meaningful part of the downlink payload
        processDownlinkMessage(downlinkBuffer, actualSize);
        
        // Clear the downlink buffer after processing a valid command
        if (validCommand) {
          Serial.println("Clearing downlink buffer after command processing");
          clearDownlinkBuffer();
        }
      }
      Serial.println("==========================================");
    }
    
    lastDownlinkCheck = currentTime;
  }
  
  // Check timers for timed relay operations
  for (uint8_t i = 0; i < NUM_RELAYS; i++) {
    if (relayTimers[i].active) {
      // Calculate elapsed time
      unsigned long now = millis();
      unsigned long endTime = relayTimers[i].startTime + relayTimers[i].duration;
      
      // Check if timer has expired, using direct comparison
      if (now >= endTime) {
        // Timer has expired
        Serial.println("----------------------------------------");
        Serial.print("Timer expired for relay ");
        Serial.print(i + 1);
        Serial.print(" (end time: ");
        Serial.print(endTime);
        Serial.print(", current time: ");
        Serial.print(now);
        Serial.println(")");
        
        Serial.print("Current state: ");
        Serial.println(relays[i]->getState() ? "ON" : "OFF");
        Serial.print("Initial state was: ");
        Serial.println(relayTimers[i].initialState ? "ON" : "OFF");
        
        // Return to initial state
        if (relayTimers[i].initialState) {
          relays[i]->on();
        } else {
          relays[i]->off();
        }
        
        Serial.print("Returned relay ");
        Serial.print(i + 1);
        Serial.print(" to initial state: ");
        Serial.println(relayTimers[i].initialState ? "ON" : "OFF");
        Serial.println("----------------------------------------");
        
        // Reset timer
        relayTimers[i].active = false;
      } else {
        // Print progress every second
        if (now - relayTimers[i].lastPrintTime >= 1000) {
          unsigned long remainingTime = (endTime - now) / 1000;
          Serial.println("----------------------------------------");
          Serial.print("Relay ");
          Serial.print(i + 1);
          Serial.print(" timer: ");
          Serial.print(remainingTime);
          Serial.print(" seconds remaining (end time: ");
          Serial.print(endTime);
          Serial.print(", current time: ");
          Serial.print(now);
          Serial.println(")");
          Serial.println("----------------------------------------");
          relayTimers[i].lastPrintTime = now;
        }
      }
    }
  }
  
  // Send a status update every 5 minutes if needed
  static unsigned long lastStatusUpdate = 0;
  if (currentTime - lastStatusUpdate > 300000) { // 5 minutes in milliseconds
    // Prepare status message (relay states)
    uint8_t statusPayload[1] = {0};
    
    // Pack relay states into a bitmap
    for (uint8_t i = 0; i < NUM_RELAYS; i++) {
      if (relays[i]->getState()) {
        statusPayload[0] |= (1 << i);
      }
    }
    
    // Send the status update
    lorawan.sendData(statusPayload, sizeof(statusPayload));
    
    lastStatusUpdate = currentTime;
    Serial.println("Status update sent");
  }
}