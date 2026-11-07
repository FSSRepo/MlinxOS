#pragma once
#include "core/dom.h"

namespace linweb {
inline bool is_button_input(const Node& node) {
    if (node.tag_name != "input") return false;
    auto type = node.data.attributes.find("type");
    return type != node.data.attributes.end() && (type->second == "submit" || type->second == "button" || type->second == "reset");
}
inline std::string button_input_label(const Node& node) {
    auto value = node.data.attributes.find("value");
    if (value != node.data.attributes.end()) return value->second;
    auto type = node.data.attributes.find("type");
    return type != node.data.attributes.end() && type->second == "reset" ? "Reset" : "Submit";
}
}
