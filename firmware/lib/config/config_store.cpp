#include "config_store.h"

#include <ArduinoJson.h>
#include <LittleFS.h>

namespace glide {

namespace {
constexpr const char *kConfigPath = "/config.json";

// Strips control characters (anything below ASCII 32, e.g. a stray
// backspace byte from an old serial-input bug) out of a preset name.
// Self-heals any name that was corrupted before that input bug was
// fixed -- a name saved as an invisible 7-character "test2" reads
// back as a clean 5-character "test2" from here on, no manual
// recovery command needed.
String sanitizePresetName(const String &raw) {
  String cleaned;
  cleaned.reserve(raw.length());
  for (size_t i = 0; i < raw.length(); ++i) {
    if (static_cast<unsigned char>(raw[i]) >= 32) {
      cleaned += raw[i];
    }
  }
  return cleaned;
}
}

bool loadConfig(DeviceConfig &outConfig) {
  // Always start from a clean default -- every early-return below
  // leaves outConfig in this state, never a partially-populated one.
  outConfig = DeviceConfig();

  if (!LittleFS.exists(kConfigPath)) {
    return false;  // normal on first boot -- not an error
  }

  File file = LittleFS.open(kConfigPath, "r");
  if (!file) {
    return false;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, file);
  file.close();
  if (err) {
    return false;
  }

  if (!doc["schema_version"].is<int>() ||
      doc["schema_version"].as<int>() != kConfigSchemaVersion) {
    // Unknown/mismatched schema -- refuse to guess at how to read it.
    // A real migration path belongs here once there's ever a version
    // 2 to migrate from; for now there's only ever been version 1.
    return false;
  }

  outConfig.schema_version = kConfigSchemaVersion;

  for (JsonObject axisObj : doc["axes"].as<JsonArray>()) {
    AxisConfig axis;
    axis.travel_mm = axisObj["travel_mm"] | 0.0;
    axis.steps_per_mm = axisObj["steps_per_mm"] | 0.0;
    outConfig.axes.push_back(axis);
  }

  for (JsonObject presetObj : doc["presets"].as<JsonArray>()) {
    PresetConfig preset;
    preset.name = sanitizePresetName(presetObj["name"] | "");
    preset.pos_a_mm = presetObj["pos_a_mm"] | 0.0;
    preset.pos_b_mm = presetObj["pos_b_mm"] | 0.0;
    preset.speed_mm_s = presetObj["speed_mm_s"] | 20.0;
    preset.accel_mm_s2 = presetObj["accel_mm_s2"] | 100.0;
    preset.dwell_a_s = presetObj["dwell_a_s"] | 0.0;
    preset.dwell_b_s = presetObj["dwell_b_s"] | 0.0;
    preset.repeat = presetObj["repeat"] | true;
    outConfig.presets.push_back(preset);
  }

  return true;
}

bool saveConfig(const DeviceConfig &config) {
  JsonDocument doc;
  doc["schema_version"] = config.schema_version;

  JsonArray axesArray = doc["axes"].to<JsonArray>();
  for (const AxisConfig &axis : config.axes) {
    JsonObject axisObj = axesArray.add<JsonObject>();
    axisObj["travel_mm"] = axis.travel_mm;
    axisObj["steps_per_mm"] = axis.steps_per_mm;
  }

  JsonArray presetsArray = doc["presets"].to<JsonArray>();
  for (const PresetConfig &preset : config.presets) {
    JsonObject presetObj = presetsArray.add<JsonObject>();
    presetObj["name"] = preset.name;
    presetObj["pos_a_mm"] = preset.pos_a_mm;
    presetObj["pos_b_mm"] = preset.pos_b_mm;
    presetObj["speed_mm_s"] = preset.speed_mm_s;
    presetObj["accel_mm_s2"] = preset.accel_mm_s2;
    presetObj["dwell_a_s"] = preset.dwell_a_s;
    presetObj["dwell_b_s"] = preset.dwell_b_s;
    presetObj["repeat"] = preset.repeat;
  }

  File file = LittleFS.open(kConfigPath, "w");
  if (!file) {
    return false;
  }
  size_t written = serializeJson(doc, file);
  file.close();
  return written > 0;
}

}  // namespace glide
