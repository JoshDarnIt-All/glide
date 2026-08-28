#pragma once

#include <functional>

// M3: bridges HTTP/WebSocket requests (which arrive on AsyncTCP's own
// FreeRTOS task, not loop()'s) into loop()'s single-threaded context,
// where all motion globals already live and are safe to read/write.
// See docs/api.md's "Command dispatch" section -- the rule is that
// nothing outside loop() ever touches motion state directly.
//
// A REST/WebSocket handler builds a closure that captures everything
// it needs (already-parsed request data, plus the AsyncWebServerRequest*
// to reply to) and pushes it here; loop() drains and runs each one on
// its own thread once per tick, via CommandQueue::drainAll(). This
// applies uniformly to reads (e.g. GET /status) and writes (e.g.
// POST /move) -- a "read-only" GET that touched g_presets (a
// std::vector of PresetConfig, each holding a heap-allocated Arduino
// String) directly from a different core while loop() might be
// mutating it (e.g. mid SAVEPRESET) would be a real data race, not
// just a cosmetic torn-read risk.
//
// This is a plain mutex-guarded deque, not a raw FreeRTOS queue: a
// FreeRTOS queue memcpy's its items in and out, which is unsafe for a
// std::function that may hold heap-allocated captured state -- the
// copy would leave two "owners" of the same heap block. A queue this
// low-volume (a person tapping a phone UI, not a firehose) doesn't
// need FreeRTOS's queue performance anyway.

namespace glide {

class CommandQueue {
 public:
  // Creates the underlying mutex. Call once from setup(), after
  // FreeRTOS/Arduino's own init has run -- not from a global
  // constructor, whose ordering relative to FreeRTOS startup isn't
  // guaranteed.
  void begin();

  // Thread-safe: call from any task (AsyncTCP's handlers included).
  void push(std::function<void()> job);

  // NOT thread-safe against itself -- call only from loop()'s own
  // thread, once per tick. Swaps the pending jobs out under the lock
  // (so push() from another task is never blocked for long, even if
  // a job itself is slow -- e.g. persistConfig()'s flash write) and
  // then runs them lock-free, in the order they were pushed.
  void drainAll();

 private:
  // Opaque pointer to a QueueState (mutex + deque), defined in the
  // .cpp -- keeps FreeRTOS headers out of this public .h.
  void *state_ = nullptr;
};

}  // namespace glide
