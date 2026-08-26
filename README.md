# Glide

Open-source ESP32 motion controller for camera sliders.

Glide drives a stepper-motor camera slider: smooth moves between two
points (A and B), configurable speed and dwell, saved as named presets
("loops") you can trigger and repeat. It's built around real hardware —
a repurposed 48" rail with a dead OEM controller — with the firmware,
web UI, and driver electronics all open source and built from scratch.

## Intended use

- **Horizontal orientation only.** The rail is mounted level, moving a
  camera carriage left-right (or front-back) along a straight track.
- **No brake.** The motor holds position by being powered and holding
  torque only. If power is cut, the carriage is free to move under
  gravity or momentum — see Safety below.
- **15 lb (6.8 kg) payload target for v1.** This is a *target*, not a
  guarantee. The rail itself may be the real limiting factor — see
  `hardware/gvm-48-inspection.md`, which records the donor rail's
  published payload rating. If that rating is below 15 lb, the rail
  is the constraint, not the motor or driver.
- Not rated, tested, or intended for vertical/inclined use, overhead
  mounting, or any load-bearing use beyond driving the camera carriage
  itself.

## v1 scope

- Slide-only motion: one axis, point A to point B.
- Named "loops": a preset bundling A position, B position, speed, and
  dwell time at each end. Save, recall, and run loops (including
  repeating A→B→A cycles).
- Travel length is user-configurable at setup time — never hard-coded
  in firmware. Every rail this runs on is a bit different.
- WiFi provisioning, REST + WebSocket control, OTA firmware updates.
- Web UI served from the ESP32 (gzipped static build).
- Companion module for use alongside Bitfocus Companion / stream decks,
  built on the same REST API (separate repo, see below).
- Optional physical controls (buttons/knob) — stretch goal, not
  required for v1.0.

### Explicit non-scope (v1)

- **Multi-axis / pan-tilt.** The config schema stores axes as an array
  so this is possible later without a schema rewrite, but v1 firmware
  and UI only drive one axis.
- **Keyframe-based motion** (arbitrary paths, more than two points,
  variable easing per segment). v1 is loop-based (A/B/speed/dwell)
  only; keyframes are a v2 feature.
- **Vertical or overhead use**, and anything that relies on the motor
  or a brake to hold position against gravity when unpowered — there
  is no brake in this design.
- **Any load-bearing claim beyond the 15 lb horizontal target.**

## Hardware

| Component | Choice | Notes |
|---|---|---|
| MCU | ESP32 (classic, `esp32dev`) | Arduino framework via PlatformIO |
| Motor driver | TMC2209, socketed | Replaceable — if a driver whines or fails, it's a $6 swap, not a rework. **Pending confirmation**: see below. |
| Step generation | FastAccelStepper | Generates step pulses in hardware timers, so motion timing doesn't jitter with WiFi/network activity |
| Rail | Repurposed GVM 48" slider | Dead OEM controller, motor confirmed good — see `hardware/gvm-48-inspection.md` for the full inspection checklist |
| E-stop | Physical switch, cuts power | Coasts, does not brake — see Safety |

**Driver status:** TMC2209 is the intended driver, but it is not yet
confirmed as correct for this motor. The donor motor's rated phase
current has not yet been measured/recorded (see
`hardware/gvm-48-inspection.md`). A TMC2209 module tops out around
2.0 A RMS / ~2.8 A peak per coil; if the motor's rated current exceeds
that with headroom to spare, TMC2209 may not be the right driver. No
firmware or driver-specific wiring work happens until that number is
in hand.

Full wiring diagrams and BOM will live in `hardware/` as the design
firms up.

## Safety

- **Pinch points.** The carriage moves along the rail under motor
  power. Keep fingers, cables, and loose clothing clear of the
  carriage path and any belt/pulley or leadscrew mechanism during
  operation and testing.
- **Physical e-stop cuts power.** The e-stop is a physical switch in
  the power path to the motor driver — not a software/firmware stop.
  It works even if the firmware has crashed, hung, or is mid-update.
- **The e-stop coasts; it does not brake.** Cutting power removes
  holding torque immediately. On a level horizontal rail this
  generally means the carriage simply stops moving under its own
  rolling friction — it does **not** mean the carriage is locked in
  place. Do not rely on the e-stop (or any powered-off state) to hold
  position against an external push, an incline, or momentum from a
  fast move. This is a direct consequence of the no-brake design
  choice above, and it applies at every milestone, not just before
  the e-stop is wired in.

## Milestones

Each milestone is a gate: don't start the next one until the current
one actually works, not just "mostly works."

- **M0 — Bench rig.** Motor turns 10 revolutions, stops, returns.
  Proves the driver, wiring, and basic step/dir control work at all,
  off the rail, before anything else is built on top of it.
- **M1 — Motion core.** Homing, soft limits, positions in millimeters
  (not raw steps), ease in/out, A-B-A loop, controlled over serial
  only — no WiFi, no web UI yet. This is the load-bearing milestone;
  see the rule below.
- **M2 — State machine + JSON config.** Persistent config with a
  `schema_version` field from day one, so future schema changes (e.g.
  multi-axis) can be migrated instead of breaking existing configs.
- **M3 — WiFi provisioning, REST, WebSocket, OTA.** Also: generic-HTTP
  Companion presets, so Companion integration is possible via plain
  HTTP requests even before the dedicated Companion module exists.
- **M4 — Web GUI.** Built against the API contract in `docs/api.md`,
  which is written before this milestone starts.
- **M5 — Companion module.** Lives in a separate repo,
  [`companion-module-glide`](https://github.com/JoshDarnIt-All/companion-module-glide),
  also built against `docs/api.md`.
- **M6 — Physical controls.** Optional. Buttons/knob for local control
  without the web UI or Companion.
- **M7 — v1.0 release.**

> **Rule: if the motion isn't beautiful at M1, no web UI fixes it.**
> M1 is serial-only, no UI at all — that's deliberate. A web interface
> can make an ugly move *convenient to trigger*, but it can't make the
> move itself smoother. If the ease-in/out, the A-B-A loop, or the
> homing/soft-limit behavior feels wrong at M1, that's a motion-core
> problem, and it gets fixed in `firmware/lib/motion` before moving on
> — not papered over with a nicer front end later. Do not advance past
> M1 on an ugly move.

## Repo layout

```
firmware/          PlatformIO project (ESP32, Arduino framework)
  platformio.ini
  src/              application code — setup/loop, wiring the pieces together
  lib/motion/       ramp + loop math only — no Arduino or WiFi deps,
                    so it can be unit-tested on the laptop, not just on-device
  lib/api/          REST/WebSocket handlers, once M3 starts
  test/             unit tests for lib/motion (and later lib/api)
webui/              Vite build; output is gzipped and flashed alongside firmware
hardware/           wiring diagrams, BOM, CAD, and the donor-rail inspection doc
docs/api.md         the API contract — written before the web UI or the
                    Companion module consume it
.github/workflows/  CI
```

The Companion module is **not** in this repo — it lives in
[`companion-module-glide`](https://github.com/JoshDarnIt-All/companion-module-glide)
so it can follow Companion's own module conventions and release
process independently of firmware releases.

## License

MIT — see [LICENSE](LICENSE).
