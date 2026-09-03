// A single global error toast, shown automatically whenever any REST
// action fails (see api.ts's req()) -- added after review found every
// screen except Device's OTA flow had no visible feedback when an
// action failed (e.g. tapping "Start Loop" with A/B unset silently
// did nothing). One shared mechanism here means no future screen can
// forget to wire this up individually.
import { signal } from "@preact/signals";

interface ToastMessage {
  id: number;
  text: string;
}

export const errorToast = signal<ToastMessage | null>(null);

let nextId = 0;
let clearTimer: ReturnType<typeof setTimeout> | null = null;

// Human-readable text for the serial-style ERR reasons docs/api.md
// defines -- falls back to the raw code for anything not listed here
// rather than hiding an unexpected error behind a vague message.
const MESSAGES: Record<string, string> = {
  NOT_HOMED: "Home the carriage first",
  TRAVEL_NOT_SET: "Set travel range first",
  NOT_CALIBRATED: "Calibrate steps-per-mm first",
  ALREADY_RUNNING: "Loop is already running",
  MOVING: "Already moving — stop first",
  OTA_IN_PROGRESS: "An update is in progress",
  NOT_FOUND: "Not found",
  INVALID_VALUE: "Invalid value",
  UNAUTHORIZED: "Unauthorized",
  SAVE_FAILED: "Save failed",
  NETWORK_ERROR: "Couldn't reach the device",
};

export function showErrorToast(code: string) {
  const id = ++nextId;
  errorToast.value = { id, text: MESSAGES[code] ?? code };
  if (clearTimer) clearTimeout(clearTimer);
  clearTimer = setTimeout(() => {
    if (errorToast.value?.id === id) errorToast.value = null;
  }, 3200);
}
