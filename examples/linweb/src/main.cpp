#include "gl_wrapper.h"
#include <iostream>
#include <thread>
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
#include "style/transform_parser.h"
#include "style/transition_engine.h"
#include "layout/layout_engine.h"
#include "system/perf.h"
#include "renderer/renderer.h"
#include "renderer/custom_font.h"
#include "app/app_state.h"
#include "app/js_api.h"
#include "scripting/js_engine.h"
#include "media/video/video_player.h"
#include "system/network.h"
#include "system/nodelite.h"
#include "system/audio.h"
#include "utils/file_utils.h"
#include "utils/dom_traversal.h"
#include "utils/layout_traversal.h"
#include "utils/scroll_manager.h"
#include "platform/window.h"
#include "platform/keyboard_handler.h"
#include "platform/gamepad_input.h"
#include "forms/form_bindings.h"
#include "forms/form_controls.h"
#include "platform/scroll_handler.h"
#include "platform/mouse_handler.h"
#include "debug/debug_visualizer.h"
#include "debug/node_tree_view.h"

using namespace linweb;

void update_animations_on_tree(const std::shared_ptr<StyledNode>& node, const StyleSheet& stylesheet, double current_time, float w, float h) {
    if (!node) return;
    TransitionEngine::apply_transitions(node, current_time, w, h);
    AnimationEngine::apply_animations(node, stylesheet, current_time);
    // the transform comes from the (possibly animated) value, as in build_style_tree
    const std::string transform_str = node->value("transform");
    if (!transform_str.empty()) {
        node->transform = TransformParser::parse_transform(transform_str);
        const std::string origin_str = node->value("transform-origin");
        if (!origin_str.empty()) TransformParser::parse_transform_origin(origin_str, node->transform);
    }
    // `translate` is re-read each frame; the transform it adds to starts again from the transform property
    if (!node->value("translate").empty() && transform_str.empty()) node->transform = Transform();
    TransformParser::apply_translate_property(node->value("translate"), node->transform);
    for (const auto& child : node->children) {
        update_animations_on_tree(child, stylesheet, current_time, w, h);
    }
}

int main(int argc, char** argv) {
    // MP3 playback runs in a child copy of this executable (see system/audio.cpp).
    if (linweb::audio::is_helper_invocation(argc, argv)) return linweb::audio::helper_main(argc, argv);

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

        // <video src="..." autoplay loop>: the first video element that asks for it starts playing
        std::vector<std::shared_ptr<ElementNode>> videos;
        find_tags(dom, "video", videos);
        bool autoplay_started = false;
        for (auto& element : videos) {
            if (!element->data.attributes.count("src")) continue;
            element->data.attributes["src"] = resolve_resource_path(html_base_dir, element->data.attributes["src"]);
            if (!autoplay_started && element->data.attributes.count("autoplay")) {
                autoplay_started = true;
                linweb::video::Result started = linweb::video::play(element->data.attributes["src"], -1,
                                                                    element->data.attributes.count("loop") > 0);
                if (!started.ok) std::cerr << "video: " << started.error << std::endl;
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

    for (const auto& face : stylesheet.font_faces) {
        if (!face.family.empty() && !face.source.empty()) CustomFonts::add(face.family, face.weight, face.style == "italic" || face.style == "oblique", face.source);
    }

    // 7. Setup viewport
    int fbw = 0, fbh = 0;
    window.get_framebuffer_size(fbw, fbh);
    Renderer::set_viewport(fbw, fbh);

    Dimensions viewport;
    viewport.content = {0, 0, static_cast<float>(fbw), static_cast<float>(fbh)};
    g_viewport_width = viewport.content.width;
    g_viewport_height = viewport.content.height;

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

    install_js_api(js, dom);

    // A measure read by a script (offsetWidth, getBoundingClientRect) first finishes the style and layout the script
    // changed, as a browser does. The stylesheet, the viewport and the hovered node live here, so the bindings ask
    // through this.
    std::shared_ptr<Node> current_hovered_dom_node;
    // The hover, the focus and the active node of the last style build: while they hold, what the selectors gave for the
    // nodes no script touched still holds, and a build that only moves animations or follows a script change near some
    // nodes matches no selector again for the rest.
    const Node* last_hovered = nullptr;
    const Node* last_focused = nullptr;
    const Node* last_active = nullptr;
    auto same_inputs = [&]() {
        return current_hovered_dom_node.get() == last_hovered && g_focused_node.get() == last_focused && g_active_node.get() == last_active;
    };
    g_flush_layout = [&]() {
        absorb_forms_dirty();
        if (!g_dom_dirty && g_style_tree && g_layout_tree) return;
        StyleEngine::set_reuse_matched_styles(g_style_tree && same_inputs() && StyleEngine::matched_scoped());
        last_hovered = current_hovered_dom_node.get();
        last_focused = g_focused_node.get();
        last_active = g_active_node.get();
        g_dom_dirty = false;
        g_style_tree = StyleEngine::build_style_tree(dom, stylesheet, current_hovered_dom_node, g_focused_node, g_active_node, get_platform_time(), viewport.content.width, viewport.content.height);
        g_layout_tree = LayoutEngine::build_layout_tree(g_style_tree);
        if (g_layout_tree) {
            apply_scroll_states(g_layout_tree);
            g_layout_tree->layout(viewport);
        }
    };

    run_page_scripts(js, dom, html_base_dir);

    // 11. Main Loop
    int last_fbw = fbw;
    int last_fbh = fbh;
    std::shared_ptr<Node> prev_hovered_dom_node;

    // Frame pacing. The loop has no vsync: left alone it draws as fast as it can even when nothing changes, which in a
    // virtual machine takes the CPU the transitions need. A frame is drawn when something changed (or moves), at most
    // every FRAME_S; a still page is redrawn every IDLE_REDRAW_S, which keeps the text caret blinking and refreshes the
    // window after it was covered.
    const double FRAME_S = 1.0 / 60.0;
    const double IDLE_REDRAW_S = 0.1;
    double last_draw = 0.0;
    double last_mouse_x = -1.0, last_mouse_y = -1.0;
    std::map<std::shared_ptr<Node>, float> last_scroll_states;
    int last_video_w = 0, last_video_h = 0;

    while (!window.should_close()) {
        const double frame_start = get_platform_time();
        // Handle Resize
        window.get_framebuffer_size(fbw, fbh);
        const bool viewport_resized = (fbw != last_fbw || fbh != last_fbh);
        if (viewport_resized) {
            viewport.content = {0, 0, static_cast<float>(fbw), static_cast<float>(fbh)};
            g_viewport_width = viewport.content.width;
            g_viewport_height = viewport.content.height;
            Renderer::set_viewport(fbw, fbh);
            last_fbw = fbw;
            last_fbh = fbh;
        }

        // Handle Hover & Click
        const double script_t0 = get_platform_time();
        mouse_handler.handle_hover_and_click(&window, g_js_engine);
        auto& mouse_state = mouse_handler.get_state();
        current_hovered_dom_node = mouse_state.current_hovered_dom_node;

        js.execute("if (window._run_raf) window._run_raf();");
        const double script_ms = (get_platform_time() - script_t0) * 1000.0;

        // the picture of a video decides the size of its box: the layout kept from earlier frames is not valid any more
        {
            int video_w = 0, video_h = 0;
            if (!video::current_size(video_w, video_h)) video_w = video_h = 0;
            if (video_w != last_video_w || video_h != last_video_h) {
                last_video_w = video_w;
                last_video_h = video_h;
                ++g_layout_epoch;
                g_dom_dirty = true;
            }
        }

        absorb_forms_dirty();

        bool viewport_changed = viewport_resized;
        bool input_changed = (prev_hovered_dom_node != current_hovered_dom_node) || mouse_state.had_click_event;
        bool layout_affecting_animations = StyleEngine::has_layout_affecting_animations();
        bool needs_full_rebuild = viewport_changed || input_changed || layout_affecting_animations || g_dom_dirty || !g_style_tree || !g_layout_tree;

        g_anim_debug_entries.clear();

        const bool animating = StyleEngine::has_running_animations();

        // Phase timings for F3 (and R2 on the gamepad): style, layout, paint and the whole frame.
        const double style_t0 = get_platform_time();
        double style_ms = 0.0;
        double layout_ms = 0.0;
        double paint_ms = 0.0;
        g_layout_calls = 0;
        g_layout_hits = 0;
        clear_layout_call_breakdown();

        // a rebuild that only moves a running transition or animation (same as app/frame.cpp of the original): it is not
        // counted as a full rebuild by the profiler
        bool animation_only = false;
        if (needs_full_rebuild) {
            // A frame that only moves a running transition matches no selector again: the page, the hover, the focus
            // and the stylesheet are the ones of the last full build, so what the selectors gave still holds. A frame
            // after a script change matches again only the nodes around what the script touched.
            const bool inputs_held = !viewport_changed && !input_changed && same_inputs() && g_style_tree;
            animation_only = layout_affecting_animations && !g_dom_dirty && inputs_held;
            StyleEngine::set_reuse_matched_styles(inputs_held && (layout_affecting_animations || g_dom_dirty) &&
                                                  (!g_dom_dirty || StyleEngine::matched_scoped()));
            last_hovered = current_hovered_dom_node.get();
            last_focused = g_focused_node.get();
            last_active = g_active_node.get();
            g_dom_dirty = false;
            g_style_tree = StyleEngine::build_style_tree(dom, stylesheet, current_hovered_dom_node, g_focused_node, g_active_node, get_platform_time(), viewport.content.width, viewport.content.height);
            style_ms = (get_platform_time() - style_t0) * 1000.0;
            const double layout_t0 = get_platform_time();
            g_layout_tree = LayoutEngine::build_layout_tree(g_style_tree);
            if (g_layout_tree) {
                apply_scroll_states(g_layout_tree);
                g_layout_tree->layout(viewport);
            }
            layout_ms = (get_platform_time() - layout_t0) * 1000.0;
        } else {
            update_animations_on_tree(g_style_tree, stylesheet, get_platform_time(), viewport.content.width, viewport.content.height);
            style_ms = (get_platform_time() - style_t0) * 1000.0;
        }
        prev_hovered_dom_node = current_hovered_dom_node;

        // Is there anything new to draw? (the scroll position and the mouse are read from what the handlers keep)
        const bool scrolled = g_scroll_states != last_scroll_states;
        const bool mouse_moved = mouse_state.px != last_mouse_x || mouse_state.py != last_mouse_y;
        const bool video_playing = video::status().playing;
        const bool changed = needs_full_rebuild || animating || scrolled || mouse_moved || input_changed || video_playing ||
                             g_debug_mode_active || g_show_node_tree;
        if (!changed && frame_start - last_draw < IDLE_REDRAW_S) {
            window.poll_events();
            std::this_thread::sleep_for(std::chrono::milliseconds(4));
            continue;
        }
        last_scroll_states = g_scroll_states;
        last_mouse_x = mouse_state.px;
        last_mouse_y = mouse_state.py;
        last_draw = frame_start;

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

            const double paint_t0 = get_platform_time();
            Renderer::render(g_layout_tree);
            paint_ms = (get_platform_time() - paint_t0) * 1000.0;

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

        perf::draw_overlay(fbw);
        const double swap_t0 = get_platform_time();
        window.swap_buffers();
        window.poll_events();
        gamepad_input::poll(keyboard_handler);
        const double swap_end = get_platform_time();
        perf::record(script_ms, style_ms, layout_ms, paint_ms, (swap_end - swap_t0) * 1000.0, (swap_end - frame_start) * 1000.0, swap_end,
                     g_layout_calls, g_layout_hits, needs_full_rebuild && !animation_only);

        // no more than FRAME_S per frame: the rest of it is left to the other processes
        const double spent = get_platform_time() - frame_start;
        if (spent < FRAME_S) std::this_thread::sleep_for(std::chrono::duration<double>(FRAME_S - spent));
    }

    linweb::audio::stop();
    linweb::net::webserver_stop();
    linweb::nodelite::stop_all();
    linweb::video::shutdown();
    return 0;
}
