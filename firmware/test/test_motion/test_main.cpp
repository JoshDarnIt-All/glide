// Unit tests for firmware/lib/motion. Deliberately framework-free
// (plain <cassert>, not Unity) so this compiles and runs with any
// C++ compiler -- no PlatformIO, no ESP32 toolchain, no hardware.
// That's the entire point of lib/motion having no Arduino/WiFi deps:
// this file is the proof it actually holds.
//
// Run directly with, e.g.:
//   clang++ -std=c++17 -I../../lib/motion test_main.cpp \
//     ../../lib/motion/scurve.cpp ../../lib/motion/soft_limits.cpp \
//     ../../lib/motion/loop_runner.cpp -o /tmp/test_motion && /tmp/test_motion
//
// Once PlatformIO is available, these same sources can be wired into
// a `pio test -e native` environment (see platformio.ini) without
// modification -- a zero-argument main() returning via assert success
// is a valid PlatformIO native test.

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>

#include "loop_runner.h"
#include "scurve.h"
#include "soft_limits.h"

using glide::buildSCurveProfile;
using glide::clampToSoftLimits;
using glide::LoopConfig;
using glide::LoopPhase;
using glide::LoopRunner;
using glide::SCurveProfile;

namespace {

constexpr double kEps = 1e-6;

bool approxEqual(double a, double b, double eps = kEps) {
  return std::fabs(a - b) <= eps;
}

// ---------- scurve tests ----------

void test_scurve_zero_distance_is_noop() {
  SCurveProfile p = buildSCurveProfile(0.0, 10.0, 50.0);
  assert(p.duration_s == 0.0);
  assert(p.positionAt(0.0) == 0.0);
  assert(p.positionAt(5.0) == 0.0);
  assert(p.velocityAt(0.0) == 0.0);
  printf("PASS test_scurve_zero_distance_is_noop\n");
}

void test_scurve_boundary_conditions() {
  SCurveProfile p = buildSCurveProfile(100.0, 20.0, 100.0);
  assert(p.duration_s > 0.0);

  // Position starts at 0, ends exactly at distance.
  assert(approxEqual(p.positionAt(0.0), 0.0));
  assert(approxEqual(p.positionAt(p.duration_s), 100.0, 1e-4));

  // Velocity is (approximately) zero at both ends -- true ease in/out.
  assert(approxEqual(p.velocityAt(0.0), 0.0));
  assert(approxEqual(p.velocityAt(p.duration_s), 0.0, 1e-4));

  printf("PASS test_scurve_boundary_conditions\n");
}

void test_scurve_monotonic_for_positive_distance() {
  SCurveProfile p = buildSCurveProfile(50.0, 15.0, 80.0);
  double prev = p.positionAt(0.0);
  const int kSamples = 200;
  for (int i = 1; i <= kSamples; ++i) {
    double t = p.duration_s * (static_cast<double>(i) / kSamples);
    double cur = p.positionAt(t);
    assert(cur + kEps >= prev);  // never goes backwards
    prev = cur;
  }
  printf("PASS test_scurve_monotonic_for_positive_distance\n");
}

void test_scurve_negative_distance_is_mirrored() {
  SCurveProfile p = buildSCurveProfile(-50.0, 15.0, 80.0);
  assert(approxEqual(p.positionAt(p.duration_s), -50.0, 1e-4));
  double prev = p.positionAt(0.0);
  const int kSamples = 200;
  for (int i = 1; i <= kSamples; ++i) {
    double t = p.duration_s * (static_cast<double>(i) / kSamples);
    double cur = p.positionAt(t);
    assert(cur - kEps <= prev);  // never goes forwards (position decreasing)
    prev = cur;
  }
  printf("PASS test_scurve_negative_distance_is_mirrored\n");
}

// A short move with a very high accel ceiling is speed-limited: peak
// velocity should reach almost exactly the requested peak_speed_mm_s.
void test_scurve_speed_limited_move_reaches_peak_speed() {
  double peakSpeed = 25.0;
  SCurveProfile p = buildSCurveProfile(200.0, peakSpeed, 10000.0);
  double maxV = 0.0;
  const int kSamples = 500;
  for (int i = 0; i <= kSamples; ++i) {
    double t = p.duration_s * (static_cast<double>(i) / kSamples);
    maxV = std::max(maxV, std::fabs(p.velocityAt(t)));
  }
  assert(approxEqual(maxV, peakSpeed, 0.05));
  printf("PASS test_scurve_speed_limited_move_reaches_peak_speed\n");
}

// A short move with a very high speed ceiling is accel-limited: peak
// acceleration (estimated via finite differences) should not exceed
// max_accel_mm_s2 by more than a small numerical-approximation margin.
void test_scurve_accel_limited_move_respects_accel_ceiling() {
  double maxAccel = 40.0;
  SCurveProfile p = buildSCurveProfile(30.0, 10000.0, maxAccel);
  double maxA = 0.0;
  const int kSamples = 2000;
  double dt = p.duration_s / kSamples;
  for (int i = 1; i < kSamples; ++i) {
    double t = i * dt;
    double a = (p.velocityAt(t + dt) - p.velocityAt(t - dt)) / (2 * dt);
    maxA = std::max(maxA, std::fabs(a));
  }
  // 3% slack for finite-difference approximation error.
  assert(maxA <= maxAccel * 1.03);
  printf("PASS test_scurve_accel_limited_move_respects_accel_ceiling\n");
}

// ---------- soft_limits tests ----------

void test_soft_limits_inside_range_unchanged() {
  bool clamped = true;
  double v = clampToSoftLimits(250.0, 500.0, &clamped);
  assert(v == 250.0);
  assert(clamped == false);
  printf("PASS test_soft_limits_inside_range_unchanged\n");
}

void test_soft_limits_clamps_below_zero() {
  bool clamped = false;
  double v = clampToSoftLimits(-10.0, 500.0, &clamped);
  assert(v == 0.0);
  assert(clamped == true);
  printf("PASS test_soft_limits_clamps_below_zero\n");
}

void test_soft_limits_clamps_above_travel() {
  bool clamped = false;
  double v = clampToSoftLimits(600.0, 500.0, &clamped);
  assert(v == 500.0);
  assert(clamped == true);
  printf("PASS test_soft_limits_clamps_above_travel\n");
}

// ---------- loop_runner tests ----------

void test_loop_single_cycle_stops_at_a() {
  LoopConfig cfg;
  cfg.pos_a_mm = 0.0;
  cfg.pos_b_mm = 200.0;
  cfg.peak_speed_mm_s = 50.0;
  cfg.max_accel_mm_s2 = 200.0;
  cfg.dwell_at_a_s = 0.5;
  cfg.dwell_at_b_s = 0.5;
  cfg.repeat = false;

  LoopRunner runner;
  runner.start(cfg, /*current_position_mm=*/0.0);

  double dt = 0.01;
  int maxTicks = 100000;  // generous safety cap against an infinite loop bug
  int ticks = 0;
  while (runner.isRunning() && ticks < maxTicks) {
    runner.update(dt);
    ++ticks;
  }
  assert(ticks < maxTicks);  // actually finished, didn't hit the safety cap
  assert(runner.phase() == LoopPhase::Stopped);
  assert(approxEqual(runner.currentPosition(), 0.0, 1e-3));
  printf("PASS test_loop_single_cycle_stops_at_a (%d ticks)\n", ticks);
}

void test_loop_current_velocity_matches_profile() {
  LoopConfig cfg;
  cfg.pos_a_mm = 0.0;
  cfg.pos_b_mm = 100.0;
  cfg.peak_speed_mm_s = 40.0;
  cfg.max_accel_mm_s2 = 200.0;
  cfg.dwell_at_a_s = 0.2;
  cfg.dwell_at_b_s = 0.2;
  cfg.repeat = true;

  LoopRunner runner;
  runner.start(cfg, 0.0);

  // Zero velocity at the very start of a move (true ease-in).
  assert(approxEqual(runner.currentVelocity(), 0.0, 1e-3));

  // Mid-move: velocity should be positive (moving toward B) and
  // roughly consistent with a plausible speed (not zero, not
  // absurdly over the configured peak).
  runner.update(0.5);
  double vMidMove = runner.currentVelocity();
  assert(vMidMove > 0.0);
  assert(vMidMove <= cfg.peak_speed_mm_s * 1.01);

  // Drive it forward until it's dwelling at B; velocity must be
  // exactly zero during a dwell -- this is the case that mattered for
  // the real bug (a follower that keeps commanding nonzero speed
  // during a dwell would just grind against the target forever).
  for (int i = 0; i < 2000 && runner.phase() != LoopPhase::DwellingAtB; ++i) {
    runner.update(0.01);
  }
  assert(runner.phase() == LoopPhase::DwellingAtB);
  assert(runner.currentVelocity() == 0.0);

  printf("PASS test_loop_current_velocity_matches_profile\n");
}

void test_loop_repeat_keeps_cycling() {
  LoopConfig cfg;
  cfg.pos_a_mm = 0.0;
  cfg.pos_b_mm = 100.0;
  cfg.peak_speed_mm_s = 80.0;
  cfg.max_accel_mm_s2 = 400.0;
  cfg.dwell_at_a_s = 0.1;
  cfg.dwell_at_b_s = 0.1;
  cfg.repeat = true;

  LoopRunner runner;
  runner.start(cfg, 0.0);

  // Run for a fixed, generous amount of simulated time (not until
  // Stopped, since repeat=true never stops) and confirm it visited
  // MovingToB at least twice -- i.e. it actually looped, rather than
  // getting stuck after the first cycle.
  double dt = 0.01;
  int movingToBCount = 0;
  LoopPhase lastPhase = LoopPhase::Idle;
  for (int i = 0; i < 2000; ++i) {  // 20 simulated seconds
    runner.update(dt);
    if (runner.phase() == LoopPhase::MovingToB && lastPhase != LoopPhase::MovingToB) {
      ++movingToBCount;
    }
    lastPhase = runner.phase();
  }
  assert(runner.isRunning());
  assert(movingToBCount >= 2);
  printf("PASS test_loop_repeat_keeps_cycling (entered MovingToB %d times)\n",
         movingToBCount);
}

void test_loop_stop_halts_immediately() {
  LoopConfig cfg;
  cfg.pos_a_mm = 0.0;
  cfg.pos_b_mm = 300.0;
  cfg.peak_speed_mm_s = 30.0;
  cfg.max_accel_mm_s2 = 100.0;
  cfg.repeat = true;

  LoopRunner runner;
  runner.start(cfg, 0.0);
  runner.update(0.5);  // partway into the first move
  double posAtStop = runner.currentPosition();
  runner.stop();

  assert(runner.phase() == LoopPhase::Stopped);
  assert(!runner.isRunning());

  double posAfter = runner.update(1.0);
  assert(posAfter == posAtStop);  // update() after stop() is a no-op
  printf("PASS test_loop_stop_halts_immediately\n");
}

void test_loop_position_stays_within_ab_bounds() {
  LoopConfig cfg;
  cfg.pos_a_mm = 20.0;
  cfg.pos_b_mm = 180.0;
  cfg.peak_speed_mm_s = 60.0;
  cfg.max_accel_mm_s2 = 300.0;
  cfg.dwell_at_a_s = 0.2;
  cfg.dwell_at_b_s = 0.2;
  cfg.repeat = true;

  LoopRunner runner;
  runner.start(cfg, 20.0);

  double dt = 0.005;
  for (int i = 0; i < 4000; ++i) {  // 20 simulated seconds
    double pos = runner.update(dt);
    assert(pos >= cfg.pos_a_mm - 1e-3);
    assert(pos <= cfg.pos_b_mm + 1e-3);
  }
  printf("PASS test_loop_position_stays_within_ab_bounds\n");
}

}  // namespace

int main() {
  test_scurve_zero_distance_is_noop();
  test_scurve_boundary_conditions();
  test_scurve_monotonic_for_positive_distance();
  test_scurve_negative_distance_is_mirrored();
  test_scurve_speed_limited_move_reaches_peak_speed();
  test_scurve_accel_limited_move_respects_accel_ceiling();

  test_soft_limits_inside_range_unchanged();
  test_soft_limits_clamps_below_zero();
  test_soft_limits_clamps_above_travel();

  test_loop_single_cycle_stops_at_a();
  test_loop_current_velocity_matches_profile();
  test_loop_repeat_keeps_cycling();
  test_loop_stop_halts_immediately();
  test_loop_position_stays_within_ab_bounds();

  printf("\nAll motion core tests passed.\n");
  return 0;
}
