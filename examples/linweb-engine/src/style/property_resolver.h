#pragma once
#include "style/style_engine.h"

namespace linweb {

class PropertyResolver {
public:
    static PropertyMap resolve_specified_values(
        const std::shared_ptr<Node>& node,
        const StyleSheet& stylesheet,
        const std::shared_ptr<Node>& hovered_node,
        const std::shared_ptr<Node>& focused_node,
        const std::shared_ptr<Node>& active_node);

    static void resolve_inherited_values(PropertyMap& values, const PropertyMap& inherited_values);
    static void resolve_currentColor(PropertyMap& values, const PropertyMap& inherited_values);
    static void resolve_default_values(PropertyMap& values, const std::string& tag);
    static void apply_cascade(PropertyMap& values, const StyleSheet& stylesheet,
        const std::shared_ptr<Node>& node,
        const std::shared_ptr<Node>& hovered_node,
        const std::shared_ptr<Node>& focused_node,
        const std::shared_ptr<Node>& active_node);
};

} // namespace linweb
