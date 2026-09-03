#include "wifi_setup.h"

#include <Arduino.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <WiFiManager.h>

#if __has_include("secrets.h")
#include "secrets.h"
#endif

namespace glide {

namespace {

#ifdef GLIDE_WIFI_SSID
// Bounded attempt: if the hardcoded network isn't reachable (stale
// password, different location than the bench), fall through to
// WiFiManager rather than hanging here indefinitely.
bool tryHardcodedCredentials() {
  Serial.println("WiFi: trying secrets.h credentials...");
  WiFi.begin(GLIDE_WIFI_SSID, GLIDE_WIFI_PASS);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
    delay(250);
  }
  return WiFi.status() == WL_CONNECTED;
}
#endif

void beginMdns() {
  if (!MDNS.begin("glide")) {
    Serial.println(
        "WARNING: mDNS (glide.local) failed to start -- use the IP "
        "address instead");
  } else {
    Serial.println("mDNS started: glide.local");
  }
}

}  // namespace

bool wifiSetupBegin() {
  bool connected = false;
  // True only if WiFiManager's own captive-portal web server actually
  // ran -- that server also binds port 80. It's torn down internally
  // once autoConnect() returns, but confirmed on real hardware that
  // the OS/LWIP doesn't always release the port instantly: starting
  // the REST API's AsyncWebServer right after can fail to bind
  // ("[E][AsyncTCP.cpp] begin(): bind error: -8", i.e. address already
  // in use) if this path was taken. Only matters the first time (or
  // after a WiFi reset) -- once a network is saved, later boots skip
  // the portal entirely and this never applies.
  bool usedConfigPortal = false;

#ifdef GLIDE_WIFI_SSID
  connected = tryHardcodedCredentials();
  if (!connected) {
    Serial.println(
        "WiFi: secrets.h credentials didn't connect -- falling back to "
        "WiFiManager");
  }
#endif

  if (!connected) {
    WiFiManager wm;
    // 3 minutes is generous enough to actually walk over and join the
    // "Glide-Setup" AP without rushing, but still bounded -- this call
    // blocks setup(), and motion isn't live yet, so a long stall here
    // doesn't jitter anything.
    wm.setConfigPortalTimeout(180);
    connected = wm.autoConnect("Glide-Setup");
    usedConfigPortal = true;
  }

  if (connected) {
    Serial.print("WiFi connected: ");
    Serial.print(WiFi.SSID());
    Serial.print(" (");
    Serial.print(WiFi.localIP());
    Serial.println(")");
    if (usedConfigPortal) {
      delay(1000);  // let WiFiManager's portal server actually release port 80
    }
    beginMdns();
  } else {
    Serial.println(
        "WiFi: no connection (portal timed out) -- REST/WebSocket API "
        "will not be reachable this boot. Serial control still works.");
  }

  return connected;
}

void wifiForgetAndRestart() {
  WiFiManager wm;
  wm.resetSettings();
  Serial.println("WiFi credentials cleared -- restarting into setup portal");
  delay(500);  // let the log line and any pending HTTP response actually flush
  ESP.restart();
}

}  // namespace glide
