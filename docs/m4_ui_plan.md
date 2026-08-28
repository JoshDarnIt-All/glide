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

## Open questions for Josh (not yet decided)

- **Preview-before-move:** no preview for ordinary recalls (matches
  the "one tap, confident" premise), but is a one-time "this will move
  the carriage" confirmation worth adding specifically on the very
  first move after homing each session?
- **Homing depth:** is "Set Home" a single tap trusting the user
  already jogged there, or a guided jog-then-confirm sub-flow?
- **Loop control granularity:** does v1 need real pause (not just
  stop) for a running loop, or is stop-then-restart enough?
- **Multi-tab/multi-client:** if two browser tabs/devices connect
  simultaneously, does last-command-wins silently, or does the UI need
  "someone else is controlling this" awareness? Not required for v1,
  but worth deciding before it surprises someone on set.
- **Preset organization at scale:** flat list is fine for v1
  (single axis, hand-curated names) — worth tags/search before that
  becomes a real problem, or wait until it actually is one?

## Grounding in the confirmed M3 API

This plan assumes (per `docs/api.md`): WebSocket push at ~10Hz
carrying position/velocity/phase/gates/active-preset; a unified phase
enum (a real firmware fix, not just an API translation); presets as
full REST resources matching `PresetConfig` fields exactly; recall as
always-interrupt-and-go; OTA and motion as mutually exclusive states
the UI should reflect (e.g., grey out motion controls during an
update) rather than needing its own separate interlock logic.
