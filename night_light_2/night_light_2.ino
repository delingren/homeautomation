#include "src/HomeSpan.h"
#include <Arduino.h>
#include <WiFiClient.h>

// A switch that controls a relay but can also be manually operated by a
// pushbutton. It uses two pins. One output pin for controlling the relay and
// one input pin for the pushbutton. If the pushbutton pin doesn't have an
// internal pullup resistor, use an external one.
struct DEV_Switch : Service::Switch {
  int pin_relay;
  int pin_button;
  SpanCharacteristic *power;

  DEV_Switch(int pin_relay, int pin_button) : Service::Switch() {
    this->pin_relay = pin_relay;
    this->pin_button = pin_button;
    power = new Characteristic::On();
    pinMode(pin_relay, OUTPUT);

    new SpanButton(pin_button, PushButton::TRIGGER_ON_LOW);
  }

  boolean update() {
    digitalWrite(pin_relay, power->getNewVal());
    return (true);
  }

  void button(int pin, int type) override {
    if (pin != pin_button) {
      return;
    }
    bool new_value = !(power->getNewVal());
    power->setVal(new_value);
    digitalWrite(pin_relay, new_value);
  }
};

class InvertedLED : public Blinkable {
  int pin;

public:
  InvertedLED(int pin) : pin{pin} {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, HIGH);
  }
  void on() { digitalWrite(pin, LOW); }
  void off() { digitalWrite(pin, HIGH); }
  int getPin() { return (pin); }
};

void setup() {
  Serial.begin(115200);

  pinMode(LED_BUILTIN, OUTPUT);

  InvertedLED *statusLed = new InvertedLED(8);
  homeSpan.enableOTA();
  homeSpan.setSketchVersion("1.0").setStatusDevice(statusLed).begin(
      Category::Switches, "Nightlight", "nightlight-v2");

  new SpanAccessory();
  new Service::AccessoryInformation();
  new Characteristic::Identify();
  new Characteristic::Name("Switch");
  new DEV_Switch(4, 3);

  homeSpan.autoPoll();
}

void loop() {}
