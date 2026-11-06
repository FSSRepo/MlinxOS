#pragma once
#include <string>

namespace linweb {

class JSEngine;

// window.addEventListener / dispatchEvent, and addEventListener on document and on every element. The native side
// reaches the listeners through dispatch_event below. Needs the window and the document bindings.
void install_event_bindings(JSEngine& js);

// Dispatches an event to JavaScript via the global JSEngine instance.
void dispatch_event(const std::string& target, const std::string& event_type, const std::string& event_data = "{}");

} // namespace linweb
