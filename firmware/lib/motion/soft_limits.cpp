#include "soft_limits.h"

#include <algorithm>

namespace glide {

double clampToSoftLimits(double target_mm, double travel_mm,
                          bool* out_clamped) {
  // Range is [min(0, travel_mm), max(0, travel_mm)] so travel_mm can
  // point either direction from home -- see soft_limits.h.
  double lo = std::min(0.0, travel_mm);
  double hi = std::max(0.0, travel_mm);
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
