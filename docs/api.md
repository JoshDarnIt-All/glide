# Glide API — placeholder

This file is intentionally empty of content right now. It exists to hold
the spot in the repo layout and to state the rule for when it gets written.

## Why this comes before the web UI or the Companion module

The web UI (M4) and the Companion module (companion-module-glide, M5) are
both *clients* of this device. If either one gets built first, the API
ends up shaped by whatever that client's framework makes convenient
(Vite's dev proxy quirks, Companion's action/feedback model, whatever),
instead of by what the motion controller actually needs to expose. That
produces an API with leftover assumptions baked in from its first
consumer, and it constrains you to redesign a client whenever the API
needs to change.

Writing the contract first — endpoints, WebSocket messages, JSON config
shape, `schema_version` handling — means both clients are built against
a stable target, and the API isn't secretly "whatever the web UI's fetch
calls happen to send today."

## When this gets written

Between M3 and M4/M5, once WiFi provisioning, REST, WebSocket, OTA, and
the generic-HTTP Companion presets exist in firmware (end of M3) and
before the web GUI (M4) or Companion module (M5) work starts.

At that point this file should define, at minimum:

- REST endpoints (config read/write, status, OTA trigger, loop presets)
- WebSocket message shapes (live position/status push, command ack)
- The JSON config schema, including `schema_version` and what a version
  bump obligates (migration? rejection? both?)
- Error response shape and status codes
- Auth/provisioning touchpoints, if any, at the API layer

Until then: no web UI work, no Companion module work. Firmware motion
core (M0–M2) comes first regardless.
