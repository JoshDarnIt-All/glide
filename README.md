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
| Motor driver | TMC2209, socketed | Replaceable — if a driver whines or fails, it's a $6 swap, not a rework. |
| Step generation | FastAccelStepper | Generates step pulses in hardware timers, so motion timing doesn't jitter with WiFi/network activity |
| Rail | Repurposed GVM 48" slider | Dead OEM controller, motor confirmed good — see `hardware/gvm-48-inspection.md` for the full inspection checklist |
| E-stop | Physical switch, cuts power | Coasts, does not brake — see Safety |

**Driver status:** TMC2209 is confirmed as the driver. The donor motor
is a standard NEMA 17 bipolar stepper (1.8°, 200 steps/rev, 4-lead);
its rated phase current was not individually measured, but is assumed
to fall in the standard NEMA 17 range of 1.2–1.7 A/phase, which sits
comfortably within the TMC2209's ~2.0 A RMS / ~2.8 A peak-per-coil
headroom. See `hardware/gvm-48-inspection.md` for the full reasoning
and what would prompt revisiting this (running hot, skipped steps, or
a later direct measurement outside that range).

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
- **M1 — Motion core.** Homing (manual/soft — `SETHOME` marks the
  current position as 0mm; switch-based auto-homing deferred until
  the rail's end-stop hardware is confirmed), soft limits, positions
  in millimeters (not raw steps), S-curve ease in/out, A-B-A loop,
  controlled over serial only — no WiFi, no web UI yet. This is the
  load-bearing milestone; see the rule below. Full command reference:
  `docs/serial_protocol.md`.
- **M2 — State machine + JSON config.** Persistent config
  (`/config.json` on LittleFS) with a `schema_version` field from day
  one, so future schema changes (e.g. multi-axis) can be migrated
  instead of breaking existing configs. Axis config (travel,
  steps-per-mm) and named multi-presets persist across a reboot; home
  deliberately does not — see `docs/serial_protocol.md` for the full
  reasoning. Presets support recall-and-go (`LOADPRESET`), the
  building block for "push a button, slider transitions there"
  control from Companion/a web GUI in later milestones.
- **M3 — WiFi provisioning, REST, WebSocket, OTA.** Also: generic-HTTP
  Companion presets, so Companion integration is possible via plain
  HTTP requests even before the dedicated Companion module exists.
  WiFi provisioning, REST (reads and writes), and WebSocket are
  confirmed working on real hardware; OTA is written but not yet
  hardware-tested (see `docs/api.md`'s bring-up checklist for status
  and the real bugs bring-up caught). WiFiManager for provisioning,
  ESPAsyncWebServer to avoid jittering the motion control tick, and a
  mutual-exclusion safety interlock between OTA and motion. REST
  handlers run synchronously on AsyncTCP's own task and respond
  immediately, guarded by a mutex against `loop()`'s own motion-state
  access — an initial design that deferred each handler's work into a
  queue drained from `loop()` didn't survive contact with
  ESPAsyncWebServer's actual requirements.
- **M4 — Web GUI.** Built against the API contract in `docs/api.md`,
  which is written before this milestone starts. Initial planning
  (screens, safety-state representation, preset recall interaction) is
  in `docs/m4_ui_plan.md`.
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
  include/          project-wide headers — secrets.h.example (copy to
                    secrets.h, gitignored, for a real OTA key / bench WiFi)
  src/              application code — setup/loop, wiring the pieces together
  lib/motion/       ramp + loop math only — no Arduino or WiFi deps,
                    so it can be unit-tested on the laptop, not just on-device
  lib/config/       config.json persistence (LittleFS + ArduinoJson) —
                    axis config + named presets, schema_version'd
  lib/api/          REST/WebSocket/OTA plumbing (M3): wifi_setup,
                    ota_handler — see firmware/src/main.cpp for the
                    actual route handlers (synchronous, guarded by a
                    mutex against loop()'s own motion-state access)
  test/             unit tests for lib/motion (and later lib/api)
webui/              Vite build; output is gzipped and flashed alongside firmware
hardware/           wiring diagrams, BOM, CAD, and the donor-rail inspection doc
docs/api.md         the REST/WebSocket API contract (M3) — written
                    before the web UI or the Companion module consume it
docs/serial_protocol.md  M1/M2's bench/dev serial command reference —
                    the debug interface, not the REST/WebSocket API
docs/m4_ui_plan.md  M4 web GUI planning — screens, interaction flow,
                    safety-state representation
.github/workflows/  CI
```

The Companion module is **not** in this repo — it lives in
[`companion-module-glide`](https://github.com/JoshDarnIt-All/companion-module-glide)
so it can follow Companion's own module conventions and release
process independently of firmware releases.

## License

MIT — see [LICENSE](LICENSE).
