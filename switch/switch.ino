#include "src/HomeSpan.h"
#include <Arduino.h>
#include <WiFiClient.h>

struct DEV_Switch : Service::Switch {
  int pin;
  SpanCharacteristic *power;

  DEV_Switch(int pin) : Service::Switch() {
    this->pin = pin;
    power = new Characteristic::On();
    pinMode(pin, OUTPUT);
  }

  boolean update() {
    digitalWrite(pin, power->getNewVal());
    return (true);
  }
};

void setup() {
  Serial.begin(115200);
  // homeSpan.enableOTA();
  homeSpan.setSketchVersion("1.00").setStatusPin(8).setControlPin(4).begin(
      Category::Switches, "Fireplace", "FIREPLACE-SWITCH");

  new SpanAccessory();
  new Service::AccessoryInformation();
  new Characteristic::Identify();
  new Characteristic::Name("Fireplace");
  new DEV_Switch(5);

  homeSpan.autoPoll();
}

void loop() {}
