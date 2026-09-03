// Single WebSocket connection feeding fine-grained Signals -- every
// screen reads only the specific fields it needs, so a ~10Hz position
// update re-renders the position dot, not all five screens' worth of
// components. See docs/m4_ui_plan.md's "Technical architecture"
// section for the full reasoning.

import { signal, computed } from "@preact/signals";
import { api } from "./api";
import type { AxisConfig, LoopConfig, Phase, Preset, StatusFrame, WsMessage } from "./types";

export const status = signal<StatusFrame | null>(null);
export const connected = signal(false);

// These three don't travel over the WebSocket (docs/api.md only
// streams live motion/phase state there) -- fetched over REST and
// refreshed on connect, after any action known to change them, and
// once per heartbeat (~5s) as a light catch-all in case something
// else (serial, a future Companion module) changed them out from
// under this client.
export const loopConfig = signal<LoopConfig | null>(null);
export const axis = signal<AxisConfig | null>(null);
export const presets = signal<Preset[]>([]);

export async function refreshLoopConfig() {
  loopConfig.value = await api.getLoopConfig();
}
export async function refreshAxis() {
  axis.value = await api.getAxis();
}
export async function refreshPresets() {
  presets.value = await api.getPresets();
}
function refreshAll() {
  void refreshLoopConfig();
  void refreshAxis();
  void refreshPresets();
}
// Cleared automatically a few seconds after it fires -- see
// scheduleToastClear() below. Only ever one at a time; a second event
// arriving replaces it rather than queuing, matching this app's
// "never make the user wait to see current truth" bias.
export const clampToastVisible = signal(false);

export const phase = computed<Phase>(() => status.value?.phase ?? "IDLE");
export const isActive = computed(
  () => phase.value === "MOVING" || phase.value === "MOVING_TO_A" || phase.value === "MOVING_TO_B"
);
export const isDwelling = computed(
  () => phase.value === "DWELLING_AT_A" || phase.value === "DWELLING_AT_B"
);
export const isBusy = computed(() => isActive.value || isDwelling.value);

const RECONNECT_DELAY_MS = 2000;
// ~2 missed heartbeats (heartbeats arrive every ~5s) -- matches
// docs/m4_ui_plan.md's connection-loss spec exactly. Reset on ANY
// message, not just heartbeats, so an active status stream (which
// arrives far more often than heartbeats while something's moving)
// counts as just as alive.
const LIVENESS_TIMEOUT_MS = 13000;

let ws: WebSocket | null = null;
let livenessTimer: ReturnType<typeof setTimeout> | null = null;
let toastTimer: ReturnType<typeof setTimeout> | null = null;

function resetLiveness() {
  connected.value = true;
  if (livenessTimer) clearTimeout(livenessTimer);
  livenessTimer = setTimeout(() => {
    connected.value = false;
  }, LIVENESS_TIMEOUT_MS);
}

function scheduleToastClear() {
  if (toastTimer) clearTimeout(toastTimer);
  clampToastVisible.value = true;
  toastTimer = setTimeout(() => {
    clampToastVisible.value = false;
  }, 2600);
}

function handleMessage(raw: string) {
  let msg: WsMessage;
  try {
    msg = JSON.parse(raw);
  } catch {
    return;
  }
  resetLiveness();

  if (msg.type === "status") {
    const { type: _type, ...rest } = msg;
    status.value = rest as StatusFrame;
  } else if (msg.type === "event" && msg.event === "clamped_to_soft_limit") {
    scheduleToastClear();
  } else if (msg.type === "event" && msg.event === "preset_loaded") {
    // A preset load changes A/B/speed/accel/dwell/repeat all at once --
    // refresh loop-config rather than trying to reconstruct it from
    // the preset list already held client-side.
    void refreshLoopConfig();
  } else if (msg.type === "heartbeat") {
    refreshAll();
  }
  // "phase_change" doesn't need separate handling here -- the next
  // "status" frame (pushed at ~10Hz while anything is busy) already
  // carries the resulting phase.
}

export function connectWebSocket() {
  if (ws && (ws.readyState === WebSocket.OPEN || ws.readyState === WebSocket.CONNECTING)) {
    return;
  }
  const proto = location.protocol === "https:" ? "wss:" : "ws:";
  ws = new WebSocket(`${proto}//${location.host}/ws`);

  ws.onopen = () => {
    resetLiveness();
    refreshAll();
  };
  ws.onmessage = (e) => handleMessage(e.data);
  ws.onclose = () => {
    connected.value = false;
    setTimeout(connectWebSocket, RECONNECT_DELAY_MS);
  };
  ws.onerror = () => {
    ws?.close();
  };
}
