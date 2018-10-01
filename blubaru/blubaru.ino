// Copyright 2017 Joel Ebel jbebel@mybox.org
/*
  https://create.arduino.cc/editor/jbebel/57201c9b-37c9-4a78-8ebf-50898adf0f21/preview

  My text-based bug tracker:

  The watchdog timer appears to interfere with the Pro Micro bootloader. It won't be a
  problem on the 4313, but it makes testing it here difficult.

  Error handling. Most likely errors seem to be
  1. BT module locked up. No responses. reset needed.
  2. Spurious command received by BT module '?' is returned. Flush and send 'Q' again.
  3. BT module resets spontaneously, and CMD is in the buffer. Flush and send 'Q' again.
  Need to consider how to handle these at any point within the program.
  Can make use of the watchdog timer to reset the MCU on failures.

  Consider supporting operation with parts missing or broken. Notably, an unresponsive or
  missing BT module should not prevent line-in from operating. Potentially also support
  no line_in audio switch.

  For CMD mode, I can either:
  1. Leave it in CMD mode and disable SPP profile
  2. Disable SPP and allow for switching in and out of CMD mode for some error checking
  3. Enable SPP mode and write a serial debug mode for the module.
  For now, stick with 1.

  Perform device configuration in setup(). Suspect with 4313 I will be able to use 115200,
  but if not, I can pull GPIO7 LOW to force 9600 and then reset. Check values and only set
  and restart if they aren't correct. Could use EEPROM to save state, but probably not necessary.
  Potentially only perform setup if MCU was reset. This is stored in a register.
  G% gets extended feature sets (0807)
  - enable AVRCP buttons (which I break on on the board but don't use yet.
  - enable reconnect on power on
  - enable discoverable on startup
  - Enable latch event indicator
  GC gets service class (240420) (Identifies it as a car audio device)
  GD gets discovery profile mask (0C) (for disabling SPP and iAP discovery)
  GK gets connection profile mask (0C) (same for connections)
  GN gets device name (Blubaru)
  GS gets speaker level (0F)
  GU sets baud, but hopefully don't need to modify it.


  Set up functions for tasks. Use for loops for things that should be retried.
  Use break to leave them once you get the data you need.

*/
#include <avr/wdt.h>

// Hardware serial is on pins 0 and 1.
HardwareSerial& BT = Serial;
const long kBaudRate = 115200;    // Speed to talk to RN-52

// GPIO on RN52
// Input
const int kStateChangePin = 5;   // GPIO2 Driven low on state change.
                                 // Latched until read.
// Output
const int kPwrEnPin = 8;         // Drive HIGH to enable power of RN52
const int kRstPin = 4;           // GPIO3 HIGH for 100 ms to reset RN52
                                 // (longer will cause DFU mode)
const int kLowBaudPin = 7;       // GPIO7 LOW on startup Forces 9600 baud
const int kCmdModePin = 6;       // GPIO9 Drive low to enter command mode

// Other Inputs
const int kLineInSensePin = 9;   // HIGH when something is in the line_in jack

// Other Outputs
const int kLED1Pin = 12;           // debug LED1
const int kLED2Pin = 13;           // debug LED2
const int kLineInTriggerPin = 11; // BT:LOW line_in:HIGH
const int kTriggerPin = 10;       // Enables audio output (HIGH)


void WatchdogSetup() {
  cli();
  wdt_reset();

  /*
    WDTCR configuration:
    WDIE = 0: Interrupt Disable
    WDE = 1 :Reset Enable
    WDP3 = 1 :For 4000ms Time-out
    WDP2 = 0 :For 4000ms Time-out
    WDP1 = 0 :For 4000ms Time-out
    WDP0 = 0 :For 4000ms Time-out
  */
  // Enter Watchdog Configuration mode:
  WDTCR |= (1 << WDCE) | (1 << WDE);

  // Set Watchdog settings:
  WDTCR = (0 << WDIE) | (1 << WDE) |
          (1 << WDP3) | (0 << WDP2) | (0 << WDP1) |
          (0 << WDP0);

  sei();
}


void ResetBTModule() {
  digitalWrite(kRstPin, HIGH);
  delay(100);
  digitalWrite(kRstPin, LOW);
}


void FlushInput() {
  while (BT.available() > 0) {
    BT.read();
  }
}


void setup() {
  pinMode(kStateChangePin, INPUT);
  pinMode(kPwrEnPin, OUTPUT);
  pinMode(kRstPin, OUTPUT);
  pinMode(kLineInSensePin, INPUT);
  pinMode(kLED1Pin, OUTPUT);
  pinMode(kLED2Pin, OUTPUT);
  pinMode(kLineInTriggerPin, OUTPUT);
  pinMode(kTriggerPin, OUTPUT);
  pinMode(kLowBaudPin, OUTPUT);
  pinMode(kCmdModePin, OUTPUT);
  digitalWrite(kPwrEnPin, LOW);
  digitalWrite(kRstPin, LOW);
  digitalWrite(kLED1Pin, LOW);
  digitalWrite(kLED2Pin, LOW);
  digitalWrite(kLineInTriggerPin, LOW);
  digitalWrite(kTriggerPin, LOW);
  digitalWrite(kLowBaudPin, HIGH);
  digitalWrite(kCmdModePin, LOW);
  BT.begin(kBaudRate);

  // delay(2000);  // For debugging: so I can start the serial console
  char input[5];
  bool prepared = false;
  while (prepared == false) {
    FlushInput();
    ResetBTModule();  // Does nothing on first start since PwrEn not yet driven high.
    digitalWrite(kLED1Pin, HIGH);
    delay(500);  // Reset time measured around 445 ms
    digitalWrite(kLED1Pin, LOW);
    digitalWrite(kPwrEnPin, HIGH); // Does nothing after reset since power already enabled?
    digitalWrite(kLED2Pin, HIGH);
    delay(1900);  // Startup time measured around 1830 ms
    digitalWrite(kLED2Pin, LOW);
    const size_t charsRead = BT.readBytesUntil('\n', input, sizeof(input));
    if (charsRead != sizeof(input) - 1) { // Something weird happened. Flash 3 times and restart the loop with a reset.
      digitalWrite(kLED2Pin, HIGH);
      delay(200);
      digitalWrite(kLED2Pin, LOW);
      delay(200);
      digitalWrite(kLED2Pin, HIGH);
      delay(200);
      digitalWrite(kLED2Pin, LOW);
      delay(200);
      digitalWrite(kLED2Pin, HIGH);
      delay(200);
      digitalWrite(kLED2Pin, LOW);
      continue;
    }
    if (strncmp(input, "CMD\r", 4) == 0) {
      prepared = true; // We're done
    } else { // Flash once and restart the loop with a reset.
      digitalWrite(kLED1Pin, HIGH);
      digitalWrite(kLED2Pin, HIGH);
      delay(200);
      digitalWrite(kLED1Pin, LOW);
      digitalWrite(kLED2Pin, LOW);
    }
  }
  WatchdogSetup();
}

// Make sure we get 4 hex digits followed by \r
bool ValidateState(char *input) {
  for (int i = 0; i < 4; i++) {
    if (!isHexadecimalDigit(input[i])) {
      return false;
    }
  }
  if (input[4] != '\r') {
    return false;
  }
  return true;
}


void ResetMCU() {
  cli();
  wdt_reset();
  /*
    WDTCR configuration:
    WDIE = 0: Interrupt Disable
    WDE = 1 :Reset Enable
    WDP3 = 0 :For 16ms Time-out
    WDP2 = 0 :For 16ms Time-out
    WDP1 = 0 :For 16ms Time-out
    WDP0 = 0 :For 16ms Time-out
  */
  // Enter Watchdog Configuration mode:
  WDTCR |= (1 << WDCE) | (1 << WDE);

  // Set Watchdog settings:
  WDTCR = (0 << WDIE) | (1 << WDE) |
          (0 << WDP3) | (0 << WDP2) | (0 << WDP1) |
          (0 << WDP0);
  sei();
  // begin an infinite loop of nothing until death
  while (1) { }
}


void ReadState(char *input, const size_t bufsize) {
  // Try to read the state 4 times.
  for (int i = 0; i < 4; i++) {
    FlushInput();
    BT.println("Q");
    // Is it problematic setting this as a const now that I'm looping?
    const size_t charsRead = BT.readBytesUntil('\n', input, bufsize);
    // I wanted this inside ValidateState, but didn't want to pass charsRead.
    if (charsRead != bufsize - 1) { // Probably read nothing. Try again.
      continue;
    }
    if (ValidateState(input)) {
      // We got our state in *input
      return;
    }
  }
  ResetMCU();
}


bool ProcessStateChange() {
  digitalWrite(kLED2Pin, HIGH);
  char input[6];
  ReadState(input, sizeof(input));
  const char& State = input[3];
  digitalWrite(kLED2Pin, LOW);
  return ((State >= '4' && State <= '9') || (State >= 'A' && State <= 'D'));
}


void loop() {
  wdt_reset();
  static bool bt_state = false;
  if (digitalRead(kStateChangePin) == LOW) {
    bt_state = ProcessStateChange();
  }

  const bool linein_state = (digitalRead(kLineInSensePin) == HIGH);

  if (linein_state) {
    digitalWrite(kLineInTriggerPin, HIGH);
  } else {
    digitalWrite(kLineInTriggerPin, LOW);
  }

  if (linein_state || bt_state) {
    digitalWrite(kTriggerPin, HIGH);
  } else {
    digitalWrite(kTriggerPin, LOW);
  }
}
