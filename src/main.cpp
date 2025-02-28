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
unsigned long relayTimers[4] = {0, 0, 0, 0};
unsigned long relayDurations[4] = {0, 0, 0, 0};

// Buffer for downlink messages
uint8_t downlinkBuffer[256];
// Time tracking for downlink polling
unsigned long lastDownlinkCheck = 0;
const unsigned long DOWNLINK_CHECK_INTERVAL = 100;

// Function to handle downlink messages
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
  
  // Extract command type
  uint8_t command = payload[0];
  
  // Process command based on type
  if (command == 0x01) { // Direct relay control
    // Check if we have enough bytes for this command
    if (size < 3) {
      Serial.println("Direct control command incomplete");
      return;
    }
    
    uint8_t relayBitmap = payload[1];
    uint8_t state = payload[2];
    
    Serial.print("Direct control command: relays=");
    Serial.print(relayBitmap, BIN);
    Serial.print(", state=");
    Serial.println(state);
    
    // Process for each relay
    for (uint8_t i = 0; i < NUM_RELAYS; i++) {
      if (relayBitmap & (1 << i)) {
        if (state == 0) {
          relays[i]->off();
          relayTimers[i] = 0; // Cancel any timers
          Serial.print("Relay ");
          Serial.print(i + 1);
          Serial.println(" OFF");
        } 
        else if (state == 1) {
          relays[i]->on();
          relayTimers[i] = 0; // Cancel any timers
          Serial.print("Relay ");
          Serial.print(i + 1);
          Serial.println(" ON");
        }
        else if (state == 2) {
          relays[i]->toggle();
          relayTimers[i] = 0; // Cancel any timers
          Serial.print("Relay ");
          Serial.print(i + 1);
          Serial.print(" TOGGLED to ");
          Serial.println(relays[i]->getState() ? "ON" : "OFF");
        }
      }
    }
  }
  else if (command == 0x02) { // Timed relay operation
    // Check if we have enough bytes for this command
    if (size < 5) {
      Serial.println("Timed operation command incomplete");
      return;
    }
    
    uint8_t relayNum = payload[1];
    
    // Check relay number is valid
    if (relayNum >= NUM_RELAYS) {
      Serial.print("Invalid relay number: ");
      Serial.println(relayNum);
      return;
    }
    
    // Duration in seconds (LSB first)
    unsigned long duration = ((unsigned long)payload[3] << 8) | payload[2];
    uint8_t action = payload[4];
    
    // Convert seconds to milliseconds
    relayDurations[relayNum] = duration * 1000;
    relayTimers[relayNum] = millis();
    
    Serial.print("Timed operation for Relay ");
    Serial.print(relayNum + 1);
    Serial.print(", Duration: ");
    Serial.print(duration);
    Serial.print("s, Action: ");
    
    if (action == 0) {
      Serial.println("OFF after timeout");
    }
    else if (action == 1) {
      Serial.println("ON after timeout");
    }
    else if (action == 2) {
      relays[relayNum]->on();
      Serial.println("ON now, OFF after timeout");
    }
    
    // Store the action in the high byte of relayDurations for later reference
    relayDurations[relayNum] |= ((unsigned long)action << 24);
  }
  else {
    Serial.print("Unknown command type: 0x");
    Serial.println(command, HEX);
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
      
      // Extract the command type and determine the expected message length
      if (length > 0) {
        uint8_t cmdType = downlinkBuffer[0];
        Serial.print("Command type: 0x");
        Serial.println(cmdType, HEX);
        
        if (cmdType == 0x01 && length >= 3) {
          // Direct relay control command (Command, Bitmap, State)
          actualSize = 3;
          Serial.print("Direct control command. Relay bitmap: 0x");
          Serial.print(downlinkBuffer[1], HEX);
          Serial.print(", State: ");
          Serial.println(downlinkBuffer[2]);
        } else if (cmdType == 0x02 && length >= 5) {
          // Timed relay operation (Command, Relay, Duration LSB, Duration MSB, Action)
          actualSize = 5;
          Serial.println("Timed relay operation command");
        } else {
          // Unknown command or incomplete payload
          actualSize = length > 10 ? 10 : length; // Limit to first 10 bytes for safety
          Serial.println("Unknown or incomplete command");
        }
        
        Serial.print("Processing command with actual size: ");
        Serial.println(actualSize);
        
        // Process only the meaningful part of the downlink payload
        processDownlinkMessage(downlinkBuffer, actualSize);
      }
      Serial.println("==========================================");
    }
    
    lastDownlinkCheck = currentTime;
  }
  
  // Check timers for timed relay operations
  for (uint8_t i = 0; i < NUM_RELAYS; i++) {
    if (relayTimers[i] > 0) {
      if (currentTime - relayTimers[i] >= (relayDurations[i] & 0x00FFFFFF)) {
        // Timer expired
        uint8_t action = (relayDurations[i] >> 24) & 0xFF;
        
        switch (action) {
          case 0: // OFF after duration
            relays[i]->off();
            Serial.print("Timed OFF for Relay ");
            Serial.println(i + 1);
            break;
          case 1: // ON after duration
            relays[i]->on();
            Serial.print("Timed ON for Relay ");
            Serial.println(i + 1);
            break;
          case 2: // ON then OFF after duration
            relays[i]->on();
            delay(relayTimers[i]);
            relays[i]->off();
            Serial.print("Timed OFF for Relay ");
            Serial.println(i + 1);
            break;
        }
        
        // Reset timer
        relayTimers[i] = 0;
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