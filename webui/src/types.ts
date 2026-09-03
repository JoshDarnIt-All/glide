// Mirrors docs/api.md exactly -- field names are not renamed/reshaped
// anywhere in this app, so this file stays the single source of truth
// for what the firmware actually sends/expects.

export type Phase =
  | "IDLE"
  | "MOVING"
  | "MOVING_TO_A"
  | "MOVING_TO_B"
  | "DWELLING_AT_A"
  | "DWELLING_AT_B"
  | "STOPPED";

export interface StatusFrame {
  position_mm: number;
  velocity_mm_s: number;
  phase: Phase;
  homed: boolean;
  travel_set: boolean;
  calibrated: boolean;
  travel_mm?: number; // present on GET /status and the WS "status" frame's REST sibling; WS frame omits it (see docs/api.md)
  active_preset: string | null;
}

export type WsEventName = "phase_change" | "clamped_to_soft_limit" | "preset_loaded";

export type WsMessage =
  | ({ type: "status" } & StatusFrame)
  | { type: "event"; event: "phase_change"; phase: Phase }
  | { type: "event"; event: "clamped_to_soft_limit" }
  | { type: "event"; event: "preset_loaded"; name: string }
  | { type: "heartbeat"; t: number };

export interface AxisConfig {
  travel_mm: number;
  steps_per_mm: number;
}

export interface LoopConfig {
  pos_a_mm: number;
  pos_b_mm: number;
  speed_mm_s: number;
  accel_mm_s2: number;
  dwell_a_s: number;
  dwell_b_s: number;
  repeat: boolean;
}

export interface Preset extends LoopConfig {
  name: string;
}

export interface WifiInfo {
  ssid: string;
  rssi: number;
  ip: string;
  firmware_version: string;
}

export interface ApiOk {
  ok: true;
}
export interface ApiErr {
  error: string;
}
export type ApiResult = ApiOk | ApiErr;

export function isApiError(r: ApiResult): r is ApiErr {
  return "error" in r;
}
