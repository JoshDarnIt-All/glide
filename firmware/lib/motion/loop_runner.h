#pragma once

#include "scurve.h"

namespace glide {

enum class LoopPhase {
  Idle,          // never started
  MovingToB,
  DwellingAtB,
  MovingToA,
  DwellingAtA,
  Stopped,       // finished (repeat=false) or stop() was called
};

struct LoopConfig {
  double pos_a_mm = 0.0;
  double pos_b_mm = 0.0;
  double peak_speed_mm_s = 10.0;
  double max_accel_mm_s2 = 50.0;
  double dwell_at_a_s = 0.0;
  double dwell_at_b_s = 0.0;
  bool repeat = true;  // false = one A-B-A cycle then Stopped
};

// Drives an A-B-A "loop" preset: move to B, dwell, move to A, dwell,
// repeat (or stop after one cycle if repeat=false). This is pure
// state -- it produces a target position over time, it does not touch
// any hardware. The caller (firmware/src) is responsible for feeding
// update() a real elapsed-time each control tick and turning the
// returned position into actual FastAccelStepper commands.
//
// Soft limits are NOT enforced here -- pos_a_mm/pos_b_mm are assumed
// already validated (e.g. via clampToSoftLimits) by whatever set them.
// This class only sequences the moves; it trusts its inputs.
class LoopRunner {
 public:
  // Begins the loop from current_position_mm. Note the FIRST leg goes
  // from wherever the carriage actually is to B -- not necessarily
  // from A. If you want a strict A-B-A start, command a move to A
  // first and start the loop once that completes.
  void start(const LoopConfig& cfg, double current_position_mm);

  // Advances the state machine by dt_s seconds and returns the
  // commanded position (mm) for "now". Call this every control tick
  // while isRunning() is true. Calling it while not running just
  // returns the last known position.
  double update(double dt_s);

  LoopPhase phase() const { return phase_; }
  bool isRunning() const {
    return phase_ != LoopPhase::Idle && phase_ != LoopPhase::Stopped;
  }
  double currentPosition() const { return current_position_mm_; }

  // Instantaneous signed velocity (mm/s) "right now" -- zero during
  // dwell/idle/stopped, otherwise the S-curve profile's velocity at
  // the current elapsed time into the active leg. Intended for a
  // caller that continuously re-targets a stepper driver's speed to
  // track the real profile instead of commanding position waypoints
  // alone -- see the note in firmware/src/main.cpp about why that
  // matters (sprinting to a position waypoint and idling between
  // ticks produces audible/mechanical bursting, not smooth motion).
  double currentVelocity() const;

  // Halts sequencing immediately: phase becomes Stopped and update()
  // will stop advancing. This does NOT decelerate the physical
  // stepper -- that's the caller's job (e.g. commanding FastAccelStepper
  // to ramp down using its own deceleration, or in an emergency, the
  // physical e-stop described in the README, which this class has no
  // relationship to at all).
  void stop();

 private:
  void startLeg(double target_mm, LoopPhase moving_phase);
  void advancePhaseAfterMove();
  void advancePhaseAfterDwell();

  LoopConfig cfg_;
  LoopPhase phase_ = LoopPhase::Idle;

  double current_position_mm_ = 0.0;
  double leg_start_mm_ = 0.0;
  SCurveProfile leg_profile_;
  double leg_elapsed_s_ = 0.0;
  double dwell_elapsed_s_ = 0.0;
};

}  // namespace glide
