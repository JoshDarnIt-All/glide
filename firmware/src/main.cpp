// Glide — M0 bench rig
//
// The whole job of M0 is to prove the driver, wiring, and basic
// step/dir control work at all, off the rail, before anything else
// (homing, soft limits, mm-based positioning — all of that is M1) is
// built on top of it. So this file deliberately does the simplest
// possible thing: turn the motor 10 revolutions, stop, and return to
// where it started. It runs once automatically after boot/reset —
// there's no serial command interface yet (that's also M1's job).
//
// Pin assignment and reasoning: see hardware/pinout.md — that file is
// the source of truth if this ever needs re-wiring; keep it in sync.

#include <Arduino.h>
#include <FastAccelStepper.h>

// ---- Pins (standalone STEP/DIR/EN — see hardware/pinout.md) ----
constexpr uint8_t PIN_STEP = 25;
constexpr uint8_t PIN_DIR = 26;
constexpr uint8_t PIN_EN = 27;  // TMC2209 EN is active-LOW

// ---- Motor + microstepping ----
// Confirmed motor spec (hardware/gvm-48-inspection.md): 1.8 deg/step,
// i.e. 200 full steps per revolution.
constexpr int FULL_STEPS_PER_REV = 200;

// NOT YET CONFIRMED: standalone mode sets microstepping via the
// TMC2209's physical MS1/MS2 pins, which depends on the specific
// driver board — unknown until hardware arrives. Defaulting to 1
// (full step) as the only value that doesn't guess at a board we
// haven't seen yet. Update this once the board's MS1/MS2 setting is
// known — see hardware/pinout.md.
constexpr int MICROSTEPS = 1;

constexpr int STEPS_PER_REV = FULL_STEPS_PER_REV * MICROSTEPS;

// ---- Bench test parameters ----
constexpr int TEST_REVOLUTIONS = 10;

// Deliberately slow and gentle for a first-ever powered move on
// unfamiliar wiring: 1 rev/sec, ramping over roughly a quarter
// revolution. These are bench-safety numbers, not tuned-for-use
// numbers — M1 is where real speed/accel tuning happens.
constexpr int TEST_SPEED_STEPS_PER_SEC = STEPS_PER_REV * 1;
constexpr int TEST_ACCEL_STEPS_PER_SEC2 = STEPS_PER_REV * 2;

FastAccelStepperEngine engine = FastAccelStepperEngine();
FastAccelStepper *stepper = nullptr;

// Waits for the current move to finish, printing nothing in the loop
// so the serial log stays readable — FastAccelStepper runs the actual
// step pulses off a hardware timer in the background, so this loop is
// just polling for "done," not driving the motion itself.
void waitForMoveToComplete() {
  while (stepper->isRunning()) {
    delay(10);
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);  // let USB-serial enumerate before the first print

  Serial.println();
  Serial.println("=== Glide M0 bench rig ===");
  Serial.printf("Steps/rev: %d (full-steps=%d, microsteps=%d)\n",
                 STEPS_PER_REV, FULL_STEPS_PER_REV, MICROSTEPS);

  pinMode(PIN_EN, OUTPUT);
  digitalWrite(PIN_EN, LOW);  // enable driver (active-LOW)

  engine.init();
  stepper = engine.stepperConnectToPin(PIN_STEP);
  if (!stepper) {
    Serial.println("ERROR: could not connect stepper to STEP pin — halting");
    while (true) {
      delay(1000);
    }
  }
  stepper->setDirectionPin(PIN_DIR);
  stepper->setSpeedInHz(TEST_SPEED_STEPS_PER_SEC);
  stepper->setAcceleration(TEST_ACCEL_STEPS_PER_SEC2);

  const long moveSteps = static_cast<long>(STEPS_PER_REV) * TEST_REVOLUTIONS;

  Serial.printf("Forward: %ld steps (%d rev)\n", moveSteps, TEST_REVOLUTIONS);
  stepper->move(moveSteps);
  waitForMoveToComplete();
  Serial.println("Forward move complete.");

  delay(1000);  // pause at the far end so the stop is visibly distinct from the return

  Serial.printf("Return: %ld steps\n", -moveSteps);
  stepper->move(-moveSteps);
  waitForMoveToComplete();
  Serial.println("Return complete.");

  // De-energize once idle. There's no load being held here (this is
  // an off-rail bench test) and no brake in this design anyway, so
  // there's no reason to keep the coils powered — and it means the
  // driver isn't dissipating heat while nothing is happening.
  digitalWrite(PIN_EN, HIGH);

  Serial.println("=== M0 bench test done. Reset the board to run again. ===");
}

void loop() {
  // Nothing here on purpose — M0 is a one-shot test triggered by
  // boot/reset, not a continuously-running program.
}
