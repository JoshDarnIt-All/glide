#pragma once

namespace glide {

// A minimum-jerk (quintic polynomial) point-to-point motion profile.
// Produces true "ease in/out": velocity AND acceleration are both
// zero at the start and end of the move, so there's no mechanical
// snap at either end -- that's what ease in/out means physically,
// not just a marketing term.
//
// Why this profile instead of a phase-by-phase (7-segment)
// jerk-limited trapezoid: both produce genuinely smooth S-curve
// motion. The 7-segment approach lets you independently cap peak
// velocity, acceleration, AND jerk to specific hardware limits, but
// requires branching on several move-length cases (does the move
// even reach max velocity/acceleration before it has to start
// decelerating again?) and is a common source of subtle
// motion-planning bugs. This quintic profile is closed-form -- one
// polynomial, no case analysis -- at the cost of only indirectly
// controlling peak velocity/acceleration: they fall out of the
// chosen move duration, not the other way around. For point-to-point
// A/B moves that trade is worth it: simpler to get right, and
// duration is exactly what a "speed" preset naturally maps to anyway.
//
// position(t) = distance * (10*u^3 - 15*u^4 + 6*u^5), u = t/duration
//
// Peak velocity occurs at u=0.5 and equals 1.875 * distance/duration.
// Peak |acceleration| occurs at u=(3-sqrt(3))/6 (~0.2113, mirrored at
// ~0.7887) and equals (10/sqrt(3)) * distance/duration^2 (~5.7735x).
// Both constants are derived (not copied from a source) in scurve.cpp
// and checked by the boundary/peak assertions in
// firmware/test/test_motion.
struct SCurveProfile {
  double distance_mm = 0.0;  // signed: negative = decreasing position
  double duration_s = 0.0;   // total move time; 0 means "no move"

  // Position and velocity at time t_s into the move, relative to the
  // move's own start (t_s=0 => 0mm). t_s is clamped internally to
  // [0, duration_s], so callers don't need to guard against
  // stepping slightly past the end of a move.
  double positionAt(double t_s) const;
  double velocityAt(double t_s) const;
};

// Builds a profile for a point-to-point move of `distance_mm` (may be
// negative or zero). Duration is chosen so that neither
// `peak_speed_mm_s` (desired top speed at the midpoint of the move)
// nor `max_accel_mm_s2` (hardware/comfort acceleration ceiling) is
// exceeded -- whichever constraint demands the longer, gentler
// duration wins. Both must be > 0; distance_mm == 0 yields a
// zero-duration (no-op) profile.
SCurveProfile buildSCurveProfile(double distance_mm,
                                  double peak_speed_mm_s,
                                  double max_accel_mm_s2);

}  // namespace glide
