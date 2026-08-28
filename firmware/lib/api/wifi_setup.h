#pragma once

// M3: gets the device onto the LAN before the REST/WebSocket API can
// do anything. See docs/api.md's "WiFi provisioning" section.

namespace glide {

// Blocks until connected (WiFiManager's own behavior): tries saved
// credentials first, falls back to a "Glide-Setup" AP + captive
// portal if none work, saves whatever's entered there, and returns
// once connected. This runs during setup(), before the motion control
// tick starts, so blocking here doesn't jitter anything.
//
// If firmware/src/secrets.h exists and defines GLIDE_WIFI_SSID /
// GLIDE_WIFI_PASS, those are used directly via WiFi.begin() instead --
// a bench-testing shortcut that skips the portal entirely (secrets.h
// is gitignored; see firmware/src/secrets.h.example). Falls back to
// WiFiManager automatically if the hardcoded credentials don't connect
// within a few seconds, so a stale secrets.h can't strand the device.
//
// Also starts mDNS ("glide.local") once connected, so Companion
// buttons/bookmarks survive a DHCP lease change.
//
// Returns false only if WiFiManager's own portal timed out with no
// connection made -- the device is then not on any network, and the
// caller (main.cpp) should still let the rest of firmware boot (serial
// control keeps working), just without REST/WebSocket reachable.
bool wifiSetupBegin();

}  // namespace glide
