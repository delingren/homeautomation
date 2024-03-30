#include "src/HomeSpan.h"
#include <Arduino.h>
#include <vector>

struct DEV_GarageDoor : Service::GarageDoorOpener {
  // Debounce time for the contact sensors. Adjust based the particular sensor
  // and probably the speed of the motor.
  const unsigned long millis_debounce = 600;
  // The longest time between triggering a sensor, after an opening or closing
  // event starts. For my garage door, it reverses the course when it hits an
  // obstruction, so this should be a little longer than the time it takes to
  // (almost) fully close and open again.
  const unsigned long millis_timeout = 5000;

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

  static std::vector<DEV_GarageDoor *> all_openers;

  static void isr() {
    // The interrupt handler for all sensor pins. I don't think there is an easy
    // way to create an ISR for each individual pin. So they'll have to share an
    // ISR.
    for (auto it = all_openers.begin(); it != all_openers.end(); it++) {
      uint8_t new_value_upper = digitalRead((*it)->pin_upper);
      uint8_t new_value_lower = digitalRead((*it)->pin_lower);

      // loop() will do the debouncing logic. We are only informing it that one
      // of the sensors has changed state and when.
      if (new_value_lower != (*it)->value_lower ||
          new_value_upper != (*it)->value_upper) {
        (*it)->sensor_triggered = true;
        (*it)->millis_last = millis();
      }
    }
  }

  Characteristic::CurrentDoorState current_state;
  Characteristic::TargetDoorState target_state;
  Characteristic::ObstructionDetected obstruction;

  uint8_t pin_upper;
  uint8_t pin_lower;
  uint8_t pin_relay;

  uint8_t value_upper;
  uint8_t value_lower;

  door_state state = door_state::STOPPED;

  bool sensor_triggered = false;
  unsigned long millis_last = 0;
  unsigned long millis_timer = 0;
  bool pending_operation = false;

  DEV_GarageDoor(uint8_t pin_upper, uint8_t pin_lower, uint8_t pin_relay)
      : Service::GarageDoorOpener() {
    this->pin_upper = pin_upper;
    this->pin_lower = pin_lower;
    this->pin_relay = pin_relay;

    pinMode(pin_upper, INPUT_PULLUP);
    pinMode(pin_lower, INPUT_PULLUP);

    value_upper = digitalRead(pin_upper);
    value_lower = digitalRead(pin_lower);

    pinMode(pin_relay, OUTPUT);
    digitalWrite(pin_relay, LOW);

    // Determine the initial state.
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

    all_openers.push_back(this);
    attachInterrupt(pin_upper, isr, CHANGE);
    attachInterrupt(pin_lower, isr, CHANGE);
  }

  void trigger_relay() {
    Serial.println("Turning on the relay for 400 ms.");
    digitalWrite(pin_relay, HIGH);
    delay(400);
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
        Serial.println("-- Open operation completed.");
        pending_operation = false;
      }
      if (state == door_state::CLOSED || state == door_state::STOPPED) {
        Serial.printf(
            "Trying to reach target state %d from current state %d.\n",
            target_state, state);
        trigger_relay();
      }
    } else {
      if (state == door_state::CLOSED) {
        Serial.println("-- Close operation completed.");
        pending_operation = false;
      }
      if (state == door_state::OPEN || state == door_state::STOPPED) {
        Serial.printf(
            "Trying to reach target state %d from current state %d.\n",
            target_state, state);
        trigger_relay();
      }
    }
  }

  void transition(door_state new_state) {
    Serial.printf("-- Transitioning from %d to %d.\n", state, new_state);

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

  boolean update() {
    pending_operation = true;
    reach_target_state(target_state.getNewVal());
    return true;
  }

  void loop() {
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
    if (!sensor_triggered && millis_timer != 0 &&
        millis_since(millis_timer) > millis_timeout) {
      Serial.println("-- Timer ran out.");
      if (state != door_state::CLOSING && state != door_state::OPENING) {
        Serial.printf("-- Timeout. Invalid state: %d.\n", state);
      }
      transition(door_state::STOPPED);
    }

    // Sensor change event. This happens when the door reaches or leaves either
    // end of the track. It's not practically possible to have two transitions
    // in one iteration, since it takes at least a few seconds to open or close
    // a garage door. hence we only consider one transition here.
    if (sensor_triggered && millis_since(millis_last) > millis_debounce) {
      // Debouncing logic: it's been enough time since the last change, so it
      // should be stable now.
      uint8_t new_value_upper = digitalRead(pin_upper);
      uint8_t new_value_lower = digitalRead(pin_lower);

      if (value_upper == LOW && new_value_upper == HIGH) {
        // Upper, Rising
        if (state != door_state::OPEN) {
          Serial.printf("-- Upper sensor falling. Invalid state: %d.\n", state);
        }
        transition(door_state::CLOSING);
      } else if (value_upper == HIGH && new_value_upper == LOW) {
        // Upper, Falling
        if (state != door_state::OPENING && state != door_state::CLOSING &&
            state != door_state::STOPPED) {
          Serial.printf("-- Upper sensor rising. Invalid state: %d.\n", state);
        }
        transition(door_state::OPEN);
      } else if (value_lower == LOW && new_value_lower == HIGH) {
        // Lower, Rising
        if (state != door_state::CLOSED) {
          Serial.printf("-- Lower sensor falling. Invalid state: %d.\n", state);
        }
        transition(door_state::OPENING);
      } else if (value_lower == HIGH && new_value_lower == LOW) {
        // Lower, Falling
        if (state != door_state::OPENING && state != door_state::CLOSING &&
            state != door_state::STOPPED) {
          Serial.printf("-- Lower sensor rising. Invalid state: %d. \n", state);
        }
        transition(door_state::CLOSED);
      }

      value_upper = new_value_upper;
      value_lower = new_value_lower;
      sensor_triggered = false;
    }
  }
};

std::vector<DEV_GarageDoor *> DEV_GarageDoor::all_openers;

void setup() {
  Serial.begin(115200);

  homeSpan.setStatusPin(2).setControlPin(26).begin(Category::GarageDoorOpeners,
                                                    "Garage Door");

  new SpanAccessory();
  new Service::AccessoryInformation();
  new Characteristic::Identify();
  new Characteristic::Name("Garage Door");
  new DEV_GarageDoor(22, 23, 13);

  homeSpan.autoPoll();
}

void loop() {}