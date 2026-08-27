# Pin Assignment & Calibration

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

## Microstepping — CONFIRMED (measured on the bench, not read from a datasheet)

Standalone mode sets microstepping via the TMC2209's MS1/MS2 pins,
tied to specific levels by jumpers or fixed wiring on the driver
board. We looked for this in BigTreeTech's own documentation first,
but their manual and schematic for this exact board
[disagree with each other](https://github.com/bigtreetech/BIGTREETECH-TMC2209-V1.2/issues/4)
on the MS1/MS2 truth table, with no resolution in that issue thread —
so neither document was trustworthy enough to hardcode a value from.

**Measured directly instead:** marked the motor shaft, ran the M0
bench test with the code's `MICROSTEPS` placeholder set to 1 (i.e.
commanding 2000 pulses for a nominal "10 revolutions" at
`FULL_STEPS_PER_REV=200`), and counted the actual physical rotation:
**1.25 revolutions**. Since each pulse is one microstep, the real
microstep resolution is `R = 10 / 1.25 = 8`.

**Result: MICROSTEPS = 8** (1/8 step), now set in
`firmware/src/main.cpp`. STEPS_PER_REV is therefore 200 × 8 = 1600.

If this driver board is ever swapped (e.g. the "$6 swap" scenario from
the README), re-measure rather than assuming the same MS1/MS2 wiring —
different board revisions are exactly what caused the documentation
conflict in the first place.

## Steps-per-mm — CONFIRMED (measured on the bench, belt+pulley mounted)

Same reasoning as microstepping: the mm<->steps conversion depends on
the belt pitch and pulley tooth count (and any gearbox reduction),
none of which are independently known, so this was measured directly
rather than guessed or derived from an assumed belt/pulley spec.

**Measurement:** with the motor mounted to the slider (belt wrapped
around its pulley — real drivetrain, not a bare-shaft bench test),
commanded `JOG 50` at a placeholder `SETSTEPSPERMM 10` (i.e. 500
actual steps sent). Measured real carriage travel: **5/8" = 15.875mm**.

`R = 500 steps / 15.875mm = 31.496 steps/mm`

**Result: SETSTEPSPERMM 31.496**, set at runtime via the serial
protocol (this one is intentionally NOT a firmware constant — see the
comment in `firmware/src/main.cpp` above `g_stepsPerMm` — so it can be
recalibrated without a rebuild if the drivetrain ever changes).

**Precision note:** 5/8" is a fairly short distance for this kind of
measurement — a small ruler-reading error is a larger fraction of a
short move than a long one. This value is usable as-is; re-measuring
with a longer commanded move (e.g. `JOG 400`) would tighten the
number further if that precision ever matters.
