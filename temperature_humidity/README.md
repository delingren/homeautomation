# Temperature and Humidity Sensor

## Goal
* Decode temperature and humidity data from an AcuRite weather station with an ESP32.


## Hardware
* The sensor I bought: * Model: 592TXR [AcuRite Wireless Indoor Outdoor Temperature and Humidity Sensor (06002M)](https://www.amazon.com/dp/B00T0K8NXC). It transfers data over the [standard LPD433 band](https://en.wikipedia.org/wiki/LPD433).
* 433 Mhz receiver.
* Microcontroller. I already have an ESP32 running to monitor the contact sensors on the doors and windows. So I'm piggybacking on that MCU.

## RF signal format
I came across this [blog post](https://rayshobby.net/wordpress/reverse-engineer-wireless-temperature-humidity-rain-sensors-part-1/) where the author reverse engineered the data format.

https://www.techspin.info/archives/1049

Reverse engineering the data protocol was the more difficult part of the whole effort. I captured multiple bit streams and some patterns were immediately obvious. There was random looking data followed by a consistent pattern followed by a series of wide and short pulses. Eventually I figured out the random pulses at the start of data must be for radio synchronization between the transmitter and receiver. None of the “random” data at the start of the bit stream was consistent between any runs and I ended up simply chopping it off and ignoring it in the data stream.

After the random bits there is a low pulse of varying length followed by 4 data sync pulses. The data sync pulses are 1.2 msec long, 50% duty cycle with 0.61 msec high and 0.61 msec low. Immediately after the 4 data sync pulses are 56 data bit pulses. Each data bit pulse is ~0.6 msec long. A logic high (1) bit is encoded as a 0.4 msec high pulse followed by a 0.2 msec low pulse. A logic low (0) bit is encoded as a 0.2 msec high followed by a 0.4 msec low.

Transmission order: MSB first

First 2 bits depend on the switch position
A: 11
B: 10
C: 00

The next 18 bits (6 bits of byte 0 plus bytes 1 and 2) are always 101011 00110101 01000100 in my case.
The next byte is related to humidity. The lowest 7 bits represent the percentage. The highest bit is an even parity bit.
The next 2 bytes (bytes 4 & 5) are related to temperature. Lowest 4 bits of
Byte 6 is the checksum.

00000000 11111111 22222222 33333333 44444444 55555555 66666666
--****** ******** ******** phhhhhhh ----tttt pttttttt ssssssss

/**
Acurite 592TXR Temperature Humidity sensor decoder.

Also:
- Acurite 592TX (without humidity sensor)

Message Type 0x04, 7 bytes

| Byte 0    | Byte 1    | Byte 2    | Byte 3    | Byte 4    | Byte 5    | Byte 6    |
| --------- | --------- | --------- | --------- | --------- | --------- | --------- |
| CCII IIII | IIII IIII | pB00 0100 | pHHH HHHH | p??T TTTT | pTTT TTTT | KKKK KKKK |


- C: Channel 00: C, 10: B, 11: A, (01 is invalid)
- I: Device ID (14 bits)
- B: Battery, 1 is battery OK, 0 is battery low (observed low < 2.5V)
- M: Message type (6 bits), 0x04
- T: Temperature Celsius (11 - 14 bits?), + 1000 * 10
- H: Relative Humidity (%) (7 bits)
- K: Checksum (8 bits)
- p: Parity bit

Notes:

- Temperature
  - Encoded as Celsius + 1000 * 10
  - only 11 bits needed for specified range -40 C to 70 C (-40 F - 158 F)
  - However 14 bits available for temperature, giving possible range of -100 C to 1538.4 C
  - @todo - check if high 3 bits ever used for anything else

*/

## Receiver & Antenna

No antenna
MX-RM-5V
  5cm: 2 packets and no errors
  30cm: no reception at all
WL433
  5cm: 3 packets with some parity and checksum errors
  30cm: 3 packets with a lot of parity and checksum errors
  2m: no reception or 0 packets (i.e. errored out during transmission)  
RXB6
  5cm: 3 packets and no errors
  30cm: 3 packets and no errors
  2m: 3 packets with occasional errors

Coiled antenna
MX-RM-5V
  2m: no reception
WL433
  2m: 3 packets with a lof of errors
RXB6
  2m: 3 packets with occasional errors
  8m /w a wall: 3 packets with occasional errors, acceptable
  12m /w a wall: 3 packets with a lot of errors

17cm wire antenna
WL433
  2m: 3 packets with a lot of errors
RXB6
  2m: 3 packets with occasional errors
  8m /w a wall: 3 packets with occasional errors
  12m /w a wall: 3 packets with occasional errors, acceptable
  15m w/ 2 walls: no reception or 3 packets with many errors. however, it greatly improves when powered by 5v

RXB6 is the winner. With a coiled antenna, 8m w/ a wall is acceptable.

## Software

## Resources
https://www.osengr.org/WxShield/Downloads/Weather-Sensor-RF-Protocols.pdf
https://wiki.jmehan.com/display/KNOW/Reverse+Engineering+Acurite+Temperature+Sensor
https://www.techspin.info/archives/1049
https://rayshobby.net/wordpress/reverse-engineer-wireless-temperature-humidity-rain-sensors-part-1/
https://github.com/merbanan/rtl_433/blob/master/src/devices/acurite.c