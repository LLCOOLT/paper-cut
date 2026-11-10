// The browser build's stand-in for detached threads (port/include/port/detach.h). Each piece of
// work gets a fiber of its own (Emscripten's), started and then resumed once at the end of every
// frame (portWebYield, port/src/web/web_pc.c) until it returns; work that waits for the browser
// (an HTTPS request, port/settings/http.cpp) gives the frame back with portWebTaskYield while it
// waits, so the game goes on meanwhile, as it does beside a thread.
#include <emscripten/fiber.h>

#include <cstdlib>
#include <functional>
#include <list>
#include <utility>

#include "port/detach.h"

namespace {
constexpr size_t kStack = 512 * 1024;      // the fiber's C stack
constexpr size_t kAsyncStack = 256 * 1024; // Asyncify's copy of its wasm stack while it is switched out

struct Task {
  std::function<void()> work;
  emscripten_fiber_t fiber{};
  void* stack = nullptr;
  void* asyncStack = nullptr;
  bool started = false, done = false;
};

std::list<Task>& tasks() {
  static std::list<Task> t;
  return t;
}
emscripten_fiber_t sCaller;      // the frame's context, while a task runs
void* sCallerAsync;
Task* sRunning;                  // the task running now, or null

void entry(void* arg) {
  Task* t = static_cast<Task*>(arg);
  t->work();
  t->work = nullptr;
  t->done = true;
  emscripten_fiber_swap(&t->fiber, &sCaller); // (never resumed)
}
} // namespace

void portWebQueue(std::function<void()> work) {
  Task& t = tasks().emplace_back();
  t.work = std::move(work);
}

// In a task: back to the frame until the next one's end (true). Outside one: false (the caller
// can't wait without holding up the game).
extern "C" int portWebTaskYield(void) {
  if (sRunning == nullptr) {
    return 0;
  }
  Task* t = sRunning;
  emscripten_fiber_swap(&t->fiber, &sCaller);
  return 1;
}

extern "C" void portWebRunQueued(void) {
  if (sRunning != nullptr) {
    return; // (a task's own work reached the frame's end: not again inside it)
  }
  if (sCallerAsync == nullptr) {
    sCallerAsync = std::malloc(kAsyncStack);
  }
  for (auto it = tasks().begin(); it != tasks().end();) {
    Task& t = *it;
    if (!t.started) {
      t.stack = std::malloc(kStack);
      t.asyncStack = std::malloc(kAsyncStack);
      if (t.stack == nullptr || t.asyncStack == nullptr) {
        std::free(t.stack);
        std::free(t.asyncStack);
        it = tasks().erase(it);
        continue;
      }
      emscripten_fiber_init(&t.fiber, entry, &t, t.stack, kStack, t.asyncStack, kAsyncStack);
      t.started = true;
    }
    emscripten_fiber_init_from_current_context(&sCaller, sCallerAsync, kAsyncStack);
    sRunning = &t;
    emscripten_fiber_swap(&sCaller, &t.fiber);
    sRunning = nullptr;
    if (t.done) {
      std::free(t.stack);
      std::free(t.asyncStack);
      it = tasks().erase(it);
    } else {
      ++it;
    }
  }
}
