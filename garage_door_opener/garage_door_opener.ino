#include "src/HomeSpan.h"
#include <Arduino.h>

struct DEV_GarageDoor : Service::GarageDoorOpener {
  // Debounce time for the contact sensors. Adjust based the particular sensor
  // and the speed of the motor.
  const unsigned long millis_debounce = 500;
  // The longest time between triggering a sensor after an opening or closing
  // event starts. For my garage door, it reverses the course when it hits an
  // obstruction, so set this to a little longer than the time it takes to
  // (almost) fully close and open again.
  const unsigned long millis_timeout = 5000;
  // Time need to keep the relay on to trigger the motor. Half a second works
  // well for my garage door.
  const unsigned long millis_relay = 500;

  // These values are the same as the enum defined in
  // GarageDoorOpener::CurrentDoorState. We would use that enum but it's
  // anonymous.
  enum door_state {
    OPEN = 0,
    CLOSED = 1,
    OPENING = 2,
    CLOSING = 3,
    STOPPED = 4
  };

  static const char *door_state_to_string(door_state state) {
    switch (state) {
    case door_state::OPEN:
      return "OPEN";
    case door_state::CLOSED:
      return "CLOSED";
    case door_state::OPENING:
      return "OPENING";
    case door_state::CLOSING:
      return "CLOSING";
    case door_state::STOPPED:
      return "STOPPED";
    default:
      return "Huh?";
    }
  }

  static const char *target_state_to_string(uint8_t state) {
    return state == Characteristic::TargetDoorState::OPEN ? "OPEN" : "CLOSED";
  }

  Characteristic::CurrentDoorState current_state;
  Characteristic::TargetDoorState target_state;
  Characteristic::ObstructionDetected obstruction;

  uint8_t pin_upper;
  uint8_t pin_lower;
  uint8_t pin_relay;

  door_state state = door_state::STOPPED;

  unsigned long millis_timer = 0;
  bool pending_operation = false;

  SpanToggle *toggle_upper;
  SpanToggle *toggle_lower;

  DEV_GarageDoor(uint8_t pin_upper, uint8_t pin_lower, uint8_t pin_relay)
      : Service::GarageDoorOpener() {
    this->pin_upper = pin_upper;
    this->pin_lower = pin_lower;
    this->pin_relay = pin_relay;

    toggle_upper =
        new SpanToggle(pin_upper, PushButton::TRIGGER_ON_LOW, millis_debounce);
    toggle_lower =
        new SpanToggle(pin_lower, PushButton::TRIGGER_ON_LOW, millis_debounce);

    pinMode(pin_upper, INPUT_PULLUP);
    pinMode(pin_lower, INPUT_PULLUP);

    pinMode(pin_relay, OUTPUT);
    digitalWrite(pin_relay, LOW);

    // Determine the initial state.
    uint8_t value_upper = digitalRead(pin_upper);
    uint8_t value_lower = digitalRead(pin_lower);

    if (value_upper == LOW && value_lower == HIGH) {
      state = door_state::OPEN;
    } else if (value_upper == HIGH && value_lower == LOW) {
      state = door_state::CLOSED;
    } else {
      // The door could be OPENING, CLOSING, or STOPPED. But there is no way we
      // can tell. So we assume it's STOPPED. If it's indeed OPENING or CLOSING,
      // it'll trigger a sensor change event (if successful) and we will be able
      // to transition into the OPEN or CLOSED state. If unsuccessful, STOPPED
      // will be the correct state.
      state = door_state::STOPPED;
    }
    current_state.setVal(state);
    Serial.printf("-- Initial state: %s.\n", door_state_to_string(state));
  }

  ~DEV_GarageDoor() {
    if (toggle_upper != nullptr) {
      delete toggle_upper;
    }

    if (toggle_lower != nullptr) {
      delete toggle_lower;
    }
  }

  void trigger_relay() {
    Serial.printf("-- Turning on the relay for %d milliseconds.\n",
                  millis_relay);
    digitalWrite(pin_relay, HIGH);
    delay(millis_relay);
    digitalWrite(pin_relay, LOW);
  }

  static unsigned long millis_since(unsigned long millis_then) {
    unsigned long millis_now = millis();
    // millis() overflows after about 50 days. So, if millis_now < millis_last,
    // that's what happened.
    return millis_now >= millis_then
               ? millis_now - millis_then
               : 0xFFFFFFFF - (millis_then - millis_now) + 1;
  }

  void reach_target_state(uint8_t target_state) {
    if (!pending_operation) {
      return;
    }

    // 1. If the target state matches the current state, do nothing.
    // 2. If the target state is the opposite of the current state, trigger the
    // relay to simulate a button press.
    // 3. If the current state is either OPENING or CLOSING, the safest approach
    // is to do nothing for now, but wait till it's reached one of the three
    // other states and come back here.
    // 4. If we are in the STOPPED state, trigger the relay. But we don't know
    // which direction the door will be moving. So we do that anyway and wait
    // till it's reached OPEN or CLOSED state. If the state doesn't match the
    // target state, we trigger the relay again.

    // TODO: if there is an obstruction, and we try to close, we could be stuck
    // in an infinite loop: OPEN -> CLOSING -> (reversing, but unknown to us) ->
    // OPEN. Should we not try at all or do it anway and devise a mechanism to
    // detect such infinite loops?
    if (target_state == Characteristic::TargetDoorState::OPEN) {
      if (state == door_state::OPEN) {
        Serial.println("-- OPEN operation completed.");
        pending_operation = false;
      }
      if (state == door_state::CLOSED || state == door_state::STOPPED) {
        Serial.printf(
            "-- Trying to reach target state %s from current state %s.\n",
            target_state_to_string(target_state), door_state_to_string(state));
        trigger_relay();
      }
    } else {
      if (state == door_state::CLOSED) {
        Serial.println("-- CLOSE operation completed.");
        pending_operation = false;
      }
      if (state == door_state::OPEN || state == door_state::STOPPED) {
        Serial.printf(
            "-- Trying to reach target state %s from current state %s.\n",
            target_state_to_string(target_state), door_state_to_string(state));
        trigger_relay();
      }
    }
  }

  void transition(door_state new_state) {
    Serial.printf("-- Transitioning from %s to %s.\n",
                  door_state_to_string(state), door_state_to_string(new_state));

    // When closing, my opener reverses the course if it detects an
    // obstruction. So, if we see a CLOSING -> OPEN transition, we consider
    // that an obstruction has been detected. However, this scenario is also
    // possible with manual intervention with the wall mount switch or a
    // remote. But there is no way for us to distinguish. In that case, we
    // will let the human correct the situation by pressing the switch again.
    if (state == door_state::CLOSING && new_state == door_state::OPEN) {
      Serial.println("-- Obstruction detected.");
    }

    obstruction.setVal(state == door_state::CLOSING &&
                       new_state == door_state::OPEN);

    // If the door is being opened or closed manually, we need to set the target
    // state so that the Home apps is not confused.
    if (new_state == door_state::OPENING) {
      target_state.setVal(Characteristic::TargetDoorState::OPEN);
    }

    if (new_state == door_state::CLOSING) {
      target_state.setVal(Characteristic::TargetDoorState::CLOSED);
    }

    // If we were stopped, we have probably previously set a target state. Now
    // we have been fully opened or close, this doesn't necessarily agree with
    // that previously set state. So, we need to let the Home app know.
    // An example scenario:
    //   1. Manually trigger CLOSED -> OPENING, setting target state to OPEN
    //   2. Manually stop the door, causing a timeout, OPENING -> STOPPED
    //   3. Later, manually close the door, STOPPED -> CLOSED
    //   4. Now the target state is OPEN but the door is CLOSED
    if (state == door_state::STOPPED && new_state == door_state::OPEN) {
      target_state.setVal(Characteristic::TargetDoorState::OPEN);
    }

    if (state == door_state::STOPPED && new_state == door_state::CLOSED) {
      target_state.setVal(Characteristic::TargetDoorState::CLOSED);
    }

    if (new_state == door_state::OPENING || new_state == door_state::CLOSING) {
      Serial.println("-- Starting the timer.");
      millis_timer = millis();
    } else {
      Serial.println("-- Stopping the timer.");
      millis_timer = 0;
    }

    state = new_state;
    current_state.setVal(state);

    // Check if we have a pending operation.
    reach_target_state(target_state.getVal());
  }

  boolean update() override {
    Serial.printf("-- %s operation requested.\n",
                  target_state_to_string(target_state.getNewVal()));
    pending_operation = true;
    reach_target_state(target_state.getNewVal());
    return true;
  }

  // State transitions.
  //         |   UR   |   UF   |   LR   |   LF   |   TO   |
  // --------+--------+--------+--------+--------+--------+
  // open    |closing |        |        |        |        |
  // closed  |        |        |opening |        |        |
  // opening |        |  open  |        | closed |stopped |
  // closing |        |  open  |        | closed |stopped |
  // stopped |        |  open  |        | closed |        |
  // --------+--------+--------+--------+--------+--------+

  // All empty cells are invalid. However, we always transition the the state
  // determined by the previous pin values and current ones, even if we are in
  // an invalid state. It's the only way we can recover. But we report such
  // errors.

  // Timeout event: when transitioning into OPENING or CLOSING, we start a
  // timer. The timer is stopped if we reach OPEN or CLOSED state. But once it
  // runs out, we consider the door in the STOPPED state. This could be caused
  // by manual intervention with a remote or wall switch, a power outage, a
  // motor failure, or a number of other reasons.

  void button(int pin, int type) override {
    // This function is called whenever either sensor changes state, which means
    // the door has reached or left either end of the track.

    if (pin == pin_upper && type == SpanButton::OPEN) {
      // Upper, Rising
      if (state != door_state::OPEN) {
        Serial.printf("-- Upper sensor falling. Invalid state: %s.\n",
                      door_state_to_string(state));
      }
      transition(door_state::CLOSING);
    }

    if (pin == pin_upper && type == SpanButton::CLOSED) {
      // Upper, Falling
      if (state != door_state::OPENING && state != door_state::CLOSING &&
          state != door_state::STOPPED) {
        Serial.printf("-- Upper sensor rising. Invalid state: %s.\n",
                      door_state_to_string(state));
      }
      transition(door_state::OPEN);
    }

    if (pin == pin_lower && type == SpanButton::OPEN) {
      // Lower, Rising
      if (state != door_state::CLOSED) {
        Serial.printf("-- Lower sensor falling. Invalid state: %s.\n",
                      door_state_to_string(state));
      }
      transition(door_state::OPENING);
    }

    if (pin == pin_lower && type == SpanButton::CLOSED) {
      // Lower, Falling
      if (state != door_state::OPENING && state != door_state::CLOSING &&
          state != door_state::STOPPED) {
        Serial.printf("-- Lower sensor rising. Invalid state: %s. \n",
                      door_state_to_string(state));
      }
      transition(door_state::CLOSED);
    }
  }

  void loop() override {
    if (millis_timer != 0 && millis_since(millis_timer) > millis_timeout) {
      Serial.println("-- Timer ran out.");
      if (state != door_state::CLOSING && state != door_state::OPENING) {
        Serial.printf("-- Timeout. Invalid state: %s.\n",
                      door_state_to_string(state));
      }
      transition(door_state::STOPPED);
    }
  }
};

void setup() {
  Serial.begin(115200);

  homeSpan.setStatusPin(32).setControlPin(26).begin(Category::GarageDoorOpeners,
                                                    "Garage Door");

  new SpanAccessory();
  new Service::AccessoryInformation();
  new Characteristic::Identify();
  new Characteristic::Name("Garage Door");
  new DEV_GarageDoor(23, 22, 13);

  homeSpan.autoPoll();
}

void loop() {}