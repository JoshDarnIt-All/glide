#include "scurve.h"

#include <algorithm>
#include <cmath>

namespace glide {

namespace {

// Derived, not copied from a reference: see scurve.h for the full
// derivation notes. s(u) = 10u^3 - 15u^4 + 6u^5 is the normalized
// (distance=1, duration=1) quintic position curve.
double normalizedPosition(double u) {
  return u * u * u * (10.0 - 15.0 * u + 6.0 * u * u);
}

// s'(u) = 30u^2 - 60u^3 + 30u^4 = 30 u^2 (1-u)^2
double normalizedVelocity(double u) {
  return 30.0 * u * u * (1.0 - u) * (1.0 - u);
}

// Peak of s'(u) is at u=0.5: 30*0.25*0.25 = 1.875
constexpr double kPeakVelocityFactor = 1.875;

// Peak of s''(u) is at u=(3-sqrt(3))/6, value = 10/sqrt(3) (~5.7735).
// See scurve.h for the derivation.
const double kPeakAccelFactor = 10.0 / std::sqrt(3.0);

}  // namespace

double SCurveProfile::positionAt(double t_s) const {
  if (duration_s <= 0.0) return 0.0;
  // std::min/std::max instead of std::clamp (C++17) -- see the note
  // in soft_limits.cpp.
  double t = std::min(std::max(t_s, 0.0), duration_s);
  double u = t / duration_s;
  return distance_mm * normalizedPosition(u);
}

double SCurveProfile::velocityAt(double t_s) const {
  if (duration_s <= 0.0) return 0.0;
  // std::min/std::max instead of std::clamp (C++17) -- see the note
  // in soft_limits.cpp.
  double t = std::min(std::max(t_s, 0.0), duration_s);
  double u = t / duration_s;
  return (distance_mm / duration_s) * normalizedVelocity(u);
}

SCurveProfile buildSCurveProfile(double distance_mm, double peak_speed_mm_s,
                                  double max_accel_mm_s2) {
  SCurveProfile profile;
  profile.distance_mm = distance_mm;

  if (distance_mm == 0.0) {
    profile.duration_s = 0.0;
    return profile;
  }

  double absDistance = std::fabs(distance_mm);

  // Duration long enough that peak velocity (1.875 * D/T) doesn't
  // exceed peak_speed_mm_s:
  //   T >= kPeakVelocityFactor * D / peak_speed_mm_s
  double durationForSpeed =
      kPeakVelocityFactor * absDistance / peak_speed_mm_s;

  // Duration long enough that peak acceleration (kPeakAccelFactor *
  // D/T^2) doesn't exceed max_accel_mm_s2:
  //   T >= sqrt(kPeakAccelFactor * D / max_accel_mm_s2)
  double durationForAccel =
      std::sqrt(kPeakAccelFactor * absDistance / max_accel_mm_s2);

  profile.duration_s = std::max(durationForSpeed, durationForAccel);
  return profile;
}

}  // namespace glide
