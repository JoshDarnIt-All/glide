#include "ota_handler.h"

#include <Update.h>

namespace glide {

namespace {

// Only one OTA can ever be in flight at a time (enforced below before
// Update.begin() is even called), so a single global outcome is safe
// -- no need to track per-request state across the upload's chunk
// callbacks and its final response callback.
struct OtaOutcome {
  bool rejected = false;
  int httpCode = 200;
  String body = "{\"ok\":true}";
};
OtaOutcome g_otaOutcome;
bool g_otaInProgress = false;

bool checkOtaAuth(AsyncWebServerRequest *request, const char *otaKey) {
  if (!request->hasHeader("X-Glide-OTA-Key")) return false;
  return request->getHeader("X-Glide-OTA-Key")->value() == otaKey;
}

void rejectOutcome(int httpCode, const char *errorReason) {
  g_otaOutcome.rejected = true;
  g_otaOutcome.httpCode = httpCode;
  g_otaOutcome.body = String("{\"error\":\"") + errorReason + "\"}";
}

}  // namespace

bool otaInProgress() { return g_otaInProgress; }

void otaRegisterRoute(AsyncWebServer &server, const char *otaKey,
                      std::function<bool()> isMotionActive) {
  server.on(
      "/api/v1/ota", HTTP_POST,
      [](AsyncWebServerRequest *request) {
        // Called once the whole multipart body has streamed through
        // the upload handler below -- sends the one final response.
        g_otaInProgress = false;
        request->send(g_otaOutcome.httpCode, "application/json",
                      g_otaOutcome.body);
      },
      [otaKey, isMotionActive](AsyncWebServerRequest *request,
                                String filename, size_t index, uint8_t *data,
                                size_t len, bool final) {
        if (index == 0) {
          // First chunk of a new upload -- run every check exactly
          // once, not on every chunk.
          g_otaOutcome = OtaOutcome();

          if (!checkOtaAuth(request, otaKey)) {
            rejectOutcome(401, "UNAUTHORIZED");
            return;
          }
          if (isMotionActive()) {
            rejectOutcome(409, "MOVING");
            return;
          }
          if (g_otaInProgress) {
            rejectOutcome(409, "OTA_IN_PROGRESS");
            return;
          }
          g_otaInProgress = true;

          size_t sizeHint = request->contentLength() > 0
                                ? request->contentLength()
                                : UPDATE_SIZE_UNKNOWN;
          if (!Update.begin(sizeHint)) {
            g_otaInProgress = false;
            rejectOutcome(500, "OTA_BEGIN_FAILED");
            return;
          }
        }

        if (g_otaOutcome.rejected) {
          // Already rejected on an earlier chunk (or this one) --
          // discard the rest of the body instead of writing into a
          // flash region Update never actually opened.
          return;
        }

        if (len) {
          Update.write(data, len);
        }

        if (final) {
          if (!Update.end(true) || Update.hasError()) {
            rejectOutcome(500, "OTA_FAILED");
          }
          // g_otaInProgress is cleared in the completion handler
          // above, once the final response is actually sent -- not
          // here, so a motion command arriving in the brief window
          // between this last chunk and that response still
          // correctly sees the OTA as in-flight.
        }
      });
}

}  // namespace glide
