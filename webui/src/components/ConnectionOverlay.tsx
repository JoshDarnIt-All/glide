import { SpinnerIcon } from "../icons";

// Full-surface takeover, not a corner icon -- docs/m4_ui_plan.md is
// explicit that a live control surface silently going stale while
// looking normal is the worst failure mode for a device with no
// brake. Every control underneath is visually degraded (see
// .content-degraded in style.css) so there's no tap-and-get-an-error
// loop.
export function ConnectionOverlay() {
  return (
    <div class="overlay-lost">
      <SpinnerIcon size={30} color="var(--text)" />
      <div>
        <div class="overlay-lost-title">Reconnecting…</div>
        <div class="overlay-lost-body">Controls are paused until the connection returns</div>
      </div>
    </div>
  );
}
