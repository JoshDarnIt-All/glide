// Glide — M1/M2/M3 motion core (serial control + persistent config +
// REST/WebSocket/OTA)
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
// M3 addition: WiFi (WiFiManager), a REST + WebSocket API
// (ESPAsyncWebServer, see setupApiRoutes()) and OTA firmware updates
// (firmware/lib/api/ota_handler). The serial protocol above is
// untouched and still the primary bench/debug interface -- the
// network API is "just another input source" layered on top via
// runCommandForApi(), which runs a REST-originated command through
// the exact same handleCommand() dispatch. See docs/api.md for the
// full network contract.
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
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <FastAccelStepper.h>
#include <LittleFS.h>
#include <WiFi.h>

#include <vector>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "config_store.h"
#include "loop_runner.h"
#include "ota_handler.h"
#include "scurve.h"
#include "soft_limits.h"
#include "webui_assets.h"
#include "wifi_setup.h"

#if __has_include("secrets.h")
#include "secrets.h"
#endif
#ifndef GLIDE_OTA_KEY
// No secrets.h (or it didn't define this) -- fall back to a compiled-
// in default so OTA still works out of the box on a single LAN-only
// bench device. See firmware/include/secrets.h.example. Loudly warned
// about at boot in setup() below, not silently accepted.
#define GLIDE_OTA_KEY "glide-default-ota-key-change-me"
#endif

// M4: shown on the Device screen and returned from GET /api/v1/wifi.
// Manually maintained, not derived from git -- bump it when a change
// is worth a user seeing it changed (a real OTA update, a meaningful
// behavior change), not on every commit.
constexpr const char *GLIDE_FIRMWARE_VERSION = "0.4.0-m4";

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

// M3: instantaneous velocity, mirroring g_currentPositionMm -- set
// every controlTick() alongside position, so STATUS/the REST API can
// report "velocity_mm_s" without re-deriving it. Previously this
// existed only as controlTick()'s own local instVelocityMmS and was
// never exposed anywhere.
double g_currentVelocityMmS = 0.0;

// M3: name of the preset currently "live" -- set on a successful
// LOADPRESET, cleared the instant any parameter/move happens after
// that isn't itself a fresh preset load (clear-on-divergence; see
// docs/api.md's active_preset field and docs/m4_ui_plan.md's "active
// preset chip"). Empty string means "no preset is active."
String g_activePresetName;

// M3: REST + WebSocket network API. See firmware/lib/api for the
// reusable pieces (WiFi/mDNS setup, OTA upload handling) and
// docs/api.md for the full contract these routes implement.
AsyncWebServer g_apiServer(80);
AsyncWebSocket g_apiWs("/ws");
unsigned long g_lastWsStatusMs = 0;
unsigned long g_lastWsHeartbeatMs = 0;

// Guards every read/write of motion state (the g_* globals above,
// g_loopRunner, g_presets) against the real cross-task race between
// loop()'s own thread and AsyncTCP's task, which is what REST/
// WebSocket handlers run on.
//
// The original design deferred each REST handler's actual work into
// a queue drained from loop(), specifically to avoid touching motion
// globals from AsyncTCP's task at all -- but that broke on real
// hardware: ESPAsyncWebServer requires request->send() to be called
// SYNCHRONOUSLY, before the handler function returns (confirmed via
// the exact "Handler did not handle the request" fallback it sends
// when a handler returns without calling send() itself -- every route
// hit this on first hardware test). So REST handlers now do their
// work directly on AsyncTCP's task, synchronously, guarded by this
// mutex instead of being deferred -- loop() takes the same mutex
// around its own motion-state access (serial command dispatch,
// controlTick()) so the two are never touching this state at once.
SemaphoreHandle_t g_motionMutex = nullptr;

// RAII scoped lock for g_motionMutex -- several handlers below have
// more than one return path (e.g. an early 404), and a plain
// take/give pair is easy to get wrong once there's more than one
// `return` between them. Declaring one of these as a local variable
// takes the mutex immediately and releases it whenever that scope
// ends, return included.
struct MotionLock {
  MotionLock() { xSemaphoreTake(g_motionMutex, portMAX_DELAY); }
  ~MotionLock() { xSemaphoreGive(g_motionMutex); }
  MotionLock(const MotionLock &) = delete;
  MotionLock &operator=(const MotionLock &) = delete;
};

// ---- Serial reply helpers ----
// OK/ERR for direct command replies, "# " for asynchronous events
// (like a loop phase changing) so they're visually distinct from a
// reply to whatever you just typed.
//
// M3 addition: a REST handler needs handleCommand()'s OK/ERR outcome
// as DATA (to turn into an HTTP status + JSON body), not a serial
// print. Rather than duplicate every command's branch logic for the
// network path, these helpers also capture the outcome into
// g_captured* whenever g_capturingReply is set (see
// runCommandForApi() below) -- Serial still gets the print either way,
// so watching the bench monitor shows network-originated commands too.
bool g_capturingReply = false;
bool g_capturedOk = false;
String g_capturedDetail;

void printOk() {
  Serial.println("OK");
  if (g_capturingReply) {
    g_capturedOk = true;
    g_capturedDetail = "";
  }
}
void printOk(const String &data) {
  Serial.print("OK ");
  Serial.println(data);
  if (g_capturingReply) {
    g_capturedOk = true;
    g_capturedDetail = data;
  }
}
void printErr(const char *reason) {
  Serial.print("ERR ");
  Serial.println(reason);
  if (g_capturingReply) {
    g_capturedOk = false;
    g_capturedDetail = reason;
  }
}
void printEvent(const String &msg) {
  Serial.print("# ");
  Serial.println(msg);
}

// Forward declarations -- handleCommand() is the main serial dispatch
// (defined much further down, after all the other handler helpers),
// and wsBroadcastEvent() lives with the other M3 JSON/WebSocket
// helpers just after printStatus() -- both are needed by functions
// defined earlier in the file than they are.
void handleCommand(String line);
void wsBroadcastEvent(const char *event, const String &extraJson = "");

// Runs `line` through the exact same handleCommand() dispatch serial
// commands use, capturing its OK/ERR outcome as data instead of
// letting it only go to Serial. This is THE mechanism that makes REST
// "just another input source" rather than a second control path (see
// docs/api.md's architecture section) -- every mutating REST endpoint
// below ultimately calls this with a serial-equivalent command string.
// No default member initializers here on purpose -- this ESP32
// Arduino toolchain compiles below the C++14 rule that lets an
// aggregate keep default member initializers (see the file-header
// note on std::clamp for the same class of "toolchain default is
// older than expected" issue). A struct with a default member
// initializer stops being a plain aggregate on this compiler, which
// broke every `CommandResult{ok, detail}`-style construction below
// with "no matching constructor." Plain members instead: every
// construction site here already supplies both values explicitly.
struct CommandResult {
  bool ok;
  String detail;  // "" for a bare OK, or the ERR reason
};
CommandResult runCommandForApi(const String &line) {
  g_capturingReply = true;
  g_capturedOk = false;
  g_capturedDetail = "";
  handleCommand(line);
  g_capturingReply = false;
  return CommandResult{g_capturedOk, g_capturedDetail};
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

// True if anything is actively controlling the stepper right now -- a
// direct MOVETO/JOG move, or the A-B-A loop (moving OR dwelling; only
// Idle/Stopped count as "not active"). A direct move and the loop are
// two separate state machines layered over the same stepper, so
// neither one alone answers "is the carriage doing something."
bool isActive() { return g_directMoveActive || g_loopRunner.isRunning(); }

// Unified phase for anything external (STATUS today; the M3 REST/
// WebSocket API next) that needs "what is the carriage doing right
// now" -- IDLE, MOVING, MOVING_TO_A, MOVING_TO_B, DWELLING_AT_A,
// DWELLING_AT_B, STOPPED (see docs/api.md's phase enum). Before this
// fix, printStatus() reported PHASE=IDLE whenever the A-B-A loop
// wasn't running -- even mid a plain MOVETO/JOG -- and separately
// collapsed the loop's own Stopped state back to IDLE too, since
// isRunning() is false once stopped and the old ternary gated
// phaseName() behind it. This restores both distinctions instead of
// checking isRunning() at all.
const char *reportedPhaseName() {
  if (g_directMoveActive) {
    return "MOVING";
  }
  return phaseName(g_loopRunner.phase());
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
    wsBroadcastEvent("clamped_to_soft_limit");
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

// Strips control characters (anything below ASCII 32) out of a name
// before it's stored. Defense-in-depth alongside the loop()'s
// backspace/DEL handling: even if some other input path ever
// introduces a stray control byte, a preset name can't silently end
// up containing one -- see config_store.cpp's matching helper, which
// self-heals names already corrupted from before that fix existed.
String sanitizePresetName(const String &raw) {
  String cleaned;
  cleaned.reserve(raw.length());
  for (size_t i = 0; i < raw.length(); ++i) {
    if (static_cast<unsigned char>(raw[i]) >= 32) {
      cleaned += raw[i];
    }
  }
  return cleaned;
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
  s += reportedPhaseName();
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

// ---- M3: JSON builders shared by REST responses and the WebSocket
// status push -- see docs/api.md for the exact response shapes these
// match. Callers are responsible for holding g_motionMutex while
// calling these (see setupApiRoutes() and apiTick()) -- these
// functions don't take it themselves so a caller that already needs
// the mutex for other work in the same handler doesn't have to
// release and immediately re-take it.
String buildStatusJson() {
  JsonDocument doc;
  doc["position_mm"] = g_currentPositionMm;
  doc["velocity_mm_s"] = g_currentVelocityMmS;
  doc["phase"] = reportedPhaseName();
  doc["homed"] = g_homed;
  doc["travel_set"] = g_travelSet;
  doc["calibrated"] = g_calibrated;
  doc["travel_mm"] = g_travelMm;
  if (g_activePresetName.length()) {
    doc["active_preset"] = g_activePresetName;
  } else {
    doc["active_preset"] = nullptr;
  }
  String out;
  serializeJson(doc, out);
  return out;
}

String buildAxisJson() {
  JsonDocument doc;
  doc["travel_mm"] = g_travelMm;
  doc["steps_per_mm"] = g_stepsPerMm;
  String out;
  serializeJson(doc, out);
  return out;
}

String buildLoopConfigJson() {
  JsonDocument doc;
  doc["pos_a_mm"] = g_posAMm;
  doc["pos_b_mm"] = g_posBMm;
  doc["speed_mm_s"] = g_speedMmS;
  doc["accel_mm_s2"] = g_accelMmS2;
  doc["dwell_a_s"] = g_dwellAS;
  doc["dwell_b_s"] = g_dwellBS;
  doc["repeat"] = g_repeat;
  String out;
  serializeJson(doc, out);
  return out;
}

void presetToJson(const PresetConfig &preset, JsonObject obj) {
  obj["name"] = preset.name;
  obj["pos_a_mm"] = preset.pos_a_mm;
  obj["pos_b_mm"] = preset.pos_b_mm;
  obj["speed_mm_s"] = preset.speed_mm_s;
  obj["accel_mm_s2"] = preset.accel_mm_s2;
  obj["dwell_a_s"] = preset.dwell_a_s;
  obj["dwell_b_s"] = preset.dwell_b_s;
  obj["repeat"] = preset.repeat;
}

String buildPresetsJson() {
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (const PresetConfig &preset : g_presets) {
    presetToJson(preset, arr.add<JsonObject>());
  }
  String out;
  serializeJson(doc, out);
  return out;
}

String buildSinglePresetJson(const PresetConfig &preset) {
  JsonDocument doc;
  presetToJson(preset, doc.to<JsonObject>());
  String out;
  serializeJson(doc, out);
  return out;
}

String buildWifiJson() {
  JsonDocument doc;
  doc["ssid"] = WiFi.SSID();
  doc["rssi"] = WiFi.RSSI();
  doc["ip"] = WiFi.localIP().toString();
  doc["firmware_version"] = GLIDE_FIRMWARE_VERSION;
  String out;
  serializeJson(doc, out);
  return out;
}

// Maps a serial-style ERR reason to the HTTP status docs/api.md's
// error table specifies for it.
int httpStatusForError(const String &err) {
  if (err == "NOT_HOMED" || err == "TRAVEL_NOT_SET" ||
      err == "NOT_CALIBRATED" || err == "ALREADY_RUNNING" ||
      err == "MOVING" || err == "OTA_IN_PROGRESS") {
    return 409;
  }
  if (err == "NOT_FOUND") return 404;
  if (err == "UNAUTHORIZED") return 401;
  return 400;  // INVALID_VALUE and anything unexpected
}

void sendCommandResult(AsyncWebServerRequest *request,
                        const CommandResult &result) {
  if (result.ok) {
    request->send(200, "application/json", "{\"ok\":true}");
  } else {
    request->send(httpStatusForError(result.detail), "application/json",
                  "{\"error\":\"" + result.detail + "\"}");
  }
}

// One WebSocket frame type -- see docs/api.md's WebSocket section.
// Skips building/serializing anything if nobody's listening.
void wsBroadcastEvent(const char *event, const String &extraJson) {
  if (g_apiWs.count() == 0) return;
  String out = String("{\"type\":\"event\",\"event\":\"") + event + "\"";
  if (extraJson.length()) {
    out += "," + extraJson;
  }
  out += "}";
  g_apiWs.textAll(out);
}

void wsBroadcastStatus() {
  if (g_apiWs.count() == 0) return;
  // buildStatusJson() returns a plain status object; the WebSocket
  // frame needs the same fields plus "type":"status" -- rebuilding
  // via the same JsonDocument fields (rather than string-surgery on
  // buildStatusJson()'s output) keeps this from silently drifting out
  // of sync with the REST GET /status shape.
  JsonDocument doc;
  doc["type"] = "status";
  doc["position_mm"] = g_currentPositionMm;
  doc["velocity_mm_s"] = g_currentVelocityMmS;
  doc["phase"] = reportedPhaseName();
  doc["homed"] = g_homed;
  doc["travel_set"] = g_travelSet;
  doc["calibrated"] = g_calibrated;
  if (g_activePresetName.length()) {
    doc["active_preset"] = g_activePresetName;
  } else {
    doc["active_preset"] = nullptr;
  }
  String out;
  serializeJson(doc, out);
  g_apiWs.textAll(out);
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
    g_activePresetName = "";  // manual A/B change diverges from any loaded preset
    if (clamped) {
      printEvent("CLAMPED_TO_SOFT_LIMIT");
      wsBroadcastEvent("clamped_to_soft_limit");
    }
    printOk();

  } else if (verb == "SETSPEED") {
    double v = rest.toFloat();
    if (v <= 0) {
      printErr("INVALID_VALUE");
      return;
    }
    g_speedMmS = v;
    g_activePresetName = "";
    printOk();

  } else if (verb == "SETACCEL") {
    double v = rest.toFloat();
    if (v <= 0) {
      printErr("INVALID_VALUE");
      return;
    }
    g_accelMmS2 = v;
    g_activePresetName = "";
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
    g_activePresetName = "";
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
    g_activePresetName = "";
    printOk();

  } else if (verb == "MOVETO") {
    if (!requireReady()) return;
    if (glide::otaInProgress()) {
      printErr("OTA_IN_PROGRESS");
      return;
    }
    // Reject only if the A-B-A LOOP is running -- before this check
    // existed at all, a MOVETO issued while the loop was running set
    // g_directMoveActive true, but controlTick()'s loop-takes-priority
    // branch meant it was never actually acted on -- the command
    // replied OK and nothing happened. Matches docs/api.md's `409
    // MOVING` contract for the REST equivalent.
    //
    // Deliberately NOT checking g_directMoveActive here (an earlier
    // version of this check used the broader isActive(), which also
    // covers direct moves) -- that conflict doesn't exist for two
    // direct moves back to back: beginDirectMove() always computes a
    // fresh profile from wherever the carriage actually is right now,
    // the same smooth-retarget behavior LOADPRESET already relies on
    // to interrupt-and-go. Blocking a JOG because the PREVIOUS jog
    // hadn't finished its own short move yet made rapid repeated
    // jogging (tap-tap-tap, or a press-and-hold sending jogs on an
    // interval) feel broken -- confirmed on real hardware: holding the
    // Control screen's jog button got stuck reporting MOVING instead
    // of just continuing to nudge.
    if (g_loopRunner.isRunning()) {
      printErr("MOVING");
      return;
    }
    beginDirectMove(rest.toFloat());
    g_activePresetName = "";  // manual move diverges from any loaded preset
    printOk();

  } else if (verb == "JOG") {
    // Deliberately NOT requireHomed() here, unlike MOVETO -- JOG is a
    // RELATIVE move from wherever the carriage currently is, so it
    // doesn't need a home reference to be meaningful the way an
    // absolute MOVETO target does. This matters in practice: if the
    // carriage is sitting somewhere inconvenient after a power cycle,
    // jogging it to a safe spot is the whole reason to jog BEFORE
    // homing, then set home once it's there -- exactly what the
    // web UI's own not-homed banner already says ("Jog to a safe
    // reference point, then set home to unlock motion"), which the
    // firmware wasn't actually honoring until this fix. Still
    // requires travel range + calibration, since those are what let a
    // relative mm delta and the soft-limit clamp mean anything at all
    // (and both normally persist across reboots via config.json,
    // unlike home).
    if (!requireTravel() || !requireCalibrated()) return;
    if (glide::otaInProgress()) {
      printErr("OTA_IN_PROGRESS");
      return;
    }
    if (g_loopRunner.isRunning()) {
      printErr("MOVING");
      return;
    }
    beginDirectMove(g_currentPositionMm + rest.toFloat());
    g_activePresetName = "";
    printOk();

  } else if (verb == "STOP") {
    handleStop();

  } else if (verb == "LOOPSTART") {
    if (!requireReady()) return;
    if (glide::otaInProgress()) {
      printErr("OTA_IN_PROGRESS");
      return;
    }
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
    String cleanName = sanitizePresetName(rest);
    if (cleanName.length() == 0) {
      printErr("USAGE_SAVEPRESET_NAME");
      return;
    }
    PresetConfig preset;
    preset.name = cleanName;
    preset.pos_a_mm = g_posAMm;
    preset.pos_b_mm = g_posBMm;
    preset.speed_mm_s = g_speedMmS;
    preset.accel_mm_s2 = g_accelMmS2;
    preset.dwell_a_s = g_dwellAS;
    preset.dwell_b_s = g_dwellBS;
    preset.repeat = g_repeat;
    int idx = findPresetIndex(cleanName);
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
    if (glide::otaInProgress()) {
      printErr("OTA_IN_PROGRESS");
      return;
    }
    if (rest.length() == 0) {
      printErr("USAGE_LOADPRESET_NAME");
      return;
    }
    int idx = findPresetIndex(rest);
    if (idx < 0) {
      // Diagnostic: show exactly what was searched for and its
      // length, so it can be compared against LISTPRESETS's
      // [name](len=N) output to spot a hidden-character mismatch.
      printEvent("SEARCHED_FOR=[" + rest + "](len=" +
                 String(rest.length()) + ")");
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
      wsBroadcastEvent("clamped_to_soft_limit");
    }
    // Use the preset's own stored (already-sanitized) name, not the
    // raw typed argument -- LOADPRESET matches case-insensitively, so
    // "test2" and the stored "Test2" would otherwise disagree here.
    g_activePresetName = preset.name;
    // Recall-and-go: immediately starts moving toward the preset,
    // smoothly transitioning from wherever the carriage currently is
    // -- see startLoopFromCurrentSettings()'s comment.
    startLoopFromCurrentSettings();
    wsBroadcastEvent("preset_loaded", "\"name\":\"" + preset.name + "\"");
    printOk();

  } else if (verb == "LISTPRESETS") {
    if (g_presets.empty()) {
      printOk("NONE");
    } else {
      for (const PresetConfig &preset : g_presets) {
        // Wrapped in [] with an explicit length so a hidden/invisible
        // character in a name (stray whitespace, a copy-paste
        // artifact) becomes visible as unexpected padding inside the
        // brackets or a length that doesn't match what it looks like
        // -- printing the name bare wouldn't reveal that.
        String s = "[" + preset.name + "](len=" +
                   String(preset.name.length()) + ")";
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
      printEvent("SEARCHED_FOR=[" + rest + "](len=" +
                 String(rest.length()) + ")");
      printErr("NOT_FOUND");
      return;
    }
    // Deleting the preset that's currently "active" leaves nothing for
    // that name to still refer to -- clear it rather than leave a
    // stale reference to a preset that no longer exists.
    if (g_presets[idx].name.equalsIgnoreCase(g_activePresetName)) {
      g_activePresetName = "";
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

  // Real bug found on the first hardware test of M4: the WebSocket
  // "status" push in apiTick() only fires while isActive() (something
  // is physically moving) -- that's the right call for the ~10Hz
  // position stream during a move, but it meant SETHOME/SETTRAVEL/
  // SETSTEPSPERMM (none of which move anything) never told the web UI
  // they'd happened at all. The Setup screen's checkmarks and progress
  // bar read homed/travel_set/calibrated from that same WS status
  // signal, so tapping "Set Home" visibly did nothing even though the
  // firmware genuinely recorded it (confirmed: a direct curl POST
  // /home returned {"ok":true} and printed OK on serial the whole
  // time). Broadcasting once here, unconditionally, after every
  // command that didn't hit an early `return` above (i.e. every one
  // that actually took effect) covers every field a client might be
  // waiting to see change, not just the three this bug was caught on --
  // cheap and safe to call unconditionally since wsBroadcastStatus()
  // itself already no-ops when nobody's connected, and this runs at
  // command-typing/button-tapping frequency, nowhere near the 10Hz
  // motion stream's own volume.
  wsBroadcastStatus();
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
      wsBroadcastEvent(
          "phase_change",
          String("\"phase\":\"") + phaseName(g_loopRunner.phase()) + "\"");
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

  // Mirrors g_currentPositionMm regardless of haveTarget, so a caller
  // (STATUS, the REST API) sees a real 0 once motion actually stops,
  // not a stale nonzero value from the last active tick.
  g_currentVelocityMmS = haveTarget ? instVelocityMmS : 0.0;

  if (haveTarget) {
    g_currentPositionMm = targetMm;
    applyFollowerVelocity(instVelocityMmS);
    long targetSteps = lround(targetMm * g_stepsPerMm);
    stepper->moveTo(targetSteps);
  }
}

// ---- M3: REST + WebSocket route registration ----
// Every handler below runs SYNCHRONOUSLY on AsyncTCP's own task and
// calls request->send() itself before returning -- confirmed on real
// hardware that ESPAsyncWebServer requires this (a handler that
// returns without responding gets its request answered with the
// framework's own generic "Handler did not handle the request"
// fallback instead, which is what an earlier, deferred-to-loop()
// version of this file hit on every single route). Thread safety
// against loop()'s own motion-state access (serial dispatch,
// controlTick()) comes from g_motionMutex/MotionLock instead of
// deferring the work -- see the comment on g_motionMutex above.
//
// The PATCH/PUT/POST-with-body routes use `new
// AsyncCallbackJsonWebHandler(uri, callback)` + `handler->setMethod(...)`
// + `server.addHandler(handler)` to get a parsed JsonVariant for a
// JSON request body. Like the regex routes noted below, this is
// written from established knowledge of ESPAsyncWebServer's long-
// stable API and wasn't compile-verified in this environment -- worth
// a look on the first real build if these don't compile.
void handleWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
                    AwsEventType type, void *arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    Serial.printf("WebSocket client #%u connected\n", client->id());
  } else if (type == WS_EVT_DISCONNECT) {
    Serial.printf("WebSocket client #%u disconnected\n", client->id());
  }
  // No incoming WS commands to handle -- everything that mutates state
  // goes over REST (see docs/api.md), so WS_EVT_DATA is deliberately
  // ignored here.
}

void setupApiRoutes() {
  g_apiWs.onEvent(handleWsEvent);
  g_apiServer.addHandler(&g_apiWs);

  g_apiServer.on("/api/v1/home", HTTP_POST, [](AsyncWebServerRequest *request) {
    MotionLock lock;
    sendCommandResult(request, runCommandForApi("SETHOME"));
  });

  g_apiServer.on("/api/v1/axis", HTTP_GET, [](AsyncWebServerRequest *request) {
    MotionLock lock;
    request->send(200, "application/json", buildAxisJson());
  });

  {
    auto *handler = new AsyncCallbackJsonWebHandler(
        "/api/v1/axis", [](AsyncWebServerRequest *request, JsonVariant &json) {
          JsonObject body = json.as<JsonObject>();
          bool hasTravel = body["travel_mm"].is<double>();
          bool hasSteps = body["steps_per_mm"].is<double>();
          double travel = hasTravel ? body["travel_mm"].as<double>() : 0.0;
          double steps = hasSteps ? body["steps_per_mm"].as<double>() : 0.0;
          MotionLock lock;
          CommandResult result{true, ""};
          if (hasTravel) {
            result = runCommandForApi("SETTRAVEL " + String(travel, 3));
          }
          if (result.ok && hasSteps) {
            result = runCommandForApi("SETSTEPSPERMM " + String(steps, 6));
          }
          sendCommandResult(request, result);
        });
    handler->setMethod(HTTP_PATCH);
    g_apiServer.addHandler(handler);
  }

  g_apiServer.on("/api/v1/loop-config", HTTP_GET,
                 [](AsyncWebServerRequest *request) {
                   MotionLock lock;
                   request->send(200, "application/json", buildLoopConfigJson());
                 });

  {
    auto *handler = new AsyncCallbackJsonWebHandler(
        "/api/v1/loop-config",
        [](AsyncWebServerRequest *request, JsonVariant &json) {
          JsonObject body = json.as<JsonObject>();
          bool hasA = body["pos_a_mm"].is<double>();
          bool hasB = body["pos_b_mm"].is<double>();
          bool hasSpeed = body["speed_mm_s"].is<double>();
          bool hasAccel = body["accel_mm_s2"].is<double>();
          bool hasDwellA = body["dwell_a_s"].is<double>();
          bool hasDwellB = body["dwell_b_s"].is<double>();
          bool hasRepeat = body["repeat"].is<bool>();
          double a = hasA ? body["pos_a_mm"].as<double>() : 0.0;
          double b = hasB ? body["pos_b_mm"].as<double>() : 0.0;
          double speed = hasSpeed ? body["speed_mm_s"].as<double>() : 0.0;
          double accel = hasAccel ? body["accel_mm_s2"].as<double>() : 0.0;
          double dwellA = hasDwellA ? body["dwell_a_s"].as<double>() : 0.0;
          double dwellB = hasDwellB ? body["dwell_b_s"].as<double>() : 0.0;
          bool repeat = hasRepeat ? body["repeat"].as<bool>() : false;
          // Applied in a fixed order so a request that sets several
          // fields at once has a predictable outcome if one of them
          // is invalid partway through (earlier fields already took
          // effect; later ones don't) -- matches how a client sending
          // these as separate serial commands one at a time would
          // behave anyway.
          MotionLock lock;
          CommandResult result{true, ""};
          if (result.ok && hasA) result = runCommandForApi("SETA " + String(a, 3));
          if (result.ok && hasB) result = runCommandForApi("SETB " + String(b, 3));
          if (result.ok && hasSpeed)
            result = runCommandForApi("SETSPEED " + String(speed, 3));
          if (result.ok && hasAccel)
            result = runCommandForApi("SETACCEL " + String(accel, 3));
          if (result.ok && hasDwellA)
            result = runCommandForApi("SETDWELL A " + String(dwellA, 3));
          if (result.ok && hasDwellB)
            result = runCommandForApi("SETDWELL B " + String(dwellB, 3));
          if (result.ok && hasRepeat)
            result = runCommandForApi(String("SETREPEAT ") +
                                      (repeat ? "ON" : "OFF"));
          sendCommandResult(request, result);
        });
    handler->setMethod(HTTP_PATCH);
    g_apiServer.addHandler(handler);
  }

  g_apiServer.on("/api/v1/loop-config/mark-a", HTTP_POST,
                 [](AsyncWebServerRequest *request) {
                   MotionLock lock;
                   sendCommandResult(request, runCommandForApi("SETA"));
                 });
  g_apiServer.on("/api/v1/loop-config/mark-b", HTTP_POST,
                 [](AsyncWebServerRequest *request) {
                   MotionLock lock;
                   sendCommandResult(request, runCommandForApi("SETB"));
                 });

  {
    auto *handler = new AsyncCallbackJsonWebHandler(
        "/api/v1/move", [](AsyncWebServerRequest *request, JsonVariant &json) {
          double posMm = json["pos_mm"] | 0.0;
          MotionLock lock;
          sendCommandResult(request,
                            runCommandForApi("MOVETO " + String(posMm, 3)));
        });
    handler->setMethod(HTTP_POST);
    g_apiServer.addHandler(handler);
  }

  {
    auto *handler = new AsyncCallbackJsonWebHandler(
        "/api/v1/jog", [](AsyncWebServerRequest *request, JsonVariant &json) {
          double deltaMm = json["delta_mm"] | 0.0;
          MotionLock lock;
          sendCommandResult(request,
                            runCommandForApi("JOG " + String(deltaMm, 3)));
        });
    handler->setMethod(HTTP_POST);
    g_apiServer.addHandler(handler);
  }

  g_apiServer.on("/api/v1/stop", HTTP_POST, [](AsyncWebServerRequest *request) {
    MotionLock lock;
    sendCommandResult(request, runCommandForApi("STOP"));
  });

  g_apiServer.on("/api/v1/loop/start", HTTP_POST,
                 [](AsyncWebServerRequest *request) {
                   MotionLock lock;
                   sendCommandResult(request, runCommandForApi("LOOPSTART"));
                 });

  g_apiServer.on("/api/v1/presets", HTTP_GET, [](AsyncWebServerRequest *request) {
    MotionLock lock;
    request->send(200, "application/json", buildPresetsJson());
  });

  g_apiServer.on("/api/v1/config/save", HTTP_POST,
                 [](AsyncWebServerRequest *request) {
                   MotionLock lock;
                   sendCommandResult(request, runCommandForApi("SAVECONFIG"));
                 });

  g_apiServer.on("/api/v1/status", HTTP_GET, [](AsyncWebServerRequest *request) {
    MotionLock lock;
    request->send(200, "application/json", buildStatusJson());
  });

  g_apiServer.on("/api/v1/wifi", HTTP_GET, [](AsyncWebServerRequest *request) {
    // WiFi.SSID()/RSSI()/localIP() aren't motion state -- no lock
    // needed, they're managed by the WiFi stack, not loop().
    request->send(200, "application/json", buildWifiJson());
  });

  // M4: Device screen actions. Both restart the device outright, so
  // both are rejected while anything is moving (409 MOVING) -- same
  // "disruptive action requires idle first" reasoning as OTA, just
  // without OTA's auth requirement: restarting into working firmware,
  // or into the setup portal, isn't the same brick-the-device risk a
  // bad OTA image is, so this stays in the general no-auth API.
  g_apiServer.on("/api/v1/restart", HTTP_POST, [](AsyncWebServerRequest *request) {
    MotionLock lock;
    if (isActive()) {
      request->send(409, "application/json", "{\"error\":\"MOVING\"}");
      return;
    }
    request->send(200, "application/json", "{\"ok\":true}");
    delay(500);  // let the response actually flush before rebooting
    ESP.restart();
  });

  g_apiServer.on("/api/v1/wifi/forget", HTTP_POST,
                 [](AsyncWebServerRequest *request) {
                   MotionLock lock;
                   if (isActive()) {
                     request->send(409, "application/json",
                                   "{\"error\":\"MOVING\"}");
                     return;
                   }
                   request->send(200, "application/json", "{\"ok\":true}");
                   glide::wifiForgetAndRestart();  // never returns
                 });

  // ---- Preset-name-parameterized routes ----
  // Matched with ESPAsyncWebServer's regex URL support ("^...$" pattern
  // + request->pathArg(0) for the captured name), enabled by the
  // -DASYNCWEBSERVER_REGEX build flag in platformio.ini. This is the
  // one part of this file relying on a library feature that couldn't
  // be compile-verified in this environment (no ESP32 toolchain here)
  // -- if these routes fail to compile or never match on the first
  // real build, that flag/feature is the first thing to check.
  static const char *kPresetNamePattern = "^\\/api\\/v1\\/presets\\/([^\\/]+)$";
  static const char *kPresetSaveCurrentPattern =
      "^\\/api\\/v1\\/presets\\/([^\\/]+)\\/save-current$";
  static const char *kPresetLoadPattern =
      "^\\/api\\/v1\\/presets\\/([^\\/]+)\\/load$";

  g_apiServer.on(kPresetNamePattern, HTTP_GET, [](AsyncWebServerRequest *request) {
    String name = request->pathArg(0);
    MotionLock lock;
    int idx = findPresetIndex(name);
    if (idx < 0) {
      request->send(404, "application/json", "{\"error\":\"NOT_FOUND\"}");
      return;
    }
    request->send(200, "application/json", buildSinglePresetJson(g_presets[idx]));
  });

  g_apiServer.on(kPresetNamePattern, HTTP_DELETE,
                 [](AsyncWebServerRequest *request) {
                   String name = request->pathArg(0);
                   MotionLock lock;
                   sendCommandResult(request,
                                     runCommandForApi("DELETEPRESET " + name));
                 });

  {
    auto *handler = new AsyncCallbackJsonWebHandler(
        kPresetNamePattern,
        [](AsyncWebServerRequest *request, JsonVariant &json) {
          String name = request->pathArg(0);
          JsonObject body = json.as<JsonObject>();
          // Explicit body values, NOT "snapshot current settings" --
          // see docs/api.md: PUT /presets/:name creates/overwrites
          // with exactly what's in the request body, unlike
          // SAVEPRESET/save-current which snapshots live A/B/speed.
          bool hasA = body["pos_a_mm"].is<double>();
          bool hasB = body["pos_b_mm"].is<double>();
          double posA = body["pos_a_mm"] | 0.0;
          double posB = body["pos_b_mm"] | 0.0;
          double speed = body["speed_mm_s"] | 20.0;
          double accel = body["accel_mm_s2"] | 100.0;
          double dwellA = body["dwell_a_s"] | 0.0;
          double dwellB = body["dwell_b_s"] | 0.0;
          bool repeat = body["repeat"] | true;
          if (!hasA || !hasB) {
            request->send(400, "application/json",
                          "{\"error\":\"INVALID_VALUE\"}");
            return;
          }
          MotionLock lock;
          PresetConfig preset;
          preset.name = sanitizePresetName(name);
          preset.pos_a_mm = posA;
          preset.pos_b_mm = posB;
          preset.speed_mm_s = speed;
          preset.accel_mm_s2 = accel;
          preset.dwell_a_s = dwellA;
          preset.dwell_b_s = dwellB;
          preset.repeat = repeat;
          int idx = findPresetIndex(preset.name);
          if (idx >= 0) {
            g_presets[idx] = preset;
          } else {
            g_presets.push_back(preset);
          }
          if (persistConfig()) {
            request->send(200, "application/json", "{\"ok\":true}");
          } else {
            request->send(500, "application/json", "{\"error\":\"SAVE_FAILED\"}");
          }
        });
    handler->setMethod(HTTP_PUT);
    g_apiServer.addHandler(handler);
  }

  g_apiServer.on(kPresetSaveCurrentPattern, HTTP_POST,
                 [](AsyncWebServerRequest *request) {
                   String name = request->pathArg(0);
                   MotionLock lock;
                   sendCommandResult(request,
                                     runCommandForApi("SAVEPRESET " + name));
                 });

  g_apiServer.on(kPresetLoadPattern, HTTP_POST, [](AsyncWebServerRequest *request) {
    String name = request->pathArg(0);
    MotionLock lock;
    sendCommandResult(request, runCommandForApi("LOADPRESET " + name));
  });

  glide::otaRegisterRoute(g_apiServer, GLIDE_OTA_KEY, []() {
    MotionLock lock;
    return isActive();
  });

  // M4: serves the embedded web UI (firmware/lib/api/webui_assets.h,
  // generated from webui/ by firmware/scripts/embed_webui.py) for
  // anything that didn't match a more specific route above.
  // onNotFound only fires when NOTHING else matched, so this can't
  // shadow any REST endpoint registered earlier in this function.
  //
  // NOTE: beginResponse_P() for serving a PROGMEM byte buffer directly
  // is written from established ESPAsyncWebServer knowledge -- this is
  // the standard pattern for embedded web assets on ESP32, but like
  // the rest of this file's network layer, it wasn't compile-verified
  // in this environment (no ESP32 toolchain here). Worth checking
  // first if this specific handler fails to compile.
  g_apiServer.onNotFound([](AsyncWebServerRequest *request) {
    if (request->method() != HTTP_GET) {
      request->send(404, "application/json", "{\"error\":\"NOT_FOUND\"}");
      return;
    }
    String path = request->url();
    if (path.startsWith("/api/")) {
      // A genuinely unmatched API route -- don't paper over a typo'd
      // endpoint by serving HTML for it.
      request->send(404, "application/json", "{\"error\":\"NOT_FOUND\"}");
      return;
    }

    const glide::WebuiAsset *asset = glide::findWebuiAsset(path.c_str());
    bool isSpaFallback = false;
    if (!asset) {
      // SPA fallback: any other unmatched GET (the root path, or a
      // page refresh mid-navigation within the app's own tab state,
      // which is signal-based, not URL-based) serves index.html so
      // the app can take over and restore its own view.
      asset = glide::findWebuiAsset("/index.html");
      isSpaFallback = true;
    }
    if (!asset) {
      request->send(503, "text/plain",
                    "Web UI not built into this firmware image -- see "
                    "firmware/scripts/embed_webui.py.");
      return;
    }

    AsyncWebServerResponse *response =
        request->beginResponse_P(200, asset->mimeType, asset->data, asset->length);
    response->addHeader("Content-Encoding", "gzip");
    // Vite hashes every built filename by content, so a genuinely new
    // build always gets a new URL -- safe to cache those forever.
    // index.html itself (and the SPA fallback re-using it) must never
    // be cached that way, or a fresh firmware's index.html could stay
    // stuck pointing at a previous build's now-gone hashed filenames.
    if (isSpaFallback) {
      response->addHeader("Cache-Control", "no-cache");
    } else {
      response->addHeader("Cache-Control", "public, max-age=31536000, immutable");
    }
    request->send(response);
  });

  g_apiServer.begin();
  Serial.println("REST/WebSocket API listening on port 80 (/api/v1, /ws)");
}

// Pushes the ~10Hz WebSocket status frame while anything is moving,
// and a ~5s heartbeat regardless -- see docs/api.md's WebSocket
// section. Call once per loop() iteration; both are self-throttling
// via millis(), so this is cheap to call unconditionally. Runs on
// loop()'s own thread, same as controlTick() -- MotionLock here is
// what keeps it from reading motion state at the same instant a REST
// handler (on AsyncTCP's task) is writing it.
void apiTick() {
  unsigned long now = millis();

  // Edge-triggered on top of the periodic push below -- a real bug
  // found on real hardware: a direct MOVETO/JOG (or a non-repeating
  // loop) finishing on its own happens entirely inside controlTick(),
  // with no handleCommand() call at that instant (unlike an explicit
  // STOP, which does go through handleCommand() and already gets a
  // broadcast from its trailing wsBroadcastStatus() call). Since the
  // periodic push below only fires while isActive() is true, the very
  // last thing a client saw was "MOVING" -- moving to false never got
  // announced, so the web UI stayed stuck showing "Moving" until some
  // unrelated command (e.g. tapping Stop) incidentally triggered a
  // fresh broadcast. Tracking the falling edge here and forcing one
  // more broadcast exactly at that transition covers every reason
  // isActive() might drop to false, not just the specific ones already
  // found.
  static bool wasActive = false;

  {
    MotionLock lock;
    bool active = isActive();
    bool justBecameInactive = wasActive && !active;
    wasActive = active;
    if (justBecameInactive || (active && now - g_lastWsStatusMs >= 100)) {
      g_lastWsStatusMs = now;
      wsBroadcastStatus();
    }
  }

  if (now - g_lastWsHeartbeatMs >= 5000) {
    g_lastWsHeartbeatMs = now;
    if (g_apiWs.count() > 0) {
      g_apiWs.textAll(String("{\"type\":\"heartbeat\",\"t\":") + now + "}");
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);  // let USB-serial enumerate before the first print

  Serial.println();
  Serial.println("=== Glide M1/M2/M3 motion core (serial + REST/WebSocket) ===");
  Serial.println("Type HELP for commands.");

  if (strcmp(GLIDE_OTA_KEY, "glide-default-ota-key-change-me") == 0) {
    Serial.println(
        "WARNING: using the default OTA key -- see "
        "firmware/include/secrets.h.example to set a real one before "
        "this device is reachable beyond your own bench.");
  }

  g_motionMutex = xSemaphoreCreateMutex();

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

  // WiFi/API setup happens LAST, after motion is fully ready to
  // accept commands -- WiFiManager's blocking portal (if a saved
  // network isn't reachable) can take up to 3 minutes, and there's no
  // reason serial control should wait on that.
  if (glide::wifiSetupBegin()) {
    setupApiRoutes();
  } else {
    Serial.println(
        "Skipping REST/WebSocket API setup -- no WiFi connection this "
        "boot. Serial control still works.");
  }

  g_lastTickMs = millis();
}

void loop() {
  while (Serial.available()) {
    char c = static_cast<char>(Serial.read());
    if (c == '\n') {
      {
        MotionLock lock;
        handleCommand(g_lineBuffer);
      }
      g_lineBuffer = "";
    } else if (c == '\r') {
      // ignore -- part of a \r\n line ending, not real content
    } else if (c == '\b' || c == 127) {
      // Backspace (8) or DEL (127): a terminal shows you the
      // corrected text on screen, but still transmits the raw
      // backspace byte itself -- without handling it here, that byte
      // was silently appended into the buffer as an invisible
      // character instead of actually erasing the previous one. This
      // is exactly what corrupted a saved preset name (visually
      // "test2", actually 7 characters long) after a mid-typing typo
      // correction.
      if (g_lineBuffer.length() > 0) {
        g_lineBuffer.remove(g_lineBuffer.length() - 1);
      }
    } else {
      g_lineBuffer += c;
    }
  }

  unsigned long now = millis();
  if (now - g_lastTickMs >= CONTROL_TICK_MS) {
    double dtS = (now - g_lastTickMs) / 1000.0;
    g_lastTickMs = now;
    MotionLock lock;
    controlTick(dtS);
  }

  apiTick();
}
