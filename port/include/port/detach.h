// Work in a thread of its own, left to finish by itself: std::thread(f, args...).detach().
// The browser build has one thread: there the work is queued instead, and runs in
// a fiber of its own between frames (port/src/web/web_later.cpp), outside whatever lock the
// caller holds. Waiting for the browser (an HTTPS request, port/settings/http.cpp) gives the
// frame back while it waits; work that waits on anything else (a socket) would hold up the game
// there, so such features say they aren't in the browser instead.
#pragma once

#include <functional>
#include <thread>
#include <type_traits>
#include <utility>

#ifdef __EMSCRIPTEN__
void portWebQueue(std::function<void()> work);
#endif
// the thread's own alternate signal stack for crash reports (port/platform.h)
extern "C" void portCrashThreadInit(void);

template <class F, class... A> void portDetach(F&& f, A&&... args) {
#ifdef __EMSCRIPTEN__
  // (copies of f and the arguments, as std::thread makes)
  portWebQueue([fn = std::decay_t<F>(std::forward<F>(f)),
                ... a = std::decay_t<A>(std::forward<A>(args))]() mutable {
    std::invoke(std::move(fn), std::move(a)...);
  });
#else
  std::thread(
      [](std::decay_t<F> fn, std::decay_t<A>... a) {
        portCrashThreadInit(); // a stack overflow here reported too
        std::invoke(std::move(fn), std::move(a)...);
      },
      std::forward<F>(f), std::forward<A>(args)...)
      .detach();
#endif
}
