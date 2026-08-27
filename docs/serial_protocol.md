# M1/M2 Serial Command Protocol

This is the bench/dev control interface for M1 and M2 — typed
commands over the USB serial connection (115200 baud, see
`firmware/platformio.ini`). It is **not** the REST/WebSocket API —
that's `docs/api.md`, written separately before the web UI (M4) or
Companion module (M5). This protocol only needs to exist through
M1/M2; once M3 adds WiFi and a real API, this stays as a useful debug
interface but stops being the primary way to control the device.

## Design decisions

- **Verbose English commands** (`SETHOME`, `MOVETO 150`, `LOOPSTART`)
  over terse GRBL-style symbol codes (`?`, `!`, `$`). Chosen for M1
  specifically because it's easier to work with typing into a serial
  monitor by hand, as a first-time programmer, without memorizing a
  symbol table — GRBL-style is genuine, respected prior art for this
  exact problem (serial control of stepper motion), but the lookup-
  table cost wasn't worth it here.
- **Manual/soft homing**, not switch-based auto-homing. `SETHOME`
  marks wherever the carriage currently is as 0mm. Chosen because the
  rail's end-stop hardware (type, wiring) was — and still is — an
  unconfirmed item on `hardware/gvm-48-inspection.md`. Switch-based
  homing can be added later without changing anything above this
  layer: soft limits and loops don't care how "home" was established,
  only that a 0mm reference exists.
  - A related idea (sensorless homing via the TMC2209's StallGuard —
    driving to a hard stop and detecting the resulting load spike) was
    discussed and explicitly deferred: it requires UART communication
    with the driver to read the StallGuard register, which the
    standalone STEP/DIR/EN wiring chosen at M0 doesn't provide.
    Revisiting it means revisiting that wiring decision, not just
    adding a command.
- **`SETSTEPSPERMM` as a runtime-configured gate**, not a firmware
  constant. mm↔step conversion depends on the physical drivetrain
  (belt pitch × pulley teeth, or leadscrew lead, plus any gearbox
  reduction) — none of which were known at implementation time, and
  guessing a specific belt/pulley spec would have been an unfounded
  assumption of exactly the kind avoided for phase current and
  microstepping elsewhere in this project. Determined by measurement
  instead — see `hardware/pinout.md`.
- **Named multi-preset storage was deferred to M2** (this doc
  originally said "out of scope for M1" — M2 has now arrived and this
  is built: `SAVEPRESET`/`LOADPRESET`/`LISTPRESETS`/`DELETEPRESET`,
  persisted to `/config.json` on LittleFS via `firmware/lib/config`).
  This was deliberate: M2 was already scoped as "state machine + JSON
  config with `schema_version`," which is exactly what persistent
  named presets need (multiple named configs, saved to flash, loadable
  by name, versioned for future migration) — building ad hoc storage
  into M1 would have meant solving persistence and naming twice.
- **What persists across a reboot and what doesn't** (locked in with
  Josh during M2 planning): axis config (`SETTRAVEL`, `SETSTEPSPERMM`)
  and all presets persist automatically. **Home does NOT persist** —
  `SETHOME` is required fresh every boot regardless of what's in the
  saved file. Reasoning: travel range and steps-per-mm are fixed
  physical facts about the hardware (they don't become wrong just
  because the power cycled), but home is a live reference tied to
  wherever the carriage physically was at the moment `SETHOME` was
  called — on this open-loop, no-encoder, no-brake system, there's no
  way to confirm a saved zero is still physically true after a power
  cycle. Trusting it blindly risks a preset recall confidently driving
  the carriage into a hard stop.
  - A related idea was raised and declined for the same underlying
    reason: physical limit switches (or TMC2209 StallGuard-based
    sensorless homing) would let the firmware safely auto-establish
    home on every boot with no human involvement, avoiding the
    re-home-every-session friction entirely. Explicitly not pursued
    for M2 — Josh doesn't want a physical-switch requirement. Worth
    revisiting later if that friction becomes a real problem in
    practice.
  - Saving is **explicit**, not automatic on every `SET*` command:
    `SAVECONFIG` persists axis config, `SAVEPRESET`/`DELETEPRESET`
    persist immediately (presets are meant to be durable the instant
    you act on them). Flash has a finite write-cycle lifespan, and
    `SETSPEED`/`SETACCEL`/etc. change often during live tuning —
    auto-saving every one of those would wear flash for no benefit.
  - `LOADPRESET <name>` doesn't just load values — it immediately
    starts moving toward the preset (cancelling whatever's currently
    happening first), matching the "push a button, slider transitions
    there" workflow this is ultimately being built for (Bitfocus
    Companion + a web GUI, in M3+). Since `buildSCurveProfile` always
    computes fresh from wherever the carriage currently is, this is a
    genuinely smooth transition, not a jump — though since a fresh
    S-curve always begins at rest, interrupting an in-flight move to
    recall a different preset means a clean decelerate-then-ease-in,
    not a mid-flight redirect at the same velocity.
- **Soft limits are signed** (`SETTRAVEL` accepts negative values).
  Home isn't required to be the minimum-position end of the rail — it's
  just wherever `SETHOME` was called, which depends on where the
  carriage happened to be at the time. `SETTRAVEL -500` means "rail
  extends 500mm negative from home," matching `SETTRAVEL 500` meaning
  positive. See `firmware/lib/motion/soft_limits.h` for the exact
  range logic.

## Response format

- `OK` — command succeeded, no data.
- `OK <data>` — command succeeded, e.g. `STATUS`'s one-line dump.
- `ERR <reason>` — command failed, e.g. `ERR NOT_HOMED`.
- `# <message>` — an asynchronous event, not a direct reply to
  whatever you just typed (e.g. a loop phase changing, or a move
  target getting clamped to a soft limit). The `#` prefix keeps these
  visually distinct from command responses when watching the serial
  monitor.

## Readiness gates

Three independent conditions must all be true before `MOVETO`, `JOG`,
or `LOOPSTART` will do anything — each fails with a specific `ERR` so
it's clear which one is missing:

| Gate | Set by | Error if missing |
|---|---|---|
| Homed | `SETHOME` | `ERR NOT_HOMED` |
| Travel range set | `SETTRAVEL <mm>` | `ERR TRAVEL_NOT_SET` |
| Calibrated | `SETSTEPSPERMM <value>` | `ERR NOT_CALIBRATED` |

Reasoning: at boot, position 0 is meaningless until home is set, the
travel range is meaningless until it's configured (defaulting to 0
would silently clamp every move to a single point), and any mm value
is meaningless until the mm↔step conversion is known. Rather than let
any of these silently default to something plausible-looking but
wrong, each is a hard gate.

## Commands

### Config / setup

| Command | Effect |
|---|---|
| `SETHOME` | Marks current physical position as 0mm |
| `SETTRAVEL <mm>` | Soft-limit travel range, either direction from home (see above); rejects exactly 0 |
| `SETSTEPSPERMM <value>` | mm↔step calibration ratio — see `hardware/pinout.md` for how to measure it |
| `SETA` / `SETA <mm>` | Marks current position as A, or an explicit value |
| `SETB` / `SETB <mm>` | Same, for B |
| `SETSPEED <mm/s>` | Peak speed for `MOVETO`/`JOG`/loop legs |
| `SETACCEL <mm/s²>` | Acceleration ceiling |
| `SETDWELL A <s>` / `SETDWELL B <s>` | Dwell time at each end |
| `SETREPEAT ON` / `OFF` | Loop forever vs. one A-B-A cycle then stop |

### Motion

| Command | Effect |
|---|---|
| `MOVETO <mm>` | Absolute move (clamped to soft limits) |
| `JOG <mm>` | Relative move, +/- |
| `STOP` | Gracefully halts any move or loop in progress (also the way to stop a running loop — there's no separate `LOOPSTOP`) |

### Loop

| Command | Effect |
|---|---|
| `LOOPSTART` | Begins A-B-A cycling with current A/B/speed/accel/dwell/repeat config. Errors `ERR A_AND_B_REQUIRED` if either isn't set, `ERR ALREADY_RUNNING` if a loop is already active |

### Persistence (M2)

| Command | Effect |
|---|---|
| `SAVECONFIG` | Persists current travel/calibration + all presets to `/config.json` |
| `SAVEPRESET <name>` | Saves current A/B/speed/accel/dwell/repeat as a named preset (overwrites if the name exists); persists immediately |
| `LOADPRESET <name>` | Recalls a preset and immediately starts moving to it (see design notes above). Errors `ERR NOT_FOUND` if the name doesn't exist |
| `LISTPRESETS` | Lists all saved presets with their values (`OK NONE` if none saved) |
| `DELETEPRESET <name>` | Removes a saved preset; persists immediately. Errors `ERR NOT_FOUND` if the name doesn't exist |

### Query

| Command | Effect |
|---|---|
| `STATUS` | One-line dump: position, phase, homed/travel/calibration state, A/B/speed/accel/dwell/repeat, preset count |
| `HELP` | Lists all commands (also printed by the firmware itself — this doc and the firmware's `printHelp()` should be kept in sync) |

## What persists across a reboot

See the "What persists" design note above for the full reasoning.
Short version: axis config (travel, steps-per-mm) and all presets
survive a power cycle or re-flash, auto-loading from `/config.json` on
boot. **Home does not** — `SETHOME` is always required fresh, every
boot, no matter what's saved.
