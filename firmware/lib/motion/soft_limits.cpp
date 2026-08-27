#include "soft_limits.h"

#include <algorithm>

namespace glide {

double clampToSoftLimits(double target_mm, double travel_mm,
                          bool* out_clamped) {
  double lo = 0.0;
  double hi = travel_mm;
  // std::min/std::max instead of std::clamp (C++17) -- the ESP32
  // Arduino toolchain doesn't build with C++17 by default, and this
  // is simple enough not to need it.
  double clamped = std::min(std::max(target_mm, lo), hi);
  if (out_clamped != nullptr) {
    *out_clamped = (clamped != target_mm);
  }
  return clamped;
}

}  // namespace glide
