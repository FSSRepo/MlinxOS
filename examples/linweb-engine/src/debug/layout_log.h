#pragma once
#include "layout/layout_engine.h"

namespace linweb {

// Diagnostic: logs the boxes of the elements the PS3 menu depends on (position and child count), so a layout that does
// not match the painted page can be traced. Only writes when the log is open.
void log_layout_snapshot(const LayoutBox& root);

} // namespace linweb
