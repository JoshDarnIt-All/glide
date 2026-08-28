#include "command_queue.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <deque>

namespace glide {

namespace {
struct QueueState {
  SemaphoreHandle_t mutex;
  std::deque<std::function<void()>> jobs;
};
}  // namespace

void CommandQueue::begin() {
  // Deliberately leaked: this queue lives as long as the firmware
  // runs, same as the AsyncWebServer/AsyncWebSocket objects that feed
  // it -- there's no shutdown path to free it on.
  auto *state = new QueueState();
  state->mutex = xSemaphoreCreateMutex();
  state_ = state;
}

void CommandQueue::push(std::function<void()> job) {
  auto *state = static_cast<QueueState *>(state_);
  xSemaphoreTake(state->mutex, portMAX_DELAY);
  state->jobs.push_back(std::move(job));
  xSemaphoreGive(state->mutex);
}

void CommandQueue::drainAll() {
  auto *state = static_cast<QueueState *>(state_);

  // Swap the pending jobs out under the lock so push() from another
  // task is never blocked for the duration of a slow job (e.g.
  // persistConfig()'s flash write) -- then run them lock-free.
  std::deque<std::function<void()>> local;
  xSemaphoreTake(state->mutex, portMAX_DELAY);
  local.swap(state->jobs);
  xSemaphoreGive(state->mutex);

  for (auto &job : local) {
    job();
  }
}

}  // namespace glide
