#include "scripting/event_bindings.h"
#include "scripting/js_engine.h"

namespace linweb {

extern JSEngine* g_js_engine;

void dispatch_event(const std::string& target, const std::string& event_type, const std::string& event_data) {
    if (g_js_engine) {
        g_js_engine->dispatch_event(target, event_type, event_data);
    }
}

void install_event_bindings(JSEngine& js)
{
    js.execute(R"JS(
// Listeners are stored per target and type as entries that keep the callback the page registered, so
// removeEventListener can find it again even though element wrappers are rebuilt on every access.
const __listener_key = (target_id, type) => String(target_id) + ':' + type;
const __add_listener = (target_id, type, callback, this_arg) => {
  if (typeof callback !== 'function') return;
  const key = __listener_key(target_id, type);
  if (!window._listeners[key]) window._listeners[key] = [];
  const list = window._listeners[key];
  // as in a browser, the same callback is registered once per target and type
  for (const entry of list) if (entry.callback === callback) return;
  list.push({ callback: callback, call: (event) => callback.call(this_arg, event) });
};
const __remove_listener = (target_id, type, callback) => {
  const list = window._listeners[__listener_key(target_id, type)];
  if (!list || typeof callback !== 'function') return;
  for (let i = 0; i < list.length; i++) {
    if (list[i].callback === callback) { list.splice(i, 1); return; }
  }
};

// The object listeners receive: the data the engine built (keyboard fields, a control's value) plus the fields and
// methods of a DOM event. An engine preventDefault (keyboard events call __preventDefault) is kept and also called.
const __prepare_event = (data, type) => {
  const event = (data && typeof data === 'object') ? data : {};
  if (typeof event.type === 'undefined') event.type = type;
  if (typeof event.defaultPrevented === 'undefined') event.defaultPrevented = false;
  if (typeof event.stopPropagation !== 'function') {
    event.stopPropagation = () => { event._propagation_stopped = true; };
  }
  if (typeof event.stopImmediatePropagation !== 'function') {
    event.stopImmediatePropagation = () => { event._propagation_stopped = true; event._immediate_stopped = true; };
  }
  if (typeof event.preventDefault !== 'function') {
    event.preventDefault = () => { event.defaultPrevented = true; };
  } else if (!event._prevent_default_wrapped) {
    const engine_prevent_default = event.preventDefault;
    event.preventDefault = function(...args) {
      event.defaultPrevented = true;
      return engine_prevent_default.apply(event, args);
    };
    event._prevent_default_wrapped = true;
  }
  return event;
};

// The object a target id names, when it can be built cheaply: the document, the window, or the element itself.
const __event_target = (target_id) => {
  if (target_id === 'document') return document;
  if (target_id === 'window') return window;
  const text = String(target_id);
  if (/^\d+$/.test(text)) return document._wrap({ _addr: BigInt(text) });   // an element address, as the engine sends it
  const element = document.getElementById(text);
  return element || text;
};

const __deliver = (target_id, type, data) => {
  const event = __prepare_event(data, type);
  // a dispatch starts with the propagation flags clear, even for an event object that was dispatched before
  event._propagation_stopped = false;
  event._immediate_stopped = false;
  const list = window._listeners[__listener_key(target_id, type)];
  if (!list || list.length === 0) return !event.defaultPrevented;
  try {
    if (typeof event.target === 'undefined') event.target = __event_target(target_id);
  } catch (e) {
    event.target = String(target_id);
  }
  event.currentTarget = event.target;
  for (const entry of list.slice()) {
    if (event._immediate_stopped) break;      // stopImmediatePropagation: no more listeners of this dispatch
    try { entry.call(event); } catch (e) { console.error(e); }   // one failing listener must not stop the others
  }
  return !event.defaultPrevented;
};

Object.assign(__window, {
  dispatchEvent: (target_id, type, data) => {
    // a page may call window.dispatchEvent(event) with an object, as in the DOM
    if (target_id && typeof target_id === 'object') return __deliver('window', target_id.type, target_id);
    return __deliver(target_id, type, data);
  },
  addEventListener: (type, callback) => __add_listener('window', type, callback, window),
  removeEventListener: (type, callback) => __remove_listener('window', type, callback)
});

__mixin(document, {
  addEventListener: (type, callback) => __add_listener('document', type, callback, document),
  removeEventListener: (type, callback) => __remove_listener('document', type, callback),
  dispatchEvent: (event) => (event && event.type) ? __deliver('document', event.type, event) : false
});

document._wrappers.push((node) => ({
  addEventListener: (type, callback) => __add_listener(node._addr, type, callback, document._wrap(node)),
  removeEventListener: (type, callback) => __remove_listener(node._addr, type, callback),
  dispatchEvent: (event) => (event && event.type) ? __deliver(node._addr, event.type, event) : false
}));
)JS");
}

} // namespace linweb
