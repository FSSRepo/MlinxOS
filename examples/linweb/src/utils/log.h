#pragma once
#include <string>

namespace linweb {

// A plain-text log of what the engine does: one line per event, "[seconds] category: message", with the seconds since
// the engine started. Nothing is written until log_open() succeeds (the --log=<file> option). Every line is flushed,
// so the log is complete up to the last event even if the engine stops.
bool log_open(const std::string& path);
bool log_enabled();
void log_event(const std::string& category, const std::string& message);
void log_close();

} // namespace linweb
