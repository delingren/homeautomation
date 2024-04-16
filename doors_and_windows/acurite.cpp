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
#include "src/HomeSpan.h"
#include <map>

namespace {
constexpr int sync_pulse = 632;
constexpr int sync_pulse_tolerance = 350;
constexpr int sync_gap = 580;
constexpr int sync_gap_tolerance = 350;
constexpr int reset_gap = 2180;
constexpr int reset_gap_tolerance = 200;
constexpr int cancelation_limit = 5000;

constexpr int bytes_per_packet = 7;
constexpr int bits_per_packet = 8 * bytes_per_packet;
constexpr int packets_per_transmission = 3;

uint8_t pin_rx = 0;
std::map<uint16_t, DEV_Acurite *> devices;
uint8_t payload[packets_per_transmission][bytes_per_packet];

volatile int synced_edges;
volatile unsigned long time_last;
volatile int packet_count;
volatile int bit_count;
volatile bool transmission_ended;

// 1: too long, -1: too short, 0: matched within tolerance
int match_timing(unsigned int timing1, unsigned int timing2,
                 unsigned int tolerance) {
  if (timing1 > timing2 + tolerance) {
    return 1;
  } else if (timing1 < timing2 - tolerance) {
    return -1;
  } else {
    return 0;
  }
}

void write_paylaod_bit(int packet, int bit, uint8_t data) {
  if (packet >= packets_per_transmission || bit >= bits_per_packet) {
    LOG0("** Error: Out of range. **");
    return;
  }

  int byte = bit / 8;
  int bit_in_byte = 7 - (bit % 8);

  if (data == 0) {
    payload[packet][byte] &= ~(1 << bit_in_byte);
  } else {
    payload[packet][byte] |= 1 << bit_in_byte;
  }
}

bool parity_check(uint8_t data) {
  uint8_t parity = 0;
  for (int i = 0; i < 8; i++) {
    parity ^= (data >> i & 0x01);
  }
  return parity == 0;
}

bool parse_packet(uint8_t *packet, uint16_t &id, double &temperature,
                  double &humidity, bool &battery_ok) {
  // clang-format off
  // | Byte 0    | Byte 1    | Byte 2    | Byte 3    | Byte 4    | Byte 5    | Byte 6    |
  // | --------- | --------- | --------- | --------- | --------- | --------- | --------- |
  // | CCII IIII | IIII IIII | pB00 0100 | pHHH HHHH | p??T TTTT | pTTT TTTT | KKKK KKKK |
  // clang-format on

  id = packet[0] << 8 | packet[1];
  battery_ok = (packet[2] >> 6) & 0x01;
  humidity = packet[3] & 0x7F;
  temperature =
      (((packet[4] << 7) & 0x0780 | packet[5] & 0x007F) - 1000.0) / 10.0;

  uint8_t sum = 0;
  for (int i = 0; i < 6; i++) {
    sum += packet[i];
  }

  return parity_check(packet[2]) && parity_check(packet[3]) &&
         parity_check(packet[4]) && parity_check(packet[5]) && sum == packet[6];
}

void interrupt_handler() {
  static unsigned int current_bit_pulse;

  if (transmission_ended) {
    return;
  }

  unsigned int value = digitalRead(pin_rx);
  unsigned long time = micros();
  unsigned long duration = time - time_last;
  time_last = time;

  if (value == HIGH) {
    // Triggered on a rising edge. Duration is the width of the last gap.
    if (duration > cancelation_limit && synced_edges > 0) {
      // The gap is too long for any valid data.
      transmission_ended = true;
      return;
    }

    if (0 == match_timing(duration, reset_gap, reset_gap_tolerance)) {
      // We detected a gap between packets.
      synced_edges = 0;
      bit_count = 0;
      return;
    }

    if (synced_edges < 8) {
      if (0 == match_timing(duration, sync_gap, sync_gap_tolerance)) {
        if (++synced_edges == 8) {
          // Starting reading payload bits
          bit_count = 0;
        }
      } else {
        synced_edges = 0;
      }
      return;
    }

    // We are reading the payload
    uint8_t data = current_bit_pulse > duration ? 1 : 0;
    write_paylaod_bit(packet_count, bit_count++, data);
    if (bit_count == bits_per_packet) {
      // We have received all the bits in the packet.
      synced_edges = 0;
      bit_count = 0;
      if (++packet_count == packets_per_transmission) {
        transmission_ended = true;
      }
    }
  } else {
    // Triggered on a falling edge. Duration is the width of the last pulse.
    if (synced_edges < 8) {
      if (0 == match_timing(duration, sync_pulse, sync_pulse_tolerance)) {
        synced_edges++;
      } else {
        synced_edges = 0;
      }
      return;
    }

    // We are reading the payload
    current_bit_pulse = duration;
  }
}

void reset_transmission() {
  time_last = micros();
  synced_edges = 0;
  packet_count = 0;
  bit_count = 0;
  transmission_ended = false;
}

void process_packets() {
  if (!transmission_ended && (packet_count > 0 || synced_edges > 0) &&
      micros() - time_last > cancelation_limit) {
    transmission_ended = true;
  }

  if (transmission_ended) {
    if (packet_count > 0) {
      LOG1("Received %d packet(s).\n", packet_count);
    }
    for (int i = 0; i < packet_count; i++) {
      LOG1("  Raw bytes: ");
      for (int j = 0; j < bytes_per_packet; j++) {
        LOG1("%02X ", payload[i][j]);
      }
      LOG1("\n");

      double humidity;
      double temperature;
      bool battery_ok;
      uint16_t id;
      if (parse_packet(payload[i], id, temperature, humidity, battery_ok)) {
        if (auto search = devices.find(id); search != devices.end()) {
          search->second->data_available(temperature, humidity, battery_ok);
        } else {
          LOG1("  No sensor associated with %X\n", id);
        }
        // The 3 packets in a transmission should be identical. Break
        // after finding the first good one.
        break;
      }
    }

    reset_transmission();
  }
}
} // namespace

DEV_Acurite::DEV_Acurite(uint8_t channel, uint16_t device_id) {
  id = channel << 14 | device_id;
  temperature.setRange(-50, 100);

  first_instance = devices.empty();
  devices[id] = this;
}

void DEV_Acurite::setPin(uint8_t pin) {
  if (pin_rx != 0) {
    LOG0("Do not call setPin() more than once");
  }
  pin_rx = pin;
  pinMode(pin_rx, INPUT);
  attachInterrupt(digitalPinToInterrupt(pin_rx), interrupt_handler, CHANGE);
}

void DEV_Acurite::loop() {
  if (first_instance) {
    process_packets();
  }
}

void DEV_Acurite::data_available(double temperature, double humidity,
                                 bool battery_ok) {
  LOG1("  Temperature: %.1f C, humidity: %d%%, battery: %s.\n", temperature,
       int(humidity), battery_ok ? "OK" : "LOW");

  this->temperature.setVal(temperature);
  this->humidity.setVal(humidity);
  this->battery.setVal(battery_ok
                           ? Characteristic::StatusLowBattery::NOT_LOW_BATTERY
                           : Characteristic::StatusLowBattery::LOW_BATTERY);
}