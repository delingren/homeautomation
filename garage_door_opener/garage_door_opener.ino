#include "src/HomeSpan.h"
#include <Arduino.h>
#include <WiFiClient.h>
#include <queue>

class HttpLog {
private:
  const char *channel = "garage";
  const int timeout = 5000;

  const char *server_;
  int port_;
  std::queue<char *> queue_;

public:
  HttpLog(const char *server, int port) {
    server_ = server;
    port_ = port;
  }

  void Log(const char *format, ...) {
    // Warning: later, we will put message directly into a json string.
    // We do nothing to make sure the json string is valid or escape any
    // characters.
    char *message;
    va_list args;
    va_start(args, format);
    vasprintf(&message, format, args);
    va_end(args);
    queue_.push(message);
    LOG1("-- %s\n", message);
  }

  void loop() {
    if (!queue_.empty()) {
      char *message = queue_.front();
      queue_.pop();

      WiFiClient client;
      client.connect(server_, port_, 5000);
      if (client.connected()) {
        char *json;
        asprintf(&json, "{\"channel\":\"%s\",\"message\":\"%s\"}", channel,
                 message);

        // clang-format off
        const char *format = 
          "POST / HTTP/1.1\r\n"
          "Host: %s:%d\r\n"
          "Accept: */*\r\n"
          "Content-Type: application/json\r\n"
          "Content-Length: %d\r\n\r\n"
          "%s\r\n\r\n";
        // clang-format on
        char *request;
        asprintf(&request, format, server_, port_, strlen(json), json);
        if (0 == client.write(request)) {
          LOG0("Failed to log on http log server %s:%d\n", server_, port_);
        }
        free(json);
        free(request);
      } else {
        LOG0("Failed to connect to http log server %s:%d\n", server_, port_);
      }

      free(message);
    }
  }
};

HttpLog httpLog("andromeda", 3000);

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

    init();
  }

  ~DEV_GarageDoor() {
    if (toggle_upper != nullptr) {
      delete toggle_upper;
    }

    if (toggle_lower != nullptr) {
      delete toggle_lower;
    }
  }

  void init() {
    pending_operation = false;
    millis_timer_start = 0;
    millis_timer_finish = 0;

    // Determine the initial state.
    uint8_t value_upper = digitalRead(pin_upper);
    uint8_t value_lower = digitalRead(pin_lower);

    uint8_t target;
    if (value_upper == LOW && value_lower == HIGH) {
      state = door_state::OPEN;
      target = Characteristic::TargetDoorState::OPEN;
    } else if (value_upper == HIGH && value_lower == LOW) {
      state = door_state::CLOSED;
      target = Characteristic::TargetDoorState::CLOSED;
    } else {
      if (value_upper == LOW && value_lower == LOW) {
        // This scenario shouldn't be possible. If we are here, the sensors
        // must be malfunctioning.
        httpLog.Log("Yo, fix your sensors. They are both closed.");
      }
      // The door could be OPENING, CLOSING, or STOPPED. But there is no way we
      // can tell. So we assume it's STOPPED. If it's indeed OPENING or CLOSING,
      // it'll trigger a sensor change event (if successful) and we will be able
      // to transition into the OPEN or CLOSED state. If unsuccessful, STOPPED
      // will be the correct state.
      state = door_state::STOPPED;
      // Pick a rather arbitrary state as the target state, so the behavior is
      // at least determinstics. We choose CLOSED so that the Home app thinks we
      // stopped while trying to close.
      target = Characteristic::TargetDoorState::CLOSED;
    }
    current_state.setVal(state);
    target_state.setVal(target);
    httpLog.Log("Initializing, current state: %s, target state: %s.",
                door_state_to_string(state),
                target == Characteristic::TargetDoorState::OPEN ? "OPEN"
                                                                : "CLOSED");
  }

  void trigger_relay() {
    if (state != door_state::OPEN && state != door_state::CLOSED &&
        state != door_state::STOPPED) {
      httpLog.Log("Unexpected call to trigger_relay() in state %s.",
                  door_state_to_string(state));
      return;
    }
    httpLog.Log("Turning on the relay for %d milliseconds.", millis_relay);
    digitalWrite(pin_relay, HIGH);
    delay(millis_relay);
    digitalWrite(pin_relay, LOW);
    httpLog.Log("Starting the start rimer.");
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

  boolean update() override {
    httpLog.Log("Operation requested: %s.",
                target_state.getNewVal() ==
                        Characteristic::TargetDoorState::OPEN
                    ? "OPEN"
                    : "CLOSE");
    httpLog.Log("Current state is %s.", door_state_to_string(state));

    uint8_t target = target_state.getNewVal();

    if (target == Characteristic::TargetDoorState::OPEN) {
      switch (state) {
      case door_state::OPEN:
      case door_state::OPENING:
        httpLog.Log("Noop.");
        break;
      case door_state::CLOSING:
        // Do nothing for now, but once we have reached the CLOSED state,
        // trigger the relay to open.
        httpLog.Log("Noop, but setting pending_operation.");
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
        httpLog.Log("Noop.");
        break;
      case door_state::OPENING:
        httpLog.Log("Noop, but setting pending_operation.");
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

  // State transitions. See README.md for more details.
  // UR = Upper rising. Upper sensor is being closed.
  // UF = Upper falling. Upper sensor is being opened.
  // LR = Lower rising. Lower sensor is being closed.
  // LF = Lower falling. Lower sensor is being opened.
  // FTO = Finish timer runs out.

  //         |   UR   |   UF   |   LR   |   LF   |  FTO   |
  // --------+--------+--------+--------+--------+--------+
  // OPEN    |CLOSING |        |        |        |        |
  // CLOSED  |        |        |OPENING |        |        |
  // OPENING |        |  OPEN  |        | CLOSED |STOPPED |
  // CLOSING |        |  OPEN  |        | CLOSED |STOPPED |
  // STOPPED |        |  OPEN  |        | CLOSED |        |
  // --------+--------+--------+--------+--------+--------+

  void button(int pin, int type) override {
    // This function is called whenever either sensor changes state, which
    // means the door has reached or left either end of the track.

    if (pin == pin_upper && type == SpanButton::OPEN) {
      // Upper, Rising
      if (state != door_state::OPEN) {
        httpLog.Log("Upper sensor falling. Invalid state: %s. Resetting.",
                    door_state_to_string(state));
        init();
      }
      transition(door_state::CLOSING);
    }

    if (pin == pin_upper && type == SpanButton::CLOSED) {
      // Upper, Falling
      if (state != door_state::OPENING && state != door_state::CLOSING &&
          state != door_state::STOPPED) {
        httpLog.Log("Upper sensor rising. Invalid state: %s. Resetting.",
                    door_state_to_string(state));
        init();
      }
      transition(door_state::OPEN);
    }

    if (pin == pin_lower && type == SpanButton::OPEN) {
      // Lower, Rising
      if (state != door_state::CLOSED) {
        httpLog.Log("Lower sensor falling. Invalid state: %s. Resetting.",
                    door_state_to_string(state));
        init();
      }
      transition(door_state::OPENING);
    }

    if (pin == pin_lower && type == SpanButton::CLOSED) {
      // Lower, Falling
      if (state != door_state::OPENING && state != door_state::CLOSING &&
          state != door_state::STOPPED) {
        httpLog.Log("Lower sensor rising. Invalid state: %s. Resetting.",
                    door_state_to_string(state));
        init();
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
        httpLog.Log("Finish timer running out. Invalid state: %s.",
                    door_state_to_string(state));
        pending_operation = false;
      }
      transition(door_state::STOPPED);
      return;
    }

    if (millis_timer_start != 0 &&
        millis_since(millis_timer_start) > millis_timeout_start) {
      httpLog.Log("Start timer ran out. The motor is malfunctioning.");
      millis_timer_start = 0;
      // An operation started but has timed out. This indicates a possible motor
      // failure. We are not able to leave the current state without external
      // intervention. So, we abandon the pending opeartion, and set the target
      // state to match the current state. Otherwise, the Home app will be
      // waiting indefinitely. The STOPPED state is considered as open by the
      // Home app.
      if (pending_operation) {
        httpLog.Log("Abandoning pending operation.");
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
        httpLog.Log("Invalid state: %s.", door_state_to_string(state));
        pending_operation = false;
        break;
      }
    }
  }

  void transition(door_state new_state) {
    httpLog.Log("Transitioning from %s to %s.", door_state_to_string(state),
                door_state_to_string(new_state));
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
      httpLog.Log("Obstruction detected.");
      obstruction.setVal(true);
      if (pending_operation) {
        httpLog.Log("Abandoning pending operation.");
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
        httpLog.Log("Abandoning pending operation.");
        pending_operation = false;
      }
    }

    if (new_state != door_state::STOPPED && millis_timer_start != 0) {
      // A start timer is running, expecting us to reach one of these states:
      // OPENING, CLOSING, OPEN, or CLOSED.  Now we can stop it.
      httpLog.Log("Stopping the start timer.");
      millis_timer_start = 0;
    }

    if (new_state == door_state::OPENING || new_state == door_state::CLOSING) {
      httpLog.Log("Starting the finish timer.");
      // We expect to reach OPEN or CLOSED shortly, or STOPPED if the user has
      // intervenved.
      millis_timer_finish = millis();
    } else if (millis_timer_finish != 0) {
      httpLog.Log("Stopping the finish timer.");
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
        httpLog.Log("Abandoning pending operation.");
        pending_operation = false;
        break;
      case door_state::OPEN:
        if (target == Characteristic::TargetDoorState::CLOSED) {
          // At this point, the previous state must be STOPPED.
          // Otherwise we would've abandoned the pending operation.
          httpLog.Log("Continuing pending operation: CLOSE.");
          trigger_relay();
        } else {
          httpLog.Log("Operation completed: OPEN.");
          pending_operation = false;
        }
        break;
      case door_state::CLOSED:
        if (target == Characteristic::TargetDoorState::OPEN) {
          // At this point, the previous state must be STOPPED.
          // Otherwise, we would've abandonded the pending operation.
          httpLog.Log("Continuing pending operation: OPEN.");
          trigger_relay();
        } else {
          httpLog.Log("Operation completed: CLOSE.");
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
};

void setup() {
  // Serial.begin(115200);

  homeSpan.setStatusPin(32).setControlPin(26).begin(Category::GarageDoorOpeners,
                                                    "Garage Door");

  new SpanAccessory();
  new Service::AccessoryInformation();
  new Characteristic::Identify();
  new Characteristic::Name("Garage Door");
  new DEV_GarageDoor(23, 22, 19);

  homeSpan.autoPoll();
}

void loop() { httpLog.loop(); }