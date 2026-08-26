# Pin Assignment — M0 Bench Rig

TMC2209 driven in **standalone STEP/DIR/EN mode** (decided over UART
single-wire mode): fewer pins, no extra library, nothing to configure in
software before the motor even proves it can turn. The tradeoff, noted
for later: microstepping and current are set in hardware (jumpers /
trimpot) rather than in firmware config, which cuts against the general
"config drives behavior" approach — acceptable for M0, worth revisiting
if a later milestone wants software-controlled current tuning.

| Signal | ESP32 GPIO | Notes |
|---|---|---|
| STEP | GPIO 25 | Pulses to advance one (micro)step per pulse |
| DIR | GPIO 26 | Direction level (which way is "positive" is arbitrary until homing/soft-limits exist in M1) |
| EN | GPIO 27 | TMC2209 EN is **active-LOW**: driving this LOW enables the driver, HIGH disables it |

These three were chosen because they're general-purpose ESP32 pins with
no special boot-time role:

- Avoided: GPIO 0, 2, 5, 12, 15 — these affect boot mode / flash voltage
  selection at reset; holding them in the wrong state during boot can
  put the ESP32 into the wrong boot mode or prevent flashing.
- Avoided: GPIO 34–39 — input-only, can't drive an output signal like
  STEP/DIR/EN.

## Microstepping — NOT YET CONFIRMED

Standalone mode sets microstepping via the TMC2209's MS1/MS2 pins,
which are typically tied to specific levels by jumpers or fixed
wiring on the driver breakout board — and the default varies by which
specific board you end up using. This is not knowable until the board
is in hand.

`firmware/src/main.cpp` currently assumes **full-step (1x, no
microstepping)** as a placeholder — i.e. 200 steps/rev, matching the
confirmed motor spec directly with no multiplier. This is very likely
*not* what you'll actually run (microstepping is almost always used in
practice, for quieter/smoother motion), but it's the only value that
doesn't require guessing at a specific board's MS1/MS2 default.

**When the driver board arrives:** check its silkscreen/documentation
for the MS1/MS2 truth table, note the resulting microstep setting here,
and update `MICROSTEPS` in `firmware/src/main.cpp` to match.
