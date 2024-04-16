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

#include "acurite.h"
#include "door_window.h"
#include "src/HomeSpan.h"

#include <utility>

void setup() {
  Serial.begin(115200);

  homeSpan.enableOTA();
  homeSpan.setSketchVersion("1.01");
  homeSpan.setStatusPin(2).begin(Category::Bridges, "Walk-in Closet",
                                 "WALK-IN-CLOSET");

  new SpanAccessory();
  new Service::AccessoryInformation();
  new Characteristic::Identify();

  new SpanAccessory();
  new Service::AccessoryInformation();
  new Characteristic::Identify();
  new Characteristic::Name("Fireplace L");
  new DEV_DoorWindow(32);

  new SpanAccessory();
  new Service::AccessoryInformation();
  new Characteristic::Identify();
  new Characteristic::Name("Picture Window");
  new DEV_DoorWindow(33);

  new SpanAccessory();
  new Service::AccessoryInformation();
  new Characteristic::Identify();
  new Characteristic::Name("Fireplace R");
  new DEV_DoorWindow(25);

  new SpanAccessory();
  new Service::AccessoryInformation();
  new Characteristic::Identify();
  new Characteristic::Name("Den Window");
  new DEV_DoorWindow(26);

  new SpanAccessory();
  new Service::AccessoryInformation();
  new Characteristic::Identify();
  new Characteristic::Name("Backyard Window");
  new DEV_DoorWindow(27);

  new SpanAccessory();
  new Service::AccessoryInformation();
  new Characteristic::Identify();
  new Characteristic::Name("Kitchen Window");
  new DEV_DoorWindow(14);

  new SpanAccessory();
  new Service::AccessoryInformation();
  new Characteristic::Identify();
  new Characteristic::Name("Bay Window");
  new DEV_DoorWindow(12);

  new SpanAccessory();
  new Service::AccessoryInformation();
  new Characteristic::Identify();
  new Characteristic::Name("Front Door");
  new DEV_DoorWindow(13);

  new SpanAccessory();
  new Service::AccessoryInformation();
  new Characteristic::Identify();
  new Characteristic::Name("Patio Door");
  new DEV_DoorWindow(22);

  new SpanAccessory();
  new Service::AccessoryInformation();
  new Characteristic::Identify();
  new Characteristic::Name("Bathroom Window");
  new DEV_DoorWindow(21);

  new SpanAccessory();
  new Service::AccessoryInformation();
  new Characteristic::Identify();
  new Characteristic::Name("Garage Entry");
  new DEV_DoorWindow(19);

  // Pin 18 is connected on the PCB but commented out since it's not being used.
  // new SpanAccessory();
  // new Service::AccessoryInformation();
  // new Characteristic::Identify();
  // new Characteristic::Name("18");
  // new DEV_DoorWindow(18);

  new SpanAccessory();
  new Service::AccessoryInformation();
  new Characteristic::Identify();
  new Characteristic::Name("Master Bathroom");
  // 0x03 = Channel A, 0x02 = Channel B, 0x00 = Channel C
  // 0x2B35 = 592TXR
  new DEV_Acurite(0x03, 0x2B35);
  DEV_Acurite::setPin(23);

  homeSpan.autoPoll();
}

void loop() {}
