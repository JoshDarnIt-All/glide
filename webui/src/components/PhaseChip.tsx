import type { Phase } from "../types";
import { ArrowRightIcon, DwellIcon, IdleDotIcon, StoppedSquareIcon } from "../icons";

const ACTIVE = new Set<Phase>(["MOVING", "MOVING_TO_A", "MOVING_TO_B"]);
const DWELL = new Set<Phase>(["DWELLING_AT_A", "DWELLING_AT_B"]);

export function PhaseChip({ phase }: { phase: Phase }) {
  const isActive = ACTIVE.has(phase);
  const isDwell = DWELL.has(phase);
  const isStopped = phase === "STOPPED";

  // Moving and dwelling share the accent color (both are "the loop is
  // doing something on purpose") but get different animation
  // treatments so they never read as the same state -- moving pulses
  // fast, dwelling breathes slowly ("paused on purpose", not stuck).
  // Stopped drops out of amber into neutral gray entirely, and idle is
  // the dimmest, quietest treatment of all four -- see
  // docs/m4_ui_plan.md's phase-representation requirements.
  let bg = "oklch(0.24 0.016 250)";
  let fg = "oklch(0.62 0.012 250)";
  let animClass = "";
  if (isActive) {
    bg = "var(--accent)";
    fg = "var(--accent-fg)";
    animClass = "anim-active";
  } else if (isDwell) {
    bg = "var(--accent)";
    fg = "var(--accent-fg)";
    animClass = "anim-dwell";
  } else if (isStopped) {
    bg = "var(--bg-panel-3)";
    fg = "oklch(0.86 0.01 250)";
  }

  return (
    <div class={`phase-chip ${animClass}`} style={{ background: bg, color: fg }}>
      {isActive && <ArrowRightIcon size={15} color={fg} />}
      {isDwell && <DwellIcon size={15} color={fg} />}
      {isStopped && <StoppedSquareIcon size={14} color={fg} />}
      {!isActive && !isDwell && !isStopped && <IdleDotIcon size={9} color={fg} />}
      <span class="phase-chip-label">{phase.replace(/_/g, " ")}</span>
    </div>
  );
}
