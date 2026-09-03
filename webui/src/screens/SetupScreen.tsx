import { useEffect, useState } from "preact/hooks";
import { api } from "../api";
import { status, axis, refreshAxis } from "../store";
import { CheckIcon, CloseIcon } from "../icons";

export function SetupScreen() {
  const s = status.value;
  const homed = s?.homed ?? false;
  const travelSet = s?.travel_set ?? false;
  const calibrated = s?.calibrated ?? false;
  const doneCount = [homed, travelSet, calibrated].filter(Boolean).length;

  const [travelInput, setTravelInput] = useState("");
  const [stepsInput, setStepsInput] = useState("");
  const [travelSaved, setTravelSaved] = useState(false);
  const [stepsSaved, setStepsSaved] = useState(false);
  const [homeSaved, setHomeSaved] = useState(false);

  useEffect(() => {
    void refreshAxis();
  }, []);

  const currentTravel = axis.value?.travel_mm;
  const currentSteps = axis.value?.steps_per_mm;

  // The checkmark badge alone isn't real confirmation -- it only
  // visibly changes the FIRST time you home each session; tapping
  // "Re-set Home Here" afterward (the whole point of "re-set") leaves
  // the badge already showing done, so there was nothing to actually
  // tell you it worked. Flash text the same way Travel/Steps already
  // confirm a save, regardless of whether the badge itself changes.
  async function setHome() {
    const r = await api.home();
    if (!("error" in r)) {
      flashSaved(setHomeSaved);
    }
  }

  async function saveTravel() {
    const v = parseFloat(travelInput);
    if (!v) return;
    const r = await api.patchAxis({ travel_mm: v });
    if (!("error" in r)) {
      flashSaved(setTravelSaved);
      await refreshAxis();
    }
  }
  async function saveSteps() {
    const v = parseFloat(stepsInput);
    if (!v) return;
    const r = await api.patchAxis({ steps_per_mm: v });
    if (!("error" in r)) {
      flashSaved(setStepsSaved);
      await refreshAxis();
    }
  }

  return (
    <div class="screen">
      <div class="page-header">
        <div class="page-title">Setup &amp; Calibration</div>
        <div class="page-subtitle">Once per boot, before motion unlocks.</div>
      </div>

      <div style={{ flex: 1, overflowY: "auto", padding: "16px 20px 24px", display: "flex", flexDirection: "column", gap: "14px" }}>
        <div class="progress-card">
          <div class="progress-segments">
            {[homed, travelSet, calibrated].map((done, i) => (
              <div key={i} class={`progress-segment ${done ? "done" : ""}`} />
            ))}
          </div>
          <span class="progress-text">{doneCount} of 3 steps complete</span>
        </div>

        <div class="step-card">
          <div class="step-head">
            <div class={`step-badge ${homed ? "done pop-check" : ""}`}>
              {homed ? <CheckIcon size={17} color="var(--success)" /> : <CloseIcon size={15} color="var(--text-dimmer)" />}
            </div>
            <span class="step-name">Home</span>
          </div>
          <div class="step-body">
            Zero reference set this session. Not saved across reboots — required fresh every time.
          </div>
          <button class="btn btn-ghost press" style={{ width: "100%", height: "48px" }} onClick={setHome}>
            {homeSaved ? "Home Set ✓" : homed ? "Re-set Home Here" : "Set Home Here"}
          </button>
        </div>

        <div class="step-card">
          <div class="step-head">
            <div class={`step-badge ${travelSet ? "done pop-check" : ""}`}>
              {travelSet ? <CheckIcon size={17} color="var(--success)" /> : <CloseIcon size={15} color="var(--text-dimmer)" />}
            </div>
            <span class="step-name">Travel Range</span>
          </div>
          <div class="step-body">Soft-limit distance from home. Persists across reboots.</div>
          <div class="field-row">
            <input
              class="field-input"
              type="number"
              placeholder={currentTravel ? String(currentTravel) : "500"}
              value={travelInput}
              onInput={(e) => setTravelInput((e.target as HTMLInputElement).value)}
            />
            <div class="field-unit">mm</div>
            <button class="field-save-btn press" onClick={saveTravel}>
              {travelSaved ? "Saved" : "Save"}
            </button>
          </div>
        </div>

        <div class="step-card">
          <div class="step-head">
            <div class={`step-badge ${calibrated ? "done pop-check" : ""}`}>
              {calibrated ? <CheckIcon size={17} color="var(--success)" /> : <CloseIcon size={15} color="var(--text-dimmer)" />}
            </div>
            <span class="step-name">Steps per mm</span>
          </div>
          <div class="step-body">
            {calibrated ? "Calibrated." : "Not set — motion is locked until this is calibrated."}
          </div>
          <div class="step-hint">
            Command a known move, measure the actual distance traveled, then enter steps ÷ mm. Don't guess from
            belt/pulley specs.
          </div>
          <div class="field-row">
            <input
              class="field-input"
              type="number"
              step="0.001"
              placeholder={currentSteps ? String(currentSteps) : "0.000"}
              value={stepsInput}
              onInput={(e) => setStepsInput((e.target as HTMLInputElement).value)}
            />
            <button class="field-save-btn press" onClick={saveSteps}>
              {stepsSaved ? "Saved" : "Save"}
            </button>
          </div>
        </div>

        <button
          class="btn btn-panel press"
          style={{ height: "48px" }}
          onClick={() => api.saveConfig()}
          title="Persists travel + calibration + all presets to flash"
        >
          Save Config to Flash
        </button>
      </div>
    </div>
  );
}

function flashSaved(setter: (v: boolean) => void) {
  setter(true);
  setTimeout(() => setter(false), 1600);
}
