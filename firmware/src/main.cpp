// Glide — M1 motion core (serial control)
//
// Supersedes the M0 one-shot bench test (which proved the driver,
// wiring, and microstepping were correct — see hardware/pinout.md).
// This is the real motion core: homing, soft limits, positions in mm
// (not raw steps), S-curve ease in/out, and an A-B-A loop, all driven
// by typed serial commands. Type HELP once connected for the command
// list; see the project chat/docs for the full protocol design.
//
// No WiFi, no web UI yet — that's M3/M4. This is deliberately the
// "prove the motion itself is beautiful" milestone: don't advance
// past this file until it is.
//
// --- How S-curve motion is achieved with FastAccelStepper ---
// FastAccelStepper only has a native TRAPEZOIDAL ramp (constant
// acceleration), not a jerk-limited S-curve. To get true ease in/out
// anyway, this firmware computes an S-curve profile in lib/motion
// (pure math, unit-tested on a laptop — see firmware/test/test_motion)
// and then WAYPOINT-CHASES it: every CONTROL_TICK_MS, it samples the
// profile's position at the current elapsed time and issues
// stepper->moveTo() to that exact spot. FastAccelStepper's own ramp is
// set aggressively fast (see armFollower()) so it always closes each
// tiny per-tick gap well before the next tick — at that point its
// built-in ramp is just "keep up with the waypoint," not shaping the
// move; our S-curve waypoints are what shapes the move. This is a
// standard technique for tracing an arbitrary velocity profile with a
// library that only natively supports trapezoidal ramps. Known
// limitation: because each tick briefly "catches up" to a moving
// target, the instantaneous step rate can spike slightly above the
// profile's nominal velocity at that instant — negligible at a 20ms
// tick rate for smooth profiles in practice, but worth knowing if
// motion looks anything other than beautiful on real hardware.

#include <Arduino.h>
#include <FastAccelStepper.h>

#include "loop_runner.h"
#include "scurve.h"
#include "soft_limits.h"

using glide::buildSCurveProfile;
using glide::clampToSoftLimits;
using glide::LoopConfig;
using glide::LoopPhase;
using glide::LoopRunner;
using glide::SCurveProfile;

// ---- Pins (standalone STEP/DIR/EN — see hardware/pinout.md) ----
constexpr uint8_t PIN_STEP = 25;
constexpr uint8_t PIN_DIR = 26;
constexpr uint8_t PIN_EN = 27;  // TMC2209 EN is active-LOW

// ---- Motor + microstepping (measured on the bench — hardware/pinout.md) ----
constexpr int FULL_STEPS_PER_REV = 200;
constexpr int MICROSTEPS = 8;
constexpr int STEPS_PER_REV = FULL_STEPS_PER_REV * MICROSTEPS;

// ---- Control loop timing ----
// How often we sample the S-curve profile and re-issue a position
// waypoint. 20ms (50Hz) is frequent enough that FastAccelStepper's
// own ramp between waypoints is invisible at slider speeds, without
// spamming the step-generation timer with pointless updates.
constexpr unsigned long CONTROL_TICK_MS = 20;

FastAccelStepperEngine engine = FastAccelStepperEngine();
FastAccelStepper *stepper = nullptr;

// ---- Runtime state ----
// Three gates must all be true before any real motion command is
// allowed — see requireReady(). Until then, position 0 and any mm
// value are meaningless (undefined home, unknown travel range, or
// unknown steps-per-mm conversion).
bool g_homed = false;
bool g_travelSet = false;
bool g_calibrated = false;

double g_travelMm = 0.0;
// Steps-per-mm is NOT hardcoded on purpose: it depends on the
// physical drivetrain (belt pitch x pulley teeth, or leadscrew lead,
// plus any gearbox reduction), none of which are recorded yet in
// hardware/gvm-48-inspection.md. Guessing a specific belt/pulley spec
// here would be exactly the kind of unfounded assumption we avoided
// for phase current and microstepping — instead this is set at
// runtime via SETSTEPSPERMM, determined the same way microstepping
// was: command a known move, physically measure the actual distance
// traveled, and divide.
double g_stepsPerMm = 0.0;

bool g_aSet = false;
bool g_bSet = false;
double g_posAMm = 0.0;
double g_posBMm = 0.0;

double g_speedMmS = 20.0;    // peak speed, mm/s — default is a cautious starting point, not tuned
double g_accelMmS2 = 100.0;  // acceleration ceiling, mm/s^2
double g_dwellAS = 0.0;
double g_dwellBS = 0.0;
bool g_repeat = true;

double g_currentPositionMm = 0.0;  // firmware's own estimate — open-loop, no encoder feedback

LoopRunner g_loopRunner;
LoopPhase g_lastLoopPhase = LoopPhase::Idle;

bool g_directMoveActive = false;
SCurveProfile g_directMoveProfile;
double g_directMoveStartMm = 0.0;
double g_directMoveElapsedS = 0.0;

unsigned long g_lastTickMs = 0;
String g_lineBuffer;

// ---- Serial reply helpers ----
// OK/ERR for direct command replies, "# " for asynchronous events
// (like a loop phase changing) so they're visually distinct from a
// reply to whatever you just typed.
void printOk() { Serial.println("OK"); }
void printOk(const String &data) {
  Serial.print("OK ");
  Serial.println(data);
}
void printErr(const char *reason) {
  Serial.print("ERR ");
  Serial.println(reason);
}
void printEvent(const String &msg) {
  Serial.print("# ");
  Serial.println(msg);
}

const char *phaseName(LoopPhase p) {
  switch (p) {
    case LoopPhase::Idle:
      return "IDLE";
    case LoopPhase::MovingToB:
      return "MOVING_TO_B";
    case LoopPhase::DwellingAtB:
      return "DWELLING_AT_B";
    case LoopPhase::MovingToA:
      return "MOVING_TO_A";
    case LoopPhase::DwellingAtA:
      return "DWELLING_AT_A";
    case LoopPhase::Stopped:
      return "STOPPED";
  }
  return "UNKNOWN";
}

bool requireHomed() {
  if (!g_homed) {
    printErr("NOT_HOMED");
    return false;
  }
  return true;
}
bool requireTravel() {
  if (!g_travelSet) {
    printErr("TRAVEL_NOT_SET");
    return false;
  }
  return true;
}
bool requireCalibrated() {
  if (!g_calibrated) {
    printErr("NOT_CALIBRATED");
    return false;
  }
  return true;
}
bool requireReady() {
  // Order matters for a clear error message: homing first, since
  // travel/calibration are meaningless without a home reference too,
  // but all three are independently required either way.
  return requireHomed() && requireTravel() && requireCalibrated();
}

// Sets FastAccelStepper's own speed/accel high enough that it always
// closes the gap to the next waypoint well within one control tick —
// see the file-header note on waypoint-chasing. Scaled off the
// currently configured peak speed, not a fixed constant, so it stays
// sane regardless of whatever steps-per-mm calibration ends up being.
void armFollower() {
  double nominalStepRate = g_speedMmS * g_stepsPerMm;
  uint32_t followerHz = static_cast<uint32_t>(nominalStepRate * 3.0) + 1000;
  stepper->setSpeedInHz(followerHz);
  stepper->setAcceleration(followerHz * 20);
}

void beginDirectMove(double targetMm) {
  bool clamped = false;
  double clampedTarget = clampToSoftLimits(targetMm, g_travelMm, &clamped);
  g_directMoveStartMm = g_currentPositionMm;
  g_directMoveProfile = buildSCurveProfile(
      clampedTarget - g_currentPositionMm, g_speedMmS, g_accelMmS2);
  g_directMoveElapsedS = 0.0;
  g_directMoveActive = true;
  armFollower();
  if (clamped) {
    printEvent("CLAMPED_TO_SOFT_LIMIT");
  }
}

void handleStop() {
  if (g_loopRunner.isRunning()) {
    g_loopRunner.stop();
  }
  g_directMoveActive = false;
  stepper->stopMove();  // FastAccelStepper's own graceful decel-to-stop
  printOk();
}

void printHelp() {
  Serial.println("# Commands:");
  Serial.println("#   SETHOME                    mark current position as 0mm");
  Serial.println("#   SETTRAVEL <mm>              soft-limit travel range [0, mm]");
  Serial.println("#   SETSTEPSPERMM <value>       steps-per-mm calibration (see hardware/pinout.md)");
  Serial.println("#   SETA [mm]                   mark current pos (or explicit mm) as A");
  Serial.println("#   SETB [mm]                   mark current pos (or explicit mm) as B");
  Serial.println("#   SETSPEED <mm/s>             peak speed for moves/loop legs");
  Serial.println("#   SETACCEL <mm/s2>            acceleration ceiling");
  Serial.println("#   SETDWELL A|B <s>            dwell time at A or B");
  Serial.println("#   SETREPEAT ON|OFF            loop forever vs one A-B-A cycle");
  Serial.println("#   MOVETO <mm>                 absolute move");
  Serial.println("#   JOG <mm>                    relative move (+/-)");
  Serial.println("#   STOP                        halt any move or loop");
  Serial.println("#   LOOPSTART                   begin A-B-A loop with current config");
  Serial.println("#   STATUS                      report position/phase/config");
  Serial.println("#   HELP                        this list");
  printOk();
}

void printStatus() {
  String s = "POS=" + String(g_currentPositionMm, 3);
  s += " PHASE=";
  s += g_loopRunner.isRunning() ? phaseName(g_loopRunner.phase()) : "IDLE";
  s += " HOMED=";
  s += g_homed ? "Y" : "N";
  s += " TRAVEL=";
  s += g_travelSet ? String(g_travelMm, 1) : String("UNSET");
  s += " STEPS_PER_MM=";
  s += g_calibrated ? String(g_stepsPerMm, 3) : String("UNSET");
  s += " A=";
  s += g_aSet ? String(g_posAMm, 2) : String("UNSET");
  s += " B=";
  s += g_bSet ? String(g_posBMm, 2) : String("UNSET");
  s += " SPEED=" + String(g_speedMmS, 2);
  s += " ACCEL=" + String(g_accelMmS2, 2);
  s += " DWELLA=" + String(g_dwellAS, 2);
  s += " DWELLB=" + String(g_dwellBS, 2);
  s += " REPEAT=";
  s += g_repeat ? "ON" : "OFF";
  printOk(s);
}

void handleCommand(String line) {
  line.trim();
  if (line.length() == 0) return;

  int sp = line.indexOf(' ');
  String verb = (sp == -1) ? line : line.substring(0, sp);
  String rest = (sp == -1) ? "" : line.substring(sp + 1);
  verb.toUpperCase();
  rest.trim();

  if (verb == "SETHOME") {
    g_currentPositionMm = 0.0;
    stepper->setCurrentPosition(0);
    g_homed = true;
    printOk();

  } else if (verb == "SETTRAVEL") {
    double v = rest.toFloat();
    if (v <= 0) {
      printErr("INVALID_VALUE");
      return;
    }
    g_travelMm = v;
    g_travelSet = true;
    printOk();

  } else if (verb == "SETSTEPSPERMM") {
    double v = rest.toFloat();
    if (v <= 0) {
      printErr("INVALID_VALUE");
      return;
    }
    g_stepsPerMm = v;
    g_calibrated = true;
    printOk();

  } else if (verb == "SETA" || verb == "SETB") {
    if (!requireReady()) return;
    double v = rest.length() ? rest.toFloat() : g_currentPositionMm;
    bool clamped = false;
    double clampedV = clampToSoftLimits(v, g_travelMm, &clamped);
    if (verb == "SETA") {
      g_posAMm = clampedV;
      g_aSet = true;
    } else {
      g_posBMm = clampedV;
      g_bSet = true;
    }
    if (clamped) printEvent("CLAMPED_TO_SOFT_LIMIT");
    printOk();

  } else if (verb == "SETSPEED") {
    double v = rest.toFloat();
    if (v <= 0) {
      printErr("INVALID_VALUE");
      return;
    }
    g_speedMmS = v;
    printOk();

  } else if (verb == "SETACCEL") {
    double v = rest.toFloat();
    if (v <= 0) {
      printErr("INVALID_VALUE");
      return;
    }
    g_accelMmS2 = v;
    printOk();

  } else if (verb == "SETDWELL") {
    int sp2 = rest.indexOf(' ');
    if (sp2 == -1) {
      printErr("USAGE_SETDWELL_A_OR_B_SECONDS");
      return;
    }
    String which = rest.substring(0, sp2);
    which.toUpperCase();
    double v = rest.substring(sp2 + 1).toFloat();
    if (v < 0) {
      printErr("INVALID_VALUE");
      return;
    }
    if (which == "A") {
      g_dwellAS = v;
    } else if (which == "B") {
      g_dwellBS = v;
    } else {
      printErr("USAGE_SETDWELL_A_OR_B_SECONDS");
      return;
    }
    printOk();

  } else if (verb == "SETREPEAT") {
    String v = rest;
    v.toUpperCase();
    if (v == "ON") {
      g_repeat = true;
    } else if (v == "OFF") {
      g_repeat = false;
    } else {
      printErr("USAGE_SETREPEAT_ON_OR_OFF");
      return;
    }
    printOk();

  } else if (verb == "MOVETO") {
    if (!requireReady()) return;
    beginDirectMove(rest.toFloat());
    printOk();

  } else if (verb == "JOG") {
    if (!requireReady()) return;
    beginDirectMove(g_currentPositionMm + rest.toFloat());
    printOk();

  } else if (verb == "STOP") {
    handleStop();

  } else if (verb == "LOOPSTART") {
    if (!requireReady()) return;
    if (!g_aSet || !g_bSet) {
      printErr("A_AND_B_REQUIRED");
      return;
    }
    if (g_loopRunner.isRunning()) {
      printErr("ALREADY_RUNNING");
      return;
    }
    LoopConfig cfg;
    cfg.pos_a_mm = g_posAMm;
    cfg.pos_b_mm = g_posBMm;
    cfg.peak_speed_mm_s = g_speedMmS;
    cfg.max_accel_mm_s2 = g_accelMmS2;
    cfg.dwell_at_a_s = g_dwellAS;
    cfg.dwell_at_b_s = g_dwellBS;
    cfg.repeat = g_repeat;
    g_directMoveActive = false;  // loop takes over position control
    armFollower();
    g_loopRunner.start(cfg, g_currentPositionMm);
    g_lastLoopPhase = g_loopRunner.phase();
    printOk();

  } else if (verb == "STATUS") {
    printStatus();

  } else if (verb == "HELP") {
    printHelp();

  } else {
    printErr("UNKNOWN_COMMAND");
  }
}

void controlTick(double dtS) {
  double targetMm = g_currentPositionMm;
  bool haveTarget = false;

  if (g_loopRunner.isRunning()) {
    targetMm = g_loopRunner.update(dtS);
    haveTarget = true;
    if (g_loopRunner.phase() != g_lastLoopPhase) {
      printEvent(String("PHASE ") + phaseName(g_loopRunner.phase()));
      g_lastLoopPhase = g_loopRunner.phase();
    }
  } else if (g_directMoveActive) {
    g_directMoveElapsedS += dtS;
    if (g_directMoveElapsedS >= g_directMoveProfile.duration_s) {
      targetMm = g_directMoveStartMm + g_directMoveProfile.distance_mm;
      g_directMoveActive = false;
    } else {
      targetMm =
          g_directMoveStartMm + g_directMoveProfile.positionAt(g_directMoveElapsedS);
    }
    haveTarget = true;
  }

  if (haveTarget) {
    g_currentPositionMm = targetMm;
    long targetSteps = lround(targetMm * g_stepsPerMm);
    stepper->moveTo(targetSteps);
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);  // let USB-serial enumerate before the first print

  Serial.println();
  Serial.println("=== Glide M1 motion core (serial control) ===");
  Serial.println("Type HELP for commands.");

  pinMode(PIN_EN, OUTPUT);
  digitalWrite(PIN_EN, LOW);  // enable driver (active-LOW)
  // Unlike M0's one-shot test, the driver stays enabled continuously
  // here: M1 needs to hold position during dwell phases and between
  // commands, not just while a move is actively happening.

  engine.init();
  stepper = engine.stepperConnectToPin(PIN_STEP);
  if (!stepper) {
    Serial.println("ERROR: could not connect stepper to STEP pin — halting");
    while (true) {
      delay(1000);
    }
  }
  stepper->setDirectionPin(PIN_DIR);
  // Safe idle defaults before the first real move — armFollower()
  // overwrites these with move-appropriate values every time a move
  // or loop actually starts.
  stepper->setSpeedInHz(1000);
  stepper->setAcceleration(2000);

  g_lastTickMs = millis();
}

void loop() {
  while (Serial.available()) {
    char c = static_cast<char>(Serial.read());
    if (c == '\n') {
      handleCommand(g_lineBuffer);
      g_lineBuffer = "";
    } else if (c != '\r') {
      g_lineBuffer += c;
    }
  }

  unsigned long now = millis();
  if (now - g_lastTickMs >= CONTROL_TICK_MS) {
    double dtS = (now - g_lastTickMs) / 1000.0;
    g_lastTickMs = now;
    controlTick(dtS);
  }
}
