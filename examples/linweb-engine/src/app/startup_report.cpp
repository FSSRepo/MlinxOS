#include "app/startup_report.h"
#include <iomanip>
#include <iostream>
#include <string>

namespace linweb {

void print_startup_report(const StartupTimes& times, const StartupCounts& counts)
{
    auto total_duration = std::chrono::duration_cast<std::chrono::microseconds>(times.end - times.start).count() / 1000.0;

    std::cout << "\n" << std::string(40, '=') << "\n";
    std::cout << "  LINWEB PERFORMANCE & INFO\n";
    std::cout << std::string(40, '=') << "\n";

    auto print_step = [](const std::string& name, auto start, auto end, size_t info_val, const std::string& info_label) {
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count() / 1000.0;
        std::cout << std::left << std::setw(20) << name << ": "
                  << std::right << std::setw(8) << std::fixed << std::setprecision(3) << duration << " ms | "
                  << info_val << " " << info_label << "\n";
    };

    print_step("HTML Parsing", times.html_start, times.html_end, counts.dom_nodes, "nodes");
    print_step("Resources Loading", times.resources_start, times.resources_end, counts.style_sources, "styles");
    print_step("CSS Parsing", times.css_start, times.css_end, counts.css_rules, "rules");
    print_step("Style Tree", times.style_start, times.style_end, counts.styled_nodes, "styled nodes");
    print_step("Layout Tree", times.layout_start, times.layout_end, counts.boxes, "boxes");

    std::cout << std::string(40, '-') << "\n";
    std::cout << std::left << std::setw(20) << "Total Pipeline" << ": "
              << std::right << std::setw(8) << total_duration << " ms\n";
    std::cout << std::string(40, '=') << "\n\n";
}

} // namespace linweb
