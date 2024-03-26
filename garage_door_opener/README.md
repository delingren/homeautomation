# Garage Door Opener with ESP32

## Objective

My house has a dumb garage door opener. And I wanted to integrate it with Apple's Home app so that I can monitor its status and operate it remotely. And I don't want to replace with a smart garage door opener or buy any off-the-shelf product.

## Mechanics and hardware

My garage door opener is very rudimentary. The wall mount switch simply shorts two leads. When you press the button, the following logic applies:

* If the door is closed, it starts the motor to open it.
* If the door is open, it starts the motor to close it.
* If the motor is running (opening or closing), it stops the motor.
* If the motor is stopped but the door is half open, it starts the motor opposite of the direction it was previously running.

Also, when it's closing and it detects an obstruction, it reverses the direction of the motor and reverts to an open position.

This is probably how most dumb garage openers work, or at least in a similar fasion. The remote works exactly as the button.

* Dev board  
WiFi ESP WROOM-32
Pinout:  
![pinout](esp32-38pin.png)

The brain of the project is ESP32 MCU. I am using this MCU for the following reasons:

* It has built-in WiFi capability.
* There is a HAP library in Arduino framework.
* It is cheap.

To operate the door, I simply use a relay to simulate a button press. The relay is controled with a GPIO pin. The state of the garage door is detected by two reed switches, one placed at the open position (upper) and one placed at the closed position (lower). A magnet is mounted on the shuttle of the opener and the switches are mounted on the track. 

* Relay  
I am using this [5v Relay Module](https://www.amazon.com/dp/B00VRUAHLE). But any 5V DC relay with one normally open channel will do.

* Sensors  
I am using two [reed switches](https://www.amazon.com/gp/product/B0735BP1K4/). But any normally open switches will do.

* Misc

- Status LED
- Push button
- Reset button

## Sofwtare

### Development settings  
* Arduino IDE
* HomeSpan Library

### Design

There are two relatively independent components:
* State determination (detecting the current state of the door)
* Operations (opening and closing the door)

Since the door can be manually closed and opened, we should not use the operations to change the state. Rather, the state should be purely determined by the sensors. Let's call the upper sensor U and the lower sensor L. U is low if the door is closed. L is low if the door is closed.

We have the following 5 defined states, which are all recognized in [HAP](https://hexdocs.pm/hap/HAP.Characteristics.CurrentDoorState.html).

* OPEN: U=0, L=1
* CLOSED: U=1, L=0
* OPENING: U=1, L=1, previous state=CLOSED
* CLOSING: U=1, L=1, previous state=OPEN
* STOPPED: U=1, L=1, previous state \in {OPENING, CLOSING} and a certain period of time has passed since the last state transition (i.e. a timeout has happened)

When the program starts, we check if the state is OPEN or CLOSED. If both U and L are 1, we assume state = STOPPED, until an event changes it to another state.

The way my garage door operates, while the door is being opened or closed, two things can happen.

* A human presses the wall mount button or a remote and stops the door. 
* The door hits an obstacle and reverses its direction while closing

Therefore, we need to set a timeout after transitioning to OPENING or CLOSING states. After the timeout, we should transition to STOPPED state. Every time we transition to OPENING or CLOSING state, we start a timer. When the timer runs out, a timeout event is triggered. When we transition to OPEN, CLOSED, or STOPPED state, the timer stops.

There have 4 events driven by the sensors and timeout.

* U 1->0 (UF)
* U 0->1 (UR)
* L 1->0 (LF)
* L 0->1 (LR)
* Timeout (TO)

Instead of drawing a state machine, I'm just listing all the transitions in the following table.

```
        |   UR   |   UF   |   LR   |   LF   |   TO   | 
--------+--------+--------+--------+--------+--------+
OPEN    |closing |        |        |        |        |
CLOSED  |        |        |OPENING |        |        |
OPENING |        |  OPEN  |        | CLOSED |STOPPED |
CLOSING |        |  OPEN  |        | CLOSED |STOPPED |
STOPPED |        |  OPEN  |        | CLOSED |        |
--------+--------+--------+--------+--------+--------+
```

All empty cells should be invalid transitions, as far as our unit is concerned. Some are due to lack of information. E.g. the door could transition from STOPPED to both OPENING or CLOSING. But since such operations don't go through our unit, we have no way to detect. So we stay in STOPPED until the opening or closing finishes and triggers a transition to OPEN or CLOSED.

### Test cases
