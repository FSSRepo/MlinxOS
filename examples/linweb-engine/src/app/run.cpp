#include "gl_wrapper.h"
#include <algorithm>
#include <cctype>
#include <iostream>
#include <chrono>
#include <map>
#include <thread>
#include <vector>

#include "app/app_state.h"
#include "app/frame.h"
#include "app/js_api.h"
#include "app/module.h"
#include "media/video_player.h"
#include "scripting/media_bindings.h"
#include "renderer/video_surface.h"
#include "app/options.h"
#include "app/scripted_keys.h"
#include "app/startup_report.h"
#include "core/dom.h"
#include "core/dom_helpers.h"
#include "debug/anim_panel.h"
#include "debug/debug_visualizer.h"
#include "debug/fps_counter.h"
#include "debug/profiler.h"
#include "debug/frame_capture.h"
#include "debug/node_tree_view.h"
#include "layout/layout_engine.h"
#include "network/http_client.h"
#include "network/page_downloader.h"
#include "parser/css/css_parser.h"
#include "parser/html/html_parser.h"
#include "platform/keyboard_handler.h"
#include "platform/mouse_handler.h"
#include "platform/scroll_handler.h"
#include "platform/window.h"
#include "renderer/custom_font.h"
#include "renderer/renderer.h"
#include "renderer/texture_manager.h"
#include "scripting/js_engine.h"
#include "style/animation_engine.h"
#include "style/style_engine.h"
#include "utils/dom_traversal.h"
#include "utils/file_utils.h"
#include "utils/layout_traversal.h"
#include "utils/log.h"
#include "utils/memory_files.h"
#include "utils/page_resources.h"
#include "utils/scroll_manager.h"
#if !defined(WESTON_PLATFORM) && !defined(XORG_PLATFORM)
#include <GLFW/glfw3.h>
#endif

using namespace linweb;

int linweb::run(int argc, char** argv) {
    Options options;
    if (!parse_options(argc, argv, options)) return 1;
    g_show_fps = options.show_fps;
    g_show_profiler = options.show_profiler;
    std::string html_file = options.html_file;

    log_event("start", "page " + html_file);

    // 1. Initialize Window
#if !defined(WESTON_PLATFORM) && !defined(XORG_PLATFORM)
    if (!options.screenshot_path.empty()) {
        if (!glfwInit()) return 1;
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    }
#endif
    Window window;
    if (!window.init(options.window_width, options.window_height, "Linweb")) {
        return -1;
    }
    window.enable_blend();

    // 2. Initialize Renderer
    Renderer::init();
    // the command line can cap a picture's longest side and the memory of the large textures (0 keeps the default)
    if (options.max_texture_side > 0) TextureManager::get().set_max_side(options.max_texture_side);
    TextureManager::get().set_budget_mb(options.texture_budget_mb);
    Renderer::set_debug_mode(options.debug_mode);

    // 3. Setup Input Handlers
    KeyboardHandler keyboard_handler;
    keyboard_handler.setup(&window);
    ScrollHandler scroll_handler;
    scroll_handler.setup(&window);
    MouseHandler mouse_handler;
    mouse_handler.setup(&window);

    // 4. Load HTML
    StartupTimes times;
    times.start = StartupTimes::Clock::now();

    if (is_http_url(html_file)) {
        // a URL is downloaded with its files into memory (nothing is written to disk), and the page is loaded from there
        const std::string directory = memory_files::new_directory();
        DownloadSummary summary = download_page(html_file, [&directory](const std::string& name, const std::string& bytes) {
            memory_files::put(directory + "/" + name, bytes);
        });
        log_event("download", html_file + " -> memory (" + std::to_string(summary.downloaded) + " files, " +
                  std::to_string(summary.failed) + " failed)");
        if (summary.html_path.empty()) {
            std::cerr << "Could not download " << html_file << std::endl;
            return -1;
        }
        html_file = directory + "/" + summary.html_path;
    }

    std::string html_base_dir = get_directory(html_file);
    g_html_base_dir = html_base_dir;
    modules_on_page_loaded(html_base_dir);

    std::string html = read_file(html_file);
    if (html.empty()) {
        std::cerr << "Could not load " << html_file << std::endl;
        return -1;
    }

    // a page that does not start with a doctype is laid out in quirks mode
    {
        std::string head = html.substr(0, 512);
        std::transform(head.begin(), head.end(), head.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        g_quirks_mode = head.find("<!doctype") == std::string::npos;
    }

    // 5. Parse HTML
    times.html_start = StartupTimes::Clock::now();
    auto dom = HTMLParser::parse(html);
    g_dom_root = dom;
    log_event("parse", std::to_string(html.size()) + " bytes, " + std::to_string(count_dom_nodes(dom)) + " nodes");
    times.html_end = StartupTimes::Clock::now();

    // Resolve local downloaded resources and expand CSS imports in document order.
    times.resources_start = StartupTimes::Clock::now();
    std::string css = "body { color: black; }" + prepare_page_resources(dom, html_base_dir);
    if (!options.user_css_path.empty()) {
        // the user's stylesheet comes last, so it wins over the page's own rules
        std::string user_css = read_file(options.user_css_path);
        if (user_css.empty()) std::cerr << "Could not read the user stylesheet: " << options.user_css_path << std::endl;
        css += "\n" + user_css;
        log_event("css", "user stylesheet " + options.user_css_path + " (" + std::to_string(user_css.size()) + " bytes)");
    }
    std::vector<std::shared_ptr<ElementNode>> links, style_tags;
    find_tags(dom, "link", links);
    find_tags(dom, "style", style_tags);
    times.resources_end = StartupTimes::Clock::now();

    times.css_start = StartupTimes::Clock::now();
    int initial_width = 800, initial_height = 600;
    window.get_framebuffer_size(initial_width, initial_height);
    auto stylesheet = CSSParser::parse(css, static_cast<float>(initial_width), static_cast<float>(initial_height));
    // every @font-face rule of the page loads its file, so text can be drawn in that family
    for (const auto& face : stylesheet.font_faces) {
        if (!face.family.empty() && !face.source.empty()) CustomFonts::add(face.family, face.weight, face.style == "italic" || face.style == "oblique", face.source);
    }
    times.css_end = StartupTimes::Clock::now();
    log_event("css", "parsed " + std::to_string(stylesheet.rules.size()) + " rules");

    // 7. Setup viewport
    int fbw = 0, fbh = 0;
    window.get_framebuffer_size(fbw, fbh);
    Renderer::set_viewport(fbw, fbh);

    Dimensions viewport;
    viewport.content = {0, 0, static_cast<float>(fbw), static_cast<float>(fbh)};
    g_viewport_width = viewport.content.width;
    g_viewport_height = viewport.content.height;

    // 8. Style & Layout
    times.style_start = StartupTimes::Clock::now();
    auto style_tree = StyleEngine::build_style_tree(dom, stylesheet, nullptr, nullptr, nullptr, get_platform_time(), viewport.content.width, viewport.content.height);
    times.style_end = StartupTimes::Clock::now();

    g_style_tree = style_tree;
    log_event("style", "styled tree built");

    times.layout_start = StartupTimes::Clock::now();
    auto layout_tree = LayoutEngine::build_layout_tree(style_tree);
    log_event("layout-tree", "boxes built");

    if (layout_tree) {
        apply_scroll_states(layout_tree);
        log_event("layout-tree", "running layout");
        layout_tree->layout(viewport);
        log_event("layout-tree", "layout done");
    }
    times.layout_end = StartupTimes::Clock::now();

    g_layout_tree = layout_tree;
    log_event("layout", std::to_string(stylesheet.rules.size()) + " css rules");

    // 9. Print Information
    times.end = StartupTimes::Clock::now();
    StartupCounts counts;
    counts.dom_nodes = count_dom_nodes(dom);
    counts.style_sources = links.size() + style_tags.size();
    counts.css_rules = stylesheet.rules.size();
    counts.styled_nodes = count_dom_nodes(style_tree->node);
    counts.boxes = count_dom_nodes(layout_tree->styled_node->node);
    print_startup_report(times, counts);

    // 10. JS Engine
    JSEngine js;
    g_js_engine = &js;
    keyboard_handler.set_js_engine(g_js_engine);
    install_js_api(js, dom);
    modules_install_js(js);   // the registered modules add their own bindings

    // A measure read by a script (offsetWidth, getBoundingClientRect) first finishes the style and layout the script
    // changed, as a browser does. The stylesheet, the viewport and the hovered node live here, so the bindings ask
    // through this. Reading the hovered node each time keeps the flush in step with the frame.
    std::shared_ptr<Node> current_hovered_dom_node;
    g_flush_layout = [&]() {
        absorb_forms_dirty();
        if (!g_dom_dirty && g_style_tree && g_layout_tree) return;
        rebuild_page(dom, stylesheet, current_hovered_dom_node, viewport, RebuildCause{});
    };

    if (!options.static_mode) run_page_scripts(js, dom, html_base_dir);

    // 11. Main Loop
    int last_fbw = fbw;
    int last_fbh = fbh;
    std::shared_ptr<Node> prev_hovered_dom_node;
    const double screenshot_start = get_platform_time();
    ScriptedKeys scripted_keys(options.scripted_keys);
    FpsCounter fps_counter;

    // Frame pacing. The loop has no vsync: left alone it draws as fast as it can even when nothing changes, which in a
    // virtual machine takes the CPU the transitions need. A frame is drawn when something changed (or moves), at most
    // every FRAME_S; a still page is redrawn every IDLE_REDRAW_S, which keeps the text caret blinking and refreshes the
    // window after it was covered.
    const double FRAME_S = 1.0 / 60.0;
    const double IDLE_REDRAW_S = 0.1;
    double last_draw = 0.0;
    double last_mouse_x = -1.0, last_mouse_y = -1.0;
    std::map<std::shared_ptr<Node>, float> last_scroll_states;

    while (!window.should_close()) {
        const double frame_start = get_platform_time();
        // Handle Resize
        window.get_framebuffer_size(fbw, fbh);
        bool viewport_changed = (fbw != last_fbw || fbh != last_fbh);
        if (viewport_changed) {
            viewport.content = {0, 0, static_cast<float>(fbw), static_cast<float>(fbh)};
            g_viewport_width = viewport.content.width;
            g_viewport_height = viewport.content.height;
            Renderer::set_viewport(fbw, fbh);
            stylesheet = CSSParser::parse(css, static_cast<float>(fbw), static_cast<float>(fbh));
            last_fbw = fbw;
            last_fbh = fbh;
        }

        std::unique_ptr<ProfScope> script_scope = std::make_unique<ProfScope>(ProfSection::Script);
        scripted_keys.update(g_js_engine);

        modules_on_frame(keyboard_handler);   // a module polls its devices and may inject keys

        // Handle Hover & Click
        mouse_handler.handle_hover_and_click(&window, g_js_engine);
        auto& mouse_state = mouse_handler.get_state();
        current_hovered_dom_node = mouse_state.current_hovered_dom_node;

        if (!options.static_mode) js.execute("if (window._run_raf) window._run_raf();");
        script_scope.reset();

        absorb_forms_dirty();
        RebuildCause cause;
        cause.viewport_changed = viewport_changed;
        cause.input_changed = (prev_hovered_dom_node != current_hovered_dom_node) || mouse_state.had_click_event;
        cause.layout_animations = StyleEngine::has_layout_affecting_animations();
        bool needs_full_rebuild = cause.viewport_changed || cause.input_changed || cause.layout_animations || g_dom_dirty || !g_style_tree || !g_layout_tree;

        g_anim_debug_entries.clear();

        if (needs_full_rebuild) {
            rebuild_page(dom, stylesheet, current_hovered_dom_node, viewport, cause);
        } else {
            ProfScope animations(ProfSection::Anim);
            update_animations_on_tree(g_style_tree, stylesheet, get_platform_time(), viewport.content.width, viewport.content.height);
        }
        prev_hovered_dom_node = current_hovered_dom_node;

        // Is there anything new to draw? (the scroll position and the mouse are read from what the handlers keep)
        const bool scrolled = g_scroll_states != last_scroll_states;
        const bool mouse_moved = mouse_state.px != last_mouse_x || mouse_state.py != last_mouse_y;
        const bool video_playing = video::any_playing();
        // pictures the worker thread decoded become textures here; frames keep coming while some are still loading
        const bool texture_arrived = TextureManager::get().upload_pending();
        const bool textures_loading = TextureManager::get().loading();
        // a shader background that follows the music is drawn on every frame, not only when the page changes
        const bool shader_moving = Renderer::animated_background_visible();
        const bool animating = StyleEngine::has_running_animations();
        const bool changed = needs_full_rebuild || animating || scrolled || mouse_moved || cause.input_changed || video_playing ||
                             shader_moving || texture_arrived || textures_loading || g_debug_mode_active || g_show_node_tree ||
                             !options.screenshot_path.empty() || modules_want_frame();
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

        std::unique_ptr<ProfScope> render_scope = std::make_unique<ProfScope>(ProfSection::Render);
        if (g_layout_tree) {
            if (g_debug_mode_active) {
                if (g_show_node_tree) {
                    float temp_y = 30.0f;
                    g_debug_hovered_node = nullptr;
                    if (options.debug_mode == DebugType::Dimens) {
                        update_dimens_tree_hover(g_layout_tree, temp_y, 0, mouse_state.px, mouse_state.py, g_debug_hovered_node);
                    } else {
                        update_node_tree_hover(g_layout_tree, temp_y, 0, mouse_state.px, mouse_state.py, g_debug_hovered_node);
                    }
                }

                auto box = find_box_for_node(g_layout_tree, g_debug_hovered_node);
                Renderer::set_debug_box(box);
            }

            Renderer::render(g_layout_tree);

            if (options.debug_mode == DebugType::Anim) draw_animation_panel();

            if (g_show_node_tree) {
                float y_offset = 30.0f;
                if (options.debug_mode == DebugType::Dimens) {
                    render_dimens_tree(g_layout_tree, y_offset, 0, mouse_state.px, mouse_state.py);
                } else {
                    render_node_tree(g_layout_tree, y_offset, 0, mouse_state.px, mouse_state.py);
                }
            }
        }

        render_scope.reset();

        // engine overlays (F3 profiler / --fps), drawn over the page and independent of it
        Profiler::draw();
        if (!g_show_profiler) fps_counter.draw();

        if (!options.screenshot_path.empty() && get_platform_time() - screenshot_start >= options.screenshot_delay) {
            if (!options.layout_dump_path.empty() && g_layout_tree && !dump_layout(options.layout_dump_path, *g_layout_tree)) return 1;
            if (!capture_frame(options.screenshot_path, fbw, fbh)) {
                std::cerr << "Could not save screenshot: " << options.screenshot_path << std::endl;
                return 1;
            }
            std::cout << "Screenshot: " << options.screenshot_path << std::endl;
            log_event("screenshot", options.screenshot_path);
            break;
        }
        {
            ProfScope swap(ProfSection::Swap);
            window.swap_buffers();
            window.poll_events();
        }
        fps_counter.frame_done();
        Profiler::end_frame();

        // no more than FRAME_S per frame: the rest of it is left to the other processes
        const double spent = get_platform_time() - frame_start;
        if (spent < FRAME_S) std::this_thread::sleep_for(std::chrono::duration<double>(FRAME_S - spent));
    }

    modules_shutdown();
    shutdown_media();
    release_video_textures();
    return 0;
}
