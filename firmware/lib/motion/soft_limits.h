#pragma once

namespace glide {

// Clamps a target position (mm) into the travel range between 0 and
// travel_mm -- in WHICHEVER direction travel_mm points. travel_mm may
// be positive (rail extends positive from home) or negative (rail
// extends negative from home): the effective range is always
// [min(0, travel_mm), max(0, travel_mm)]. This matters because "home"
// isn't required to be the minimum-position end of the rail -- it's
// just wherever SETHOME was called, which could be either end
// depending on how the carriage happened to be positioned at the
// time. Position 0 is wherever "home" was last set (see the
// manual/soft homing note in hardware/pinout.md and docs/api.md once
// that's written) -- this function doesn't know or care how home was
// established, it only enforces the configured travel range.
//
// If out_clamped is non-null, it's set to true when the input was
// outside range (and therefore adjusted) and false otherwise. Pass
// nullptr if you don't need to know.
double clampToSoftLimits(double target_mm, double travel_mm,
                          bool* out_clamped = nullptr);

}  // namespace glide
