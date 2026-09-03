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
