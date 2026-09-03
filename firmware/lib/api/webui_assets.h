#pragma once

#include <cstddef>
#include <cstdint>

// M4: the built web UI, embedded into the firmware app partition as
// gzipped PROGMEM byte arrays -- see
// firmware/scripts/embed_webui.py (the generator, runs automatically
// on every PlatformIO build) and docs/m4_ui_plan.md's "Technical
// architecture" section for why this lives in the app partition and
// not LittleFS.
//
// kWebuiAssets/kWebuiAssetCount are defined in the AUTO-GENERATED
// firmware/lib/api/webui_assets_data.cpp (gitignored, regenerated
// every build) -- never edit that file by hand. This header and its
// matching .cpp are the stable, hand-written interface the generated
// file plugs into.

namespace glide {

struct WebuiAsset {
  const char *path;      // e.g. "/index.html", "/assets/index-abc123.js"
  const uint8_t *data;   // gzip-compressed bytes, PROGMEM
  size_t length;         // compressed length in bytes
  const char *mimeType;
};

extern const WebuiAsset kWebuiAssets[];
extern const size_t kWebuiAssetCount;

// Linear scan over kWebuiAssets -- fine at this scale (a handful of
// build output files, not thousands), no need for a hash map. Returns
// nullptr if path isn't a known embedded asset.
const WebuiAsset *findWebuiAsset(const char *path);

}  // namespace glide
