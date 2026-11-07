#include "app/js_api.h"
#include "app/app_state.h"
#include "core/dom_helpers.h"
#include "forms/form_bindings.h"
#include "forms/form_controls.h"
#include "renderer/renderer.h"
#include "scripting/console_bindings.h"
#include "scripting/dom_bindings.h"
#include "scripting/event_bindings.h"
#include "scripting/layout_bindings.h"
#include "scripting/media_bindings.h"
#include "scripting/style_bindings.h"
#include "scripting/timer_bindings.h"
#include "scripting/window_bindings.h"
#include "utils/dom_traversal.h"
#include "utils/file_utils.h"
#include "utils/layout_traversal.h"
#include "utils/page_resources.h"

namespace linweb {

void install_js_api(JSEngine& js, const std::shared_ptr<Node>& dom)
{
    // form controls: the focused node is the one :focus reads, text is measured with the renderer's font
    forms::bind_focus(&g_focused_node);
    forms::bind_dirty(&g_forms_dirty);
    forms::set_root(dom);
    forms::set_measure([](const std::string& text, float font_size) { return Renderer::measure_text(text, font_size); });
    forms::set_visibility([](const std::shared_ptr<Node>& node) {
        return g_layout_tree && find_box_for_node(g_layout_tree, node) != nullptr;
    });

    // Each binding registers its native functions and adds its part of the browser-like API. The order matters: the
    // window comes first, the document before what adds properties to its elements.
    install_window_bindings(js);
    install_console_bindings(js);
    install_timer_bindings(js);
    install_dom_bindings(js);
    install_style_bindings(js);
    install_layout_bindings(js);
    forms::install_bindings(js);
    install_event_bindings(js);
    install_media_bindings(js);   // after the events: it listens to DOMContentLoaded and fires events at its elements
}

void run_page_scripts(JSEngine& js, const std::shared_ptr<Node>& dom, const std::string& base_dir)
{
    std::vector<std::shared_ptr<ElementNode>> scripts;
    find_tags(dom, "script", scripts);
    for (auto& script : scripts) {
        if (script->data.attributes.count("src")) {
            std::string js_path = resolve_resource_path(base_dir, script->data.attributes.at("src"));
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
}

} // namespace linweb
