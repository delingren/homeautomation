/*********************************************************************************
 *  MIT License
 *
 *  Copyright (c) 2024 Deling Ren
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to
 *deal in the Software without restriction, including without limitation the
 *rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 *sell copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 *all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 *FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 *IN THE SOFTWARE.
 *
 ********************************************************************************/

#include "src/HomeSpan.h"
#include <utility>
#include <vector>

// Represents a contact sensor typically used by security system to monitor the
// status of doors and windows. The sensor is connected to a pullup input pin
// and the ground. When it's open, the pin is pulled up high. When it's closed,
// it's pulled to the ground. Debouncing is done in the software.
struct DEV_DOORWINDOW : Service::ContactSensor {
  const unsigned long millis_debounce = 50;

  static std::vector<std::pair<uint8_t, DEV_DOORWINDOW *>> all_sensors;

  static int pin_value_to_state(int val) {
    return val == LOW ? Characteristic::ContactSensorState::DETECTED
                      : Characteristic::ContactSensorState::NOT_DETECTED;
  }

  static void isr() {
    unsigned long millis_now = millis();
    for (auto it = all_sensors.begin(); it != all_sensors.end(); it++) {
      uint8_t new_value = digitalRead(it->first);
      if (it->second->value != new_value) {
        it->second->value = new_value;
        it->second->need_to_update = true;
        it->second->millis_last = millis_now;
      }
    }
  }

  uint8_t pin;
  SpanCharacteristic *state;

  // These values are written in the ISR. So they need to be volatile.
  volatile int value;
  volatile unsigned long millis_last;
  volatile bool need_to_update;

  DEV_DOORWINDOW(uint8_t pin) : Service::ContactSensor() {
    state = new Characteristic::ContactSensorState();
    this->pin = pin;
    pinMode(pin, INPUT_PULLUP);

    // Report initial state
    value = digitalRead(pin);
    need_to_update = true;
    millis_last = millis();

    all_sensors.push_back(std::pair<uint8_t, DEV_DOORWINDOW *>(pin, this));
    attachInterrupt(pin, isr, CHANGE);
  }

  void loop() {
    unsigned long millis_now = millis();
    // millis() overflows after about 50 days. So, if millis_now < millis_last,
    // that's what happened.
    unsigned long millis_lapsed =
        millis_now >= millis_last ? millis_now - millis_last
                                  : 0xFFFFFFFF - (millis_last - millis_now) + 1;

    if (need_to_update && millis_lapsed > millis_debounce) {
      // We only update if
      //   * An interrupt has occured since the last update, or we have never
      //   updated since booting, reflected by need_to_update.
      //   * Enough time has passed since the last interrupt, for debouncing.
      // And we read the pin again here since we could have missed an interrupt
      // or two due to the bouncing.
      need_to_update = false;
      value = digitalRead(pin);
      Serial.printf("---- Updating pin %d's value to %d\n", pin, value);
      state->setVal(pin_value_to_state(value));
    }
  }
};

std::vector<std::pair<uint8_t, DEV_DOORWINDOW *>> DEV_DOORWINDOW::all_sensors;

void setup() {
  Serial.begin(115200);

  homeSpan.setStatusPin(26).setControlPin(27).begin(Category::Bridges,
                                                    "Doors and Windows");

  new SpanAccessory();
  new Service::AccessoryInformation();
  new Characteristic::Identify();

  new SpanAccessory();
  new Service::AccessoryInformation();
  new Characteristic::Identify();
  new Characteristic::Name("Front Door");
  new DEV_DOORWINDOW(13);

  new SpanAccessory();
  new Service::AccessoryInformation();
  new Characteristic::Identify();
  new Characteristic::Name("Garage Door");
  new DEV_DOORWINDOW(4);

  new SpanAccessory();
  new Service::AccessoryInformation();
  new Characteristic::Identify();
  new Characteristic::Name("Patio Door");
  new DEV_DOORWINDOW(25);

  new SpanAccessory();
  new Service::AccessoryInformation();
  new Characteristic::Identify();
  new Characteristic::Name("Backyard Window");
  new DEV_DOORWINDOW(16);

  new SpanAccessory();
  new Service::AccessoryInformation();
  new Characteristic::Identify();
  new Characteristic::Name("Bathoom Window");
  new DEV_DOORWINDOW(17);

  new SpanAccessory();
  new Service::AccessoryInformation();
  new Characteristic::Identify();
  new Characteristic::Name("Den Window");
  new DEV_DOORWINDOW(18);

  new SpanAccessory();
  new Service::AccessoryInformation();
  new Characteristic::Identify();
  new Characteristic::Name("Bay Window");
  new DEV_DOORWINDOW(19);

  new SpanAccessory();
  new Service::AccessoryInformation();
  new Characteristic::Identify();
  new Characteristic::Name("Picture Window");
  new DEV_DOORWINDOW(21);

  new SpanAccessory();
  new Service::AccessoryInformation();
  new Characteristic::Identify();
  new Characteristic::Name("Kitchen Window");
  new DEV_DOORWINDOW(22);

  new SpanAccessory();
  new Service::AccessoryInformation();
  new Characteristic::Identify();
  new Characteristic::Name("Fireplace L Window");
  new DEV_DOORWINDOW(23);

  new SpanAccessory();
  new Service::AccessoryInformation();
  new Characteristic::Identify();
  new Characteristic::Name("Fireplace R Window");
  new DEV_DOORWINDOW(2);

  homeSpan.autoPoll();
}

void loop() {}
