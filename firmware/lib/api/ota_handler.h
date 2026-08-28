#pragma once

#include <ESPAsyncWebServer.h>

#include <functional>

// M3: OTA firmware upload. See docs/api.md's OTA section -- push-based
// (client uploads a .bin, device never needs outbound TLS/certs),
// requires the X-Glide-OTA-Key header, and is mutually exclusive with
// motion: rejected outright (never auto-stop) if a move/loop is
// active, since flash erase/write can briefly stall code execution and
// soft limits aren't continuously watched during an already-running
// move (only enforced when a move is first built).
//
// NOTE: the exact AsyncWebServerRequest header-access calls here
// (hasHeader/getHeader) and the upload-handler signature are written
// from established knowledge of ESPAsyncWebServer's long-stable API,
// not verified against the current ESP32Async fork's headers in this
// environment (no ESP32 toolchain here to compile against). Worth a
// close look on the first real build if this file errors.

namespace glide {

// Registers POST /api/v1/ota on server. isMotionActive is called once
// per upload attempt to decide whether to reject with 409 MOVING --
// this module has no knowledge of motion globals itself; main.cpp
// supplies that check (its own isActive()).
void otaRegisterRoute(AsyncWebServer &server, const char *otaKey,
                      std::function<bool()> isMotionActive);

// True from the moment an authorized, non-rejected OTA upload begins
// until its response is sent. main.cpp's motion-command handlers
// consult this to reject with 409 OTA_IN_PROGRESS.
bool otaInProgress();

}  // namespace glide
