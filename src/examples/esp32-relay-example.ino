#include <Arduino.h>
#include "Relay.h"

// Example usage with a relay connected to GPIO pin 26
Relay relay1(36);
Relay relay2(35);
Relay relay3(34);
Relay relay4(33);

void setup() {
  Serial.begin(115200);
}


void loop() {
  // Cycle through all relays
  for (Relay* relay : {&relay1, &relay2, &relay3, &relay4}) {
    // Turn on
    relay->on();
    delay(500);
    relay->off();
    delay(500);
    
    // Toggle example
    relay->toggle();
    delay(250);
    relay->toggle();
    delay(250);
  }
}