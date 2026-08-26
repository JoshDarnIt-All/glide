# GVM 48" Slider — Donor Rail Inspection

Purpose: this rail's original controller is dead; the stepper motor is
confirmed good. Before any driver or firmware decision, capture the
motor's real electrical and mechanical specs here. In particular: the
**rated phase current** is what decides whether a TMC2209 (2.0 A RMS /
2.8 A peak per coil, in the common module form factor) is actually
adequate, or whether the motor needs a driver with more headroom. Don't
guess this — measure and record it.

## Motor nameplate

- [ ] Manufacturer / part number: _______________
- [ ] Step angle: _______________ (typically 1.8° = 200 steps/rev, or 0.9° = 400 steps/rev — confirm, don't assume)
- [ ] Rated voltage: _______________
- [ ] Rated phase current: _______________ (A, per coil — this is the number that gates the TMC2209 decision)
- [ ] Holding torque, if printed: _______________
- [ ] Number of leads: _______________ (4 = bipolar, likely; 6 or 8 = check wiring options)

If the nameplate is worn or missing, note that here and flag it — a
current reading from stall/thermal testing is a fallback, not a
substitute for the printed rating, and should be treated as a lower-
confidence number.

## Coil-pair continuity test

With the motor unpowered and disconnected from any driver, use a
multimeter in continuity/resistance mode to identify the two coil pairs
among the motor leads.

- [ ] Lead pair 1 identified (leads: _____ / _____), resistance: _______________ Ω
- [ ] Lead pair 2 identified (leads: _____ / _____), resistance: _______________ Ω
- [ ] Confirm NO continuity between pair 1 and pair 2 (i.e. the two coils are electrically isolated from each other — if they show continuity to each other, that's a short, not a healthy bipolar motor)
- [ ] Resistance is roughly equal between the two pairs (a large mismatch suggests a damaged winding even if "the motor turns")

## Gearbox / reduction

- [ ] External gearbox present? Y / N
- [ ] If yes, reduction ratio: _______________ (e.g. 5.18:1 — check for a stamped ratio or count teeth if undocumented)
- [ ] If no gearbox, confirm direct-drive to belt/pulley: _______________
- [ ] Backlash check: rotate the output by hand, feel/measure play before the motor shaft resists: _______________

## End stops

- [ ] Mechanical (physical hard stop) / optical / magnetic / none — circle or note: _______________
- [ ] Count: _______________ (one per end, or a single home switch only?)
- [ ] Wiring type: NO / NC / other: _______________
- [ ] Connector and pinout, if a connector is present: _______________

## Connector (motor side)

- [ ] Connector type/part number, if identifiable: _______________
- [ ] Pin count: _______________
- [ ] Pinout mapping (pin # → function), as far as it can be determined: _______________
- [ ] Photo taken? Y / N (recommended — connectors are easy to misremember)

## Measured travel

- [ ] Physical rail length (end to end, outside dimension): _______________
- [ ] Usable carriage travel (measured, not nameplate — run the carriage to both physical limits and measure the actual distance): _______________
- [ ] Note: this measured number is what feeds the "travel length is user-configurable, never hard-coded" requirement — record it here, but it belongs in the runtime config, not in firmware source.

## Published payload rating

- [ ] GVM's published payload rating for this rail model: _______________
- [ ] Source (manual, product page, etc.): _______________
- [ ] Note: v1 targets 15 lb horizontal, no brake — if the rail's published rating is below 15 lb, the rail (not the motor or driver) is the limiting factor and the 15 lb target should be revisited.

## Summary / decision inputs

Once the above is filled in, the two numbers that most directly affect
firmware and hardware decisions are:

1. **Rated phase current** → confirms or rules out TMC2209 as the driver.
2. **Step angle + gearbox ratio** → determines steps-per-mm once travel
   and pulley/belt pitch (or leadscrew pitch) are also known — needed
   for the mm-not-steps motion core in M1.

Do not proceed to driver selection or firmware motion-core work until
this checklist is complete.
