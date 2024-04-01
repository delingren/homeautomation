# Smartifying a Garage Door Opener with ESP32

## Objective

My house has a dumb garage door opener. And I wanted to integrate it with Apple's Home app so that I can monitor its status and operate it remotely. And I want to DIY. So here's the design of the control unit.

## Mechanics and hardware

My garage door opener Genie [CM8600](https://www.geniecompany.com/product-support/model-cm8600---legacy-screw-drive-series) is very rudimentary. The wall mount switch simply shorts two leads, one of which is the ground. When you press the button, the following logic applies:

* If the door is closed, it starts the motor to open it.
* If the door is open, it starts the motor to close it.
* If the motor is running (opening or closing), it stops the motor.
* If the motor is stopped but the door is half open, it starts the motor opposite of the direction it was previously running.

Also, when it's closing and it detects an obstruction, it reverses the direction of the motor and reverts to an open position. This is probably how most dumb garage openers work, or at least in a similar fasion. The remote works exactly as the button.

Two limit switches are installed on the track. One for the open position (upper) and one for the closed position (lower). When the carriage hits a limit switch, the opener stops the motor. The limit switches are normally open. One of the leads is the ground. The other one appears to carry a +7.8V DC voltage.

### Microcontroller

The brain of the project is an ESP32-WROOM-32UE MCU. I am using this MCU for the following reasons:

* It has built-in WiFi capability.
* It comes with an external antenna connector.
* There is a HAP library in Arduino framework.
* It's cheap.

Pinout:  
![pinout](esp32-38pin.png)

To operate the door, I simply use a relay to simulate a button press. The relay is controled with a GPIO pin.

### Relay  
I am using this [5v Relay Module](https://www.amazon.com/dp/B00VRUAHLE). But any 5V DC relay with one normally open channel will do.

### Sensors  
For detecting the state of the door, I am piggybacking on the existing limit switches, instead of using my own sensors. The MCU and the opener share the ground. The GPIO pin is connected to the non-ground wire of the limit switch via a diode. When the siwtch is open, the diode prevents the 7.8v from backflowing to the GPIO. Essentially, the pin is free floating. So, it should be configured as a pullup input pin. When the switch closes, the GPIO is shorted to the ground. In practice, I found that there is a ~0.7v voltage drop on the diodes I'm using (1N4007, which is technically a rectifying diode, but I happened to have a whole lot). But that's low enough that the GPIO would still read low.

Ideally, one would want to complately separate the two units. But I'm not too worried since an ESP32 is extremely cheap. And I don't want to deal with the hassle of installing two extra mechanical or magnetic switches and running the wires.

Alternative: I could also use two [reed switches](https://www.amazon.com/gp/product/B0735BP1K4/) and a magnet on the carriage.

### Indicators
To make it easy to visualize the status of the switches, I am using two LEDs, one red and one green, tied to the limit switches. When a switch is closed, the corresponding LED lights up. The anode is connected to +5v and the cathode is connected to the GPIO pin. I'm using the +5v instead of the +3.3v due to that 0.7v drop on the 1N4007, which would reduce 3.3 to ~2.6, a bit low for the LEDs I have on hand. One caveat here is that the +5v is not regulated on the board. So there's a risk of frying the LEDs even with a small increase of voltage. But I using a quality iPhone USB power adapter, so it shouldn't be a problem.

Also, interestingly, in practice, I am only getting ~4.6v from the +5v pin when powered through the USB port. I thought it was directed connected to the Vcc of the USB port? Oh well. The remaining 3.9v is good enough for the LEDs and I'm using very small current limiting resistors (27 Ohm) here.

### Misc
- Status LED. Optionally, HomeSpan uses an LED to indicate its status. I am using a blue LED for this purpose.
- Control button. You can also interact with HomeSpan with a pushbutton.
- Reset button. I am also using a pushbutton for resetting the devices, which is a bit easier than disconnecting the power supply.
- Antenna & U/FL to PR-SMA adapter. Since this device is running in the garage, where the WiFi reception isn't the strongest, I am using a board with an external antenna connector. 
- USB C breakout pad. My ESP32 dev board comes with a micro USB port and I want to power it with a USB C plug. What's more, I don't want the layout to be constrained by the position of the USB port. Therefore I'm using a USB breakout board to interface with the outside. 
- PCB. I am not well versed in designing PCB boards. And it's too much hassle to manufacture just one board. So I'm using a [solderable breadboard](https://www.amazon.com/EPLZON-Solderable-Breadboard-Gold-Plated-Electronics/dp/B0BP28GYTV) like this:  
![Solderable Breadboard](solderable_breadboard.jpg)

### Schematic
![Schematic](schematic.png)

## Software

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

### Prototyping and debugging
I used a dev board with a PCB antenna for prototyping. Interestingly, when inserted into a breadboard, it does not connect to the WiFi, probably due to shielding of the metal in the breadboard. This is another reason I decided to get a board with an external antenna for the final product. This made prototyping difficult because I can't use the breadboard. So, here's the prototype I built:

![prototype](IMG_0917.jpeg)

The two rocker switches simulate the limit switches. The two pushbuttons were harvested from old mice. Their pins are 5mm pitch.

To debug the final product where the USB port on the ESP is hard to reach, I use a USB to UART CP2102 adapter. Connect 5v, GND, Rx and Tx pins. To upload a sketch, you need to press and bold boot, then press and release reset. After uploading the sketch, you'll need to manually reset.

### Test cases
