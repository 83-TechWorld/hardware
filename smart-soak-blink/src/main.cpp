#include <Arduino.h>
#include <ESP32Servo.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

Servo dispenser;
#define SERVO_PIN   13
#define GATE_CLOSED 0
#define GATE_OPEN   90

#define TARGET 30.0        // grams we want

float weight = 0.0;        // simulated cup weight
bool dispensing = true;

void showScreen(const char* status) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("Target: ");
  display.print(TARGET, 0);
  display.println(" g");

  display.setTextSize(2);
  display.setCursor(0, 16);
  display.print(weight, 1);
  display.println(" g");

  display.setTextSize(1);
  display.setCursor(0, 48);
  display.println(status);
  display.display();
}

void setup() {
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.setTextColor(SSD1306_WHITE);

  dispenser.attach(SERVO_PIN);
  dispenser.write(GATE_OPEN);   // start with gate open
}

void loop() {
  if (dispensing) {
    // gate is OPEN, so nuts fall in -> weight rises a little each loop
    weight = weight + 2.5;
    dispenser.write(GATE_OPEN);

    if (weight >= TARGET) {
      dispenser.write(GATE_CLOSED);   // hit target -> SNAP SHUT
      dispensing = false;
      showScreen("DONE - gate shut");
    } else {
      showScreen("Dispensing...");
    }
  } else {
    showScreen("DONE - gate shut");   // stays done, gate stays shut
  }

  delay(400);   // slow enough to watch the number climb
}