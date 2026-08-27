#pragma once

#include <Arduino.h>
#include <vector>

// Persistent device config: axis calibration + named loop presets,
// stored as a single JSON file (config.json) on the ESP32's LittleFS
// flash filesystem. This is Arduino/ESP32-specific (uses String,
// LittleFS, ArduinoJson) -- unlike firmware/lib/motion, it can't be
// unit-tested on a laptop; it only runs on real hardware.
//
// What persists and what doesn't, and why -- see docs/serial_protocol.md
// and the M2 planning discussion for the full reasoning:
//   - Axis config (travel_mm, steps_per_mm) and presets DO persist --
//     they're fixed facts about the physical hardware/saved setups,
//     not live position references.
//   - Home (the SETHOME zero reference) does NOT persist -- it's
//     re-established fresh every boot via the serial protocol, never
//     written to this file. There's no way to confirm a saved zero is
//     still physically true after a power cycle on this open-loop,
//     no-encoder, no-brake system.

namespace glide {

struct AxisConfig {
  double travel_mm = 0.0;
  double steps_per_mm = 0.0;
};

struct PresetConfig {
  String name;
  double pos_a_mm = 0.0;
  double pos_b_mm = 0.0;
  double speed_mm_s = 20.0;
  double accel_mm_s2 = 100.0;
  double dwell_a_s = 0.0;
  double dwell_b_s = 0.0;
  bool repeat = true;
};

// The schema this firmware writes and expects to read. Bump this and
// add a migration path in loadConfig() if the JSON shape ever changes
// in a way older configs can't just be read as-is.
constexpr int kConfigSchemaVersion = 1;

struct DeviceConfig {
  int schema_version = kConfigSchemaVersion;
  // Array on purpose, even though v1 firmware only ever uses axes[0]
  // -- multi-axis support was designed in from the start (see
  // README's v1 non-scope section) specifically so this doesn't need
  // a schema rewrite later.
  std::vector<AxisConfig> axes;
  std::vector<PresetConfig> presets;
};

// Reads /config.json from LittleFS into outConfig. Returns false if:
// no file exists yet (normal on first boot -- not an error), the file
// can't be parsed, or its schema_version doesn't match
// kConfigSchemaVersion. In every false case, outConfig is left as a
// fresh default-constructed DeviceConfig (empty axes/presets) --
// callers should never partially trust a failed load.
bool loadConfig(DeviceConfig &outConfig);

// Writes config to /config.json on LittleFS, overwriting any existing
// file. Returns false on a filesystem/write failure.
bool saveConfig(const DeviceConfig &config);

}  // namespace glide
