#include <Arduino.h>

#include "src/HomeSpan.h"

struct DEV_GarageDoor : Service::GarageDoorOpener {
  // Debounce time for the contact sensors. Adjust based on the particular
  // sensor and the speed of the motor.
  const unsigned long millis_debounce = 500;
  // Timeout between the start of an open or close operation and a state that is
  // not STOPPED is reached. We could start an operation while in the STOPPED
  // state, so this value should be a little longer than it takes to fully open
  // or close the door.
  const unsigned long millis_timeout_start = 5000;
  // Timeout between when a sensor is triggered and reaching OPEN or CLOSED. My
  // garage door reverses the course when it hits an obstruction when closing.
  // So this value should be a little longer than it takes to make a round trip.
  const unsigned long millis_timeout_finish = 8000;
  // Time need to keep the relay on to trigger the motor. Half a second works
  // well for my garage door.
  const unsigned long millis_relay = 1000;

  // These values are the same as the those defined in
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

  Characteristic::CurrentDoorState current_state;
  Characteristic::TargetDoorState target_state;
  Characteristic::ObstructionDetected obstruction;

  uint8_t pin_upper;
  uint8_t pin_lower;
  uint8_t pin_relay;

  door_state state = door_state::STOPPED;

  unsigned long millis_timer_start = 0;
  unsigned long millis_timer_finish = 0;
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
    if (state != door_state::OPEN && state != door_state::CLOSED &&
        state != door_state::STOPPED) {
      Serial.printf("-- Unexpected call to trigger_relay() in state %s.\n",
                    door_state_to_string(state));
      return;
    }
    Serial.printf("-- Turning on the relay for %d milliseconds.\n",
                  millis_relay);
    digitalWrite(pin_relay, HIGH);
    delay(millis_relay);
    digitalWrite(pin_relay, LOW);
    Serial.printf("-- Starting the start rimer.\n");
    millis_timer_start = millis();
  }

  static unsigned long millis_since(unsigned long millis_then) {
    unsigned long millis_now = millis();
    // millis() overflows after about 50 days. So, if millis_now < millis_last,
    // that's what happened.
    return millis_now >= millis_then
               ? millis_now - millis_then
               : 0xFFFFFFFF - (millis_then - millis_now) + 1;
  }

  void transition(door_state new_state) {
    Serial.printf("-- Transitioning from %s to %s.\n",
                  door_state_to_string(state), door_state_to_string(new_state));
    door_state old_state = state;
    state = new_state;
    current_state.setVal(state);

    // When closing, my opener reverses the course if it detects an obstruction.
    // So, if we see a CLOSING -> OPEN transition, we assume that an obstruction
    // has been detected. However, this scenario is also possible with manual
    // intervention with the wall mount switch or a remote. But there is no way
    // for us to distinguish. In that case, we will let the human correct the
    // situation by pressing the switch again.
    if (old_state == door_state::CLOSING && new_state == door_state::OPEN) {
      Serial.printf("-- Obstruction detected.\n");
      obstruction.setVal(true);
      if (pending_operation) {
        Serial.printf("-- Abandoning pending operation.\n");
        pending_operation = false;
      }
    } else {
      obstruction.setVal(false);
    }

    // If a user manually stops the door while it's opening and reverses it,
    // we should think we were opening but have reached the CLOSED state. We
    // should abandon the pending operation, assuming the user's intention was
    // to close it.
    if (old_state == door_state::OPENING && new_state == door_state::CLOSED) {
      if (pending_operation) {
        Serial.printf("-- Abandoning pending operation.\n");
        pending_operation = false;
      }
    }

    if (new_state != door_state::STOPPED && millis_timer_start != 0) {
      // A start timer is running, expecting us to reach one of these states:
      // OPENING, CLOSING, OPEN, or CLOSED.  Now we can stop it.
      Serial.println("-- Stopping the start timer.");
      millis_timer_start = 0;
    }

    if (new_state == door_state::OPENING || new_state == door_state::CLOSING) {
      Serial.println("-- Starting the finish timer.");
      // We expect to reach OPEN or CLOSED shortly, or STOPPED if the user has
      // intervenved.
      millis_timer_finish = millis();
    } else if (millis_timer_finish != 0) {
      Serial.println("-- Stopping the finish timer.");
      // A finish timer is running, expecting us to reach one of the three
      // stable states (OPEN, CLOSED, STOPPTED). Now we can stop it.
      millis_timer_finish = 0;
    }

    if (pending_operation) {
      // We are trying to complete an operation requested by the Home app.
      uint8_t target = target_state.getVal();
      switch (new_state) {
      case door_state::STOPPED:
        // We are here because of a manual intervention. So we abandon the
        // pending operation, assuming that's why the user intervened.
        Serial.printf("-- Abandoning pending operation.\n");
        pending_operation = false;
        break;
      case door_state::OPEN:
        if (target == Characteristic::TargetDoorState::CLOSED) {
          // At this point, the previous state must be STOPPED.
          // Otherwise we would've abandoned the pending operation.
          Serial.printf("-- Continuing pending operation: CLOSE.\n");
          trigger_relay();
        } else {
          Serial.printf("-- Operation completed: OPEN.\n");
          pending_operation = false;
        }
        break;
      case door_state::CLOSED:
        if (target == Characteristic::TargetDoorState::OPEN) {
          // At this point, the previous state must be STOPPED.
          // Otherwise, we would've abandonded the pending operation.
          Serial.printf("-- Continuing pending operation: OPEN.\n");
          trigger_relay();
        } else {
          Serial.printf("-- Operation completed: CLOSE.\n");
          pending_operation = false;
        }
        break;
      }
    } else {
      // If the door is being opened or closed manually, or an operation has
      // been abandoned, we need to set the target state so that the Home app
      // is not confused.
      if (new_state == door_state::OPENING || new_state == door_state::OPEN) {
        target_state.setVal(Characteristic::TargetDoorState::OPEN);
      }
      if (new_state == door_state::CLOSING || new_state == door_state::CLOSED) {
        target_state.setVal(Characteristic::TargetDoorState::CLOSED);
      }
      return;
    }
  }

  boolean update() override {
    Serial.printf("-- Operation requested: %s.\n",
                  target_state.getNewVal() ==
                          Characteristic::TargetDoorState::OPEN
                      ? "OPEN"
                      : "CLOSE");
    Serial.printf("-- Current state is %s.\n", door_state_to_string(state));

    uint8_t target = target_state.getNewVal();

    if (target == Characteristic::TargetDoorState::OPEN) {
      switch (state) {
      case door_state::OPEN:
      case door_state::OPENING:
        Serial.printf("-- Noop.\n");
        break;
      case door_state::CLOSING:
        // Do nothing for now, but once we have reached the CLOSED state,
        // trigger the relay to open.
        Serial.printf("-- Noop, but setting pending_operation.\n");
        pending_operation = true;
        break;
      case door_state::CLOSED:
      case door_state::STOPPED:
        pending_operation = true;
        trigger_relay();
        break;
      }
    } else {
      switch (state) {
      case door_state::CLOSED:
      case door_state::CLOSING:
        Serial.printf("-- Noop.\n");
        break;
      case door_state::OPENING:
        Serial.printf("-- Noop, but setting pending_operation.\n");
        pending_operation = true;
        break;
      case door_state::OPEN:
      case door_state::STOPPED:
        pending_operation = true;
        trigger_relay();
        break;
      }
    }

    return true;
  }

  // State transitions.
  // UR = Upper rising. Upper sensor is being closed.
  // UF = Upper falling. Upper sensor is being opened.
  // LR = Lower rising. Lower sensor is being closed.
  // LF = Lower falling. Lower sensor is being opened.
  // FTO = Finish timer runs out.
  // STO = Start timer runs out. This indicates a possible motor failure.
  //         |   UR   |   UF   |   LR   |   LF   |  FTO   |  STO   |
  // --------+--------+--------+--------+--------+--------+--------+
  // OPEN    |CLOSING |        |        |        |        |  OPEN  |
  // CLOSED  |        |        |OPENING |        |        | CLOSED |
  // OPENING |        |  OPEN  |        | CLOSED |STOPPED |        |
  // CLOSING |        |  OPEN  |        | CLOSED |STOPPED |        |
  // STOPPED |        |  OPEN  |        | CLOSED |        |STOPPED |
  // --------+--------+--------+--------+--------+--------+--------+

  // All empty cells are invalid. However, we always transition the the state
  // determined by the previous pin values and current ones, even if we are in
  // an invalid state. It's the only way we can recover. But we log such
  // errors and abandon any pending operations.

  // Timeout event: when transitioning into OPENING or CLOSING, we start a
  // timer. The timer is stopped if we reach OPEN or CLOSED state. But once it
  // runs out, we consider the door in the STOPPED state. This could be caused
  // by manual intervention with a remote or wall switch, a power outage, a
  // motor failure, or a number of other reasons.

  void button(int pin, int type) override {
    // This function is called whenever either sensor changes state, which
    // means the door has reached or left either end of the track.

    if (pin == pin_upper && type == SpanButton::OPEN) {
      // Upper, Rising
      if (state != door_state::OPEN) {
        Serial.printf("-- Upper sensor falling. Invalid state: %s.\n",
                      door_state_to_string(state));
        pending_operation = false;
      }
      transition(door_state::CLOSING);
    }

    if (pin == pin_upper && type == SpanButton::CLOSED) {
      // Upper, Falling
      if (state != door_state::OPENING && state != door_state::CLOSING &&
          state != door_state::STOPPED) {
        Serial.printf("-- Upper sensor rising. Invalid state: %s.\n",
                      door_state_to_string(state));
        pending_operation = false;
      }
      transition(door_state::OPEN);
    }

    if (pin == pin_lower && type == SpanButton::OPEN) {
      // Lower, Rising
      if (state != door_state::CLOSED) {
        Serial.printf("-- Lower sensor falling. Invalid state: %s.\n",
                      door_state_to_string(state));
        pending_operation = false;
      }
      transition(door_state::OPENING);
    }

    if (pin == pin_lower && type == SpanButton::CLOSED) {
      // Lower, Falling
      if (state != door_state::OPENING && state != door_state::CLOSING &&
          state != door_state::STOPPED) {
        Serial.printf("-- Lower sensor rising. Invalid state: %s.\n",
                      door_state_to_string(state));
        pending_operation = false;
      }
      transition(door_state::CLOSED);
    }
  }

  void loop() override {
    if (millis_timer_finish != 0 &&
        millis_since(millis_timer_finish) > millis_timeout_finish) {
      // We were opening or closing. But the timer has run out, probably due to
      // user intervention.
      if (state != door_state::CLOSING && state != door_state::OPENING) {
        Serial.printf("-- Finish timer running out. Invalid state: %s.\n",
                      door_state_to_string(state));
        pending_operation = false;
      }
      transition(door_state::STOPPED);
      return;
    }

    if (millis_timer_start != 0 &&
        millis_since(millis_timer_start) > millis_timeout_start) {
      Serial.printf("-- Start timer ran out. The motor is malfunctioning.\n");
      millis_timer_start = 0;
      // An operation started but has timed out. This indicates a possible motor
      // failure. We are not able to leave the current state without external
      // intervention. So, we abandon the pending opeartion, and set the target
      // state to match the current state. Otherwise, the Home app will be
      // waiting indefinitely. The STOPPED state is considered as open by the
      // Home app.
      if (pending_operation) {
        Serial.printf("-- Abandoning pending operation.\n");
        pending_operation = false;
      }

      switch (state) {
      case door_state::CLOSED:
        target_state.setVal(Characteristic::TargetDoorState::CLOSED);
        break;
      case door_state::OPEN:
      case door_state::STOPPED:
        target_state.setVal(Characteristic::TargetDoorState::OPEN);
        break;
      default:
        Serial.printf("-- Invalid state: %s.\n", door_state_to_string(state));
        pending_operation = false;
        break;
      }
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
  new DEV_GarageDoor(23, 22, 19);

  homeSpan.autoPoll();
}

void loop() {}