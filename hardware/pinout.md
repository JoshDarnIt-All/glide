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
