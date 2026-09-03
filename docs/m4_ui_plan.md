# M4 Web GUI — Planning Doc

Planned by a UI/UX-focused agent working from the actual firmware and
`docs/serial_protocol.md`, cross-checked live against the M3 backend
plan (see `docs/api.md`) so this isn't designed in isolation from what
the API will actually support. This is planning depth — information
architecture and interaction flow, not final pixel-perfect mockups.
Revisit this doc when M4 implementation actually starts; it's a
starting point, not a spec frozen in time.

## Core screens

- **Control** (home screen). Live position/phase, jog, Go-to-A/B,
  Start/Stop loop, a homing gate banner when needed, and a persistent
  "active preset" chip. Everything touched during an actual shoot
  lives here — zero navigation required to do the main thing.
- **Preset Library.** Card grid: name, A/B, speed, dwell, repeat at a
  glance; tap-to-recall; edit/duplicate/delete.
- **Preset Editor.** Jog-and-set A/B (not typed coordinates — setting
  A/B is a physical act), speed/accel/dwell/repeat fields, save/name.
- **Setup & Calibration.** Home, travel, steps/mm — the one-time-per-
  boot ritual, hard-gated: nothing motion-related is reachable until
  this is satisfied.
- **Device.** WiFi, OTA, connection info — lowest-frequency screen.

Rationale: this separates three different mental modes — *doing
something right now* (Control), *preparing something for later*
(Library/Editor), and *making the hardware trustworthy at all*
(Setup) — rather than burying one-tap recall under a preset picker,
or treating the homing gate as an optional settings toggle instead of
a mandatory per-session checkpoint.

## Recalling a preset

The product's whole selling point is "one button push, smooth
transition." The interaction has to earn that:

- The **entire card is the tap target**, not a small button buried in
  a browsing card — one-handed use demands a big, unambiguous hit
  zone.
- **No confirmation dialog on tap.** A modal would contradict the
  product's own premise. Recall *is* the action (confirmed: recall
  always interrupts-and-goes, never queues or asks first — see
  `docs/api.md`).
- Immediate visible feedback: the card highlights, the phase chip
  flips to a moving state, and the position indicator **animates
  smoothly toward the new target** in real time via the WebSocket
  position stream — never a jump-cut, because the real hardware move
  isn't one either.
- If recalled mid-move, the indicator should visibly
  decelerate-and-redirect through the transition (a fresh S-curve
  always starts at rest) — never implying the carriage teleported or
  instantly reversed.
- A persistent "active preset" chip stays visible on Control so a
  distracted operator can glance back and confirm state. Clears the
  instant a manual jog/parameter change happens post-load (matches
  the API's `active_preset` semantics).

## Safety/trust state representation

- **Not homed:** a persistent, un-dismissable banner disabling every
  motion control (jog, move, recall, loop start) — visually disabled,
  not just functionally blocked, so there's no tap-and-get-an-error
  loop. Framed as an expected per-session checklist step, not a fault.
- **Motion phase:** a single, always-visible phase chip driven only by
  the WebSocket phase enum (`IDLE, MOVING, MOVING_TO_A, MOVING_TO_B,
  DWELLING_AT_A, DWELLING_AT_B, STOPPED`) — never inferred from
  position, since dwelling and stopped-between-moves look identical
  positionally. Dwelling gets a visually distinct "paused on purpose"
  treatment so it doesn't read as stuck.
- **Soft-limit clamp:** a one-shot, non-sticky, non-alarming toast —
  it's the system working as designed, not a malfunction.
- **Connection lost:** full-surface takeover (not a corner icon) —
  every control disables, last-known values stay visible but visibly
  desaturated/frozen, with a "reconnecting..." state. Trigger after
  ~2 missed heartbeats (~10-15s), not the first miss, to avoid
  flapping on a normal brief WiFi hiccup. A live control surface that
  silently goes stale while looking normal is the worst failure mode
  for a device with no brake.
- **Position honesty:** labeled "Target," not "Position" — this is an
  open-loop system with no encoder, so the number is what was *asked
  for*, not confirmed telemetry. Small wording choice, sets correct
  expectations.

## Information hierarchy (phone/tablet, one-handed, outdoors)

Closer to an action-camera app than a dashboard: high contrast, large
type, generous touch targets (44pt minimum, 60-72pt for primary
actions). Stop is fixed chrome in the same screen position always,
sized for a thumb, never nested in a menu. Every status signal pairs
color with an icon or word — sunlight/glare wash out color-only cues,
and this matters for colorblind accessibility too. Layout priority
top to bottom: phase/position status → jog/Go-to/Stop (dominant
panel) → preset shortcuts → Settings behind an explicit tap, never a
swipe gesture that could trigger by accident while handling the rig.
Numeric fine-tuning (exact mm values, acceleration curves) lives in
Setup/Editor, not Control — a live shoot needs big unambiguous
buttons, not precision entry.

## Preset creation/editing

Setting A/B is a physical act (jogging the real carriage), so the
editor shouldn't let you type a fictional position as the primary
path: jog controls (small increments + press-and-hold) move the real
carriage live, "Set A here"/"Set B here" buttons capture current
commanded position. Typing raw mm values is an advanced/optional
override, not the primary flow — most users are standing there
watching the actual carriage. Speed/accel/dwell/repeat are ordinary
numeric fields (not physical-position facts). A "preview this leg"
test-move button lets you check a leg before committing without
saving. Saving requires a name, warns before overwriting an existing
one (case-insensitive, matching firmware behavior), and is a
deliberate action — no autosave-on-slider-tick, matching the
firmware's explicit-save philosophy (`SAVEPRESET`/`SAVECONFIG` are
intentional actions, not automatic, partly due to flash write
endurance). Duplicate-then-tweak from the Library is a first-class
entry point, not just "new from scratch" — the realistic workflow on
set.

## Decisions locked for M4 (confirmed with Josh, not picked silently)

- **Preview-before-move:** no confirmation, ever — including the first
  move after homing. Consistent with the product's core premise
  elsewhere (preset recall never confirms either); the physical e-stop
  remains the real safety boundary, not a modal.
- **Homing depth:** single tap, matching the existing serial `SETHOME`
  behavior exactly (marks current position as 0mm, no sub-flow) — jog
  controls are already visible on the same screen if repositioning
  first is needed. **A guided jog-then-confirm sub-flow is explicitly
  pinned as a v2 candidate**, not rejected outright.
- **Loop control granularity:** stop-then-restart only for v1, matching
  today's firmware exactly — `LoopRunner` has no pause concept, only
  stop (see `firmware/lib/motion/loop_runner.h`). **Real pause/resume
  is pinned as a v2 candidate** — it's a genuine new firmware
  capability (resume from mid-leg? mid-dwell?), not just a UI change,
  so it would expand a future milestone into firmware work too.
- **Multi-tab/multi-client:** not addressed in v1 — last-command-wins
  silently, per this doc's own original fallback suggestion. Revisit
  if it actually surprises someone on set.
- **Preset organization at scale:** flat list for v1 (single axis,
  hand-curated names), per this doc's own original fallback
  suggestion. Add tags/search only once it's a real problem, not
  preemptively.

## Technical architecture (confirmed with Josh, not picked silently)

Researched and proposed by a specialist agent working from the actual
repo state (real M3 build size, real partition table) — see the
process note in project memory. Every real fork here was confirmed
with Josh via explicit choice before any app code was written, same
process as every other milestone.

- **Asset delivery: embedded in the firmware app partition**, not the
  LittleFS partition config.json/presets live on. Verified against
  Josh's actual M3 build: the app partition is 1.875MB (`0x1E0000`,
  per `min_spiffs.csv`), the M3 firmware (WiFi+REST+WebSocket+OTA+
  motion, no UI) uses 1.35MB of it — leaving **~598KB of headroom**
  for the built web UI. The LittleFS partition, by contrast, is only
  128KB (`0x20000` — an earlier doc comment said ~190KB, which was
  wrong and has been corrected in `firmware/platformio.ini`), and
  storing the UI there would need an entirely separate OTA mechanism
  built just to update it independently (the existing `/api/v1/ota`
  only targets the app partition). Mechanism: a PlatformIO
  `extra_scripts` build hook gzips the Vite build output and generates
  `PROGMEM` byte arrays + a path→handler manifest automatically on
  every build — no separate manual step. For local development, run
  Vite's own dev server with `server.proxy` pointing `/api` and `/ws`
  at the device's real IP, rather than developing against the embedded
  build.
- **Framework: Preact**, not React, vanilla JS, or Svelte. Same
  component/hooks/JSX API as React (nothing unfamiliar to relearn) at
  ~3-4KB gzipped versus React's ~130KB — meaningful against the ~598KB
  budget above, especially since that budget also needs to leave room
  for firmware to keep growing across future milestones. Target: well
  under ~350-400KB total gzipped (JS+CSS+HTML+assets) for real margin.
  This also constrains the visual design directly — hand-rolled/
  minimal CSS rather than a full design-system dependency, inline SVG
  for icons (not an icon font/library), no custom webfonts unless a
  single small self-hosted variable-weight file. The published design
  mockup already follows this (system fonts only, by design).
- **State management:** a single native WebSocket connection (no
  library) feeding a Preact Context/reducer all 5 screens read from a
  shared hook. `status` frames merge into state; `event` frames
  trigger one-shot UI reactions (toasts, phase-chip flips);
  `heartbeat` only resets a liveness timer. Connection considered lost
  after ~12-15s since the last message of any kind (matches this
  doc's "~2 missed heartbeats" spec), triggering the full-surface
  takeover above, with auto-reconnect on close. Mutating actions
  (move/jog/preset CRUD/etc.) go over REST via `fetch()`; the
  resulting state change is read back from the next WebSocket `status`
  push, not synthesized from the REST response itself. No external
  state-management library — overkill at this scope (5 screens, one
  live-data stream).
- **New tooling dependency:** Node.js/npm, for `webui/`'s own Vite/
  Preact build — separate from the PlatformIO/C++ toolchain already
  in use.

## Visual design

Published as an Artifact (see project memory for the current link) —
a dark, high-contrast "field tool" aesthetic per this doc's own tone
reference above ("closer to an action-camera app than a dashboard"):
near-black surfaces for outdoor glare, a single amber accent, system
fonts only (both a design choice and a flash-budget constraint above),
tabular numerals on the live position readout. Covers all 5 screens
plus every state this doc specifies: all 7 phase values with a real
visual distinction between moving/dwelling/stopped (not just a chip
color change), the not-homed banner, the soft-limit clamp toast, the
persistent active-preset chip, and a dedicated full-screen
"connection lost" takeover (a structurally different frame, not a
smaller variant of the normal screen). Static mockup, not an
interactive prototype — the environment that built it lacked the JS
runtime Claude Design's live canvas needs to seed, so this is a
one-time limitation of that build, not a statement about the final
app's interactivity.

## Grounding in the confirmed M3 API

This plan assumes (per `docs/api.md`): WebSocket push at ~10Hz
carrying position/velocity/phase/gates/active-preset; a unified phase
enum (a real firmware fix, not just an API translation); presets as
full REST resources matching `PresetConfig` fields exactly; recall as
always-interrupt-and-go; OTA and motion as mutually exclusive states
the UI should reflect (e.g., grey out motion controls during an
update) rather than needing its own separate interlock logic.
