#include "style/transition_engine.h"
#include "style/colors.h"
#include "style/transform_parser.h"
#include <sstream>
#include <cmath>
#include <algorithm>

namespace linweb {

float TransitionEngine::parse_timing_function(const std::string& timing) {
    if (timing == "ease") return 0.0f;
    if (timing == "ease-in") return 1.0f;
    return 2.0f;
}

static float apply_easing(float t, const std::string& timing_function) {
    if (timing_function == "ease") return ease(t);
    if (timing_function == "ease-in") return ease_in(t);
    if (timing_function == "ease-out") return ease_out(t);
    if (timing_function == "ease-in-out") return ease_in_out(t);

    size_t cb_start = timing_function.find("cubic-bezier(");
    if (cb_start != std::string::npos) {
        size_t paren_open = cb_start + 13;
        size_t paren_close = timing_function.find(')', paren_open);
        if (paren_close != std::string::npos) {
            std::string args = timing_function.substr(paren_open, paren_close - paren_open);
            for (auto& c : args) if (c == ',') c = ' ';
            std::stringstream ss(args);
            float x1, y1, x2, y2;
            ss >> x1 >> y1 >> x2 >> y2;
            return cubic_bezier(t, x1, y1, x2, y2);
        }
    }

    return t;
}

static std::string interpolate_box_shadow(const std::string& start_value, const std::string& end_value, float t) {
    auto parse_bs = [](const std::string& s) -> DropShadowValue {
        DropShadowValue ds;
        if (s.empty() || s == "none") return ds;

        std::string working = s;
        size_t color_pos = std::string::npos;

        size_t rgba = working.find("rgba(");
        if (rgba != std::string::npos) color_pos = rgba;
        else {
            size_t rgb = working.find("rgb(");
            if (rgb != std::string::npos) color_pos = rgb;
            else {
                size_t hash = working.find('#');
                if (hash != std::string::npos) color_pos = hash;
            }
        }

        std::string dims_str;
        std::string color_str;
        if (color_pos != std::string::npos) {
            dims_str = working.substr(0, color_pos);
            color_str = working.substr(color_pos);
        } else {
            dims_str = working;
        }

        for (auto& c : dims_str) if (c == ',') c = ' ';
        std::stringstream ss(dims_str);
        std::string token;
        std::vector<float> vals;
        while (ss >> token) {
            vals.push_back(parse_px(token));
        }

        if (vals.size() >= 1) ds.x = vals[0];
        if (vals.size() >= 2) ds.y = vals[1];
        if (vals.size() >= 3) ds.blur = vals[2];
        if (vals.size() >= 4) ds.spread = vals[3];

        if (!color_str.empty()) {
            ds.color = parse_color_str(color_str);
        }

        return ds;
    };

    DropShadowValue v1 = parse_bs(start_value);
    DropShadowValue v2 = parse_bs(end_value);

    float rx = lerp(v1.x, v2.x, t);
    float ry = lerp(v1.y, v2.y, t);
    float rb = lerp(v1.blur, v2.blur, t);
    float rs = lerp(v1.spread, v2.spread, t);

    Color rc;
    rc.r = lerp(v1.color.r, v2.color.r, t);
    rc.g = lerp(v1.color.g, v2.color.g, t);
    rc.b = lerp(v1.color.b, v2.color.b, t);
    rc.a = lerp(v1.color.a, v2.color.a, t);

    std::stringstream oss;
    oss << rx << "px " << ry << "px " << rb << "px";
    if (rs != 0.0f) oss << " " << rs << "px";
    oss << " " << color_to_str(rc);
    return oss.str();
}

std::string TransitionEngine::interpolate_value(
    const std::string& start_value,
    const std::string& end_value,
    float t,
    const std::string& property,
    float parent_width,
    float parent_height) {
    
    float eased_t = t;
    
    if (property == "background-color" || property == "color" || property == "background") {
        Color c1 = parse_color_str(start_value);
        Color c2 = parse_color_str(end_value);
        Color result;
        result.r = lerp(c1.r, c2.r, eased_t);
        result.g = lerp(c1.g, c2.g, eased_t);
        result.b = lerp(c1.b, c2.b, eased_t);
        result.a = lerp(c1.a, c2.a, eased_t);
        return color_to_str(result);
    } else if (property == "box-shadow") {
        return interpolate_box_shadow(start_value, end_value, eased_t);
    } else if (property == "border-radius" || property == "width" || property == "height" ||
               property == "min-width" ||
               property == "padding" || property == "margin" || 
               property.find("padding-") == 0 || property.find("margin-") == 0) {
        
        std::string s1 = trim(start_value);
        std::string s2 = trim(end_value);
        bool is_p1 = !s1.empty() && s1.back() == '%';
        bool is_p2 = !s2.empty() && s2.back() == '%';
        
        float ref = (property == "height") ? parent_height : parent_width;

        if (is_p1 && is_p2) {
            float v1 = std::stof(s1.substr(0, s1.size() - 1));
            float v2 = std::stof(s2.substr(0, s2.size() - 1));
            float res = lerp(v1, v2, eased_t);
            return std::to_string(res) + "%";
        } else {
            float v1 = parse_px(start_value, ref);
            float v2 = parse_px(end_value, ref);
            float res = lerp(v1, v2, eased_t);
            return std::to_string(res) + "px";
        }
    } else if (property == "opacity") {
        float v1 = 1.0f;
        try { v1 = std::stof(start_value); } catch(...) {}
        float v2 = 1.0f;
        try { v2 = std::stof(end_value); } catch(...) {}
        float res = lerp(v1, v2, eased_t);
        return std::to_string(res);
    } else if (property == "filter") {
        bool has_blur = start_value.find("blur(") != std::string::npos || end_value.find("blur(") != std::string::npos;
        bool has_invert = start_value.find("invert(") != std::string::npos || end_value.find("invert(") != std::string::npos;
        bool has_drop_shadow = start_value.find("drop-shadow(") != std::string::npos || end_value.find("drop-shadow(") != std::string::npos;

        if (has_blur || has_invert || has_drop_shadow) {
            std::stringstream oss;
            bool needs_space = false;

            if (has_blur) {
                auto extract_blur = [](const std::string& s) {
                    if (s == "none" || s.empty()) return 0.0f;
                    size_t start = s.find("blur(");
                    if (start == std::string::npos) return 0.0f;
                    std::string inner = s.substr(start + 5);
                    if (!inner.empty() && inner.back() == ')') inner.pop_back();
                    return parse_px(inner);
                };
                float v1 = extract_blur(start_value);
                float v2 = extract_blur(end_value);
                float res = lerp(v1, v2, eased_t);
                if (needs_space) oss << " ";
                oss << "blur(" << res << "px)";
                needs_space = true;
            }

            if (has_invert) {
                auto extract_val = [](const std::string& s) {
                    if (s == "none" || s.empty()) return 0.0f;
                    size_t start = s.find("invert(");
                    if (start == std::string::npos) return 0.0f;
                    std::string inner = s.substr(start + 7);
                    if (!inner.empty() && inner.back() == ')') inner.pop_back();
                    inner = trim(inner);
                    if (!inner.empty() && inner.back() == '%') {
                        return std::stof(inner.substr(0, inner.size() - 1)) / 100.0f;
                    }
                    return std::stof(inner);
                };
                try {
                    float v1 = extract_val(start_value);
                    float v2 = extract_val(end_value);
                    float res = lerp(v1, v2, eased_t);
                    if (needs_space) oss << " ";
                    oss << "invert(" << res << ")";
                    needs_space = true;
                } catch(...) {
                    if (needs_space) oss << " ";
                    oss << end_value;
                    needs_space = true;
                }
            }

            if (has_drop_shadow) {
                DropShadowValue v1 = parse_drop_shadow(start_value);
                DropShadowValue v2 = parse_drop_shadow(end_value);

                float rx = lerp(v1.x, v2.x, eased_t);
                float ry = lerp(v1.y, v2.y, eased_t);
                float rb = lerp(v1.blur, v2.blur, eased_t);
                Color rc;
                rc.r = lerp(v1.color.r, v2.color.r, eased_t);
                rc.g = lerp(v1.color.g, v2.color.g, eased_t);
                rc.b = lerp(v1.color.b, v2.color.b, eased_t);
                rc.a = lerp(v1.color.a, v2.color.a, eased_t);

                if (needs_space) oss << " ";
                oss << "drop-shadow(" << rx << "px " << ry << "px " << rb << "px " << color_to_str(rc) << ")";
            }

            return oss.str();
        } else {
            return (eased_t < 0.5f) ? start_value : end_value;
        }
    } else if (property == "transform") {
        Transform t1 = TransformParser::parse_transform(start_value);
        Transform t2 = TransformParser::parse_transform(end_value);
        
        float r = lerp(t1.rotate, t2.rotate, eased_t);
        float sx = lerp(t1.scale_x, t2.scale_x, eased_t);
        float sy = lerp(t1.scale_y, t2.scale_y, eased_t);
        float tx = lerp(t1.translate_x, t2.translate_x, eased_t);
        float ty = lerp(t1.translate_y, t2.translate_y, eased_t);
        
        std::stringstream oss;
        if (tx != 0.0f || ty != 0.0f) {
            std::string x_unit = t2.translate_x_is_percent ? "%" : "px";
            std::string y_unit = t2.translate_y_is_percent ? "%" : "px";
            if (tx != 0.0f && ty != 0.0f)
                oss << "translate(" << tx << x_unit << "," << ty << y_unit << ") ";
            else if (ty != 0.0f)
                oss << "translateY(" << ty << y_unit << ") ";
            else
                oss << "translateX(" << tx << x_unit << ") ";
        }
        if (r != 0.0f)
            oss << "rotate(" << r << "deg) ";
        if (sx != 1.0f || sy != 1.0f) {
            if (sx == sy)
                oss << "scale(" << sx << ") ";
            else
                oss << "scale(" << sx << "," << sy << ") ";
        }
        
        std::string out = oss.str();
        if (out.empty()) out = "none";
        else if (!out.empty() && out.back() == ' ') out.pop_back();
        return out;
    }
    
    return end_value;
}

bool TransitionEngine::update_transition(
    TransitionState& ts,
    const std::shared_ptr<StyledNode>& styled_node,
    double current_time,
    float parent_width,
    float parent_height) {
    
    double elapsed = current_time - ts.start_time;
    float t = (ts.duration > 0) ? (float)(elapsed / ts.duration) : 1.0f;
    
    if (t >= 1.0f) {
        styled_node->animated_values[ts.property] = ts.end_value;
        return true;
    } else {
        float eased_t = apply_easing(t, ts.timing_function);
        
        styled_node->animated_values[ts.property] = interpolate_value(
            ts.start_value, ts.end_value, eased_t, ts.property, parent_width, parent_height);
        ts.last_animated_value = styled_node->animated_values[ts.property];
        return false;
    }
}

static std::string get_specified_color(const PropertyMap& spec, const std::string& prop) {
    if (spec.count(prop)) return spec.at(prop);
    if (prop == "background-color" && spec.count("background")) return spec.at("background");
    if (prop == "background" && spec.count("background-color")) return spec.at("background-color");
    return "";
}

void TransitionEngine::apply_transitions(
    const std::shared_ptr<StyledNode>& styled_node,
    double current_time,
    float parent_width,
    float parent_height) {
    
    Node* node_ptr = styled_node->node.get();
    auto& specified = styled_node->specified_values;
    
    if (specified.count("transition")) {
        std::string transition_str = specified["transition"];

        size_t first_space = transition_str.find(' ');
        if (first_space == std::string::npos) return;
        std::string prop_match = transition_str.substr(0, first_space);

        size_t second_space = transition_str.find(' ', first_space + 1);

        std::string dur_str;
        std::string timing = "ease";

        if (second_space == std::string::npos) {
            dur_str = transition_str.substr(first_space + 1);
        } else {
            dur_str = transition_str.substr(first_space + 1, second_space - first_space - 1);
            timing = transition_str.substr(second_space + 1);
        }
        
        float duration = 0.0f;
        if (!dur_str.empty()) {
            if (dur_str.find("ms") != std::string::npos) duration = std::stof(dur_str) / 1000.0f;
            else duration = std::stof(dur_str);
        }

        if (g_last_specified_values.count(node_ptr)) {
            auto& last_spec = g_last_specified_values[node_ptr];
            
            std::vector<std::string> animatable_props = {
                "color", "background-color", "background", "box-shadow",
                "border-radius", "width", "height", "min-width",
                "padding", "padding-left", "padding-right", "padding-top", "padding-bottom",
                "margin", "margin-left", "margin-right", "margin-top", "margin-bottom",
                "transform", "opacity", "filter"
            };
            
            for (const auto& prop : animatable_props) {
                if (prop_match == "all" || prop_match == prop) {
                    std::string current_target;
                    std::string color_override = get_specified_color(specified, prop);
                    if (!color_override.empty()) {
                        current_target = color_override;
                    } else if (specified.count(prop)) {
                        current_target = specified[prop];
                    } else {
                        if (prop == "width") {
                            std::string display = specified.count("display") ? specified.at("display") : "inline";
                            current_target = (display == "block") ? "100%" : "auto";
                        }
                        else if (prop == "height") current_target = "auto";
                        else if (prop == "background-color" || prop == "background") current_target = "transparent";
                        else if (prop == "color") current_target = "black";
                        else if (prop == "transform") current_target = "none";
                        else if (prop == "opacity") current_target = "1.0";
                        else if (prop == "box-shadow") current_target = "none";
                        else current_target = "0px";
                    }

                    std::string last_val;
                    std::string last_color = get_specified_color(last_spec, prop);
                    if (!last_color.empty()) {
                        last_val = last_color;
                    } else if (last_spec.count(prop)) {
                        last_val = last_spec[prop];
                    } else {
                        if (prop == "width") {
                            std::string display = last_spec.count("display") ? last_spec.at("display") : "inline";
                            last_val = (display == "block") ? "100%" : "auto";
                        }
                        else if (prop == "height") last_val = "auto";
                        else if (prop == "background-color" || prop == "background") last_val = "transparent";
                        else if (prop == "color") last_val = "black";
                        else if (prop == "transform") last_val = "none";
                        else if (prop == "opacity") last_val = "1.0";
                        else if (prop == "box-shadow") last_val = "none";
                        else last_val = "0px";
                    }

                    if (last_val != current_target) {
                        TransitionState ts;
                        ts.property = prop;
                        ts.start_value = last_val;
                        
                        if (g_active_transitions.count(node_ptr) && g_active_transitions[node_ptr].count(prop)) {
                             ts.start_value = g_active_transitions[node_ptr][prop].last_animated_value;
                             if (ts.start_value.empty()) ts.start_value = last_val;
                        }

                        ts.end_value = current_target;
                        ts.start_time = current_time;
                        ts.duration = duration;
                        ts.timing_function = timing;
                        ts.last_animated_value = ts.start_value;
                        g_active_transitions[node_ptr][prop] = ts;
                    }
                }
            }
        }
    }
    
    if (g_active_transitions.count(node_ptr)) {
        auto& node_transitions = g_active_transitions[node_ptr];
        for (auto it = node_transitions.begin(); it != node_transitions.end(); ) {
            TransitionState& ts = it->second;
            if (update_transition(ts, styled_node, current_time, parent_width, parent_height)) {
                it = node_transitions.erase(it);
            } else {
                ++it;
            }
        }
    }
    
    g_last_specified_values[node_ptr] = specified;
}

} // namespace linweb
