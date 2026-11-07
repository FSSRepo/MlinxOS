#include "scripting/timer_bindings.h"

namespace linweb {

void install_timer_bindings(JSEngine& js)
{
    js.execute(R"JS(
// One id counter and one table for setTimeout and setInterval, and a list for requestAnimationFrame. Sharing the
// counter, as a browser does, keeps an id from one kind from clearing a timer of another kind by accident.
Object.assign(__window, {
  _next_timer_id: 1,
  _timers: new Map(),      // id -> { callback, args, due, delay, repeat }
  _raf_callbacks: [],      // { id, callback }

  _add_timer: (callback, delay, args, repeat) => {
    const id = window._next_timer_id++;
    if (typeof callback === 'function') {
      const ms = (typeof delay === 'number' && delay > 0) ? delay : 0;
      window._timers.set(id, { callback: callback, args: args, due: Date.now() + ms, delay: ms, repeat: repeat });
    }
    return id;   // a browser also returns an id for a callback that is not a function
  },
  setTimeout: (callback, delay, ...args) => window._add_timer(callback, delay, args, false),
  setInterval: (callback, delay, ...args) => window._add_timer(callback, delay, args, true),
  // an id that already ran, or is undefined/null, is simply not in the table
  clearTimeout: (id) => { window._timers.delete(id); },
  clearInterval: (id) => { window._timers.delete(id); },

  requestAnimationFrame: (callback) => {
    const id = window._next_timer_id++;
    if (typeof callback === 'function') window._raf_callbacks.push({ id: id, callback: callback });
    return id;
  },
  cancelAnimationFrame: (id) => {
    window._raf_callbacks = window._raf_callbacks.filter(entry => entry.id !== id);
  },

  // The pump the main loop calls once per frame: animation callbacks first, then at most one run of every timer that
  // is due. A repeating timer is rescheduled from now, so a slow frame never queues up several late runs.
  _run_raf: () => {
    const now = Date.now();
    const raf = window._raf_callbacks;
    window._raf_callbacks = [];
    for (const entry of raf) {
      try { entry.callback(now); } catch (e) { console.error(e); }
    }

    const due = [];
    for (const entry of window._timers) {
      if (now >= entry[1].due) due.push(entry[0]);
    }
    for (const id of due) {
      const timer = window._timers.get(id);
      if (!timer) continue;                     // cancelled by an earlier callback of this frame
      if (timer.repeat) timer.due = now + timer.delay;
      else window._timers.delete(id);
      // one callback that throws must not keep the rest of the queue from running
      try { timer.callback(...timer.args); } catch (e) { console.error(e); }
    }
  }
});

// the same names are globals, as setTimeout and requestAnimationFrame already were
const setTimeout = window.setTimeout;
const clearTimeout = window.clearTimeout;
const setInterval = window.setInterval;
const clearInterval = window.clearInterval;
const requestAnimationFrame = window.requestAnimationFrame;
const cancelAnimationFrame = window.cancelAnimationFrame;
)JS");
}

} // namespace linweb
