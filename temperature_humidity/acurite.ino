uint8_t pin_rx = 4;

const unsigned int sync_pulse = 632;
const unsigned int sync_pulse_tolerance = 350;
const unsigned int sync_gap = 580;
const unsigned int sync_gap_tolerance = 350;
const unsigned int reset_gap = 2180;
const unsigned int reset_gap_tolerance = 200;
const unsigned int cancelation_limit = 5000;

const unsigned int bytes_per_packet = 7;
const unsigned int bits_per_packet = 8 * bytes_per_packet;
const unsigned int packets_per_transmission = 3;

uint8_t payload[packets_per_transmission][bytes_per_packet];

volatile int synced_edges = 0;
volatile unsigned long time_last = 0;
volatile int packet_count = 0;
volatile int bit_count = 0;
volatile bool transmission_ended = false;

volatile int synced_times = 0;

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
    Serial.println("Out of range.");
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

void print_packet(uint8_t *packet) {
  // | Byte 0    | Byte 1    | Byte 2    | Byte 3    | Byte 4    | Byte 5    |
  // Byte 6    | | --------- | --------- | --------- | --------- | --------- |
  // --------- | --------- | | CCII IIII | IIII IIII | pB00 0100 | pHHH HHHH |
  // p??T TTTT | pTTT TTTT | KKKK KKKK |

  uint8_t channel = (packet[0] >> 6) & 0x03;
  uint16_t device_id = ((packet[0] << 8) | packet[1]) & 0x3FFF;
  bool battery_ok = (packet[2] >> 6) & 0x01;
  uint8_t message_type = packet[2] & 0x3F;
  uint8_t humidity = packet[3] & 0x7F;
  float temperature =
      (((packet[4] << 7) & 0x0780 | packet[5] & 0x007F) - 1000.0) / 10.0;

  uint8_t sum = 0;
  for (int i = 0; i < 6; i++) {
    sum += packet[i];
  }

  Serial.print("  ");
  Serial.print(channel);
  Serial.print(" ");
  Serial.print(device_id);
  Serial.print(" ");
  Serial.print(battery_ok);
  Serial.print(" ");
  Serial.print(humidity);
  Serial.print(" ");
  Serial.print(temperature);
  Serial.print(" ");
  Serial.print(sum, HEX);

  if (!parity_check(packet[2])) {
    Serial.print(" Byte 2 parity error.");
  }
  if (!parity_check(packet[3])) {
    Serial.print(" Byte 3 parity error.");
  }
  if (!parity_check(packet[4])) {
    Serial.print(" Byte 4 parity error.");
  }
  if (!parity_check(packet[5])) {
    Serial.print(" Byte 5 parity error.");
  }

  if (sum != packet[6]) {
    Serial.print(" checksum error");
  } else {
    Serial.print(" checksum ok");
  }

  Serial.println();
}

void begin() {
  time_last = micros();
  synced_edges = 0;
  packet_count = 0;
  bit_count = 0;
  transmission_ended = false;
  attachInterrupt(digitalPinToInterrupt(pin_rx), isr, CHANGE);
}

void isr() {
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
          synced_times++;
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

void setup() {
  Serial.begin(115200);
  begin();
}

void loop() {
  if (!transmission_ended && (packet_count > 0 || synced_edges > 0) &&
      micros() - time_last > cancelation_limit) {
    transmission_ended = true;
  }

  if (transmission_ended) {
    detachInterrupt(pin_rx);
    Serial.print("Received packets: ");
    Serial.println(packet_count);
    for (int i = 0; i < packet_count; i++) {
      Serial.print("  ");
      for (int j = 0; j < bytes_per_packet; j++) {
        Serial.print(payload[i][j], HEX);
        Serial.print(" ");
      }
      Serial.println();
      print_packet(payload[i]);
    }
    begin();
  }

  delay(539);
}
