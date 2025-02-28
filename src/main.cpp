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

// Array of relay pointers for easier access
Relay* relays[] = {&relay1, &relay2, &relay3, &relay4};
const uint8_t NUM_RELAYS = 4;

// Timer variables for timed relay operation
unsigned long relayTimers[4] = {0, 0, 0, 0};
unsigned long relayDurations[4] = {0, 0, 0, 0};

// Buffer for downlink messages
uint8_t downlinkBuffer[256];
// Time tracking for downlink polling
unsigned long lastDownlinkCheck = 0;
const unsigned long DOWNLINK_CHECK_INTERVAL = 30000; // Check every 30 seconds

// Function to handle downlink messages
void processDownlinkMessage(uint8_t* payload, uint8_t size) {
  if (size < 1) {
    return; // Message too short
  }
  
  Serial.print("Received downlink: ");
  for (int i = 0; i < size; i++) {
    Serial.print(payload[i], HEX);
    Serial.print(" ");
  }
  Serial.println();
  
  // Command format:
  // Byte 0: Command type
  //   0x01: Direct relay control
  //   0x02: Timed relay operation
  //
  // For command 0x01 (Direct control):
  //   Byte 1: Relay bitmap (bit 0 = relay1, bit 1 = relay2, etc.)
  //   Byte 2: State (0 = OFF, 1 = ON, 2 = TOGGLE)
  //
  // For command 0x02 (Timed operation):
  //   Byte 1: Relay number (0-3)
  //   Byte 2-3: Duration in seconds (LSB first)
  //   Byte 4: Action (0 = OFF after duration, 1 = ON after duration, 2 = ON then OFF after duration)
  
  uint8_t command = payload[0];
  
  switch (command) {
    case 0x01: // Direct relay control
      if (size >= 3) {
        uint8_t relayBitmap = payload[1];
        uint8_t state = payload[2];
        
        for (uint8_t i = 0; i < NUM_RELAYS; i++) {
          if (relayBitmap & (1 << i)) {
            switch (state) {
              case 0:
                relays[i]->off();
                relayTimers[i] = 0; // Cancel any timers
                Serial.print("Relay ");
                Serial.print(i + 1);
                Serial.println(" OFF");
                break;
              case 1:
                relays[i]->on();
                relayTimers[i] = 0; // Cancel any timers
                Serial.print("Relay ");
                Serial.print(i + 1);
                Serial.println(" ON");
                break;
              case 2:
                relays[i]->toggle();
                relayTimers[i] = 0; // Cancel any timers
                Serial.print("Relay ");
                Serial.print(i + 1);
                Serial.print(" TOGGLED to ");
                Serial.println(relays[i]->getState() ? "ON" : "OFF");
                break;
            }
          }
        }
      }
      break;
      
    case 0x02: // Timed relay operation
      if (size >= 5) {
        uint8_t relayNum = payload[1];
        if (relayNum < NUM_RELAYS) {
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
          
          switch (action) {
            case 0: // OFF after duration
              Serial.println("OFF after timeout");
              break;
            case 1: // ON after duration
              Serial.println("ON after timeout");
              break;
            case 2: // ON then OFF after duration
              relays[relayNum]->on();
              Serial.println("ON now, OFF after timeout");
              break;
          }
          
          // Store the action in the high byte of relayDurations for later reference
          relayDurations[relayNum] |= ((unsigned long)action << 24);
        }
      }
      break;
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
      Serial.println("Downlink received!");
      processDownlinkMessage(downlinkBuffer, length);
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