#include "style/animation_engine.h"
#include "style/transform_parser.h"
#include "style/colors.h"
#include <sstream>
#include <cmath>
#include <cctype>

namespace linweb {

std::vector<AnimDebugEntry> g_anim_debug_entries;

namespace {

// A time in the animation shorthand: a number with a unit, so a bare number stays an iteration count.
bool is_time_token(const std::string& token) {
    if (token.size() < 2) return false;
    if (token.compare(token.size() - 2, 2, "ms") == 0) return true;
    const char first = token[0];
    return token.back() == 's' &&
           (std::isdigit(static_cast<unsigned char>(first)) || first == '.' || first == '-' || first == '+');
}

bool is_number_token(const std::string& token) {
    if (token.empty()) return false;
    const char first = token[0];
    return std::isdigit(static_cast<unsigned char>(first)) || first == '.' || first == '-' || first == '+';
}

bool is_timing_function(const std::string& token) {
    return token == "ease" || token == "ease-in" || token == "ease-out" || token == "ease-in-out" ||
           token == "linear" || token.compare(0, 13, "cubic-bezier(") == 0 || token.compare(0, 6, "steps(") == 0;
}

bool is_fill_mode(const std::string& token) {
    return token == "none" || token == "forwards" || token == "backwards" || token == "both";
}

// Seconds, from a CSS time ("0.15s" or "150ms").
float parse_time_token(const std::string& token) {
    try {
        if (token.size() >= 2 && token.compare(token.size() - 2, 2, "ms") == 0)
            return std::stof(token.substr(0, token.size() - 2)) / 1000.0f;
        return std::stof(token);
    } catch (...) {
        return 0.0f;
    }
}

// The first value of a comma separated list: the longhands can name several animations.
std::string first_list_value(const PropertyMap& specified, const std::string& name) {
    auto it = specified.find(name);
    if (it == specified.end()) return "";
    std::string value = trim(it->second);
    size_t comma = value.find(',');
    if (comma != std::string::npos) value = trim(value.substr(0, comma));
    return value;
}

struct AnimationParams {
    std::string name;
    float duration = 0.0f;
    float delay = 0.0f;
    std::string timing_function = "ease";
    std::string iteration_count = "1";
    std::string fill_mode = "none";
};

// The shorthand is name, then times, timing, iteration count and fill mode in any order. The first time is the
// duration and the second one the delay; the longhand properties override what the shorthand said.
AnimationParams parse_animation_params(const std::string& shorthand, const PropertyMap& specified) {
    AnimationParams params;
    std::stringstream ss(shorthand);
    std::vector<std::string> tokens;
    for (std::string token; ss >> token;) tokens.push_back(token);
    if (tokens.empty()) return params;
    params.name = tokens[0];

    int time_slot = 0;
    for (size_t i = 1; i < tokens.size(); ++i) {
        const std::string& token = tokens[i];
        if (is_time_token(token)) {
            if (time_slot == 0) {
                params.duration = parse_time_token(token);
                time_slot = 1;
            } else if (time_slot == 1) {
                params.delay = parse_time_token(token);
                time_slot = 2;
            }
        } else if (is_timing_function(token)) {
            params.timing_function = token;
        } else if (token == "infinite" || is_number_token(token)) {
            params.iteration_count = token;
        } else if (is_fill_mode(token)) {
            params.fill_mode = token;
        }
        // direction and play-state are not animated by this engine
    }

    std::string longhand = first_list_value(specified, "animation-duration");
    if (!longhand.empty() && is_time_token(longhand)) params.duration = parse_time_token(longhand);
    longhand = first_list_value(specified, "animation-delay");
    if (!longhand.empty() && is_time_token(longhand)) params.delay = parse_time_token(longhand);
    longhand = first_list_value(specified, "animation-timing-function");
    if (!longhand.empty()) params.timing_function = longhand;
    longhand = first_list_value(specified, "animation-iteration-count");
    if (!longhand.empty()) params.iteration_count = longhand;
    longhand = first_list_value(specified, "animation-fill-mode");
    if (!longhand.empty()) params.fill_mode = longhand;
    return params;
}

void push_debug_entry(const AnimationState& as, const std::shared_ptr<StyledNode>& styled_node,
                      double elapsed, float raw_t, float eased_t,
                      const std::map<std::string, AnimPropDebugInfo>& animated_props) {
    AnimDebugEntry entry;
    if (styled_node->node) {
        entry.node_tag = styled_node->node->tag_name;
        if (styled_node->node->is_element()) {
            auto el = std::static_pointer_cast<ElementNode>(styled_node->node);
            if (el->data.attributes.count("id")) entry.node_id = el->data.attributes.at("id");
        }
    }
    entry.anim_name = as.name;
    entry.elapsed = elapsed;
    entry.t = raw_t;
    entry.eased_t = eased_t;
    entry.timing_function = as.timing_function;
    entry.animated_props = animated_props;
    g_anim_debug_entries.push_back(entry);
}

} // namespace

float AnimationEngine::calculate_animation_progress(const AnimationState& as, double current_time, bool& finished, double* out_elapsed, float* out_raw_t) {
    // The delay runs before the animation: the progress clock starts counting once it has passed.
    double elapsed = current_time - as.start_time - as.delay;
    if (elapsed < 0.0) elapsed = 0.0;
    finished = false;
    float t = 0.0f;

    if (as.duration > 0) {
        if (as.iteration_count == "infinite") {
            t = std::fmod(elapsed, as.duration) / (float)as.duration;
        } else {
            int max_iters = 1;
            try { max_iters = std::stoi(as.iteration_count); } catch(...) {}
            if (max_iters < 1) max_iters = 1;
            float total_dur = as.duration * max_iters;
            if (elapsed >= total_dur) {
                t = 1.0f;
                finished = true;
            } else {
                t = std::fmod(elapsed, as.duration) / (float)as.duration;
            }
        }
    } else {
        t = 1.0f;
        finished = (current_time - as.start_time) >= as.delay;
    }

    if (out_elapsed) *out_elapsed = elapsed;
    if (out_raw_t) *out_raw_t = t;

    float eased_t = t;
    if (as.timing_function == "ease") eased_t = ease(t);
    else if (as.timing_function == "ease-in") eased_t = ease_in(t);
    else if (as.timing_function == "ease-out") eased_t = ease_out(t);
    else if (as.timing_function == "ease-in-out") eased_t = ease_in_out(t);
    
    return eased_t;
}

std::map<std::string, AnimPropDebugInfo> AnimationEngine::get_keyframe_styles(
    const AnimationState& as,
    const StyleSheet& stylesheet,
    float eased_t,
    const PropertyMap& specified) {
    
    std::map<std::string, AnimPropDebugInfo> result;
    
    auto kf_it = stylesheet.keyframes.find(as.name);
    if (kf_it == stylesheet.keyframes.end()) return result;
    
    const auto& kr = kf_it->second;
    if (kr.keyframes.empty()) return result;
    
    const Keyframe* k1 = nullptr;
    const Keyframe* k2 = nullptr;
    float offset1 = 0;
    float offset2 = 1;

    auto parse_offset = [](const std::string& off) {
        if (off == "from") return 0.0f;
        if (off == "to") return 1.0f;
        if (off.back() == '%') return std::stof(off.substr(0, off.size() - 1)) / 100.0f;
        return 0.0f;
    };

    for (size_t i = 0; i < kr.keyframes.size(); ++i) {
        float off = parse_offset(kr.keyframes[i].offset);
        if (off <= eased_t) {
            k1 = &kr.keyframes[i];
            offset1 = off;
        }
        if (off >= eased_t && (!k2 || off < offset2)) {
            k2 = &kr.keyframes[i];
            offset2 = off;
        }
    }

    if (!k1 || !k2) return result;
    
    float k_t = (offset1 == offset2) ? 1.0f : (eased_t - offset1) / (offset2 - offset1);
    
    std::map<std::string, std::string> k1_props, k2_props;
    for (const auto& d : k1->declarations) k1_props[d.name] = d.value;
    for (const auto& d : k2->declarations) k2_props[d.name] = d.value;

    for (const auto& [prop, val2] : k2_props) {
        std::string val1 = k1_props.count(prop) ? k1_props[prop] : (specified.count(prop) ? specified.at(prop) : "");
        if (val1.empty()) {
            if (prop == "opacity") val1 = "1.0";
            else if (prop == "filter") val1 = "none";
            else if (prop == "background-color") {
                if (specified.count("background")) val1 = specified.at("background");
                else val1 = "transparent";
            }
            else if (prop == "color") val1 = "black";
            else if (prop == "text-shadow") val1 = "none";
            else val1 = "0px";
        }

        if (prop == "opacity") {
            float v1 = std::stof(val1);
            float v2 = std::stof(val2);
            result[prop] = {val1, val2, std::to_string(lerp(v1, v2, k_t))};
        } else if (prop == "background-color" || prop == "color") {
            Color c1 = parse_color_str(val1);
            Color c2 = parse_color_str(val2);
            Color cr;
            cr.r = lerp(c1.r, c2.r, k_t);
            cr.g = lerp(c1.g, c2.g, k_t);
            cr.b = lerp(c1.b, c2.b, k_t);
            cr.a = lerp(c1.a, c2.a, k_t);
            result[prop] = {val1, val2, color_to_str(cr)};
        } else if (prop == "transform") {
            Transform t1 = TransformParser::parse_transform(val1);
            Transform t2 = TransformParser::parse_transform(val2);
            Transform tresult;
            tresult.has_transform = true;
            tresult.translate_x = lerp(t1.translate_x, t2.translate_x, k_t);
            tresult.translate_y = lerp(t1.translate_y, t2.translate_y, k_t);
            tresult.translate_x_is_percent = t2.translate_x_is_percent;
            tresult.translate_y_is_percent = t2.translate_y_is_percent;
            tresult.rotate = lerp(t1.rotate, t2.rotate, k_t);
            tresult.scale_x = lerp(t1.scale_x, t2.scale_x, k_t);
            tresult.scale_y = lerp(t1.scale_y, t2.scale_y, k_t);
            tresult.origin_x = t2.origin_x;
            tresult.origin_y = t2.origin_y;

            std::stringstream oss;
            if (tresult.translate_x != 0.0f || tresult.translate_y != 0.0f) {
                std::string x_unit = tresult.translate_x_is_percent ? "%" : "px";
                std::string y_unit = tresult.translate_y_is_percent ? "%" : "px";
                oss << "translate(" << tresult.translate_x << x_unit << ", " << tresult.translate_y << y_unit << ") ";
            }
            if (tresult.rotate != 0.0f)
                oss << "rotate(" << tresult.rotate << "deg) ";
            if (tresult.scale_x != 1.0f || tresult.scale_y != 1.0f)
                oss << "scale(" << tresult.scale_x << ", " << tresult.scale_y << ") ";

            std::string out = oss.str();
            if (out.empty()) out = "none";
            result[prop] = {val1, val2, out};
        } else if (prop == "translate") {
            auto offsets = [](const std::string& v, float& x, float& y) {
                std::stringstream words(v);
                std::string a, b;
                words >> a >> b;
                x = (a.empty() || a == "none") ? 0.0f : parse_px(a);
                y = b.empty() ? 0.0f : parse_px(b);
            };
            float x1, y1, x2, y2;
            offsets(val1, x1, y1);
            offsets(val2, x2, y2);
            result[prop] = {val1, val2, std::to_string(lerp(x1, x2, k_t)) + "px " + std::to_string(lerp(y1, y2, k_t)) + "px"};
        } else if (prop == "filter") {
            if (val1.find("blur(") == 0 && val2.find("blur(") == 0) {
                float b1 = parse_px(val1.substr(5, val1.size() - 6));
                float b2 = parse_px(val2.substr(5, val2.size() - 6));
                result[prop] = {val1, val2, "blur(" + std::to_string(lerp(b1, b2, k_t)) + "px)"};
            } else if (val1.find("drop-shadow(") != std::string::npos || val2.find("drop-shadow(") != std::string::npos) {
                DropShadowValue v1 = parse_drop_shadow(val1);
                DropShadowValue v2 = parse_drop_shadow(val2);
                float rx = lerp(v1.x, v2.x, k_t);
                float ry = lerp(v1.y, v2.y, k_t);
                float rb = lerp(v1.blur, v2.blur, k_t);
                Color rc;
                rc.r = lerp(v1.color.r, v2.color.r, k_t);
                rc.g = lerp(v1.color.g, v2.color.g, k_t);
                rc.b = lerp(v1.color.b, v2.color.b, k_t);
                rc.a = lerp(v1.color.a, v2.color.a, k_t);
                result[prop] = {val1, val2, "drop-shadow(" + std::to_string(rx) + "px " + std::to_string(ry) + "px " + std::to_string(rb) + "px " + color_to_str(rc) + ")"};
            } else {
                std::string actual = (k_t < 0.5f) ? val1 : val2;
                result[prop] = {val1, val2, actual};
            }
        } else if (prop == "text-shadow") {
            auto parse_ts = [](const std::string& s) -> DropShadowValue {
                DropShadowValue ds;
                if (s == "none" || s.empty()) return ds;
                std::stringstream ss(s);
                std::string val;
                std::vector<float> dims;
                while (ss >> val) {
                    if (val.find("rgb") != std::string::npos || val.find("#") != std::string::npos || val.find("rgba") != std::string::npos) {
                        ds.color = parse_color_str(val);
                        break;
                    }
                    if (val.find("px") != std::string::npos) {
                        dims.push_back(parse_px(val));
                    } else if (val.find_first_not_of("0123456789.") == std::string::npos) {
                        dims.push_back(std::stof(val));
                    } else {
                        ds.color = parse_color_str(val);
                        break;
                    }
                }
                if (dims.size() >= 1) ds.x = dims[0];
                if (dims.size() >= 2) ds.y = dims[1];
                if (dims.size() >= 3) ds.blur = dims[2];
                return ds;
            };
            DropShadowValue v1 = parse_ts(val1);
            DropShadowValue v2 = parse_ts(val2);
            float rx = lerp(v1.x, v2.x, k_t);
            float ry = lerp(v1.y, v2.y, k_t);
            float rb = lerp(v1.blur, v2.blur, k_t);
            Color rc;
            rc.r = lerp(v1.color.r, v2.color.r, k_t);
            rc.g = lerp(v1.color.g, v2.color.g, k_t);
            rc.b = lerp(v1.color.b, v2.color.b, k_t);
            rc.a = lerp(v1.color.a, v2.color.a, k_t);
            result[prop] = {val1, val2, std::to_string(rx) + "px " + std::to_string(ry) + "px " + std::to_string(rb) + "px " + color_to_str(rc)};
        } else {
            float v1 = parse_px(val1);
            float v2 = parse_px(val2);
            result[prop] = {val1, val2, std::to_string(lerp(v1, v2, k_t)) + "px"};
        }
    }
    
    return result;
}

bool AnimationEngine::update_animation(
    AnimationState& as,
    const std::shared_ptr<StyledNode>& styled_node,
    const StyleSheet& stylesheet,
    double current_time) {
    
    bool finished = false;
    double elapsed = 0.0;
    float raw_t = 0.0f;
    float eased_t = calculate_animation_progress(as, current_time, finished, &elapsed, &raw_t);

    // Before the delay the animation does not advance. With a backwards (or both) fill the first keyframe is
    // shown during the wait; otherwise the element keeps its normal style.
    const bool in_delay = (current_time - as.start_time) < as.delay;
    const bool fills_backwards = as.fill_mode == "backwards" || as.fill_mode == "both";
    if (in_delay && !fills_backwards) {
        auto animated_props = get_keyframe_styles(as, stylesheet, 0.0f, styled_node->specified_values);
        for (const auto& [prop, val] : animated_props) styled_node->animated_values.erase(prop);
        push_debug_entry(as, styled_node, elapsed, raw_t, eased_t, animated_props);
        return false;
    }

    auto animated_props = get_keyframe_styles(as, stylesheet, eased_t, styled_node->specified_values);
    for (const auto& [prop, val] : animated_props) {
        styled_node->animated_values[prop] = val.actual;
        if (prop != "opacity" && prop != "filter" && prop != "text-shadow" && prop != "color" && prop != "background-color" && prop != "transform" && prop != "translate")
            as.layout_affecting = true;
    }

    push_debug_entry(as, styled_node, elapsed, raw_t, eased_t, animated_props);
    
    return finished;
}

void AnimationEngine::apply_animations(
    const std::shared_ptr<StyledNode>& styled_node,
    const StyleSheet& stylesheet,
    double current_time) {
    
    Node* node_ptr = styled_node->node.get();
    auto& specified = styled_node->specified_values;

    std::string current_anim_name;
    if (specified.count("animation")) {
        std::stringstream ss(specified["animation"]);
        ss >> current_anim_name;
    }

    // Remove animations that are no longer specified for this node
    auto it = g_active_animations.find(node_ptr);
    if (it != g_active_animations.end()) {
        for (auto anim_it = it->second.begin(); anim_it != it->second.end(); ) {
            if (anim_it->first != current_anim_name) {
                anim_it = it->second.erase(anim_it);
            } else {
                ++anim_it;
            }
        }
        if (it->second.empty()) {
            g_active_animations.erase(it);
        }
    }

    if (!current_anim_name.empty() && current_anim_name != "none") {
        const AnimationParams params = parse_animation_params(specified["animation"], specified);

        auto& node_anims = g_active_animations[node_ptr];
        auto anim_it = node_anims.find(params.name);
        if (anim_it == node_anims.end()) {
            AnimationState as;
            as.name = params.name;
            as.start_time = current_time;
            as.duration = params.duration;
            as.delay = params.delay;
            as.timing_function = params.timing_function;
            as.iteration_count = params.iteration_count;
            as.fill_mode = params.fill_mode;
            as.finished = false;
            node_anims[params.name] = as;
        } else {
            // The longhands (style.animationDelay, ...) can change while the animation runs. The start time stays,
            // so a new delay only shifts the phase of the running animation.
            AnimationState& as = anim_it->second;
            as.duration = params.duration;
            as.delay = params.delay;
            as.timing_function = params.timing_function;
            as.iteration_count = params.iteration_count;
            as.fill_mode = params.fill_mode;
        }
    }

    if (g_active_animations.count(node_ptr)) {
        auto& node_anims = g_active_animations[node_ptr];
        for (auto it = node_anims.begin(); it != node_anims.end(); ) {
            AnimationState& as = it->second;
            if (as.finished) {
                ++it;
                continue;
            }
            if (update_animation(as, styled_node, stylesheet, current_time)) {
                as.finished = true;
                ++it;
            } else {
                ++it;
            }
        }
    }
}

} // namespace linweb
