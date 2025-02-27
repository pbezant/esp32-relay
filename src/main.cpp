

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

void setup() {
  Serial.begin(115200);
    if (!lorawan.begin()) {
        Serial.println("Failed to initialize LoRaWAN");
        return;
    }
    
    // Join network
    if (!lorawan.joinNetwork()) {
        Serial.println("Failed to join network");
        return;
    }
}


void loop() {
  // Cycle through all relays

}