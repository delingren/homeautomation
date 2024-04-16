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

// Acurite 592TXR 3 channel sensor
// Reference:
// https://github.com/merbanan/rtl_433/blob/master/src/devices/acurite.c

struct DEV_Acurite : Service::TemperatureSensor {
  Characteristic::CurrentTemperature temperature;
  Characteristic::CurrentRelativeHumidity humidity;
  Characteristic::StatusLowBattery battery;

  uint16_t id;
  bool first_instance = false;

  DEV_Acurite(uint8_t channel, uint16_t device_id);

  static void setPin(uint8_t pin_rx);
  void loop() override;
  void data_available(double temperature, double humidity, bool batt_ok);
};
