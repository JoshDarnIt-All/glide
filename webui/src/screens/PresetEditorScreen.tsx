import { useEffect, useRef, useState } from "preact/hooks";
import { api } from "../api";
import { status, loopConfig, presets, refreshLoopConfig, refreshPresets } from "../store";
import { editingPreset } from "../ui";
import { ChevronDown, ChevronLeft, ChevronRight, PlayFillIcon } from "../icons";
import type { Preset } from "../types";

const JOG_STEP_MM = 2;

function findExisting(name: string): Preset | undefined {
  return presets.value.find((p) => p.name.toLowerCase() === name.toLowerCase());
}

export function PresetEditorScreen() {
  const editingName = editingPreset.value;
  const isNew = editingName === "__new__";
  const existing = !isNew && editingName ? findExisting(editingName) : undefined;

  // Seeded once on mount: a new preset starts from whatever the
  // carriage/loop-config is doing right now (a reasonable starting
  // point if you're already roughly positioned) rather than zeros --
  // an editing preset loads its own saved values.
  const seed = existing ?? {
    name: "",
    pos_a_mm: loopConfig.value?.pos_a_mm ?? 0,
    pos_b_mm: loopConfig.value?.pos_b_mm ?? 0,
    speed_mm_s: loopConfig.value?.speed_mm_s ?? 20,
    accel_mm_s2: loopConfig.value?.accel_mm_s2 ?? 100,
    dwell_a_s: loopConfig.value?.dwell_a_s ?? 0,
    dwell_b_s: loopConfig.value?.dwell_b_s ?? 0,
    repeat: loopConfig.value?.repeat ?? true,
  };

  const [name, setName] = useState(isNew ? "" : seed.name);
  const [posA, setPosA] = useState(seed.pos_a_mm);
  const [posB, setPosB] = useState(seed.pos_b_mm);
  const [speed, setSpeed] = useState(seed.speed_mm_s);
  const [accel, setAccel] = useState(seed.accel_mm_s2);
  const [dwellA, setDwellA] = useState(seed.dwell_a_s);
  const [dwellB, setDwellB] = useState(seed.dwell_b_s);
  const [repeat, setRepeat] = useState(seed.repeat);
  const [advancedOpen, setAdvancedOpen] = useState(false);
  const [saveError, setSaveError] = useState<string | null>(null);
  const [previewing, setPreviewing] = useState(false);

  // Press-and-hold jog: single tap moves once, holding repeats every
  // 180ms until released -- this IS the precision-positioning screen,
  // unlike Control's single-nudge jog buttons.
  const holdTimer = useRef<ReturnType<typeof setInterval> | null>(null);
  function startHold(deltaMm: number) {
    void api.jog(deltaMm);
    holdTimer.current = setInterval(() => void api.jog(deltaMm), 180);
  }
  function stopHold() {
    if (holdTimer.current) clearInterval(holdTimer.current);
    holdTimer.current = null;
  }
  useEffect(() => stopHold, []);

  const currentPos = status.value?.position_mm ?? 0;

  async function handleSave() {
    const trimmed = name.trim();
    if (!trimmed) {
      setSaveError("Name required");
      return;
    }
    const dup = findExisting(trimmed);
    if (dup && (isNew || trimmed.toLowerCase() !== (editingName ?? "").toLowerCase())) {
      if (!confirm(`A preset named "${dup.name}" already exists. Overwrite it?`)) return;
    }
    const result = await api.putPreset(trimmed, {
      pos_a_mm: posA,
      pos_b_mm: posB,
      speed_mm_s: speed,
      accel_mm_s2: accel,
      dwell_a_s: dwellA,
      dwell_b_s: dwellB,
      repeat,
    });
    if ("error" in result) {
      setSaveError(result.error);
      return;
    }
    await refreshPresets();
    editingPreset.value = null;
  }

  async function handlePreviewLeg() {
    setPreviewing(true);
    // "Preview this leg" without saving a preset: apply the
    // currently-edited speed/accel live (loop-config, not the preset
    // list) and move to whichever of A/B the carriage isn't already
    // closer to -- a judgment call on an ambiguous mockup label ("no
    // body" spec beyond the button existing), documented here rather
    // than decided silently.
    await api.patchLoopConfig({ speed_mm_s: speed, accel_mm_s2: accel });
    const target = Math.abs(currentPos - posA) < Math.abs(currentPos - posB) ? posB : posA;
    await api.move(target);
    await refreshLoopConfig();
    setPreviewing(false);
  }

  return (
    <div class="screen">
      <div class="editor-header">
        <button class="editor-back press" onClick={() => (editingPreset.value = null)} aria-label="Back">
          <ChevronLeft size={22} />
        </button>
        <span style={{ fontWeight: 800, fontSize: "16px" }}>{isNew ? "New Preset" : "Edit Preset"}</span>
        <button class="editor-save press" onClick={handleSave}>
          Save
        </button>
      </div>

      <div style={{ flex: 1, overflowY: "auto", padding: "6px 20px 24px", display: "flex", flexDirection: "column", gap: "20px" }}>
        <div>
          <div class="field-label">NAME</div>
          <input
            class="input-block"
            type="text"
            value={name}
            placeholder="Establishing Shot"
            onInput={(e) => setName((e.target as HTMLInputElement).value)}
          />
          {saveError && (
            <div style={{ color: "var(--danger)", fontSize: "12.5px", marginTop: "6px", fontWeight: 600 }}>
              {saveError}
            </div>
          )}
        </div>

        <div>
          <div class="field-label">POSITIONS — SET BY JOGGING THE CARRIAGE</div>
          <div style={{ display: "flex", flexDirection: "column", gap: "10px" }}>
            <PointCard
              label="Point A"
              value={posA}
              onJogStart={(d) => startHold(d)}
              onJogStop={stopHold}
              onSetHere={() => setPosA(currentPos)}
            />
            <PointCard
              label="Point B"
              value={posB}
              onJogStart={(d) => startHold(d)}
              onJogStop={stopHold}
              onSetHere={() => setPosB(currentPos)}
            />
          </div>

          <button
            class={`advanced-toggle press ${advancedOpen ? "open" : ""}`}
            onClick={() => setAdvancedOpen((v) => !v)}
          >
            <ChevronDown size={15} color="var(--text-dimmer)" />
            <span>Advanced: type an exact mm value</span>
          </button>
          <div class="advanced-panel" style={{ maxHeight: advancedOpen ? "140px" : "0", opacity: advancedOpen ? 1 : 0 }}>
            <div style={{ display: "flex", gap: "10px", paddingTop: "10px" }}>
              <NumberField label="A (mm)" value={posA} onChange={setPosA} />
              <NumberField label="B (mm)" value={posB} onChange={setPosB} />
            </div>
          </div>
        </div>

        <div>
          <div class="field-label">MOTION</div>
          <div class="settings-panel">
            <SettingsRow label="Speed" unit="mm/s" value={speed} step={1} min={0.1} onChange={setSpeed} />
            <SettingsRow label="Acceleration" unit="mm/s²" value={accel} step={5} min={1} onChange={setAccel} />
            <SettingsRow label="Dwell at A" unit="s" value={dwellA} step={0.5} min={0} onChange={setDwellA} />
            <SettingsRow label="Dwell at B" unit="s" value={dwellB} step={0.5} min={0} onChange={setDwellB} />
            <div class="settings-row">
              <span class="settings-row-label">Repeat</span>
              <button
                class={`toggle press ${repeat ? "on" : ""}`}
                role="switch"
                aria-checked={repeat}
                onClick={() => setRepeat((v) => !v)}
              >
                <div class="toggle-knob" />
              </button>
            </div>
          </div>
        </div>

        <button class="btn btn-ghost press" style={{ height: "52px" }} disabled={previewing} onClick={handlePreviewLeg}>
          <PlayFillIcon size={17} color="var(--text)" />
          {previewing ? "Previewing…" : "Preview This Leg"}
        </button>
      </div>
    </div>
  );
}

function PointCard({
  label,
  value,
  onJogStart,
  onJogStop,
  onSetHere,
}: {
  label: string;
  value: number;
  onJogStart: (deltaMm: number) => void;
  onJogStop: () => void;
  onSetHere: () => void;
}) {
  return (
    <div class="point-card">
      <div class="point-card-head">
        <span class="point-card-name">{label}</span>
        <span class="point-card-value tabular">{value.toFixed(1)} mm</span>
      </div>
      <div class="point-card-row">
        <button
          class="jog-btn press"
          onPointerDown={() => onJogStart(-JOG_STEP_MM)}
          onPointerUp={onJogStop}
          onPointerLeave={onJogStop}
        >
          <ChevronLeft size={18} />
        </button>
        <button
          class="jog-btn press"
          onPointerDown={() => onJogStart(JOG_STEP_MM)}
          onPointerUp={onJogStop}
          onPointerLeave={onJogStop}
        >
          <ChevronRight size={18} />
        </button>
        <button class="set-here-btn press" onClick={onSetHere}>
          Set {label.split(" ")[1]} Here
        </button>
      </div>
    </div>
  );
}

function NumberField({ label, value, onChange }: { label: string; value: number; onChange: (v: number) => void }) {
  return (
    <div style={{ flex: 1 }}>
      <div style={{ fontSize: "11px", color: "var(--text-dimmer)", marginBottom: "4px" }}>{label}</div>
      <input
        class="field-input"
        type="number"
        value={value}
        onInput={(e) => onChange(parseFloat((e.target as HTMLInputElement).value) || 0)}
      />
    </div>
  );
}

function SettingsRow({
  label,
  unit,
  value,
  step,
  min,
  onChange,
}: {
  label: string;
  unit: string;
  value: number;
  step: number;
  min: number;
  onChange: (v: number) => void;
}) {
  return (
    <div class="settings-row">
      <span class="settings-row-label">{label}</span>
      <div style={{ display: "flex", alignItems: "center", gap: "6px" }}>
        <input
          class="settings-row-value tabular"
          type="number"
          value={value}
          step={step}
          min={min}
          style={{ width: "64px", textAlign: "right", background: "transparent", border: "none" }}
          onInput={(e) => onChange(Math.max(min, parseFloat((e.target as HTMLInputElement).value) || min))}
        />
        <span class="settings-row-label" style={{ fontWeight: 600, color: "var(--text-dim)" }}>
          {unit}
        </span>
      </div>
    </div>
  );
}
