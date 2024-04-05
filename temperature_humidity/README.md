# Temperature and Humidity Sensor

## Goal
* Decode temperature and humidity data from an AcuRite weather station with an ESP32.

## Hardware
* The sensor I bought: [AcuRite Wireless Indoor Outdoor Temperature and Humidity Sensor (06002M)](https://www.amazon.com/dp/B00T0K8NXC). It transfers data over the [standard LPD433 band](https://en.wikipedia.org/wiki/LPD433).
* 433 Mhz receiver.
* Microcontroller. I already have an ESP32 running to monitor the contact sensors on the doors and windows. So I'm piggybacking on that MCU.

## RF signal format
I came across this [blog post](https://rayshobby.net/wordpress/reverse-engineer-wireless-temperature-humidity-rain-sensors-part-1/) where the author reverse engineered the data format.

## Software
