#pragma once

namespace glide {

// Clamps a target position (mm) into the travel range [0, travel_mm].
// Position 0 is wherever "home" was last set (see the manual/soft
// homing note in hardware/pinout.md and docs/api.md once that's
// written) -- this function doesn't know or care how home was
// established, it only enforces the configured travel range.
//
// If out_clamped is non-null, it's set to true when the input was
// outside range (and therefore adjusted) and false otherwise. Pass
// nullptr if you don't need to know.
double clampToSoftLimits(double target_mm, double travel_mm,
                          bool* out_clamped = nullptr);

}  // namespace glide
