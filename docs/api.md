# Glide API — REST + WebSocket Contract (M3)

This is the network API a web UI (M4) or the Companion module
(`companion-module-glide`, M5) is built against. It supersedes
`docs/serial_protocol.md` as the primary control surface once M3
ships WiFi — the serial protocol remains as a useful bench/debug
interface, but this file is the real contract for anything over the
network.

## Why this was written before the web UI or Companion module

The web UI and the Companion module are both *clients* of this
device. If either got built first, the API would end up shaped by
whatever that client's framework made convenient, instead of by what
the motion controller actually needs to expose — producing an API
with leftover assumptions baked in from its first consumer. Writing
the contract first means both clients are built against a stable
target.

This contract was planned via two specialist agents (an ESP32
backend/embedded engineer and a UI/UX designer) working from the
existing firmware and serial protocol, cross-checking their
assumptions against each other, with every real architectural fork
surfaced to and decided by Josh rather than picked silently — the
same process used for every prior milestone's decisions.

## Architecture (why the endpoints below are shaped this way)

- **Server:** `ESP32Async/ESPAsyncWebServer` + `ESP32Async/AsyncTCP`
  (pinned in `firmware/platformio.ini`) — the actively-maintained
  continuation of the original `me-no-dev` repo, which is stale.
  Chosen because the synchronous built-in Arduino `WebServer` blocks
  `loop()` per-request, which would
  directly jitter `controlTick()`'s 20ms S-curve velocity-following —
  precisely the stutter M1 was built to eliminate. This is a hard
  constraint, not a style preference.
- **Command dispatch:** HTTP handlers run *synchronously*, on
  AsyncTCP's own task, and reuse the exact `handleCommand()` dispatch
  logic serial already uses (via `runCommandForApi()`) — REST becomes
  "just another input source," not a second control path with its own
  bugs. **Revised from the original design**, which deferred each
  handler's actual work into a queue drained from `loop()`, specifically
  to avoid touching motion globals from AsyncTCP's task at all — that
  broke on real hardware, because ESPAsyncWebServer requires
  `request->send()` to be called synchronously, before the handler
  function returns (confirmed via its own "Handler did not handle the
  request" fallback, hit on every route). Thread safety against
  `loop()`'s own motion-state access (serial dispatch, `controlTick()`)
  now comes from a mutex (`g_motionMutex`/`MotionLock` in
  `firmware/src/main.cpp`) held for the duration of each handler's
  actual state access, not from deferring the work to another time.
- **WiFi provisioning:** `WiFiManager` — device tries saved
  credentials, falls back to its own AP + captive portal if none
  work, saves what you enter, reboots connected. WiFi credentials live
  in ESP32 NVS (`Preferences`), **not** in `config.json` — they aren't
  part of the versioned device-config schema and shouldn't be
  wiped/migrated alongside it. `secrets.h` (already gitignored) can
  still short-circuit straight to a hardcoded network for bench work.
- **mDNS:** `glide.local` via `ESPmDNS` (bundled with the ESP32
  Arduino core), so Companion buttons and bookmarks survive a DHCP
  lease change without needing a hardcoded IP.

## Auth

No authentication on the general API — same trust model as USB serial
today (anyone on the LAN can control it, same as anyone with a USB
cable could). The physical e-stop remains the actual safety boundary,
not network access control.

**Exception: `POST /api/v1/ota` requires an `X-Glide-OTA-Key` header**
matching a key configured on the device. This is the one action that
can brick or misconfigure the device if triggered by the wrong
client, so it gets its own guard even though nothing else does.

## Error format

```json
{ "error": "REASON" }
```

Reuses the existing serial `ERR` vocabulary (see
`docs/serial_protocol.md`) almost verbatim, with matching HTTP status:

| `error` | HTTP status | Meaning |
|---|---|---|
| `NOT_HOMED` | 409 | `POST /api/v1/home` hasn't been called this session — does **not** apply to `POST /jog`, which only needs travel range + calibration (see `docs/serial_protocol.md`'s readiness gates) |
| `TRAVEL_NOT_SET` | 409 | Travel range not configured |
| `NOT_CALIBRATED` | 409 | Steps-per-mm not configured |
| `ALREADY_RUNNING` | 409 | Loop already active |
| `MOVING` | 409 | A move/loop is active and this action requires idle first |
| `OTA_IN_PROGRESS` | 409 | An OTA is in flight; motion commands rejected |
| `NOT_FOUND` | 404 | Named preset doesn't exist |
| `INVALID_VALUE` | 400 | Bad/out-of-range input |
| `UNAUTHORIZED` | 401 | Missing/incorrect `X-Glide-OTA-Key` on `/ota` |

## REST endpoints

Base path: `/api/v1`. JSON request/response bodies. Field names match
`PresetConfig`/`AxisConfig` in `firmware/lib/config/config_store.h`
directly — no translation layer.

| Method + path | Serial equivalent | Notes |
|---|---|---|
| `POST /home` | `SETHOME` | Marks current position as 0mm |
| `GET /axis` | (part of `STATUS`) | `{travel_mm, steps_per_mm}` |
| `PATCH /axis` | `SETTRAVEL`, `SETSTEPSPERMM` | Partial update, either/both fields |
| `GET /loop-config` | (part of `STATUS`) | `{pos_a_mm, pos_b_mm, speed_mm_s, accel_mm_s2, dwell_a_s, dwell_b_s, repeat}` |
| `PATCH /loop-config` | `SETSPEED`/`SETACCEL`/`SETDWELL`/`SETREPEAT`/explicit-value `SETA`/`SETB` | Partial update, any subset of fields |
| `POST /loop-config/mark-a` | `SETA` (no arg) | Sets `pos_a_mm` to current position |
| `POST /loop-config/mark-b` | `SETB` (no arg) | Sets `pos_b_mm` to current position |
| `POST /move` `{"pos_mm": 150}` | `MOVETO` | Absolute move, clamped to soft limits |
| `POST /jog` `{"delta_mm": -10}` | `JOG` | Relative move |
| `POST /stop` | `STOP` | Halts any move or loop |
| `POST /loop/start` | `LOOPSTART` | Begins A-B-A loop using current `loop-config` |
| `GET /presets` | `LISTPRESETS` | Full preset objects, array |
| `GET /presets/:name` | — | Single preset |
| `PUT /presets/:name` | — | Create/overwrite with explicit body values |
| `POST /presets/:name/save-current` | `SAVEPRESET` | No body — snapshots current `loop-config` under this name |
| `POST /presets/:name/load` | `LOADPRESET` | Recall-and-go: interrupt-and-go always (confirmed decision — matches serial exactly, no queueing, no confirmation) |
| `DELETE /presets/:name` | `DELETEPRESET` | |
| `POST /config/save` | `SAVECONFIG` | Persists axis config + all presets to flash |
| `GET /status` | `STATUS` | Full snapshot — see below |
| `GET /wifi` | — | `{ssid, rssi, ip, firmware_version}` |
| `POST /ota` | — | Multipart `.bin` upload; requires `X-Glide-OTA-Key`; rejected with `OTA_IN_PROGRESS`/`MOVING` per the interlock below |
| `POST /restart` | — | Restarts the device (M4 Device screen). No body. Rejected with `409 MOVING` while anything is active — reboots into working firmware, so no auth needed (unlike `/ota`, this can't brick the device) |
| `POST /wifi/forget` | — | Clears saved WiFi credentials and restarts into the setup portal (M4 Device screen). No body. Same `409 MOVING` interlock as `/restart` |

### `GET /status` response shape

```json
{
  "position_mm": 123.4,
  "velocity_mm_s": 12.0,
  "phase": "MOVING_TO_B",
  "homed": true,
  "travel_set": true,
  "calibrated": true,
  "travel_mm": 500.0,
  "active_preset": "establishing_shot"
}
```

`active_preset` is `null` once any parameter changes or a manual
move/jog happens after loading a preset (clears on divergence — both
planning agents independently converged on this; it's what "this
preset is live" should mean to a client).

## WebSocket (`/ws`)

Single endpoint, JSON frames tagged by `type`:

```json
{"type":"status","position_mm":123.4,"velocity_mm_s":12.0,"phase":"MOVING_TO_B",
 "homed":true,"travel_set":true,"calibrated":true,"active_preset":"establishing_shot"}
{"type":"event","event":"phase_change","phase":"DWELLING_AT_B"}
{"type":"event","event":"clamped_to_soft_limit"}
{"type":"event","event":"preset_loaded","name":"establishing_shot"}
{"type":"heartbeat","t":1234567}
```

- **`status`**: pushed continuously at ~10Hz while anything is moving
  (throttled well below the 20ms control tick — that rate is for the
  stepper, not the network). Position streams live during a move, not
  just at phase edges, so a UI can animate a smoothly-tracking
  indicator rather than jumping between snapshots.
- **`event`**: one-shot, edge-triggered — phase transitions,
  soft-limit clamps, preset loads. Mirrors the serial protocol's `#`
  async-event convention.
- **`heartbeat`**: every ~5s, so a client can detect a silently-dead
  connection (WiFi drop without a clean close) rather than guessing
  from a stalled request. Recommended client behavior: don't declare
  the connection lost until ~2 heartbeats are missed, to avoid
  flapping on a normal brief WiFi hiccup.

Everything that *mutates* state (move, jog, stop, preset CRUD, OTA)
goes over REST, not WebSocket — this keeps Companion's generic-HTTP
action model fully capable of controlling the device without ever
needing a WebSocket client.

## Phase enum (firmware change needed, not just an API concern)

**Fixed during M3 implementation** — see `reportedPhaseName()` in
`firmware/src/main.cpp`. Previously, serial `STATUS` reported
`PHASE=IDLE` whenever the A-B-A loop wasn't running, even mid a plain
`MOVETO`/`JOG` — there was no way to distinguish "truly idle" from
"mid a one-shot move." The unified phase concept below required an
actual firmware state-model change (not just a translation layer):

```
IDLE, MOVING, MOVING_TO_A, MOVING_TO_B, DWELLING_AT_A, DWELLING_AT_B, STOPPED
```

## OTA

`POST /api/v1/ota`, multipart `.bin` body, `X-Glide-OTA-Key` header
required. Push-based (client uploads to the device) rather than
pull-by-URL, so the device never needs outbound TLS/cert handling.

**Motion and OTA are mutually exclusive, enforced, not just
discouraged:**
- An OTA request while a move/loop is active is rejected
  (`409 MOVING`) — the client must call `/stop` first, explicitly.
  No auto-stop-then-proceed (matches the project's existing pattern:
  `SAVECONFIG`/`SAVEPRESET` are explicit actions with no silent side
  effects).
- Once an OTA is in flight, motion commands are rejected
  (`409 OTA_IN_PROGRESS`).
- Why this matters concretely: flash erase/write can briefly stall
  code execution from flash, and soft limits are only enforced at
  profile-build time (`beginDirectMove()`), not continuously watched
  during an already-running move — if `loop()` stalled mid-flash
  while something was still moving, nothing would be watching for a
  soft-limit violation. Requiring full-stop first removes the
  question entirely rather than depending on an unverified assumption
  about timer/ISR behavior during a flash write.
- Requires the `board_build.partitions = min_spiffs.csv` +
  `board_build.filesystem = littlefs` change in `platformio.ini` (done
  — see "Resolved during M3 implementation" below) for the two OTA app
  slots this needs, since the default `esp32dev` partition table only
  has one.

## Companion generic-HTTP integration

No dedicated Companion endpoint needed. A Companion "Generic HTTP"
button configured as:

```
POST http://glide.local/api/v1/presets/<name>/load
```

(empty body) recalls a preset. A STOP button maps to
`POST http://glide.local/api/v1/stop`. Feedback/button coloring can
poll `GET /api/v1/status`, which Companion's generic-HTTP module
supports natively via JSON-path polling. Recommend preset names avoid
spaces/special characters to keep Companion's URL field simple, even
though percent-encoding technically works.

## Decisions locked for M3 (confirmed with Josh, not picked silently)

- WiFi setup: **WiFiManager** library (not a hand-rolled captive
  portal — reconsidered after weighing the fiddliness of getting
  DNS-redirect/portal edge cases right by hand).
- General API auth: **none** — LAN-only trust model, matching serial.
- OTA specifically: **shared-secret header required** (the one
  exception to no-auth, given it's the highest-consequence single
  action exposed).
- OTA vs. motion: **mutually exclusive, rejected with a clear error**,
  never auto-stop.
- Preset recall while something's already running: **always
  interrupt-and-go**, no queueing, no confirmation — matches existing
  `LOADPRESET` serial behavior exactly, everywhere.

## Resolved during M3 implementation

- **`ESPAsyncWebServer` fork/version:** `ESP32Async/ESPAsyncWebServer`
  + `ESP32Async/AsyncTCP` (pinned in `firmware/platformio.ini`). The
  `mathieucarbou` fork Josh originally confirmed has since moved/
  archived under this org name — same maintainer, same continuation,
  not a different decision.
- **`board_build.partitions`:** the stock `min_spiffs.csv` scheme
  bundled with the arduino-esp32 core (two ~1.9MB OTA app slots +
  ~190KB LittleFS) — no custom partition table needed, since
  config.json + presets are a few KB at most. Also requires
  `board_build.filesystem = littlefs` set explicitly alongside it.
- **Firmware phase-enum gap** (described below): fixed in
  `firmware/src/main.cpp` (`reportedPhaseName()`) as part of M3
  implementation, along with a related latent bug it surfaced —
  `MOVETO`/`JOG` used to silently no-op instead of erroring when
  issued during an active loop.

## Still open (non-blocking)

- Status push rate (10Hz is a starting point, not load-bearing to
  lock now).
- Exact `ESPAsyncWebServer`/`AsyncTCP` patch version may drift from
  what's pinned in `platformio.ini` by the time this is built — check
  https://github.com/ESP32Async/ESPAsyncWebServer/releases for the
  current 3.x tag.

## First build / bring-up checklist

M3 was written in an environment with no ESP32 toolchain (only
`firmware/lib/motion`'s native tests could actually be compiled and
run — see `firmware/test/test_motion`), so this checklist is what
actually walked it onto real hardware, including several real bugs it
caught that no amount of code review would have (see "Real bugs found
during hardware bring-up" below).

**Status as of the bring-up session (2026-09-03): all of M3 — WiFi
provisioning, REST (reads and writes), WebSocket, and OTA — is
confirmed working on real hardware.** OTA was verified with a full
round trip: uploaded the running image back onto itself, the device
rebooted on its own, and came back up healthy (WiFi reconnected, REST/
WebSocket API listening again) on the newly-flashed image.

**Before building:**

1. Copy `firmware/include/secrets.h.example` to
   `firmware/include/secrets.h` (gitignored, safe to put real values
   in). At minimum, set `GLIDE_OTA_KEY` to something real — a
   hardcoded default compiles in otherwise, with a loud warning printed
   over serial every boot. Optionally set `GLIDE_WIFI_SSID`/
   `GLIDE_WIFI_PASS` too, to skip WiFiManager's captive portal on
   every rebuild during bench testing.

**If `pio run -e esp32dev` fails to compile**, these are the specific
spots flagged in code comments as written from established library
knowledge rather than compiled here — check these first, in this
order:

1. The preset-name routes (`/api/v1/presets/...` in
   `setupApiRoutes()`, `firmware/src/main.cpp`) rely on
   ESPAsyncWebServer's regex URL matching (`^...$` patterns +
   `request->pathArg(0)`), gated by the `-DASYNCWEBSERVER_REGEX` build
   flag in `platformio.ini`. If these routes don't compile, or compile
   but never match a real request, confirm that flag is still the
   correct one for the pinned ESPAsyncWebServer version.
2. Every PATCH/PUT/POST-with-a-JSON-body route uses `new
   AsyncCallbackJsonWebHandler(uri, callback)` +
   `handler->setMethod(...)` + `server.addHandler(handler)`. If this
   doesn't compile, check the current class name/constructor signature
   in the installed ESPAsyncWebServer headers.
3. `firmware/lib/api/ota_handler.cpp` uses
   `request->hasHeader()`/`request->getHeader()->value()` to read
   `X-Glide-OTA-Key`. Same story if this doesn't compile.

**Testing after a successful flash, roughly in this order (each one
confirms a layer works before testing the next):**

1. ✅ **Serial monitor at boot** — should show either "WiFi connected:
   ..." or WiFiManager's own portal SSID (`Glide-Setup`) if no saved
   network was found. If the portal comes up, join it from a phone; it
   should show a captive-portal page to enter your real WiFi.
   Confirmed working, both through the portal (first boot) and a
   direct saved-credential connect (subsequent boots).
2. ✅ **REST, read-only** — from another device on the same LAN:
   `curl http://glide.local/api/v1/status` (use the device's IP
   instead if `glide.local` doesn't resolve — printed on the serial
   monitor too). Should return the same JSON shape shown above.
   Confirmed working.
3. ✅ **REST, a real command** — e.g.
   `curl -X PATCH http://glide.local/api/v1/axis -H "Content-Type: application/json" -d '{"travel_mm":500}'`
   then `curl http://glide.local/api/v1/axis` to confirm it stuck. The
   `Content-Type` header is required — `curl -d` defaults to
   `application/x-www-form-urlencoded`, which the JSON body handler
   doesn't recognize as JSON, and the request comes back a plain
   `Not found` (confirmed on real hardware: this reads as "no handler
   matched at all," a different failure than an unhandled-but-matched
   request, which is what a genuinely missing/broken route looks like).
   Confirmed working (with the header).
4. ✅ **WebSocket** — `new WebSocket("ws://glide.local/ws")` in a
   browser's dev console (or `websocat ws://glide.local/ws`). Should
   receive a `{"type":"heartbeat",...}` frame every ~5s even with
   nothing moving, and `{"type":"status",...}` frames while a move is
   active. Confirmed working — heartbeats arrived exactly 5s apart.
5. ✅ **OTA** — this one reboots the device, so confirm everything
   else works first:
   `curl -X POST http://glide.local/api/v1/ota -H "X-Glide-OTA-Key: <your key>" -F "firmware=@.pio/build/esp32dev/firmware.bin"`.
   Have a USB cable within reach in case a bad image needs recovering
   via a wired re-flash. Confirmed working — full round trip (upload,
   `{"ok":true}`, device reboots itself, comes back up healthy on the
   new image). Two things to know, not bugs:
   - The `X-Glide-OTA-Key` header value must match `secrets.h`'s
     `GLIDE_OTA_KEY` **exactly, including capitalization** — the
     comparison is case-sensitive, and a mismatched-case key returns
     `{"error":"UNAUTHORIZED"}`.
   - If `glide.local` briefly fails to resolve mid-session (a local
     mDNS/Bonjour cache hiccup, not a device problem — the device
     itself was still running fine when this happened), retry once or
     use the device's IP directly (shown at boot and via
     `GET /api/v1/wifi`) as a quick way to confirm the device itself is
     still up before troubleshooting further.

## Real bugs found during hardware bring-up (worth remembering as lessons)

None of these were catchable without an actual ESP32 toolchain and
real hardware — worth remembering the *pattern*, not just the fixes,
for future milestones written the same way (planned/coded without
hardware access, verified afterward with Josh at the bench):

1. **`CommandResult{ok, detail}` failed to compile** — a struct with a
   default member initializer (`bool ok = false;`) stops counting as a
   plain aggregate on a C++ standard older than C++14, which is what
   this ESP32 Arduino toolchain defaults to. Same class of issue as
   the earlier `std::clamp`/C++17 lesson from M1: don't assume a
   modern C++ feature "just works" against this specific toolchain's
   defaults. Fixed by dropping the default member initializer.
2. **The generic PlatformIO "Build" button builds every environment**,
   including `native` (test-only, meant for `pio test -e native`) —
   tried and failed to compile `firmware/src/main.cpp` and the
   Arduino-dependent parts of `lib/config`/`lib/api` against the host
   compiler. Fixed with `build_src_filter` on `[env:native]` plus
   `default_envs = esp32dev` in a new `[platformio]` section, so a
   plain Build only ever targets the real firmware.
3. **REST API failed to bind port 80 right after the WiFiManager setup
   portal** (`[E][AsyncTCP.cpp] begin(): bind error: -8`, i.e. address
   already in use) — WiFiManager's own captive-portal server also uses
   port 80, and doesn't release it instantly even after `autoConnect()`
   returns. Only reproduces the first time WiFi is set up (or after a
   reset); fixed with a short delay before starting the REST server,
   specifically on the portal path.
4. **The big one: every REST route returned "Handler did not handle
   the request"** — the original design deferred each handler's real
   work (and its `request->send()` call) into a queue drained from
   `loop()`, specifically to avoid touching motion globals from
   AsyncTCP's task directly. That assumption about ESPAsyncWebServer
   was wrong: it requires `request->send()` synchronously, before the
   handler returns. Reworked to have handlers respond immediately and
   use a mutex (`g_motionMutex`/`MotionLock`) for thread safety
   instead of deferring the work — see "Command dispatch" above.
   Lesson: an architecture decision reasoned out in advance, without
   the ability to verify it against the real library's actual runtime
   behavior, is a hypothesis, not a fact — worth stating that
   uncertainty explicitly rather than presenting it as settled (it
   wasn't in the original write-up).
5. **`curl -d` without `-H "Content-Type: application/json"`** returns
   a plain `Not found` from a JSON-body route, not a JSON error — a
   testing gotcha, not a firmware bug, but confusing enough on first
   hit that it's worth documenting explicitly (see the REST bring-up
   step above).
