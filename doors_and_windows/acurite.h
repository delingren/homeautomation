#include "src/HomeSpan.h"

// We only support one instance of this struct. Otherwise we would need to
// distinguish the instances in the interrupt handler.
struct DEV_Acurite : Service::TemperatureSensor {
  static DEV_Acurite *singleton;

  Characteristic::CurrentTemperature temperature;
  Characteristic::CurrentRelativeHumidity humidity;
  Characteristic::StatusLowBattery battery;

  constexpr static int sync_pulse = 632;
  constexpr static int sync_pulse_tolerance = 350;
  constexpr static int sync_gap = 580;
  constexpr static int sync_gap_tolerance = 350;
  constexpr static int reset_gap = 2180;
  constexpr static int reset_gap_tolerance = 200;
  constexpr static int cancelation_limit = 5000;

  constexpr static int bytes_per_packet = 7;
  constexpr static int bits_per_packet = 8 * bytes_per_packet;
  constexpr static int packets_per_transmission = 3;

  uint8_t payload[packets_per_transmission][bytes_per_packet];

  volatile int synced_edges;
  volatile unsigned long time_last;
  volatile int packet_count;
  volatile int bit_count;
  volatile bool transmission_ended;

  uint8_t pin_rx;

  DEV_Acurite(uint8_t pin) {
    if (singleton != nullptr) {
      LOG0("** Error: we only support one instance of DEV_Acurite **");
    }
    pin_rx = pin;
    temperature.setRange(-50, 100);
    reset_transmission();
    singleton = this;
  }

  void reset_transmission() {
    pinMode(pin_rx, INPUT);
    time_last = micros();
    synced_edges = 0;
    packet_count = 0;
    bit_count = 0;
    transmission_ended = false;
    attachInterrupt(digitalPinToInterrupt(pin_rx), isr, CHANGE);
  }

  // 1: too long, -1: too short, 0: matched within tolerance
  static int match_timing(unsigned int timing1, unsigned int timing2,
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

  static bool parity_check(uint8_t data) {
    uint8_t parity = 0;
    for (int i = 0; i < 8; i++) {
      parity ^= (data >> i & 0x01);
    }
    return parity == 0;
  }

  static bool parse_packet(uint8_t *packet, double &temp, int &hum,
                           bool &batt) {
    // clang-format off
    // | Byte 0    | Byte 1    | Byte 2    | Byte 3    | Byte 4    | Byte 5    | Byte 6    |
    // | --------- | --------- | --------- | --------- | --------- | --------- | --------- |
    // | CCII IIII | IIII IIII | pB00 0100 | pHHH HHHH | p??T TTTT | pTTT TTTT | KKKK KKKK |
    // clang-format on

    batt = (packet[2] >> 6) & 0x01;
    hum = packet[3] & 0x7F;
    temp = (((packet[4] << 7) & 0x0780 | packet[5] & 0x007F) - 1000.0) / 10.0;

    uint8_t sum = 0;
    for (int i = 0; i < 6; i++) {
      sum += packet[i];
    }

    return parity_check(packet[2]) && parity_check(packet[3]) &&
           parity_check(packet[4]) && parity_check(packet[5]) &&
           sum == packet[6];
  }

  static void isr() { singleton->interrupt_handler(); }

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

  void loop() override {
    if (!transmission_ended && (packet_count > 0 || synced_edges > 0) &&
        micros() - time_last > cancelation_limit) {
      transmission_ended = true;
    }

    if (transmission_ended) {
      detachInterrupt(pin_rx);
      LOG1("Received %d packet(s).\n", packet_count);
      for (int i = 0; i < packet_count; i++) {
        LOG1("  Raw bytes: ");
        for (int j = 0; j < bytes_per_packet; j++) {
          LOG1("%02X ", payload[i][j]);
        }
        LOG1("\n");

        int hum;
        double temp;
        bool batt;
        if (parse_packet(payload[i], temp, hum, batt)) {
          LOG1("  Temperature: %.1f C, humidity: %d%%, battery: %s.\n", temp,
               hum, batt ? "OK" : "LOW");
          temperature.setVal(temp);
          humidity.setVal(hum);
          battery.setVal(batt
                             ? Characteristic::StatusLowBattery::NOT_LOW_BATTERY
                             : Characteristic::StatusLowBattery::LOW_BATTERY);
          // The 3 packets in a transmission should be identical. Break
          // after finding the first good one.
          break;
        }
      }

      reset_transmission();
    }
  }
};

DEV_Acurite *DEV_Acurite::singleton = nullptr;