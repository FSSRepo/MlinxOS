#include "style/transform_parser.h"
#include <sstream>

namespace linweb {

static void parse_translate_value(const std::string& s, float& out_val, bool& out_is_percent) {
    std::string trimmed = trim(s);
    if (!trimmed.empty() && trimmed.back() == '%') {
        try {
            out_val = std::stof(trimmed.substr(0, trimmed.size() - 1));
            out_is_percent = true;
        } catch (...) {
            out_val = 0;
            out_is_percent = false;
        }
    } else {
        out_val = parse_px(trimmed);
        out_is_percent = false;
    }
}

void TransformParser::parse_translate(const std::string& args, Transform& transform, const std::string& func_name) {
    std::stringstream ss_args(args);
    std::string arg1, arg2;
    if (std::getline(ss_args, arg1, ',')) {
        if (func_name == "translateY") {
            parse_translate_value(arg1, transform.translate_y, transform.translate_y_is_percent);
        } else if (func_name == "translateX") {
            parse_translate_value(arg1, transform.translate_x, transform.translate_x_is_percent);
        } else {
            parse_translate_value(arg1, transform.translate_x, transform.translate_x_is_percent);
            if (std::getline(ss_args, arg2)) {
                parse_translate_value(arg2, transform.translate_y, transform.translate_y_is_percent);
            }
        }
    }
}

void TransformParser::parse_rotate(const std::string& args, Transform& transform) {
    size_t deg_pos = args.find("deg");
    if (deg_pos != std::string::npos) {
        try {
            transform.rotate = std::stof(args.substr(0, deg_pos));
        } catch (...) { transform.rotate = 0.0f; }
    } else {
        try {
            transform.rotate = std::stof(args);
        } catch (...) { transform.rotate = 0.0f; }
    }
}

void TransformParser::parse_scale(const std::string& args, Transform& transform, const std::string& func_name) {
    std::stringstream ss_args(args);
    std::string arg1, arg2;
    if (std::getline(ss_args, arg1, ',')) {
        try {
            float val1 = std::stof(arg1);
            if (func_name == "scaleX") {
                transform.scale_x = val1;
            } else if (func_name == "scaleY") {
                transform.scale_y = val1;
            } else {
                transform.scale_x = val1;
                if (std::getline(ss_args, arg2)) {
                    transform.scale_y = std::stof(arg2);
                } else {
                    transform.scale_y = val1;
                }
            }
        } catch (...) {}
    }
}

Transform TransformParser::parse_transform(const std::string& str) {
    Transform t;
    if (str.empty() || str == "none") return t;
    t.has_transform = true;

    std::string s = str;
    size_t pos = 0;
    while (pos < s.length()) {
        size_t open_paren = s.find('(', pos);
        if (open_paren == std::string::npos) break;
        
        std::string func_name = trim(s.substr(pos, open_paren - pos));
        size_t close_paren = s.find(')', open_paren);
        if (close_paren == std::string::npos) break;
        
        std::string args = s.substr(open_paren + 1, close_paren - open_paren - 1);
        
        if (func_name == "translate" || func_name == "translateX" || func_name == "translateY") {
            parse_translate(args, t, func_name);
        } else if (func_name == "rotate") {
            parse_rotate(args, t);
        } else if (func_name == "scale" || func_name == "scaleX" || func_name == "scaleY") {
            parse_scale(args, t, func_name);
        }
        
        pos = close_paren + 1;
    }
    return t;
}

void TransformParser::parse_transform_origin(const std::string& str, Transform& transform) {
    std::stringstream ss(str);
    std::string part;
    std::vector<std::string> parts;
    while (ss >> part) parts.push_back(part);

    auto parse_origin_part = [&](const std::string& p, bool is_y) -> float {
        if (p == "left") return 0.0f;
        if (p == "right") return 1.0f;
        if (p == "top") return 0.0f;
        if (p == "bottom") return 1.0f;
        if (p == "center") return 0.5f;
        if (p.empty()) return 0.5f;
        if (p.back() == '%') {
            try { return std::stof(p.substr(0, p.size() - 1)) / 100.0f; } catch(...) {}
        }
        try {
            float val = std::stof(p);
            if (val == 0.0f) return 0.0f;
        } catch(...) {}
        return 0.5f; 
    };

    auto is_y_keyword = [](const std::string& p) { return p == "top" || p == "bottom"; };
    auto is_x_keyword = [](const std::string& p) { return p == "left" || p == "right"; };

    if (parts.size() == 1) {
        if (is_y_keyword(parts[0])) {
            transform.origin_y = parse_origin_part(parts[0], true);
            transform.origin_x = 0.5f;
        } else {
            transform.origin_x = parse_origin_part(parts[0], false);
            transform.origin_y = 0.5f;
        }
    } else if (parts.size() >= 2) {
        if (is_y_keyword(parts[0]) || is_x_keyword(parts[1])) {
            transform.origin_y = parse_origin_part(parts[0], true);
            transform.origin_x = parse_origin_part(parts[1], false);
        } else {
            transform.origin_x = parse_origin_part(parts[0], false);
            transform.origin_y = parse_origin_part(parts[1], true);
        }
    }
}

} // namespace linweb
