import { useEffect } from "preact/hooks";
import { api } from "../api";
import {
  status,
  loopConfig,
  presets,
  isBusy,
  clampToastVisible,
  connected,
  refreshPresets,
} from "../store";
import { activeTab, editingPreset } from "../ui";
import { NotHomedBanner } from "../components/NotHomedBanner";
import { ActivePresetChip } from "../components/ActivePresetChip";
import { PhaseChip } from "../components/PhaseChip";
import { ClampToast } from "../components/ClampToast";
import { ChevronLeft, ChevronRight, GlideMark, PlayFillIcon, StopFillIcon, WifiIcon } from "../icons";

// A quick nudge, not precision positioning -- precise A/B placement
// happens in the Preset Editor's jog-and-set flow. 5mm keeps a single
// tap meaningful without needing press-and-hold on this screen.
const CONTROL_JOG_STEP_MM = 5;

export function ControlScreen() {
  const s = status.value;
  const lc = loopConfig.value;
  const homed = s?.homed ?? false;
  const ready = homed && (s?.travel_set ?? false) && (s?.calibrated ?? false);
  const busy = isBusy.value;
  const positionMm = s?.position_mm ?? 0;

  useEffect(() => {
    void refreshPresets();
  }, []);

  const posA = lc?.pos_a_mm ?? 0;
  const posB = lc?.pos_b_mm ?? 0;
  const span = Math.abs(posB - posA) || 1;
  const trackPercent = Math.max(0, Math.min(100, ((positionMm - Math.min(posA, posB)) / span) * 100));

  return (
    <div class="screen">
      <div class="header">
        <div class="brand">
          <GlideMark size={22} color="var(--accent)" />
          <span>GLIDE</span>
        </div>
        <WifiIcon size={20} color={connected.value ? "var(--text-dim)" : "var(--danger)"} />
      </div>
      <div style={{ flex: 1, minHeight: 0, overflowY: "auto", padding: "4px 20px 0", display: "flex", flexDirection: "column", gap: "16px", position: "relative" }}>
      {!homed && <NotHomedBanner />}

      <ActivePresetChip name={s?.active_preset ?? null} />

      <div class="panel-readout">
        <PhaseChip phase={s?.phase ?? "IDLE"} />
        <div>
          <div class="readout-label">TARGET</div>
          <div class="readout-value tabular">
            {positionMm.toFixed(1)}
            <span class="readout-unit">mm</span>
          </div>
        </div>
        <div class="track-wrap">
          <div class="track">
            <div class="track-dot" style={{ left: `${trackPercent}%` }} />
          </div>
          <div class="track-labels">
            <span class="track-label">A · {posA.toFixed(0)}mm</span>
            <span class="track-label">B · {posB.toFixed(0)}mm</span>
          </div>
        </div>
      </div>

      <div class="grid-2">
        <button
          class="btn btn-panel press"
          disabled={!ready || busy}
          onClick={() => api.jog(-CONTROL_JOG_STEP_MM)}
        >
          <ChevronLeft size={22} />
          <span>Jog</span>
        </button>
        <button
          class="btn btn-panel press"
          disabled={!ready || busy}
          onClick={() => api.jog(CONTROL_JOG_STEP_MM)}
        >
          <span>Jog</span>
          <ChevronRight size={22} />
        </button>
        <button
          class="btn btn-panel press"
          style={{ height: "52px" }}
          disabled={!ready || busy}
          onClick={() => api.move(posA)}
        >
          Go to A
        </button>
        <button
          class="btn btn-panel press"
          style={{ height: "52px" }}
          disabled={!ready || busy}
          onClick={() => api.move(posB)}
        >
          Go to B
        </button>
      </div>

      <button
        class="btn btn-accent press"
        style={{ height: "64px", fontSize: "16px" }}
        disabled={!ready || busy}
        onClick={() => api.loopStart()}
      >
        <PlayFillIcon size={20} color="var(--accent-fg)" />
        Start Loop
      </button>

      <div>
        <div class="rail-title-row">
          <span class="rail-title">PRESETS</span>
          <a
            href="#"
            onClick={(e) => {
              e.preventDefault();
              activeTab.value = "presets";
            }}
          >
            See all
          </a>
        </div>
        <div class="rail">
          {presets.value.map((p, i) => (
            <button
              key={p.name}
              class="rail-card press fade-rise"
              style={{ "--stagger-delay": `${i * 40}ms` }}
              disabled={!ready || busy}
              onClick={() => {
                void api.loadPreset(p.name);
              }}
              onDblClick={() => {
                editingPreset.value = p.name;
                activeTab.value = "presets";
              }}
            >
              <div class="rail-card-name">{p.name}</div>
              <div class="rail-card-meta">
                A→B · {p.speed_mm_s}mm/s · {p.repeat ? "∞" : "×1"}
              </div>
            </button>
          ))}
        </div>
      </div>

      {clampToastVisible.value && <ClampToast bottom={8} />}
      </div>
    </div>
  );
}

export function StopBar() {
  return (
    <div class="stop-bar">
      <button class="btn btn-danger btn-stop press" onClick={() => api.stop()}>
        <StopFillIcon size={20} color="oklch(0.98 0.01 25)" />
        STOP
      </button>
    </div>
  );
}
