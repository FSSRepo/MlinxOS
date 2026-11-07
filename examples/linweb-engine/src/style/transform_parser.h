#pragma once
#include "style/style_engine.h"
#include <string>

namespace linweb {

class TransformParser {
public:
    static Transform parse_transform(const std::string& str);
    static void parse_transform_origin(const std::string& str, Transform& transform);
    static void parse_translate(const std::string& args, Transform& transform, const std::string& func_name);
    // The individual `translate` property ("-48px 0"): added to the transform's own translation
    static void apply_translate_property(const std::string& value, Transform& transform);
    static void parse_rotate(const std::string& args, Transform& transform);
    static void parse_scale(const std::string& args, Transform& transform, const std::string& func_name);
};

} // namespace linweb
