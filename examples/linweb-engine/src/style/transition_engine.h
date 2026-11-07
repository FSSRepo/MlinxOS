#pragma once
#include "style/style_engine.h"
#include <string>

namespace linweb {

class TransitionEngine {
public:
    static void apply_transitions(
        const std::shared_ptr<StyledNode>& styled_node,
        double current_time,
        float parent_width,
        float parent_height);

    static bool update_transition(
        TransitionState& ts,
        const std::shared_ptr<StyledNode>& styled_node,
        double current_time,
        float parent_width,
        float parent_height);

    static std::string interpolate_value(
        const std::string& start_value,
        const std::string& end_value,
        float t,
        const std::string& property,
        float parent_width,
        float parent_height);

    static float parse_timing_function(const std::string& timing);
};

} // namespace linweb
