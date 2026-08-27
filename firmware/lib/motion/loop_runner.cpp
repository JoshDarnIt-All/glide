#include "loop_runner.h"

namespace glide {

void LoopRunner::start(const LoopConfig& cfg, double current_position_mm) {
  cfg_ = cfg;
  current_position_mm_ = current_position_mm;
  dwell_elapsed_s_ = 0.0;
  startLeg(cfg_.pos_b_mm, LoopPhase::MovingToB);
}

void LoopRunner::stop() { phase_ = LoopPhase::Stopped; }

void LoopRunner::startLeg(double target_mm, LoopPhase moving_phase) {
  leg_start_mm_ = current_position_mm_;
  leg_profile_ = buildSCurveProfile(target_mm - current_position_mm_,
                                     cfg_.peak_speed_mm_s,
                                     cfg_.max_accel_mm_s2);
  leg_elapsed_s_ = 0.0;
  phase_ = moving_phase;
}

double LoopRunner::update(double dt_s) {
  switch (phase_) {
    case LoopPhase::MovingToB:
    case LoopPhase::MovingToA: {
      leg_elapsed_s_ += dt_s;
      if (leg_elapsed_s_ >= leg_profile_.duration_s) {
        // Snap to the exact commanded target rather than whatever the
        // profile evaluates to right at duration_s, so small timing
        // jitter across ticks can't accumulate into position drift
        // over many loop cycles.
        current_position_mm_ = leg_start_mm_ + leg_profile_.distance_mm;
        advancePhaseAfterMove();
      } else {
        current_position_mm_ =
            leg_start_mm_ + leg_profile_.positionAt(leg_elapsed_s_);
      }
      return current_position_mm_;
    }
    case LoopPhase::DwellingAtA:
    case LoopPhase::DwellingAtB: {
      dwell_elapsed_s_ += dt_s;
      double dwell_target = (phase_ == LoopPhase::DwellingAtA)
                                 ? cfg_.dwell_at_a_s
                                 : cfg_.dwell_at_b_s;
      if (dwell_elapsed_s_ >= dwell_target) {
        advancePhaseAfterDwell();
      }
      return current_position_mm_;
    }
    case LoopPhase::Idle:
    case LoopPhase::Stopped:
    default:
      return current_position_mm_;
  }
}

double LoopRunner::currentVelocity() const {
  if (phase_ == LoopPhase::MovingToB || phase_ == LoopPhase::MovingToA) {
    return leg_profile_.velocityAt(leg_elapsed_s_);
  }
  return 0.0;
}

void LoopRunner::advancePhaseAfterMove() {
  if (phase_ == LoopPhase::MovingToB) {
    phase_ = LoopPhase::DwellingAtB;
    dwell_elapsed_s_ = 0.0;
  } else {  // MovingToA
    if (!cfg_.repeat) {
      phase_ = LoopPhase::Stopped;
      return;
    }
    phase_ = LoopPhase::DwellingAtA;
    dwell_elapsed_s_ = 0.0;
  }
}

void LoopRunner::advancePhaseAfterDwell() {
  if (phase_ == LoopPhase::DwellingAtB) {
    startLeg(cfg_.pos_a_mm, LoopPhase::MovingToA);
  } else {  // DwellingAtA
    startLeg(cfg_.pos_b_mm, LoopPhase::MovingToB);
  }
}

}  // namespace glide
