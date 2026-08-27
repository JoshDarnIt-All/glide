// Glide — M1/M2 motion core (serial control + persistent config)
//
// Supersedes the M0 one-shot bench test (which proved the driver,
// wiring, and microstepping were correct — see hardware/pinout.md).
// This is the real motion core: homing, soft limits, positions in mm
// (not raw steps), S-curve ease in/out, and an A-B-A loop, all driven
// by typed serial commands. Type HELP once connected for the command
// list; see docs/serial_protocol.md for the full protocol design.
//
// M2 addition: axis config (travel/calibration) and named presets
// persist to /config.json on LittleFS (see firmware/lib/config) and
// auto-load on boot. Home does NOT persist — SETHOME is required
// fresh every boot regardless of what's saved. See config_store.h and
// docs/serial_protocol.md for the reasoning.
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
// and every CONTROL_TICK_MS: (1) reads the profile's INSTANTANEOUS
// VELOCITY at the current elapsed time and sets FastAccelStepper's
// speed to match it, then (2) issues stepper->moveTo() toward the
// profile's position at that time, letting FastAccelStepper actually
// run at close to the real profile speed rather than sprint-and-idle.
//
// An earlier version of this file only did (2) — position waypoints,
// with FastAccelStepper's own speed set to an artificially huge fixed
// value so it would "sprint" to each 20ms waypoint and then sit idle
// until the next one. That produced a real, audible burst-then-pause
// pattern 50 times a second (confirmed on the bench: loud, vibrating
// motion at SPEED=200 ACCEL=30). Tracking instantaneous velocity
// instead keeps the stepper continuously moving near the real S-curve
// speed, so FastAccelStepper's own (trapezoidal) ramp only has to
// close the small tick-to-tick speed DELTA, not jump from a standing
// stop to a huge sprint speed every 20ms.
//
// This has not been re-verified on hardware yet — it's a reasoned fix
// for the specific bursting mechanism described above, not a
// guaranteed cure. If motion still isn't smooth after this, the next
// levers are: a faster CONTROL_TICK_MS (less time between velocity
// updates), the TMC2209's StealthChop/SpreadCycle mode jumper (if
// this board has one — worth checking), or the current trimpot
// (undercurrent can itself cause vibration/skipping, and it was never
// individually tuned — see hardware/pinout.md).

#include <Arduino.h>
#include <FastAccelStepper.h>
#include <LittleFS.h>

#include <vector>

#include "config_store.h"
#include "loop_runner.h"
#include "scurve.h"
#include "soft_limits.h"

using glide::AxisConfig;
using glide::buildSCurveProfile;
using glide::clampToSoftLimits;
using glide::DeviceConfig;
using glide::loadConfig;
using glide::LoopConfig;
using glide::LoopPhase;
using glide::LoopRunner;
using glide::PresetConfig;
using glide::saveConfig;
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

// M2: named presets, loaded from /config.json on boot (if present)
// and saved back on SAVEPRESET/DELETEPRESET. Note g_travelMm and
// g_stepsPerMm ALSO persist (see config_store.h) but g_homed does
// NOT -- home is always re-established fresh via SETHOME each boot.
std::vector<PresetConfig> g_presets;

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

// Sets FastAccelStepper's speed to match the S-curve profile's actual
// instantaneous velocity right now, converted to a step rate via the
// measured steps-per-mm calibration — see the file-header note on why
// this replaced the old fixed-huge-speed "sprint to waypoint"
// approach. Acceleration only needs to close the small tick-to-tick
// speed DELTA (not jump from zero every tick), so it's set relative
// to one control tick's worth of time rather than an arbitrary
// multiplier.
void applyFollowerVelocity(double instVelocityMmS) {
  double stepRateHz = fabs(instVelocityMmS) * g_stepsPerMm;
  // Small floor so FastAccelStepper always has *some* nonzero speed
  // set (avoids a stall waiting on a literal 0 Hz target) without
  // meaningfully affecting motion at the near-zero-velocity instants
  // right at the start/end of a move, which is what true ease in/out
  // looks like anyway.
  uint32_t hz = static_cast<uint32_t>(stepRateHz);
  if (hz < 50) hz = 50;
  stepper->setSpeedInHz(hz);
  // Enough acceleration to reach a new tick's speed within about half
  // a tick period, not an instant jump — this is what actually fixes
  // the burst-then-pause pattern from the old approach.
  double tickSeconds = CONTROL_TICK_MS / 1000.0;
  uint32_t accel = static_cast<uint32_t>(hz / (tickSeconds * 0.5)) + 500;
  stepper->setAcceleration(accel);
}

void beginDirectMove(double targetMm) {
  bool clamped = false;
  double clampedTarget = clampToSoftLimits(targetMm, g_travelMm, &clamped);
  g_directMoveStartMm = g_currentPositionMm;
  g_directMoveProfile = buildSCurveProfile(
      clampedTarget - g_currentPositionMm, g_speedMmS, g_accelMmS2);
  g_directMoveElapsedS = 0.0;
  g_directMoveActive = true;
  // No arm-once call here anymore -- controlTick() sets the follower
  // speed fresh every tick from the profile's instantaneous velocity.
  if (clamped) {
    printEvent("CLAMPED_TO_SOFT_LIMIT");
  }
}

// Starts (or restarts) the loop using whatever's currently in
// g_posAMm/g_posBMm/g_speedMmS/etc. Shared by LOOPSTART and
// LOADPRESET -- LOADPRESET first copies a saved preset's values into
// those same globals, then calls this, so "recall a preset" and
// "start the loop" are the same underlying action. Cancels any
// in-progress direct move or loop first; since buildSCurveProfile
// always starts from wherever the carriage currently IS (not from A),
// this naturally produces a smooth transition into the new target
// rather than a jump -- see loop_runner.h's note on this.
void startLoopFromCurrentSettings() {
  if (g_loopRunner.isRunning()) {
    g_loopRunner.stop();
  }
  g_directMoveActive = false;
  LoopConfig cfg;
  cfg.pos_a_mm = g_posAMm;
  cfg.pos_b_mm = g_posBMm;
  cfg.peak_speed_mm_s = g_speedMmS;
  cfg.max_accel_mm_s2 = g_accelMmS2;
  cfg.dwell_at_a_s = g_dwellAS;
  cfg.dwell_at_b_s = g_dwellBS;
  cfg.repeat = g_repeat;
  g_loopRunner.start(cfg, g_currentPositionMm);
  g_lastLoopPhase = g_loopRunner.phase();
}

// -1 if not found. Case-INSENSITIVE match -- command verbs are
// already normalized to uppercase before dispatch (so LOADPRESET,
// loadpreset, LoadPreset all work identically), but the preset NAME
// argument wasn't getting the same treatment, which meant a name
// saved as "Test2" couldn't be recalled by typing "test2" -- a real
// usability trap, not an intentional strictness. Matching names the
// same forgiving way the command word already is.
int findPresetIndex(const String &name) {
  for (size_t i = 0; i < g_presets.size(); ++i) {
    if (g_presets[i].name.equalsIgnoreCase(name)) return static_cast<int>(i);
  }
  return -1;
}

// Builds the DeviceConfig snapshot that SAVECONFIG/SAVEPRESET/
// DELETEPRESET all write to flash -- always the full current state
// (axis config + all presets), since /config.json is one document,
// not one file per setting.
DeviceConfig buildDeviceConfigSnapshot() {
  DeviceConfig config;
  AxisConfig axis;
  axis.travel_mm = g_travelMm;
  axis.steps_per_mm = g_stepsPerMm;
  config.axes.push_back(axis);
  config.presets = g_presets;
  return config;
}

// Writes the current snapshot to flash, reporting ERR on failure
// (e.g. filesystem full or not mounted) instead of silently pretending
// it worked -- callers should NOT printOk() themselves when this
// returns false.
bool persistConfig() {
  if (!saveConfig(buildDeviceConfigSnapshot())) {
    printErr("SAVE_FAILED");
    return false;
  }
  return true;
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
  Serial.println("#   SETTRAVEL <mm>              soft-limit travel from home (+ or -)");
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
  Serial.println("#   SAVECONFIG                  persist travel/calibration + all presets to flash");
  Serial.println("#   SAVEPRESET <name>           save current A/B/speed/accel/dwell/repeat as a named preset (persists immediately)");
  Serial.println("#   LOADPRESET <name>           recall a preset and immediately start moving to it");
  Serial.println("#   LISTPRESETS                 list all saved presets");
  Serial.println("#   DELETEPRESET <name>         remove a saved preset (persists immediately)");
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
  s += " PRESETS=" + String(static_cast<int>(g_presets.size()));
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
    // Signed on purpose: home isn't required to be the low end of the
    // rail. SETTRAVEL 500 means "rail extends 500mm positive from
    // home"; SETTRAVEL -500 means "rail extends 500mm negative from
    // home" -- whichever matches where you actually homed. Only exactly
    // zero is rejected (zero usable travel isn't a real range).
    double v = rest.toFloat();
    if (v == 0) {
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
    startLoopFromCurrentSettings();
    printOk();

  } else if (verb == "SAVECONFIG") {
    if (persistConfig()) printOk();

  } else if (verb == "SAVEPRESET") {
    if (rest.length() == 0) {
      printErr("USAGE_SAVEPRESET_NAME");
      return;
    }
    PresetConfig preset;
    preset.name = rest;
    preset.pos_a_mm = g_posAMm;
    preset.pos_b_mm = g_posBMm;
    preset.speed_mm_s = g_speedMmS;
    preset.accel_mm_s2 = g_accelMmS2;
    preset.dwell_a_s = g_dwellAS;
    preset.dwell_b_s = g_dwellBS;
    preset.repeat = g_repeat;
    int idx = findPresetIndex(rest);
    if (idx >= 0) {
      g_presets[idx] = preset;  // overwrite existing preset of this name
    } else {
      g_presets.push_back(preset);
    }
    // Presets are meant to be durable the instant you save them --
    // written to flash immediately, unlike the working SET* values
    // (SETSPEED etc.), which change often during tuning and would
    // wear flash needlessly if every one of them auto-saved.
    if (persistConfig()) printOk();

  } else if (verb == "LOADPRESET") {
    if (!requireReady()) return;
    if (rest.length() == 0) {
      printErr("USAGE_LOADPRESET_NAME");
      return;
    }
    int idx = findPresetIndex(rest);
    if (idx < 0) {
      printErr("NOT_FOUND");
      return;
    }
    const PresetConfig &preset = g_presets[idx];
    bool clampedA = false, clampedB = false;
    // Re-clamp against CURRENT soft limits -- a preset saved under a
    // different SETTRAVEL could otherwise recall a position that's no
    // longer in range.
    g_posAMm = clampToSoftLimits(preset.pos_a_mm, g_travelMm, &clampedA);
    g_posBMm = clampToSoftLimits(preset.pos_b_mm, g_travelMm, &clampedB);
    g_aSet = true;
    g_bSet = true;
    g_speedMmS = preset.speed_mm_s;
    g_accelMmS2 = preset.accel_mm_s2;
    g_dwellAS = preset.dwell_a_s;
    g_dwellBS = preset.dwell_b_s;
    g_repeat = preset.repeat;
    if (clampedA || clampedB) {
      printEvent("CLAMPED_TO_SOFT_LIMIT");
    }
    // Recall-and-go: immediately starts moving toward the preset,
    // smoothly transitioning from wherever the carriage currently is
    // -- see startLoopFromCurrentSettings()'s comment.
    startLoopFromCurrentSettings();
    printOk();

  } else if (verb == "LISTPRESETS") {
    if (g_presets.empty()) {
      printOk("NONE");
    } else {
      for (const PresetConfig &preset : g_presets) {
        String s = preset.name;
        s += " A=" + String(preset.pos_a_mm, 2);
        s += " B=" + String(preset.pos_b_mm, 2);
        s += " SPEED=" + String(preset.speed_mm_s, 2);
        s += " ACCEL=" + String(preset.accel_mm_s2, 2);
        s += " DWELLA=" + String(preset.dwell_a_s, 2);
        s += " DWELLB=" + String(preset.dwell_b_s, 2);
        s += " REPEAT=";
        s += preset.repeat ? "ON" : "OFF";
        printEvent(s);
      }
      printOk();
    }

  } else if (verb == "DELETEPRESET") {
    if (rest.length() == 0) {
      printErr("USAGE_DELETEPRESET_NAME");
      return;
    }
    int idx = findPresetIndex(rest);
    if (idx < 0) {
      printErr("NOT_FOUND");
      return;
    }
    g_presets.erase(g_presets.begin() + idx);
    if (persistConfig()) printOk();

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
  double instVelocityMmS = 0.0;
  bool haveTarget = false;

  if (g_loopRunner.isRunning()) {
    targetMm = g_loopRunner.update(dtS);
    instVelocityMmS = g_loopRunner.currentVelocity();
    haveTarget = true;
    if (g_loopRunner.phase() != g_lastLoopPhase) {
      printEvent(String("PHASE ") + phaseName(g_loopRunner.phase()));
      g_lastLoopPhase = g_loopRunner.phase();
    }
  } else if (g_directMoveActive) {
    g_directMoveElapsedS += dtS;
    if (g_directMoveElapsedS >= g_directMoveProfile.duration_s) {
      targetMm = g_directMoveStartMm + g_directMoveProfile.distance_mm;
      instVelocityMmS = 0.0;  // move is finishing this tick
      g_directMoveActive = false;
    } else {
      targetMm = g_directMoveStartMm +
                 g_directMoveProfile.positionAt(g_directMoveElapsedS);
      instVelocityMmS = g_directMoveProfile.velocityAt(g_directMoveElapsedS);
    }
    haveTarget = true;
  }

  if (haveTarget) {
    g_currentPositionMm = targetMm;
    applyFollowerVelocity(instVelocityMmS);
    long targetSteps = lround(targetMm * g_stepsPerMm);
    stepper->moveTo(targetSteps);
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);  // let USB-serial enumerate before the first print

  Serial.println();
  Serial.println("=== Glide M1/M2 motion core (serial control) ===");
  Serial.println("Type HELP for commands.");

  // true = format the filesystem if it's missing/corrupt, which is
  // the normal case on a brand-new board's very first boot (there's
  // no config.json yet because LittleFS itself doesn't exist yet).
  if (!LittleFS.begin(true)) {
    Serial.println("ERROR: LittleFS mount failed -- presets/config will not persist this session");
  } else {
    DeviceConfig loaded;
    if (loadConfig(loaded) && !loaded.axes.empty()) {
      g_travelMm = loaded.axes[0].travel_mm;
      g_stepsPerMm = loaded.axes[0].steps_per_mm;
      g_travelSet = (g_travelMm != 0.0);
      g_calibrated = (g_stepsPerMm > 0.0);
      g_presets = loaded.presets;
      Serial.printf(
          "Loaded config.json: TRAVEL=%.1f STEPS_PER_MM=%.3f, %d preset(s)\n",
          g_travelMm, g_stepsPerMm, static_cast<int>(g_presets.size()));
    } else {
      Serial.println(
          "No usable config.json found -- starting fresh (normal on first "
          "boot). SETHOME is always required regardless.");
    }
  }
  // Home is NEVER loaded from flash, on purpose -- see config_store.h
  // and docs/serial_protocol.md. g_homed stays false here no matter
  // what the file contains.

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
  // Safe idle defaults before the first real move — controlTick()
  // overwrites these every tick once a move or loop is active.
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
