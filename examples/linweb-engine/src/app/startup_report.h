#pragma once
#include <chrono>
#include <cstddef>

namespace linweb {

// When each step of loading the page started and ended.
struct StartupTimes {
    using Clock = std::chrono::high_resolution_clock;
    Clock::time_point start, html_start, html_end, resources_start, resources_end, css_start, css_end,
                      style_start, style_end, layout_start, layout_end, end;
};

// What each step worked on.
struct StartupCounts {
    size_t dom_nodes = 0, style_sources = 0, css_rules = 0, styled_nodes = 0, boxes = 0;
};

// The table the engine prints to the console once the page is loaded.
void print_startup_report(const StartupTimes& times, const StartupCounts& counts);

} // namespace linweb
