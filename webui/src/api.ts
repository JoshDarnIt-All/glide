// Thin REST wrapper matching docs/api.md exactly. Every mutating call
// returns only an immediate ok/error signal -- the resulting state
// change (new position, new active_preset, etc.) is read back from
// the next WebSocket "status" push, never synthesized here. Same-
// origin always: this app is served BY the device once embedded, so
// relative paths are correct both there and through Vite's dev proxy.

import type { ApiResult, AxisConfig, LoopConfig, Preset, WifiInfo } from "./types";
import { showErrorToast } from "./toast";

const BASE = "/api/v1";

// Every mutating call in this module goes through req()/postJson()/
// patchJson(), so surfacing an error here once covers every screen --
// added after review found most screens had no visible feedback at
// all when an action failed (only the Device screen's OTA flow showed
// errors, since it needed its own progress UI anyway). A screen can
// still inspect the returned ApiResult itself for inline handling
// (the Preset Editor does, to show "Name required" next to the field
// rather than just a toast) -- this doesn't replace that, it's a
// floor everything gets for free.
async function req(path: string, init?: RequestInit): Promise<ApiResult> {
  let result: ApiResult;
  try {
    const res = await fetch(BASE + path, init);
    const body = await res.json().catch(() => ({}));
    if (!res.ok) {
      result = { error: (body as { error?: string }).error ?? `HTTP_${res.status}` };
    } else {
      result = body as ApiResult;
    }
  } catch {
    result = { error: "NETWORK_ERROR" };
  }
  if ("error" in result) showErrorToast(result.error);
  return result;
}

function postJson(path: string, body: unknown): Promise<ApiResult> {
  return req(path, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(body),
  });
}

function patchJson(path: string, body: unknown): Promise<ApiResult> {
  return req(path, {
    method: "PATCH",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(body),
  });
}

async function getJson<T>(path: string): Promise<T> {
  const res = await fetch(BASE + path);
  return (await res.json()) as T;
}

export const api = {
  home: () => req("/home", { method: "POST" }),

  getAxis: () => getJson<AxisConfig>("/axis"),
  patchAxis: (patch: Partial<AxisConfig>) => patchJson("/axis", patch),

  getLoopConfig: () => getJson<LoopConfig>("/loop-config"),
  patchLoopConfig: (patch: Partial<LoopConfig>) => patchJson("/loop-config", patch),
  markA: () => req("/loop-config/mark-a", { method: "POST" }),
  markB: () => req("/loop-config/mark-b", { method: "POST" }),

  move: (posMm: number) => postJson("/move", { pos_mm: posMm }),
  jog: (deltaMm: number) => postJson("/jog", { delta_mm: deltaMm }),
  stop: () => req("/stop", { method: "POST" }),
  loopStart: () => req("/loop/start", { method: "POST" }),

  getPresets: () => getJson<Preset[]>("/presets"),
  getPreset: (name: string) => getJson<Preset>(`/presets/${encodeURIComponent(name)}`),
  putPreset: (name: string, preset: Omit<Preset, "name">) =>
    req(`/presets/${encodeURIComponent(name)}`, {
      method: "PUT",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(preset),
    }),
  saveCurrentAsPreset: (name: string) =>
    req(`/presets/${encodeURIComponent(name)}/save-current`, { method: "POST" }),
  loadPreset: (name: string) =>
    req(`/presets/${encodeURIComponent(name)}/load`, { method: "POST" }),
  deletePreset: (name: string) =>
    req(`/presets/${encodeURIComponent(name)}`, { method: "DELETE" }),

  saveConfig: () => req("/config/save", { method: "POST" }),

  getWifi: () => getJson<WifiInfo>("/wifi"),
  forgetWifi: () => req("/wifi/forget", { method: "POST" }),
  restart: () => req("/restart", { method: "POST" }),

  // OTA is a multipart upload with a user-supplied key (not compiled
  // in, unlike everything else here) -- exposed with an onProgress
  // callback via XHR since fetch() doesn't expose upload progress.
  uploadFirmware(file: File, otaKey: string, onProgress: (pct: number) => void): Promise<ApiResult> {
    return new Promise((resolve) => {
      const xhr = new XMLHttpRequest();
      xhr.open("POST", BASE + "/ota");
      xhr.setRequestHeader("X-Glide-OTA-Key", otaKey);
      xhr.upload.onprogress = (e) => {
        if (e.lengthComputable) onProgress(Math.round((e.loaded / e.total) * 100));
      };
      xhr.onload = () => {
        try {
          const body = JSON.parse(xhr.responseText) as ApiResult;
          resolve(xhr.status >= 200 && xhr.status < 300 ? body : { error: (body as { error?: string }).error ?? `HTTP_${xhr.status}` });
        } catch {
          resolve({ error: "NETWORK_ERROR" });
        }
      };
      xhr.onerror = () => resolve({ error: "NETWORK_ERROR" });
      const form = new FormData();
      form.append("firmware", file);
      xhr.send(form);
    });
  },
};
