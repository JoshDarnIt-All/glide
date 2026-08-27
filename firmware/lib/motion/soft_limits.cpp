#include "soft_limits.h"

#include <algorithm>

namespace glide {

double clampToSoftLimits(double target_mm, double travel_mm,
                          bool* out_clamped) {
  double lo = 0.0;
  double hi = travel_mm;
  double clamped = std::clamp(target_mm, lo, hi);
  if (out_clamped != nullptr) {
    *out_clamped = (clamped != target_mm);
  }
  return clamped;
}

}  // namespace glide
