#include "gl_wrapper.h"
#include <iostream>
#include <chrono>
#include <iomanip>
#include <map>
#include <cstdio>

#include "core/dom.h"
#include "core/dom_helpers.h"
#include "core/navigator.h"
#include "parser/html/html_parser.h"
#include "parser/css/css_parser.h"
#include "style/style_engine.h"
#include "style/animation_engine.h"
#include "layout/layout_engine.h"
#include "renderer/renderer.h"
#include "scripting/js_engine.h"
#include "scripting/dom_bindings.h"
#include "scripting/style_bindings.h"
#include "scripting/console_bindings.h"
#include "utils/file_utils.h"
#include "utils/dom_traversal.h"
#include "utils/layout_traversal.h"
#include "utils/scroll_manager.h"
#include "platform/window.h"
#include "platform/keyboard_handler.h"
#include "platform/scroll_handler.h"
#include "platform/mouse_handler.h"
#include "debug/debug_visualizer.h"
#include "debug/node_tree_view.h"

namespace linweb {

// Global state
std::shared_ptr<Node> g_dom_root;
std::shared_ptr<StyledNode> g_style_tree;
std::shared_ptr<LayoutBox> g_layout_tree;
std::shared_ptr<StyledNode> g_hovered_node;
std::shared_ptr<Node> g_focused_node;
std::shared_ptr<Node> g_active_node;
std::map<std::shared_ptr<Node>, float> g_scroll_states;
std::vector<std::shared_ptr<Node>> g_detached_nodes;
bool g_dom_dirty = false;
bool g_show_node_tree = false;
bool g_debug_mode_active = false;
std::shared_ptr<Node> g_debug_hovered_node = nullptr;
JSEngine* g_js_engine = nullptr;
float g_scroll_speed = 40.0f;

} // namespace linweb

using namespace linweb;

void update_animations_on_tree(const std::shared_ptr<StyledNode>& node, const StyleSheet& stylesheet, double current_time, float w, float h) {
    if (!node) return;
    StyleEngine::build_style_tree(node->node, stylesheet, g_hovered_node ? g_hovered_node->node : nullptr, g_focused_node, g_active_node, current_time, w, h);
    for (const auto& child : node->children) {
        update_animations_on_tree(child, stylesheet, current_time, w, h);
    }
}

int main(int argc, char** argv) {
    DebugType debug_mode = DebugType::None;
    std::string html_file = "assets/pages/index.html";
    bool file_specified = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--debug") {
            debug_mode = DebugType::Element;
        } else if (arg == "--debug=element") {
            debug_mode = DebugType::Element;
        } else if (arg == "--debug=text") {
            debug_mode = DebugType::Text;
        } else if (arg == "--debug=dimens") {
            debug_mode = DebugType::Dimens;
        } else if (arg == "--debug=anim") {
            debug_mode = DebugType::Anim;
        } else if (!file_specified) {
            html_file = arg;
            file_specified = true;
            if (!file_exists(html_file)) {
                std::string alt_path = "assets/pages/" + html_file;
                if (file_exists(alt_path)) {
                    html_file = alt_path;
                } else {
                    std::string exe_relative = get_executable_dir() + "/" + html_file;
                    if (file_exists(exe_relative)) {
                        html_file = exe_relative;
                    }
                }
            }
        }
    }

    // 1. Initialize Window
    Window window;
    if (!window.init(800, 600, "Linweb")) {
        return -1;
    }
    window.enable_blend();

    // 2. Initialize Renderer
    Renderer::init();
    Renderer::set_debug_mode(debug_mode);

    // 3. Setup Input Handlers
    KeyboardHandler keyboard_handler;
    keyboard_handler.setup(&window);
    ScrollHandler scroll_handler;
    scroll_handler.setup(&window);
    MouseHandler mouse_handler;
    mouse_handler.setup(&window);

    // 4. Load HTML
    auto start_time = std::chrono::high_resolution_clock::now();

    std::string html_base_dir = get_directory(html_file);

    std::string html = read_file(html_file);
    if (html.empty()) {
        std::cerr << "Could not load " << html_file << std::endl;
        return -1;
    }

    // 5. Parse HTML
    auto html_start = std::chrono::high_resolution_clock::now();
    auto dom = HTMLParser::parse(html);
    g_dom_root = dom;
    auto html_end = std::chrono::high_resolution_clock::now();
    
    // Resolve resource paths in the DOM (img src, etc.)
    {
        std::vector<std::shared_ptr<ElementNode>> imgs;
        find_tags(dom, "img", imgs);
        for (auto& img : imgs) {
            if (img->data.attributes.count("src")) {
                img->data.attributes["src"] = resolve_resource_path(html_base_dir, img->data.attributes["src"]);
            }
        }
    }

    // 6. Load Resources (CSS)
    auto resources_start = std::chrono::high_resolution_clock::now();
    std::string css =
        "body { color: black; }";

    std::vector<std::shared_ptr<ElementNode>> links;
    find_tags(dom, "link", links);
    for (auto& link : links) {
        if (link->data.attributes.count("rel") && link->data.attributes.at("rel") == "stylesheet") {
            if (link->data.attributes.count("href")) {
                std::string css_path = resolve_resource_path(html_base_dir, link->data.attributes.at("href"));
                css += read_file(css_path) + "\n";
            }
        }
    }

    std::vector<std::shared_ptr<ElementNode>> style_tags;
    find_tags(dom, "style", style_tags);
    for (auto& style_tag : style_tags) {
        for (auto& child : style_tag->children) {
            if (child->type() == NodeType::Text) {
                css += std::static_pointer_cast<TextNode>(child)->data + "\n";
            }
        }
    }
    auto resources_end = std::chrono::high_resolution_clock::now();

    auto css_start = std::chrono::high_resolution_clock::now();
    auto stylesheet = CSSParser::parse(css);
    auto css_end = std::chrono::high_resolution_clock::now();

    // 7. Setup viewport
    int fbw = 0, fbh = 0;
    window.get_framebuffer_size(fbw, fbh);
    Renderer::set_viewport(fbw, fbh);

    Dimensions viewport;
    viewport.content = {0, 0, static_cast<float>(fbw), static_cast<float>(fbh)};

    // 8. Style & Layout
    auto style_start = std::chrono::high_resolution_clock::now();
    auto style_tree = StyleEngine::build_style_tree(dom, stylesheet, nullptr, nullptr, nullptr, get_platform_time(), viewport.content.width, viewport.content.height);
    auto style_end = std::chrono::high_resolution_clock::now();

    g_style_tree = style_tree;

    auto layout_start = std::chrono::high_resolution_clock::now();
    auto layout_tree = LayoutEngine::build_layout_tree(style_tree);

    if (layout_tree) {
        apply_scroll_states(layout_tree);
        layout_tree->layout(viewport);
    }
    auto layout_end = std::chrono::high_resolution_clock::now();

    g_layout_tree = layout_tree;

    // 9. Print Information
    auto end_time = std::chrono::high_resolution_clock::now();
    auto total_duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count() / 1000.0;

    std::cout << "\n" << std::string(40, '=') << "\n";
    std::cout << "  LINWEB PERFORMANCE & INFO\n";
    std::cout << std::string(40, '=') << "\n";

    auto print_step = [](const std::string& name, auto start, auto end, size_t info_val, const std::string& info_label) {
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count() / 1000.0;
        std::cout << std::left << std::setw(20) << name << ": "
                  << std::right << std::setw(8) << std::fixed << std::setprecision(3) << duration << " ms | "
                  << info_val << " " << info_label << "\n";
    };

    print_step("HTML Parsing", html_start, html_end, count_dom_nodes(dom), "nodes");
    print_step("Resources Loading", resources_start, resources_end, links.size() + style_tags.size(), "styles");
    print_step("CSS Parsing", css_start, css_end, stylesheet.rules.size(), "rules");
    print_step("Style Tree", style_start, style_end, count_dom_nodes(style_tree->node), "styled nodes");
    print_step("Layout Tree", layout_start, layout_end, count_dom_nodes(layout_tree->styled_node->node), "boxes");

    std::cout << std::string(40, '-') << "\n";
    std::cout << std::left << std::setw(20) << "Total Pipeline" << ": "
              << std::right << std::setw(8) << total_duration << " ms\n";
    std::cout << std::string(40, '=') << "\n\n";

    // 10. JS Engine
    JSEngine js;
    g_js_engine = &js;
    keyboard_handler.set_js_engine(g_js_engine);

    js.add_function("print", js_console_log);
    js.add_function("__setStyleProperty", js_set_style_property);
    js.add_function("__setStylePropertyById", js_set_style_property_by_id);
    js.add_function("__setStylePropertyByAddr", js_set_style_property_by_addr);
    js.add_function("__querySelectorAll", js_query_selector_all);
    js.add_function("__getElementById", js_get_element_by_id);
    js.add_function("__createElement", js_create_element);
    js.add_function("__appendChild", js_append_child);
    js.add_function("__setTextContent", js_set_text_content);
    js.add_function("__getTextContent", js_get_text_content);
    js.add_function("__setInnerHTML", js_set_inner_html);
    js.add_function("__getInnerHTML", js_get_inner_html);
    js.add_function("__getClassName", js_get_class_name);
    js.add_function("__setClassName", js_set_class_name);
    js.add_function("__addClassByAddr", js_add_class_by_addr);
    js.add_function("__removeClassByAddr", js_remove_class_by_addr);
    js.add_function("__toggleClassByAddr", js_toggle_class_by_addr);
    js.add_function("__getAttribute", js_get_attribute);
    js.add_function("__querySelectorAllByAddr", js_query_selector_all_by_addr);

    // Inject browser-like API
    js.execute(
        "const console = {"
        "  log: (...args) => print(...args),"
        "  error: (...args) => print('ERROR:', ...args),"
        "  warn: (...args) => print('WARN:', ...args),"
        "  info: (...args) => print('INFO:', ...args)"
        "};"
        "const window = {"
        "  _listeners: {},"
        "  _raf_callbacks: [],"
        "  _timeouts: [],"
        "  dispatchEvent: (targetId, type, data) => {"
        "    const key = targetId + ':' + type;"
        "    if (window._listeners[key]) {"
        "      window._listeners[key].forEach(cb => cb(data));"
        "    }"
        "  },"
        "  addEventListener: (type, callback) => {"
        "    const key = 'window:' + type;"
        "    if (!window._listeners[key]) window._listeners[key] = [];"
        "    window._listeners[key].push(function(evt) { return callback.call(window, evt); });"
        "  },"
        "  requestAnimationFrame: (callback) => {"
        "    window._raf_callbacks.push(callback);"
        "  },"
        "  setTimeout: (callback, delay) => {"
        "    window._timeouts.push({ cb: callback, time: Date.now() + (delay || 0) });"
        "  },"
        "  _run_raf: () => {"
        "    const callbacks = window._raf_callbacks;"
        "    window._raf_callbacks = [];"
        "    callbacks.forEach(cb => cb());"
        "    const now = Date.now();"
        "    window._timeouts = window._timeouts.filter(t => {"
        "      if (now >= t.time) { t.cb(); return false; }"
        "      return true;"
        "    });"
        "  }"
        "};"
        "const requestAnimationFrame = window.requestAnimationFrame;"
        "const setTimeout = window.setTimeout;"
        "const document = {"
        "  _wrap: (node) => ({"
        "    _addr: node._addr,"
        "    tagName: node.tagName,"
        "    getAttribute: (name) => __getAttribute(node._addr, name),"
        "    get className() { return __getClassName(node._addr); },"
        "    set className(val) { __setClassName(node._addr, val); },"
        "    get textContent() { return __getTextContent(node._addr); },"
        "    set textContent(val) { __setTextContent(node._addr, val); },"
        "    get innerText() { return __getTextContent(node._addr); },"
        "    set innerText(val) { __setTextContent(node._addr, val); },"
        "    get innerHTML() { return __getInnerHTML(node._addr); },"
        "    set innerHTML(val) { __setInnerHTML(node._addr, val); },"
        "    style: {"
        "      set backgroundColor(val) { __setStylePropertyByAddr(node._addr, 'background-color', val); },"
        "      set color(val) { __setStylePropertyByAddr(node._addr, 'color', val); },"
        "      set display(val) { __setStylePropertyByAddr(node._addr, 'display', val); },"
        "      set opacity(val) { __setStylePropertyByAddr(node._addr, 'opacity', val); },"
        "      set transform(val) { __setStylePropertyByAddr(node._addr, 'transform', val); }"
        "    },"
        "    classList: {"
        "      add: (...args) => __addClassByAddr(node._addr, ...args),"
        "      remove: (...args) => __removeClassByAddr(node._addr, ...args),"
        "      toggle: (cls, force) => __toggleClassByAddr(node._addr, cls, force)"
        "    },"
        "    addEventListener: (type, callback) => {"
        "      const key = node._addr + ':' + type;"
        "      if (!window._listeners[key]) window._listeners[key] = [];"
        "      const self = document._wrap(node);"
        "      window._listeners[key].push(function(evt) { return callback.call(self, evt); });"
        "    },"
        "    appendChild: (child) => {"
        "      __appendChild(node._addr, child._addr);"
        "      return child;"
        "    },"
        "    querySelectorAll: (selector) => {"
        "      return __querySelectorAllByAddr(node._addr, selector).map(n => document._wrap(n));"
        "    },"
        "    querySelector: (selector) => {"
        "      const all = __querySelectorAllByAddr(node._addr, selector).map(n => document._wrap(n));"
        "      return all.length > 0 ? all[0] : null;"
        "    }"
        "  }),"
        "  createElement: (tagName) => {"
        "    const node = __createElement(tagName);"
        "    return document._wrap(node);"
        "  },"
        "  getElementById: (id) => {"
        "    const node = __getElementById(id);"
        "    return node ? document._wrap(node) : null;"
        "  },"
        "  getElementsByClassName: (name) => {"
        "    return __querySelectorAll('.' + name).map(n => document._wrap(n));"
        "  },"
        "  querySelectorAll: (selector) => {"
        "    return __querySelectorAll(selector).map(n => document._wrap(n));"
        "  },"
        "  querySelector: (selector) => {"
        "    const all = document.querySelectorAll(selector);"
        "    return all.length > 0 ? all[0] : null;"
        "  },"
        "  addEventListener: (type, callback) => {"
        "    const key = 'document:' + type;"
        "    if (!window._listeners[key]) window._listeners[key] = [];"
        "    window._listeners[key].push(function(evt) { return callback.call(document, evt); });"
        "  },"
        "  get body() { return document.querySelector('body'); },"
        "  get documentElement() { return document.querySelector('html'); }"
        "};"
    );

    // Load scripts
    std::vector<std::shared_ptr<ElementNode>> scripts;
    find_tags(dom, "script", scripts);
    for (auto& script : scripts) {
        if (script->data.attributes.count("src")) {
            std::string js_path = resolve_resource_path(html_base_dir, script->data.attributes.at("src"));
            std::string js_code = read_file(js_path);
            if (!js_code.empty()) js.execute(js_code);
        } else {
            std::string js_code;
            for (const auto& child : script->children) {
                if (child->type() == NodeType::Text) {
                    js_code += std::static_pointer_cast<TextNode>(child)->data;
                }
            }
            if (!js_code.empty()) js.execute(js_code);
        }
    }

    js.execute("window.dispatchEvent('document', 'DOMContentLoaded', {});");

    // 11. Main Loop
    int last_fbw = fbw;
    int last_fbh = fbh;
    std::shared_ptr<Node> prev_hovered_dom_node;

    while (!window.should_close()) {
        // Handle Resize
        window.get_framebuffer_size(fbw, fbh);
        if (fbw != last_fbw || fbh != last_fbh) {
            viewport.content = {0, 0, static_cast<float>(fbw), static_cast<float>(fbh)};
            Renderer::set_viewport(fbw, fbh);
            last_fbw = fbw;
            last_fbh = fbh;
        }

        // Handle Hover & Click
        mouse_handler.handle_hover_and_click(&window, g_js_engine);
        auto& mouse_state = mouse_handler.get_state();
        auto current_hovered_dom_node = mouse_state.current_hovered_dom_node;

        js.execute("if (window._run_raf) window._run_raf();");

        bool viewport_changed = (fbw != last_fbw || fbh != last_fbh);
        bool input_changed = (prev_hovered_dom_node != current_hovered_dom_node) || mouse_state.had_click_event;
        bool layout_affecting_animations = StyleEngine::has_layout_affecting_animations();
        bool needs_full_rebuild = viewport_changed || input_changed || layout_affecting_animations || g_dom_dirty || !g_style_tree || !g_layout_tree;

        g_anim_debug_entries.clear();

        if (needs_full_rebuild) {
            g_dom_dirty = false;
            g_style_tree = StyleEngine::build_style_tree(dom, stylesheet, current_hovered_dom_node, g_focused_node, g_active_node, get_platform_time(), viewport.content.width, viewport.content.height);
            g_layout_tree = LayoutEngine::build_layout_tree(g_style_tree);
            if (g_layout_tree) {
                apply_scroll_states(g_layout_tree);
                g_layout_tree->layout(viewport);
            }
        } else {
            update_animations_on_tree(g_style_tree, stylesheet, get_platform_time(), viewport.content.width, viewport.content.height);
        }
        prev_hovered_dom_node = current_hovered_dom_node;

        // Render
        glClearColor(1.0f, 1.0f, 1.0f, 1.0f);

        if (g_layout_tree) {
            if (g_debug_mode_active) {
                if (g_show_node_tree) {
                    float temp_y = 30.0f;
                    g_debug_hovered_node = nullptr;
                    if (debug_mode == DebugType::Dimens) {
                        update_dimens_tree_hover(g_layout_tree, temp_y, 0, mouse_state.px, mouse_state.py, g_debug_hovered_node);
                    } else {
                        update_node_tree_hover(g_layout_tree, temp_y, 0, mouse_state.px, mouse_state.py, g_debug_hovered_node);
                    }
                }

                auto box = find_box_for_node(g_layout_tree, g_debug_hovered_node);
                Renderer::set_debug_box(box);
            }

            Renderer::render(g_layout_tree);

            if (debug_mode == DebugType::Anim && !g_anim_debug_entries.empty()) {
                float panel_x = 10.0f, panel_y = 10.0f;
                float line_h = 18.0f;
                float panel_w = 800.0f;
                int total_lines = 1;
                for (const auto& entry : g_anim_debug_entries) {
                    total_lines += 3;
                    total_lines += (int)entry.animated_props.size();
                }
                float panel_h = total_lines * line_h + 10.0f;
                Renderer::draw_rect(panel_x, panel_y, panel_w, panel_h, 0.0f, 0.0f, 0.0f, 0.75f, 6.0f);

                float text_x = panel_x + 8.0f;
                float text_y = panel_y + 6.0f;
                Renderer::draw_text("=== ANIMATIONS (" + std::to_string(g_anim_debug_entries.size()) + " active) ===", text_x, text_y, 14.0f, 0.3f, 0.8f, 1.0f, 1.0f, true);
                text_y += line_h;

                for (const auto& entry : g_anim_debug_entries) {
                    std::string node_label = "<" + entry.node_tag + ">";
                    if (!entry.node_id.empty()) node_label += " #" + entry.node_id;

                    float pct = entry.t * 100.0f;
                    float eased_pct = entry.eased_t * 100.0f;
                    char time_buf[64];
                    snprintf(time_buf, sizeof(time_buf), "%.2fs", entry.elapsed);

                    Renderer::draw_text(node_label, text_x, text_y, 14.0f, 1.0f, 1.0f, 1.0f, 1.0f, true);
                    text_y += line_h;

                    std::string progress = std::string("  [") + entry.anim_name + "] " + time_buf + "  linear: " +
                        std::to_string((int)pct) + "%  eased: " + std::to_string((int)eased_pct) + "%  (" + entry.timing_function + ")";
                    Renderer::draw_text(progress, text_x, text_y, 13.0f, 0.6f, 0.9f, 0.6f, 1.0f);
                    text_y += line_h;

                    for (const auto& [prop, val] : entry.animated_props) {
                        std::string prop_line = "    " + prop + ": " + val.actual + "  (start: " + val.start + ", end: " + val.end + ")";
                        Renderer::draw_text(prop_line, text_x, text_y, 13.0f, 0.8f, 0.8f, 0.8f, 1.0f, false, 0.0f, 0.0f, false);
                        text_y += line_h;
                    }
                }
            }

            if (g_show_node_tree) {
                float y_offset = 30.0f;
                if (debug_mode == DebugType::Dimens) {
                    render_dimens_tree(g_layout_tree, y_offset, 0, mouse_state.px, mouse_state.py);
                } else {
                    render_node_tree(g_layout_tree, y_offset, 0, mouse_state.px, mouse_state.py);
                }
            }
        }

        window.swap_buffers();
        window.poll_events();
    }

    return 0;
}
