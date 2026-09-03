import { useEffect, useState } from "preact/hooks";
import { api } from "../api";
import { isActive } from "../store";
import { ConfirmDialog } from "../components/ConfirmDialog";
import { WifiIcon, UploadIcon, WarnIcon } from "../icons";
import type { WifiInfo } from "../types";

function signalBars(rssi: number) {
  // Typical WiFi RSSI range: roughly -30 (excellent) to -90 (unusable).
  const pct = Math.max(0, Math.min(1, (rssi + 90) / 60));
  const lit = Math.max(1, Math.round(pct * 4));
  return [1, 2, 3, 4].map((i) => i <= lit);
}

export function DeviceScreen() {
  const [wifi, setWifi] = useState<WifiInfo | null>(null);
  const [confirmRestart, setConfirmRestart] = useState(false);
  const [confirmForget, setConfirmForget] = useState(false);
  const [otaKey, setOtaKey] = useState("");
  const [otaFile, setOtaFile] = useState<File | null>(null);
  const [otaProgress, setOtaProgress] = useState<number | null>(null);
  const [otaError, setOtaError] = useState<string | null>(null);
  const busy = isActive.value;

  useEffect(() => {
    void api.getWifi().then(setWifi);
    const id = setInterval(() => void api.getWifi().then(setWifi), 5000);
    return () => clearInterval(id);
  }, []);

  async function handleOtaUpload() {
    if (!otaFile || !otaKey) return;
    setOtaError(null);
    setOtaProgress(0);
    const result = await api.uploadFirmware(otaFile, otaKey, setOtaProgress);
    if ("error" in result) {
      setOtaError(result.error);
      setOtaProgress(null);
    } else {
      setOtaProgress(100);
      // A successful OTA reboots the device on its own -- the
      // WebSocket's own reconnect loop (store.ts) picks it back up
      // once it's back, same as any other WiFi blip.
    }
  }

  return (
    <div class="screen">
      <div class="page-header">
        <div class="page-title">Device</div>
      </div>

      <div style={{ flex: 1, overflowY: "auto", padding: "16px 20px 24px", display: "flex", flexDirection: "column", gap: "18px" }}>
        <div>
          <div class="section-label">NETWORK</div>
          <div class="info-panel">
            <div class="info-row">
              <div style={{ display: "flex", alignItems: "center", gap: "10px" }}>
                <WifiIcon size={18} color="var(--success)" />
                <span style={{ fontWeight: 700, fontSize: "14.5px" }}>{wifi?.ssid ?? "—"}</span>
              </div>
              <span style={{ fontSize: "12.5px", fontWeight: 700, color: "var(--success)" }}>Connected</span>
            </div>
            <div class="info-row">
              <span class="settings-row-label" style={{ color: "var(--text-dim)" }}>
                Signal
              </span>
              <div class="signal-bars">
                {wifi &&
                  signalBars(wifi.rssi).map((lit, i) => (
                    <div key={i} class={`signal-bar ${lit ? "lit" : ""}`} style={{ height: `${5 + i * 3}px` }} />
                  ))}
              </div>
            </div>
            <div class="info-row">
              <span class="settings-row-label" style={{ color: "var(--text-dim)" }}>
                Address
              </span>
              <span class="mono" style={{ fontWeight: 700, fontSize: "13.5px" }}>
                glide.local
              </span>
            </div>
            <div class="info-row">
              <span class="settings-row-label" style={{ color: "var(--text-dim)" }}>
                IP
              </span>
              <span class="mono" style={{ fontWeight: 700, fontSize: "13.5px", color: "var(--text-dim)" }}>
                {wifi?.ip ?? "—"}
              </span>
            </div>
          </div>
          <button
            class="btn btn-ghost press"
            style={{ width: "100%", height: "48px", marginTop: "10px" }}
            disabled={busy}
            onClick={() => setConfirmForget(true)}
          >
            Forget Network
          </button>
        </div>

        <div>
          <div class="section-label">FIRMWARE</div>
          <div class="info-panel">
            <div class="info-row">
              <span class="settings-row-label" style={{ color: "var(--text-dim)" }}>
                Version
              </span>
              <span class="mono" style={{ fontWeight: 700, fontSize: "13.5px" }}>
                {wifi?.firmware_version ?? "—"}
              </span>
            </div>
          </div>

          <div style={{ display: "flex", flexDirection: "column", gap: "8px", marginTop: "10px" }}>
            <input
              class="field-input"
              type="text"
              placeholder="X-Glide-OTA-Key"
              value={otaKey}
              onInput={(e) => setOtaKey((e.target as HTMLInputElement).value)}
            />
            <input
              type="file"
              accept=".bin"
              disabled={busy}
              onChange={(e) => setOtaFile((e.target as HTMLInputElement).files?.[0] ?? null)}
              style={{ fontSize: "13px", color: "var(--text-dim)" }}
            />
            <button
              class="btn btn-accent press"
              style={{ height: "52px" }}
              disabled={busy || !otaFile || !otaKey || otaProgress !== null}
              onClick={handleOtaUpload}
            >
              <UploadIcon size={17} color="var(--accent-fg)" />
              {otaProgress !== null ? `Uploading… ${otaProgress}%` : "Update Firmware"}
            </button>
            {otaProgress !== null && (
              <div class="upload-track">
                <div class="upload-fill" style={{ width: `${otaProgress}%` }} />
              </div>
            )}
            {otaError && (
              <div style={{ color: "var(--danger)", fontSize: "12.5px", fontWeight: 600 }}>Failed: {otaError}</div>
            )}
          </div>
          <div style={{ display: "flex", alignItems: "center", gap: "7px", marginTop: "10px", padding: "0 4px" }}>
            <WarnIcon size={14} color="var(--text-dimmer)" />
            <span style={{ fontSize: "12px", color: "var(--text-dimmer)", lineHeight: 1.4 }}>
              Disabled while the carriage is moving — stop first.
            </span>
          </div>
        </div>

        <div>
          <div class="section-label">DEVICE</div>
          <button
            class="btn btn-panel press"
            style={{ width: "100%", height: "48px", color: "var(--danger)" }}
            disabled={busy}
            onClick={() => setConfirmRestart(true)}
          >
            Restart Device
          </button>
        </div>
      </div>

      {confirmRestart && (
        <ConfirmDialog
          title="Restart Device?"
          body="This reboots the ESP32 immediately. Any in-progress move stops. This does not affect saved config or presets."
          confirmLabel="Restart"
          onCancel={() => setConfirmRestart(false)}
          onConfirm={() => {
            setConfirmRestart(false);
            void api.restart();
          }}
        />
      )}
      {confirmForget && (
        <ConfirmDialog
          title="Forget This Network?"
          body="Clears the saved WiFi password and restarts into the setup portal. You'll need to rejoin from a phone or laptop before this page works again."
          confirmLabel="Forget & Restart"
          onCancel={() => setConfirmForget(false)}
          onConfirm={() => {
            setConfirmForget(false);
            void api.forgetWifi();
          }}
        />
      )}
    </div>
  );
}
