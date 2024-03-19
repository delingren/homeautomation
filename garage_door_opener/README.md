# Garage Door Opener with ESP32

## Objective

My house has a dumb garage door opener. It works in a very simple way. The wall mount switch simply shorts two leads. When you press the button, if the motor is running (the door is being opened or closed), it stops the motor; otherwise, it starts the motor in the opposite direction. A remote control works the same way. This is probably how most dumb garage openers work.

I wanted to integrate it with Apple's Home app so that I can operate it from my phone even if I'm not home and monitor its status as well.

## Mechanics and hardware

* Microcontroller  
WiFi ESP WROOM-32  
Pinout:  
![pinout](esp32-38pin.png)

The brain of the project is ESP32 MCU. I am using this MCU for the following reasons:

* It has built-in WiFi capability.
* There is a HAP library in Arduino framework.
* It is cheap.

To operate the door, I simply use a relay to simulate a button press. The relay is controled with a GPIO pin. The state of the garage door is detected by two reed switches, one placed at the open position and one placed at the closed position. A magnet is mounted on the shuttle of the opener and the switches are mounted on the track. 

* Relay  
I am using a [2 Channel DC 5V Relay Module](https://www.amazon.com/gp/product/B00E0NTPP4). But any 5V DC relay with one normally open channel will do.

* Sensors  
I am using two [reed switches](https://www.amazon.com/gp/product/B0735BP1K4/). But any normally open switches will do.

* LEDs and resistors  
I am using two LEDs to indicate the status of the sensors. I also use them to signal errors. They are not necessary. But they make troubleshooting easier.

Sensors:
```
   +- GPIO22
S1 |
   +- GND

   +- GPIO23
S2 |
   +- GND
```

Relay:
```
    +- GPIO13
    |
RY1 +- +5v
    |
    +- GND 
```

LEDs:
```
     +- R1 - GPIO 18
LED1 |
     +- GND

     +- R2 - GPIO 5
LED2 |
     +- GND

```

## Sofwtare

### Development settings  
* Arduino IDE, board: uPesy ESP32 Wroom DevKit.
* WiFi Library
* HomeSpan Library

### Design

There are two relatively independent components:
* State determination (detecting the current state of the door)
* Operation (opening and closing the door)

Since the door can be manually closed and opened, we should not use the operations to change the state. Rather, the state should be purely determined by the sensors. Let's call the upper sensor U and the lower sensor L. U is high if the door is closed. L is high if the door is closed.

We have the following 5 defined states, which are all recognized in [HAP](https://hexdocs.pm/hap/HAP.Characteristics.CurrentDoorState.html).

* Open: U=1, L=0
* Closed: U=0, L=1
* Opening: U=0, L=0, previous state=closed
* Closing: U=0, L=0, previous state=open
* Stopped: U=0, L=0, previous state \in {opening, closing} and a certain period of time has passed since the last state transition (i.e. a timeout has happened)

When the program starts, we check if the state is open or closed. If both U and L are 0, we assume state = stopped, until an event changes it to another state.

The way my garage door operates, while the door is being opened or closed, two things can happen.

* A human presses the wall mount button or a remote and stops the door. 
* The door hits an obstacle and reverses its direction.

Therefore, we need to set a timeout after transitioning to opening or closing states. After the timeout, we should transition to unknown state.

We have 4 events driven by the sensors and timeout. 

* U 1->0 (UF)
* U 0->1 (UR)
* L 1->0 (LF)
* L 0->1 (LR)
* Timeout (TO)

### Test cases
