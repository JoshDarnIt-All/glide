import { api } from "../api";
import { presets, isBusy, refreshPresets, status } from "../store";
import { editingPreset } from "../ui";
import { PlayFillIcon, EditIcon, DuplicateIcon, TrashIcon, PlusIcon } from "../icons";
import type { Preset } from "../types";

function fmtDwell(p: Preset) {
  return `${p.dwell_a_s}/${p.dwell_b_s}s`;
}

async function duplicate(p: Preset) {
  const existing = new Set(presets.value.map((x) => x.name.toLowerCase()));
  let candidate = `${p.name} copy`;
  let n = 2;
  while (existing.has(candidate.toLowerCase())) {
    candidate = `${p.name} copy ${n++}`;
  }
  const { name: _name, ...rest } = p;
  await api.putPreset(candidate, rest);
  await refreshPresets();
  editingPreset.value = candidate;
}

export function PresetsScreen() {
  const busy = isBusy.value;
  const ready = (status.value?.homed ?? false) && (status.value?.travel_set ?? false) && (status.value?.calibrated ?? false);

  return (
    <div class="screen">
      <div class="page-header">
        <div class="page-title">Presets</div>
        <div class="page-subtitle">Tap a card to recall and go — no confirmation, just like the button.</div>
      </div>

      <div style={{ flex: 1, overflowY: "auto", padding: "16px 20px 12px", display: "flex", flexDirection: "column", gap: "12px" }}>
        {presets.value.map((p, i) => {
          const isActivePreset = status.value?.active_preset === p.name;
          return (
            <div
              key={p.name}
              class="preset-card fade-rise"
              style={{
                "--stagger-delay": `${i * 40}ms`,
                borderColor: isActivePreset ? "var(--accent)" : "var(--border)",
              }}
            >
              {isActivePreset && (
                <div class="preset-card-active-tag">
                  <svg width="7" height="7" viewBox="0 0 10 10">
                    <circle cx="5" cy="5" r="4" fill="var(--accent)" />
                  </svg>
                  <span style={{ fontSize: "10.5px", fontWeight: 800, color: "var(--accent)", letterSpacing: "0.4px" }}>
                    ACTIVE
                  </span>
                </div>
              )}
              <div class="preset-card-name">{p.name}</div>
              <div class="preset-card-grid">
                <div>
                  <div class="preset-card-stat-label">A → B</div>
                  <div class="preset-card-stat-value">
                    {p.pos_a_mm}→{p.pos_b_mm}mm
                  </div>
                </div>
                <div>
                  <div class="preset-card-stat-label">SPEED</div>
                  <div class="preset-card-stat-value">{p.speed_mm_s}mm/s</div>
                </div>
                <div>
                  <div class="preset-card-stat-label">DWELL</div>
                  <div class="preset-card-stat-value">{fmtDwell(p)}</div>
                </div>
                <div>
                  <div class="preset-card-stat-label">REPEAT</div>
                  <div class="preset-card-stat-value">{p.repeat ? "∞" : "×1"}</div>
                </div>
              </div>
              <div class="preset-card-actions">
                <button
                  class="btn btn-accent press"
                  style={{ flex: 1, height: "46px" }}
                  disabled={!ready || busy}
                  onClick={() => void api.loadPreset(p.name)}
                >
                  <PlayFillIcon size={16} color="var(--accent-fg)" />
                  Recall
                </button>
                <button class="icon-btn press" onClick={() => (editingPreset.value = p.name)} aria-label="Edit">
                  <EditIcon size={17} color="var(--text)" />
                </button>
                <button class="icon-btn press" onClick={() => void duplicate(p)} aria-label="Duplicate">
                  <DuplicateIcon size={17} color="var(--text)" />
                </button>
                <button
                  class="icon-btn press"
                  aria-label="Delete"
                  onClick={async () => {
                    await api.deletePreset(p.name);
                    await refreshPresets();
                  }}
                >
                  <TrashIcon size={17} color="var(--danger)" />
                </button>
              </div>
            </div>
          );
        })}
      </div>

      <div style={{ flexShrink: 0, padding: "6px 20px 14px" }}>
        <button
          class="btn btn-dashed press"
          style={{ width: "100%", height: "54px" }}
          onClick={() => (editingPreset.value = "__new__")}
        >
          <PlusIcon size={18} color="var(--text-dim)" />
          New Preset
        </button>
      </div>
    </div>
  );
}
