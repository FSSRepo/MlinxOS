#include "scripting/layout_bindings.h"
#include "app/app_state.h"
#include "scripting/dom_bindings.h"
#include "scripting/js_engine.h"
#include "utils/scroll_manager.h"
#include <algorithm>

using namespace linweb;

// The layout box of a DOM element, found by the element's address as JS sees it (its _addr).
static std::shared_ptr<LayoutBox> find_box_by_address(const std::shared_ptr<LayoutBox>& box, const Node* raw) {
    if (!box) return nullptr;
    if (box->styled_node && box->styled_node->node.get() == raw) return box;
    for (const auto& child : box->children) {
        if (auto found = find_box_by_address(child, raw)) return found;
    }
    return nullptr;
}

// Forces the style and layout a script left pending, as reading a geometry property does in a browser.
static void flush_pending_layout() {
    if (g_flush_layout) g_flush_layout();
}

// Border box of the element in viewport coordinates (its ancestors' scroll moves it) and its content box size.
struct ElementMetrics {
    float left = 0, top = 0, width = 0, height = 0, client_width = 0, client_height = 0;
    bool has_box = false;
};

static ElementMetrics metrics_of(const std::shared_ptr<LayoutBox>& box) {
    ElementMetrics metrics;
    if (!box) return metrics;
    const Dimensions& d = box->dimensions;
    metrics.left = d.content.x - d.padding.left - d.border.left;
    metrics.top = d.content.y - d.padding.top - d.border.top;
    // the scroll of every scrolling ancestor moves the box up, as the renderer draws it
    for (auto parent = box->parent.lock(); parent; parent = parent->parent.lock()) metrics.top -= parent->scroll_y;
    metrics.width = d.content.width + d.padding.left + d.padding.right + d.border.left + d.border.right;
    metrics.height = d.content.height + d.padding.top + d.padding.bottom + d.border.top + d.border.bottom;
    metrics.client_width = d.content.width + d.padding.left + d.padding.right;
    metrics.client_height = d.content.height + d.padding.top + d.padding.bottom;
    metrics.has_box = true;
    return metrics;
}

// __flushLayout(): rebuilds the pending style and layout now. Nothing happens when no script change is pending.
JSValue js_flush_layout(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    flush_pending_layout();
    return JS_UNDEFINED;
}

// __getBoundingRect(addr) -> [left, top, width, height] of the element's border box, in viewport coordinates.
// An element without a layout box (display:none, or not in the page) has an empty rectangle, as in a browser.
JSValue js_get_bounding_rect(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    flush_pending_layout();

    double rect[4] = {0, 0, 0, 0};
    int64_t addr = 0;
    if (argc >= 1 && !JS_ToInt64Ext(ctx, &addr, argv[0])) {
        ElementMetrics metrics = metrics_of(find_box_by_address(g_layout_tree, reinterpret_cast<const Node*>(addr)));
        rect[0] = metrics.left;
        rect[1] = metrics.top;
        rect[2] = metrics.width;
        rect[3] = metrics.height;
    }
    JSValue result = JS_NewArray(ctx);
    for (uint32_t i = 0; i < 4; ++i) JS_SetPropertyUint32(ctx, result, i, JS_NewFloat64(ctx, rect[i]));
    return result;
}

// __getElementMetrics(addr) -> [offsetWidth, offsetHeight, offsetLeft, offsetTop, clientWidth, clientHeight].
// offsetLeft/offsetTop approximate the border box in viewport coordinates, as the engine has no offsetParent chain.
JSValue js_get_element_metrics(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    flush_pending_layout();

    ElementMetrics metrics;
    int64_t addr = 0;
    if (argc >= 1 && !JS_ToInt64Ext(ctx, &addr, argv[0])) {
        metrics = metrics_of(find_box_by_address(g_layout_tree, reinterpret_cast<const Node*>(addr)));
    }
    double values[6] = {0, 0, 0, 0, 0, 0};
    if (metrics.has_box) {
        values[0] = metrics.width;
        values[1] = metrics.height;
        values[2] = metrics.left;
        values[3] = metrics.top;
        values[4] = metrics.client_width;
        values[5] = metrics.client_height;
    }
    JSValue result = JS_NewArray(ctx);
    for (uint32_t i = 0; i < 6; ++i) JS_SetPropertyUint32(ctx, result, i, JS_NewFloat64(ctx, values[i]));
    return result;
}

// __getScrollTop(addr): the vertical scroll the box has now. A box that is not laid out falls back to the saved state.
JSValue js_get_scroll_top(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_NewFloat64(ctx, 0);
    int64_t addr = 0;
    if (JS_ToInt64Ext(ctx, &addr, argv[0])) return JS_NewFloat64(ctx, 0);

    Node* raw = reinterpret_cast<Node*>(addr);
    if (auto box = find_box_by_address(g_layout_tree, raw)) return JS_NewFloat64(ctx, box->scroll_y);

    auto node = find_dom_node_by_address(raw);
    if (node) {
        auto it = g_scroll_states.find(node);
        if (it != g_scroll_states.end()) return JS_NewFloat64(ctx, it->second);
    }
    return JS_NewFloat64(ctx, 0);
}

// __setScrollTop(addr, value): moves the content of the box like the wheel does, clamped to 0..max.
JSValue js_set_scroll_top(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 2) return JS_UNDEFINED;

    int64_t addr = 0;
    double requested = 0;
    if (JS_ToInt64Ext(ctx, &addr, argv[0])) return JS_UNDEFINED;
    if (JS_ToFloat64(ctx, &requested, argv[1])) return JS_UNDEFINED;

    Node* raw = reinterpret_cast<Node*>(addr);
    auto node = find_dom_node_by_address(raw);
    if (!node) return JS_UNDEFINED;

    // the content height of a container this script just built or restyled is only known after the rebuild
    flush_pending_layout();

    auto box = find_box_by_address(g_layout_tree, raw);
    if (!box) return JS_UNDEFINED; // display:none or not in the page: there is nothing to move

    float value = std::max(0.0f, std::min(static_cast<float>(requested), max_scroll_of(box, g_viewport_height)));
    box->scroll_y = value;
    // the wheel position survives a rebuild from the same store, so a programmatic scroll has to use it too
    g_scroll_states[node] = value;
    return JS_UNDEFINED;
}

void install_layout_bindings(JSEngine& js)
{
    js.add_function("__flushLayout", js_flush_layout);
    js.add_function("__getBoundingRect", js_get_bounding_rect);
    js.add_function("__getElementMetrics", js_get_element_metrics);
    js.add_function("__getScrollTop", js_get_scroll_top);
    js.add_function("__setScrollTop", js_set_scroll_top);
    js.execute(R"JS(
document._wrappers.push((node) => ({
  get scrollTop() { return __getScrollTop(node._addr); },
  set scrollTop(value) { __setScrollTop(node._addr, value); },
  get offsetWidth() { __flushLayout(); return __getElementMetrics(node._addr)[0]; },
  get offsetHeight() { __flushLayout(); return __getElementMetrics(node._addr)[1]; },
  get offsetLeft() { __flushLayout(); return __getElementMetrics(node._addr)[2]; },
  get offsetTop() { __flushLayout(); return __getElementMetrics(node._addr)[3]; },
  get clientWidth() { __flushLayout(); return __getElementMetrics(node._addr)[4]; },
  get clientHeight() { __flushLayout(); return __getElementMetrics(node._addr)[5]; },
  getBoundingClientRect: () => {
    __flushLayout();
    const r = __getBoundingRect(node._addr);
    return { x: r[0], y: r[1], left: r[0], top: r[1], width: r[2], height: r[3], right: r[0] + r[2], bottom: r[1] + r[3] };
  }
}));
)JS");
}
