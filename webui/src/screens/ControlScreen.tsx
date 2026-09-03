import { useEffect, useRef } from "preact/hooks";
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
// happens in the Preset Editor's jog-and-set flow.
const CONTROL_JOG_STEP_MM = 5;
// A quick tap fires exactly one CONTROL_JOG_STEP_MM nudge; holding
// past this threshold switches to continuous jogging (repeated small
// moves every JOG_HOLD_INTERVAL_MS) until released -- confirmed with
// Josh on real hardware: a single fixed-distance jog per tap felt
// right for small nudges, but reaching further meant a lot of
// re-tapping, and he specifically didn't want to deal with a separate
// Stop tap for that (this screen already has one, but wants jog itself
// to be "moving while held, stopped when released").
const JOG_HOLD_THRESHOLD_MS = 220;
const JOG_HOLD_INTERVAL_MS = 150;

// Not disabled by `busy` (unlike every other action button on this
// screen) -- disabling mid-hold would stop this button from ever
// receiving the pointerup/pointerleave that's supposed to end the
// hold, since a real <button disabled> stops receiving pointer events
// entirely. Each jog call retargets smoothly from wherever the
// carriage actually is (see the firmware's MOVETO/JOG handlers), so
// firing several in a row -- tap-tap-tap or a held interval -- is
// safe by design, not just tolerated.
function useJogHold(deltaMm: number) {
  const holdTimer = useRef<ReturnType<typeof setInterval> | null>(null);
  const tapTimer = useRef<ReturnType<typeof setTimeout> | null>(null);
  const holding = useRef(false);

  function start() {
    holding.current = false;
    tapTimer.current = setTimeout(() => {
      holding.current = true;
      void api.jog(deltaMm);
      holdTimer.current = setInterval(() => void api.jog(deltaMm), JOG_HOLD_INTERVAL_MS);
    }, JOG_HOLD_THRESHOLD_MS);
  }

  function clearTimers() {
    if (tapTimer.current) {
      clearTimeout(tapTimer.current);
      tapTimer.current = null;
    }
    if (holdTimer.current) {
      clearInterval(holdTimer.current);
      holdTimer.current = null;
    }
  }

  function stop() {
    const wasHolding = holding.current;
    clearTimers();
    holding.current = false;
    if (!wasHolding) {
      // Released before the hold threshold fired -- treat as a tap.
      void api.jog(deltaMm);
    }
  }

  // Unmount cleanup only -- must NOT fire a tap-jog (stop() would),
  // just stop whatever timer might still be running.
  useEffect(() => clearTimers, []);

  return { start, stop };
}

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

  const jogLeft = useJogHold(-CONTROL_JOG_STEP_MM);
  const jogRight = useJogHold(CONTROL_JOG_STEP_MM);

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
          disabled={!ready}
          onPointerDown={jogLeft.start}
          onPointerUp={jogLeft.stop}
          onPointerLeave={jogLeft.stop}
          onPointerCancel={jogLeft.stop}
        >
          <ChevronLeft size={22} />
          <span>Jog</span>
        </button>
        <button
          class="btn btn-panel press"
          disabled={!ready}
          onPointerDown={jogRight.start}
          onPointerUp={jogRight.stop}
          onPointerLeave={jogRight.stop}
          onPointerCancel={jogRight.stop}
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
