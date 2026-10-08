#pragma once
#include "core/dom.h"
#include "parser/css/css_parser.h"
#include "style/colors.h"
#include <map>
#include <vector>
#include <memory>
#include <string>

namespace linweb {

typedef std::map<std::string, std::string> PropertyMap;

struct TransitionState {
    std::string property;
    std::string start_value;
    std::string last_animated_value;
    std::string end_value;
    double start_time;
    double duration;
    std::string timing_function;
};

struct Transform {
    float rotate = 0.0f;
    float scale_x = 1.0f;
    float scale_y = 1.0f;
    float translate_x = 0.0f;
    float translate_y = 0.0f;
    float origin_x = 0.5f;
    float origin_y = 0.5f;
    bool has_transform = false;
    bool translate_x_is_percent = false;
    bool translate_y_is_percent = false;
};

struct AnimationState {
    std::string name;
    double start_time;
    double duration;
    std::string timing_function;
    std::string iteration_count;
    bool finished = false;
};

struct StyledNode {
    std::shared_ptr<Node> node;
    PropertyMap specified_values;
    PropertyMap animated_values;
    Transform transform;
    std::vector<std::shared_ptr<StyledNode>> children;

    explicit StyledNode(std::shared_ptr<Node> n) : node(std::move(n)) {}
    
    std::string value(const std::string& name) const {
        auto it = animated_values.find(name);
        if (it != animated_values.end()) return it->second;
        it = specified_values.find(name);
        return it != specified_values.end() ? it->second : "";
    }
};

struct DropShadowValue {
    float x = 0, y = 0, blur = 0, spread = 0;
    Color color = {0,0,0,0};
};

float cubic_bezier(float t, float x1, float y1, float x2, float y2);

extern std::map<Node*, std::map<std::string, TransitionState>> g_active_transitions;
extern std::map<Node*, std::map<std::string, AnimationState>> g_active_animations;
extern std::map<Node*, PropertyMap> g_last_specified_values;

float lerp(float a, float b, float t);
float ease(float t);
float ease_in(float t);
float ease_out(float t);
float ease_in_out(float t);
float parse_px(const std::string& s, float reference_val = 100.0f);
std::string format_value(float val, const std::string& original);
DropShadowValue parse_drop_shadow(const std::string& s);

class StyleEngine {
public:
    static std::shared_ptr<StyledNode> build_style_tree(
        const std::shared_ptr<Node>& root,
        const StyleSheet& stylesheet,
        const std::shared_ptr<Node>& hovered_node = nullptr,
        const std::shared_ptr<Node>& focused_node = nullptr,
        const std::shared_ptr<Node>& active_node = nullptr,
        double current_time = 0.0,
        float parent_width = 1000.0f,
        float parent_height = 1000.0f,
        PropertyMap inherited_values = {});

    static bool matches(
        const std::shared_ptr<Node>& node,
        const Selector& selector,
        const std::shared_ptr<Node>& hovered_node,
        const std::shared_ptr<Node>& focused_node,
        const std::shared_ptr<Node>& active_node);

    static bool has_layout_affecting_animations();

private:
    static std::shared_ptr<StyledNode> build_style_tree_recursive(
        const std::shared_ptr<Node>& root,
        const StyleSheet& stylesheet,
        const std::shared_ptr<Node>& hovered_node,
        const std::shared_ptr<Node>& focused_node,
        const std::shared_ptr<Node>& active_node,
        double current_time,
        float parent_width,
        float parent_height,
        const PropertyMap& inherited_values);
};

} // namespace linweb
