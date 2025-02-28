#ifndef RELAY_H
#define RELAY_H

#include <Arduino.h>

class Relay {
  private:
    int pin;
    bool state;
    bool activeHigh;  // true if relay is activated by HIGH signal, false for LOW
    
  public:
    Relay(int relayPin, bool isActiveHigh = false);
    
    void on();
    void off();
    void toggle(int duration = 5); // Default duration is 5 seconds
    bool getState();
};

#endif // RELAY_H 