#include "style/animation_engine.h"
#include "style/transform_parser.h"
#include "style/colors.h"
#include <sstream>
#include <cmath>
#include <cctype>

namespace linweb {

std::vector<AnimDebugEntry> g_anim_debug_entries;

float AnimationEngine::calculate_animation_progress(const AnimationState& as, double current_time, bool& finished, double* out_elapsed, float* out_raw_t) {
    double elapsed = current_time - as.start_time;
    finished = false;
    float t = 0.0f;

    if (as.duration > 0) {
        if (as.iteration_count == "infinite") {
            t = std::fmod(elapsed, as.duration) / (float)as.duration;
        } else {
            int max_iters = 1;
            try { max_iters = std::stoi(as.iteration_count); } catch(...) {}
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
        finished = true;
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
    
    auto animated_props = get_keyframe_styles(as, stylesheet, eased_t, styled_node->specified_values);
    for (const auto& [prop, val] : animated_props) {
        styled_node->animated_values[prop] = val.actual;
    }

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
        std::stringstream ss(specified["animation"]);
        std::vector<std::string> tokens;
        std::string token;
        while (ss >> token) {
            tokens.push_back(token);
        }

        if (tokens.empty()) return;

        std::string name = tokens[0];
        std::string dur_str;
        std::string timing_func = "ease";
        std::string iter_count = "1";

        size_t idx = 1;

        if (idx < tokens.size()) {
            const std::string& t = tokens[idx];
            if (!t.empty() && (std::isdigit(static_cast<unsigned char>(t[0])) || t[0] == '.')) {
                dur_str = t;
                idx++;
            }
        }

        for (; idx < tokens.size(); idx++) {
            const std::string& t = tokens[idx];
            if (t == "ease" || t == "ease-in" || t == "ease-out" || t == "ease-in-out" ||
                t == "linear" || t.find("cubic-bezier(") == 0 || t.find("steps(") == 0) {
                timing_func = t;
            } else if (t == "infinite" || (!t.empty() && (std::isdigit(static_cast<unsigned char>(t[0])) || t[0] == '.'))) {
                iter_count = t;
            }
        }

        float duration = 0.0f;
        if (!dur_str.empty()) {
            if (dur_str.find("ms") != std::string::npos) duration = std::stof(dur_str) / 1000.0f;
            else duration = std::stof(dur_str);
        }

        auto& node_anims = g_active_animations[node_ptr];
        if (!node_anims.count(name)) {
            AnimationState as;
            as.name = name;
            as.start_time = current_time;
            as.duration = duration;
            as.timing_function = timing_func;
            as.iteration_count = iter_count;
            as.finished = false;
            node_anims[name] = as;
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
