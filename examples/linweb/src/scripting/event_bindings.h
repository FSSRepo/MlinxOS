#pragma once
#include <string>

namespace linweb {

class JSEngine;

// Dispatches an event to JavaScript via the global JSEngine instance.
void dispatch_event(const std::string& target, const std::string& event_type, const std::string& event_data = "{}");

} // namespace linweb
