#pragma once
#include "style/style_engine.h"
#include <string>
#include <vector>

namespace linweb {

struct AnimPropDebugInfo {
    std::string start;
    std::string end;
    std::string actual;
};

struct AnimDebugEntry {
    std::string node_tag;
    std::string node_id;
    std::string anim_name;
    double elapsed;
    float t;
    float eased_t;
    std::string timing_function;
    std::map<std::string, AnimPropDebugInfo> animated_props;
};

extern std::vector<AnimDebugEntry> g_anim_debug_entries;

class AnimationEngine {
public:
    static void apply_animations(
        const std::shared_ptr<StyledNode>& styled_node,
        const StyleSheet& stylesheet,
        double current_time);

    static bool update_animation(
        AnimationState& as,
        const std::shared_ptr<StyledNode>& styled_node,
        const StyleSheet& stylesheet,
        double current_time);

    static std::map<std::string, AnimPropDebugInfo> get_keyframe_styles(
        const AnimationState& as,
        const StyleSheet& stylesheet,
        float eased_t,
        const PropertyMap& specified);

    static float calculate_animation_progress(const AnimationState& as, double current_time, bool& finished, double* out_elapsed = nullptr, float* out_raw_t = nullptr);
};

} // namespace linweb
