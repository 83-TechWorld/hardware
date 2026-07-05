#include <Arduino.h>

// sketch.ino — blink the ESP32's onboard LED

void setup() {
  // setup() runs ONCE when the board powers on.
  pinMode(2, OUTPUT);   // pin 2 is wired to the small onboard light. OUTPUT = "I will control it."
}

void loop() {
  // loop() runs FOREVER, over and over, after setup finishes.
  digitalWrite(2, HIGH); // send power to pin 2 -> light ON
  delay(500);            // do nothing for 500 milliseconds (half a second)
  digitalWrite(2, LOW);  // cut power to pin 2 -> light OFF
  delay(500);            // wait another half second, then loop repeats
}