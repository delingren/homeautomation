# Temperature and Humidity Sensor

## Goal
* Decode temperature and humidity data from an AcuRite weather station with an ESP32.

## Hardware
* The sensor I bought: * Model: 592TXR [AcuRite Wireless Indoor Outdoor Temperature and Humidity Sensor (06002M)](https://www.amazon.com/dp/B00T0K8NXC). It transfers data over the [standard LPD433 band](https://en.wikipedia.org/wiki/LPD433).
* 433 Mhz receiver.
* Microcontroller. I already have an ESP32 running to monitor the contact sensors on the doors and windows. So I'm piggybacking on that MCU.

## RF signal format
Two things need to be figured out:

* Understand the waveform
* Decode the bits

I used a RTL-SDR and rtl_433 to capture the [waveform](https://triq.org/pdv/#AAB04D0701025C018C00EC00B4087C058827148080808093A193A193A19393A1A19393A193A193A193A1A1A193A1A1A1A193A19393A193A1A1A1A193A1A1939393A193A193A1A1A1A193A1939393A1A455+AAB04D0701025C018C00EC00B4087C058827148080808093A193A193A19393A1A19393A193A193A193A1A1A193A1A1A1A193A19393A193A1A1A1A193A1A1929392A193A193A1A1A1A193A1939293A1A455+AAB04D0701025C018C00EC00B4087C058827148080808093A192A192A19393A1A19393A193A193A193A1A1A193A1A1A1A193A19293A193A1A1A1A192A1A1929393A193A193A1A1A1A193A1939392A1A555+AAB0110701025C018C00EC00B4087C05882714A655). Then I copied the logic from the [Acurite plugin for rtl_433](https://github.com/merbanan/rtl_433/blob/master/src/devices/acurite.c) to get temperature and humidity data from the bits.

In summary:

```
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
```

There have been numerous independent efforts to revserse engineer this particular sensor. I'm listing some of them that I have read in the reference.


## Receiver & Antenna
I tried a few cheap RF receivers with different (or no) antennas.

* No antenna
  - MX-RM-5V
    - 5cm: 2 packets and no errors
    - 30cm: no reception at all
  -WL433
    - 5cm: 3 packets with some parity and checksum errors
    - 30cm: 3 packets with a lot of parity and checksum errors
    - 2m: no reception or 0 packets (i.e. errored out during transmission)  
  - RXB6
    - 5cm: 3 packets and no errors
    - 30cm: 3 packets and no errors
    - 2m: 3 packets with occasional errors

* Coiled antenna
  - MX-RM-5V
    - 2m: no reception
  - WL433
    - 2m: 3 packets with a lof of errors
  - RXB6
    - 2m: 3 packets with occasional errors
    - 8m /w a wall: 3 packets with occasional errors, acceptable
    - 12m /w a wall: 3 packets with a lot of errors

* 17cm wire antenna
  - WL433
    - 2m: 3 packets with a lot of errors
  - RXB6
    - 2m: 3 packets with occasional errors
    - 8m /w a wall: 3 packets with occasional errors
    - 12m /w a wall: 3 packets with occasional errors, acceptable
    - 15m w/ 2 walls: no reception or 3 packets with many errors. however, it greatly improves when powered by 5v, compared with 3.3v

RXB6 is the winner. With a coiled antenna, 8m w/ a wall is acceptable for my use case.

## References
* https://www.osengr.org/WxShield/Downloads/Weather-Sensor-RF-Protocols.pdf
* https://wiki.jmehan.com/display/KNOW/Reverse+Engineering+Acurite+Temperature+Sensor
* https://www.techspin.info/archives/1049
* https://rayshobby.net/wordpress/reverse-engineer-wireless-temperature-humidity-rain-sensors-part-1/
* https://github.com/merbanan/rtl_433/blob/master/src/devices/acurite.c