import { WarnIcon } from "../icons";

// Used for exactly two actions in the entire app -- Restart Device and
// Forget Network (Device screen) -- both irreversible-in-the-moment
// and disruptive with no undo. Every other action (moves, preset
// recall, even deleting a preset) is deliberately instant per
// docs/m4_ui_plan.md's "one tap, confident" premise, so this needs to
// look and feel like a genuine, rare exception, not a default modal
// pattern creeping back into an app that otherwise never asks twice.
export function ConfirmDialog({
  title,
  body,
  confirmLabel,
  onConfirm,
  onCancel,
}: {
  title: string;
  body: string;
  confirmLabel: string;
  onConfirm: () => void;
  onCancel: () => void;
}) {
  return (
    <div class="confirm-scrim" onClick={onCancel}>
      <div class="confirm-card" onClick={(e) => e.stopPropagation()}>
        <WarnIcon size={28} color="var(--warn)" />
        <div class="confirm-title">{title}</div>
        <div class="confirm-body">{body}</div>
        <div class="confirm-actions">
          <button class="btn btn-panel press" onClick={onCancel}>
            Cancel
          </button>
          <button class="btn btn-danger press" onClick={onConfirm}>
            {confirmLabel}
          </button>
        </div>
      </div>
    </div>
  );
}
