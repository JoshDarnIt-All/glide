#include "webui_assets.h"

#include <cstring>

namespace glide {

const WebuiAsset *findWebuiAsset(const char *path) {
  for (size_t i = 0; i < kWebuiAssetCount; ++i) {
    if (strcmp(kWebuiAssets[i].path, path) == 0) {
      return &kWebuiAssets[i];
    }
  }
  return nullptr;
}

}  // namespace glide
