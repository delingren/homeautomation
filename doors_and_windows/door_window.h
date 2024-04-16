/*******************************************************************************
The MIT License (MIT)

Copyright (c) 2024 Deling Ren

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*******************************************************************************/

#include "src/HomeSpan.h"

// Represents a contact sensor typically used by security systems to monitor the
// status of doors and windows. The sensor is connected to a pullup input pin
// and the ground. When it's open, the pin is pulled up high. When it's closed,
// it's pulled to the ground. Debouncing is done in the software.
struct DEV_DoorWindow : Service::ContactSensor {
  const unsigned long millis_debounce = 100;

  static int pin_value_to_state(int val) {
    return val == LOW ? Characteristic::ContactSensorState::DETECTED
                      : Characteristic::ContactSensorState::NOT_DETECTED;
  }

  Characteristic::ContactSensorState state;

  SpanToggle *toggle;

  DEV_DoorWindow(uint8_t pin) : Service::ContactSensor() {
    pinMode(pin, INPUT_PULLUP);

    // Report initial state
    uint8_t value = digitalRead(pin);
    LOG1("Initial pin %d state: %d\n", pin, value);
    state.setVal(pin_value_to_state(value));

    toggle = new SpanToggle(pin, PushButton::TRIGGER_ON_LOW, millis_debounce);
  }

  ~DEV_DoorWindow() {
    if (toggle != nullptr) {
      delete toggle;
    }
  }

  void button(int pin, int type) override {
    uint8_t value = type == SpanButton::OPEN ? HIGH : LOW;
    LOG1("Updating pin %d's state to %d\n", pin, value);
    state.setVal(pin_value_to_state(value));
  }
};
