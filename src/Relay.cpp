#include "Relay.h"
#include <Arduino.h>

// Remove the "class Relay {" and just implement the methods
Relay::Relay(int relayPin, bool isActiveHigh) {
  pin = relayPin;
  activeHigh = isActiveHigh;
  state = false;
  pinMode(pin, OUTPUT);
  off();  // Initialize relay to off state
}

void Relay::on() {
  digitalWrite(pin, activeHigh ? HIGH : LOW);
  state = true;
}

void Relay::off() {
  digitalWrite(pin, activeHigh ? LOW : HIGH);
  state = false;
}

void Relay::toggle(int duration) {
  if (state) {
    off();
    delay(duration * 1000);  // Convert seconds to milliseconds
    on();
  } else {
    on();
    delay(duration * 1000);  // Convert seconds to milliseconds
    off();
  }
}

bool Relay::getState() {
  return state;
}
