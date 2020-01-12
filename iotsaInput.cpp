#include "iotsa.h"
#include "iotsaInput.h"
#include "iotsaConfigFile.h"

#define DEBOUNCE_DELAY 50 // 50 ms debouncing

static void dummyTouchCallback() {}
static bool anyWakeOnTouch;
static uint64_t bitmaskButtonWakeHigh;
static int buttonWakeLow;

void IotsaInputMod::setup() {
  anyWakeOnTouch = false;
  bitmaskButtonWakeHigh = 0;
  buttonWakeLow = -1;
  for(int i=0; i<nInput; i++) {
    inputs[i]->setup();
  }
  esp_err_t err;
  if (anyWakeOnTouch) {
    IFDEBUG IotsaSerial.println("IotsaInputMod: enable wake on touch");
    err = esp_sleep_enable_touchpad_wakeup();
    if (err != ESP_OK) IotsaSerial.println("Error in touchpad_wakeup");
  }
  if (bitmaskButtonWakeHigh) {
    err = esp_sleep_enable_ext1_wakeup(bitmaskButtonWakeHigh, ESP_EXT1_WAKEUP_ANY_HIGH);
    if (err != ESP_OK) IotsaSerial.println("Error in ext1_wakeup");
  }
  if (buttonWakeLow > 0) {
    err = esp_sleep_enable_ext0_wakeup((gpio_num_t)buttonWakeLow, 0);
    if (err != ESP_OK) IotsaSerial.println("Error in ext0_wakeup");
  }
}

void IotsaInputMod::serverSetup() {
}

void IotsaInputMod::loop() {

  for (int i=0; i<nInput; i++) {
    inputs[i]->loop();
  }
}


Input::Input(bool _actOnPress, bool _actOnRelease, bool _wake)
: actOnPress(_actOnPress), 
  actOnRelease(_actOnRelease), 
  wake(_wake), 
  activationCallback(NULL)
{
}

void Input::setCallback(ActivationCallbackType callback)
{
  activationCallback = callback;
}

Touchpad::Touchpad(int _pin, bool _actOnPress, bool _actOnRelease, bool _wake)
: Input(_actOnPress, _actOnRelease, _wake),
  pressed(false),
  pin(_pin),
  debounceState(false),
  debounceTime(0),
  threshold(20)
{
}

void Touchpad::setup() {
  if (wake) {
    anyWakeOnTouch = true;
    touchAttachInterrupt(pin, dummyTouchCallback, threshold);
  }
}

void Touchpad::loop() {
  uint16_t value = touchRead(pin);
  if (value == 0) return;
  bool state = value < threshold;
  if (state != debounceState) {
    // The touchpad seems to have changed state. But we want
    // it to remain in the new state for some time (to cater for 50Hz/60Hz interference)
    debounceTime = millis();
    iotsaConfig.postponeSleep(DEBOUNCE_DELAY*2);
  }
  debounceState = state;
  if (millis() > debounceTime + DEBOUNCE_DELAY && state != pressed) {
    // The touchpad has been in the new state for long enough for us to trust it.
    pressed = state;
    bool doSend = (pressed && actOnPress) || (!pressed && actOnRelease);
    IFDEBUG IotsaSerial.printf("Touch callback for button pin %d state %d value %d\n", pin, state, value);
    if (doSend && activationCallback) {
      activationCallback();
    }
  }
  
}

Button::Button(int _pin, bool _actOnPress, bool _actOnRelease, bool _wake)
: Input(_actOnPress, _actOnRelease, _wake),
  pressed(false),
  pin(_pin),
  debounceState(false),
  debounceTime(0)
{
}

void Button::setup() {
  pinMode(pin, INPUT_PULLUP);
  if (wake) {
    // Buttons should be wired to GND. So press gives a low level.
    if (actOnPress) {
      if (buttonWakeLow > 0) IotsaSerial.println("Multiple low wake inputs");
      buttonWakeLow = pin;
    } else {
      bitmaskButtonWakeHigh |= 1LL << pin;
    }
  }

}

void Button::loop() {
  bool state = digitalRead(pin) == LOW;
  if (state != debounceState) {
    // The touchpad seems to have changed state. But we want
    // it to remain in the new state for some time (to cater for 50Hz/60Hz interference)
    debounceTime = millis();
    iotsaConfig.postponeSleep(DEBOUNCE_DELAY*2);
  }
  debounceState = state;
  if (millis() > debounceTime + DEBOUNCE_DELAY && state != pressed) {
    // The touchpad has been in the new state for long enough for us to trust it.
    pressed = state;
    bool doSend = (pressed && actOnPress) || (!pressed && actOnRelease);
    IFDEBUG IotsaSerial.printf("Button callback for button pin %d state %d\n", pin, state);
    if (doSend && activationCallback) {
      activationCallback();
    }
  }
}

RotaryEncoder::RotaryEncoder(int _pinA, int _pinB, bool _wake)
: Input(true, true, _wake),
  value(0),
  pinA(_pinA),
  pinB(_pinB),
  pinAstate(false)
{
}

void RotaryEncoder::setup() {
  pinMode(pinA, INPUT_PULLUP);
  pinMode(pinB, INPUT_PULLUP);
  pinAstate = digitalRead(pinA) == LOW;
  if (wake) {
    // xxxjack unsure about this: would "wake on any high" mean on positive flanks (as I hope) or
    // would this mean the cpu remain awake when any pin is level high? To be determined.
    bitmaskButtonWakeHigh |= 1LL << pinA;
    bitmaskButtonWakeHigh |= 1LL << pinB;
  }

}

void RotaryEncoder::loop() {
  bool pinAnewState = digitalRead(pinA) == LOW;

  if (pinAnewState != pinAstate) {
    // PinA is in a new state
    pinAstate = pinAnewState;
    // If pinA changed state high read pinB to determine whether this is an increment or a decrement.
    bool pinBstate = digitalRead(pinB) == LOW;
    bool increment = pinAstate != pinBstate;
    if (increment) {
      value++;
    } else {
      value--;
    }
    bool doSend = (increment && actOnPress) || (!increment && actOnRelease);
    IFDEBUG IotsaSerial.printf("RotaryEncoder callback for button pin %d,%d state %d,%d increment %d value %d\n", pinA, pinB, pinAstate, pinBstate, increment, value);
    if (doSend && activationCallback) {
      activationCallback();
    }
  }
}