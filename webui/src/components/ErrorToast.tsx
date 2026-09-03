import { errorToast } from "../toast";
import { WarnIcon } from "../icons";

// Global, viewport-fixed (unlike ClampToast, which is scoped to the
// Control screen's own scroll area) -- an action can fail from any
// screen, so this needs to be visible regardless of which one is
// active. Danger-tinted, unlike ClampToast's deliberately-neutral
// styling, since this really is something that didn't work, not the
// system working as designed.
export function ErrorToast() {
  const msg = errorToast.value;
  if (!msg) return null;
  return (
    <div class="error-toast" key={msg.id}>
      <WarnIcon size={16} color="var(--danger)" />
      <span class="toast-text">{msg.text}</span>
    </div>
  );
}
