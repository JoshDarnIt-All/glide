// Navigation/view state -- deliberately separate from store.ts's live
// device data. No router library: this is 4 tabs plus one pushed
// editor screen, not deep-linked routes (docs/m4_ui_plan.md's
// "Technical architecture" section).
import { signal } from "@preact/signals";

export type Tab = "control" | "presets" | "setup" | "device";

export const activeTab = signal<Tab>("control");
// null = library view; a string = editing that preset (or the literal
// "__new__" sentinel for a brand-new, unsaved preset).
export const editingPreset = signal<string | null>(null);

// User-adjustable jog distance for the Control screen's Jog buttons
// (both the single-tap nudge and each step of a press-and-hold) --
// requested live: 5mm is a sensible default for fine positioning, but
// a user may want much bigger steps (e.g. sweeping toward the far end
// of a long rail to find its real travel limit) without switching
// screens. Session-only (resets to the default on reload), same as
// the rest of this file's navigation state -- not worth persisting
// for a value this quick to re-type.
export const controlJogStepMm = signal(5);
