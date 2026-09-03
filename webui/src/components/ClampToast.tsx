import { CloseIcon } from "../icons";

// Non-alarming by design (docs/m4_ui_plan.md: "it's the system working
// as designed, not a malfunction") -- neutral panel color, no red/
// warning tint, auto-dismisses on its own (see store.ts's
// scheduleToastClear). Positioned relative to a `.screen`-scoped
// wrapper so `bottom` stays correct regardless of what's below it.
export function ClampToast({ bottom }: { bottom: number }) {
  return (
    <div class="toast" style={{ bottom: `${bottom}px` }}>
      <CloseIcon size={17} color="var(--warn)" />
      <span class="toast-text">Move clamped to travel limit</span>
    </div>
  );
}
