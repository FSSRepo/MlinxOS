#include "forms/form_controls.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <unordered_map>

namespace linweb {
namespace forms {

namespace {

const char kMask[] = "*";   // one ASCII mask per character of a password; the renderer draws dots in their place
const size_t kMaskLength = 1;
const double kBlinkMs = 530.0;

struct TextState {
    size_t caret = 0;          // byte offset in the value, on a code point boundary
    size_t first = 0;          // first visible byte of the shown text
    std::string at_focus;      // the value when the control got focus or was last committed
    double last_edit = 0;      // clock (ms) of the last edit or caret move: the caret is solid just after it
};

struct Drag {
    std::shared_ptr<Node> node;
    Rect content;
    std::string start_value;
};

Emitter g_emit;
Measure g_measure;
Visibility g_visible;
std::shared_ptr<Node> g_local_focus;
std::shared_ptr<Node>* g_focus_slot = &g_local_focus;
bool g_local_dirty = false;
bool* g_dirty = &g_local_dirty;
std::weak_ptr<Node> g_root;
std::unordered_map<const Node*, TextState> g_text;
Drag g_drag;
std::function<double()> g_clock;

struct Geometry {
    Rect content;
    float font_size = 16.0f;
};
std::unordered_map<const Node*, Geometry> g_boxes;   // where each select was last drawn
std::shared_ptr<Node> g_popup;                       // the select whose list is open
int g_highlight = -1;                                // the highlighted row of that list

std::shared_ptr<Node>& focus_slot() { return *g_focus_slot; }

double now_ms() {
    if (g_clock) return g_clock();
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

void emit(const std::shared_ptr<Node>& node, const char* type) {
    if (g_emit && node) g_emit(node, type);
}

float measure(const std::string& text, float font_size) {
    if (text.empty()) return 0.0f;
    return g_measure ? g_measure(text, font_size) : static_cast<float>(text.size()) * font_size * 0.5f;
}

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string attr(const Node& node, const char* name) {
    auto it = node.data.attributes.find(name);
    return it == node.data.attributes.end() ? std::string() : it->second;
}

bool has_attr(const Node& node, const char* name) {
    return node.data.attributes.find(name) != node.data.attributes.end();
}

// ---- UTF-8 -----------------------------------------------------------------------------------------------------
size_t prev_boundary(const std::string& s, size_t pos) {
    if (pos == 0) return 0;
    --pos;
    while (pos > 0 && (static_cast<unsigned char>(s[pos]) & 0xC0) == 0x80) --pos;
    return pos;
}

size_t next_boundary(const std::string& s, size_t pos) {
    if (pos >= s.size()) return s.size();
    ++pos;
    while (pos < s.size() && (static_cast<unsigned char>(s[pos]) & 0xC0) == 0x80) ++pos;
    return pos;
}

size_t count_code_points(const std::string& s) {
    size_t n = 0;
    for (unsigned char c : s) if ((c & 0xC0) != 0x80) ++n;
    return n;
}

size_t clamp_boundary(const std::string& s, size_t pos) {
    pos = std::min(pos, s.size());
    while (pos > 0 && pos < s.size() && (static_cast<unsigned char>(s[pos]) & 0xC0) == 0x80) --pos;
    return pos;
}

// ---- text controls ----------------------------------------------------------------------------------------------
bool is_password(const Node& node) { return lower(attr(node, "type")) == "password"; }
bool is_number(const Node& node) { return lower(attr(node, "type")) == "number"; }
bool is_readonly(const Node& node) { return has_attr(node, "readonly"); }

size_t max_length(const Node& node) {
    long n = std::strtol(attr(node, "maxlength").c_str(), nullptr, 10);
    return n > 0 ? static_cast<size_t>(n) : 0;
}

TextState& state_of(const Node& node) {
    TextState& st = g_text[&node];
    std::string value = attr(node, "value");
    st.caret = clamp_boundary(value, st.caret);
    return st;
}

void touch(TextState& st) { st.last_edit = now_ms(); }

void mark_dirty() { *g_dirty = true; }

bool is_control(const std::shared_ptr<Node>& node) {
    return node && kind_of(*node) != Kind::None;
}

// ---- range ------------------------------------------------------------------------------------------------------
double parse_number(const std::string& s, double fallback) {
    if (s.empty()) return fallback;
    char* end = nullptr;
    double v = std::strtod(s.c_str(), &end);
    return end == s.c_str() || !std::isfinite(v) ? fallback : v;
}

double snap(double value, const RangeSpec& spec) {
    value = std::min(spec.max, std::max(spec.min, value));
    if (spec.step > 0) {
        value = spec.min + std::round((value - spec.min) / spec.step) * spec.step;
        if (value > spec.max + 1e-9) value -= spec.step;   // the last step that fits
        value = std::min(spec.max, std::max(spec.min, value));
    }
    return std::round(value * 1e6) / 1e6;
}

bool store_range_value(const std::shared_ptr<Node>& node, double value) {
    RangeSpec spec = range_spec(*node);
    std::string text = format_number(snap(value, spec));
    if (text == attr(*node, "value") && text == format_number(spec.value)) return false;
    bool changed = text != format_number(spec.value);
    node->data.attributes["value"] = text;
    return changed;
}

bool range_step(const std::shared_ptr<Node>& node, long steps) {
    RangeSpec spec = range_spec(*node);
    double step = spec.step > 0 ? spec.step : (spec.max - spec.min) / 100.0;
    if (!store_range_value(node, spec.value + static_cast<double>(steps) * step)) return false;
    emit(node, "input");
    emit(node, "change");
    return true;
}

bool range_to(const std::shared_ptr<Node>& node, double value) {
    if (!store_range_value(node, value)) return false;
    emit(node, "input");
    emit(node, "change");
    return true;
}

std::string shown_text(const Node& node, const std::string& value) {
    if (!is_password(node)) return value;
    std::string masked;
    for (size_t i = count_code_points(value); i > 0; --i) masked += kMask;
    return masked;
}

// Byte offset in the shown text of a byte offset in the value.
size_t shown_offset(const Node& node, const std::string& value, size_t caret) {
    if (!is_password(node)) return caret;
    return count_code_points(value.substr(0, caret)) * kMaskLength;
}

void collect(const std::shared_ptr<Node>& node, std::vector<std::shared_ptr<Node>>& out) {
    if (!node) return;
    if (is_focusable(*node) && (!g_visible || g_visible(node))) out.push_back(node);
    for (const auto& child : node->children) collect(child, out);
}

// ---- select: options, the chosen one, and the list that opens below it ---------------------------------------
bool contains(const Rect& r, float x, float y) {
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

const Geometry* geometry_of(const Node* node) {
    auto it = g_boxes.find(node);
    return it == g_boxes.end() ? nullptr : &it->second;
}

std::string trimmed(const std::string& s) {
    size_t first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return std::string();
    size_t last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

std::string option_value(const Node& option) {
    return has_attr(option, "value") ? attr(option, "value") : trimmed(option_text(option));
}

// Makes an option the chosen one without events (a script does this, or a user choice before it is reported).
bool select_option(const std::shared_ptr<Node>& select, int index) {
    std::vector<std::shared_ptr<Node>> options = options_of(*select);
    if (index < 0 || index >= static_cast<int>(options.size())) return false;
    if (selected_index(*select) == index) return false;
    for (const auto& option : options) option->data.attributes.erase("selected");
    options[index]->data.attributes["selected"] = "";
    mark_dirty();
    return true;
}

// A user choice: "input" and "change" follow when it changed something.
bool choose_option(const std::shared_ptr<Node>& select, int index) {
    if (!select_option(select, index)) return false;
    emit(select, "input");
    emit(select, "change");
    return true;
}

void open_popup(const std::shared_ptr<Node>& node) {
    if (options_of(*node).empty()) return;
    g_popup = node;
    g_highlight = selected_index(*node);
    mark_dirty();
}

void close_popup() {
    if (!g_popup) return;
    g_popup = nullptr;
    g_highlight = -1;
    mark_dirty();
}

} // namespace

// ---- select: public ---------------------------------------------------------------------------------------------
std::vector<std::shared_ptr<Node>> options_of(const Node& select) {
    std::vector<std::shared_ptr<Node>> options;
    for (const auto& child : select.children) {
        if (child->tag_name == "option") options.push_back(child);
    }
    return options;
}

int selected_index(const Node& select) {
    std::vector<std::shared_ptr<Node>> options = options_of(select);
    for (size_t i = 0; i < options.size(); ++i) {
        if (has_attr(*options[i], "selected")) return static_cast<int>(i);
    }
    return options.empty() ? -1 : 0;
}

std::string option_text(const Node& option) {
    std::string text;
    for (const auto& child : option.children) {
        if (child->type() == NodeType::Text) text += static_cast<const TextNode&>(*child).data;
    }
    return trimmed(text);
}

float popup_row_height(float font_size) {
    return font_size * 1.5f;
}

Rect popup_area(const Rect& content, size_t rows, float font_size) {
    return {content.x, content.y + content.h, content.w, static_cast<float>(rows) * popup_row_height(font_size) + 2.0f};
}

const std::shared_ptr<Node>& popup_node() {
    return g_popup;
}

int popup_highlight() {
    return g_highlight;
}

void remember_box(const std::shared_ptr<Node>& node, const Rect& content, float font_size) {
    if (node) g_boxes[node.get()] = Geometry{content, font_size};
}

bool popup_press(float x, float y) {
    if (!g_popup) return false;
    const Geometry* g = geometry_of(g_popup.get());
    if (!g) {
        close_popup();
        return false;
    }
    if (contains(g->content, x, y)) return false;   // the select itself: its press toggles the list
    std::shared_ptr<Node> select = g_popup;
    Rect area = popup_area(g->content, options_of(*select).size(), g->font_size);
    close_popup();
    if (!contains(area, x, y)) return false;
    int row = static_cast<int>((y - area.y - 1.0f) / popup_row_height(g->font_size));
    choose_option(select, std::max(0, row));
    return true;
}

void open_picker(const std::shared_ptr<Node>& node) {
    if (!node || kind_of(*node) != Kind::Select || is_disabled(*node)) return;
    set_focus(node);
    if (g_popup != node) open_popup(node);
}

// ---- classification -------------------------------------------------------------------------------------------
Kind kind_of(const Node& node) {
    if (node.tag_name == "select") return Kind::Select;
    if (node.tag_name == "textarea") return Kind::Text;   // single-line editing for now; the look is the same
    if (node.tag_name == "progress") return Kind::Progress;
    if (node.tag_name != "input") return Kind::None;
    std::string type = lower(attr(node, "type"));
    if (type == "range") return Kind::Range;
    if (type.empty() || type == "text" || type == "password" || type == "search" || type == "email" ||
        type == "url" || type == "tel" || type == "number") {
        return Kind::Text;
    }
    return Kind::None;
}

bool is_disabled(const Node& node) {
    return has_attr(node, "disabled");
}

bool is_focusable(const Node& node) {
    Kind kind = kind_of(node);
    return (kind == Kind::Text || kind == Kind::Range || kind == Kind::Select) && !is_disabled(node);
}

// ---- wiring -----------------------------------------------------------------------------------------------------
void set_emitter(Emitter emitter) { g_emit = std::move(emitter); }
void set_measure(Measure m) { g_measure = std::move(m); }
void set_visibility(Visibility visibility) { g_visible = std::move(visibility); }
void bind_focus(std::shared_ptr<Node>* slot) { g_focus_slot = slot ? slot : &g_local_focus; }
void bind_dirty(bool* flag) { g_dirty = flag ? flag : &g_local_dirty; }
void set_root(const std::shared_ptr<Node>& root) { g_root = root; }
void set_clock(std::function<double()> clock_ms) { g_clock = std::move(clock_ms); }

void reset() {
    g_emit = nullptr;
    g_measure = nullptr;
    g_visible = nullptr;
    g_local_focus.reset();
    g_focus_slot = &g_local_focus;
    g_local_dirty = false;
    g_dirty = &g_local_dirty;
    g_root.reset();
    g_text.clear();
    g_drag = Drag();
    g_clock = nullptr;
}

// ---- values ---------------------------------------------------------------------------------------------------
std::string format_number(double value) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.6f", value);
    std::string s = buffer;
    if (s.find('.') != std::string::npos) {
        while (!s.empty() && s.back() == '0') s.pop_back();
        if (!s.empty() && s.back() == '.') s.pop_back();
    }
    return s == "-0" ? "0" : s;
}

RangeSpec range_spec(const Node& node) {
    RangeSpec spec;
    spec.min = parse_number(attr(node, "min"), 0.0);
    spec.max = parse_number(attr(node, "max"), 100.0);
    if (spec.max < spec.min) spec.max = spec.min;
    std::string step = lower(attr(node, "step"));
    spec.step = step == "any" ? 0.0 : parse_number(step, 1.0);
    if (spec.step < 0) spec.step = 1.0;
    double middle = spec.min + (spec.max - spec.min) / 2.0;
    spec.value = snap(parse_number(attr(node, "value"), middle), spec);
    return spec;
}

double range_fraction(const Node& node) {
    RangeSpec spec = range_spec(node);
    if (spec.max <= spec.min) return 0.0;
    return std::min(1.0, std::max(0.0, (spec.value - spec.min) / (spec.max - spec.min)));
}

double progress_fraction(const Node& node) {
    if (!has_attr(node, "value")) return -1.0;
    double max = parse_number(attr(node, "max"), 1.0);
    if (max <= 0) max = 1.0;
    return std::min(1.0, std::max(0.0, parse_number(attr(node, "value"), 0.0) / max));
}

std::string value_of(const Node& node) {
    if (kind_of(node) == Kind::Range) return format_number(range_spec(node).value);
    if (kind_of(node) == Kind::Select) {
        int index = selected_index(node);
        return index < 0 ? std::string() : option_value(*options_of(node)[index]);
    }
    return attr(node, "value");
}

bool set_value(const std::shared_ptr<Node>& node, const std::string& value) {
    if (!node) return false;
    Kind kind = kind_of(*node);
    if (kind == Kind::Range) {
        RangeSpec spec = range_spec(*node);
        double wanted = parse_number(value, spec.min + (spec.max - spec.min) / 2.0);
        bool changed = format_number(snap(wanted, spec)) != format_number(spec.value);
        node->data.attributes["value"] = format_number(snap(wanted, spec));
        return changed;
    }
    if (kind == Kind::Progress) {
        bool changed = attr(*node, "value") != value;
        node->data.attributes["value"] = value;
        return changed;
    }
    if (kind == Kind::Select) {
        std::vector<std::shared_ptr<Node>> options = options_of(*node);
        for (size_t i = 0; i < options.size(); ++i) {
            if (option_value(*options[i]) == value) return select_option(node, static_cast<int>(i));
        }
        return false;
    }
    if (kind != Kind::Text) return false;

    std::string cut = value;
    if (size_t limit = max_length(*node)) {
        size_t pos = 0;
        for (size_t i = 0; i < limit && pos < cut.size(); ++i) pos = next_boundary(cut, pos);
        cut = cut.substr(0, pos);
    }
    bool changed = cut != attr(*node, "value");
    node->data.attributes["value"] = cut;
    TextState& st = state_of(*node);
    st.caret = cut.size();
    st.first = 0;
    st.at_focus = cut;   // a script setting the value is not an edit by the user: no "change" for it later
    return changed;
}

// ---- focus ----------------------------------------------------------------------------------------------------
std::shared_ptr<Node> focused() {
    return focus_slot();
}

void set_focus(const std::shared_ptr<Node>& node) {
    std::shared_ptr<Node> previous = focus_slot();
    if (previous == node) return;
    if (g_popup && g_popup != node) close_popup();
    if (is_control(previous) && kind_of(*previous) == Kind::Text) commit(previous);
    focus_slot() = node;
    mark_dirty();
    if (is_control(previous)) emit(previous, "blur");
    if (node && is_focusable(*node)) {
        if (kind_of(*node) == Kind::Text) {
            TextState& st = state_of(*node);
            st.caret = attr(*node, "value").size();
            st.at_focus = attr(*node, "value");
            st.first = 0;
            touch(st);
        }
        emit(node, "focus");
    }
}

void blur() {
    set_focus(nullptr);
}

bool focus_next(bool backwards) {
    std::vector<std::shared_ptr<Node>> controls;
    collect(g_root.lock(), controls);
    if (controls.empty()) return false;
    auto current = std::find(controls.begin(), controls.end(), focus_slot());
    size_t index;
    if (current == controls.end()) {
        index = backwards ? controls.size() - 1 : 0;
    } else {
        size_t at = static_cast<size_t>(current - controls.begin());
        index = backwards ? (at + controls.size() - 1) % controls.size() : (at + 1) % controls.size();
    }
    if (controls[index] == focus_slot()) return false;
    set_focus(controls[index]);
    return true;
}

// ---- text editing -----------------------------------------------------------------------------------------------
size_t caret_of(const Node& node) {
    return kind_of(node) == Kind::Text ? state_of(node).caret : 0;
}

bool insert_text(const std::shared_ptr<Node>& node, const std::string& text) {
    if (!node || kind_of(*node) != Kind::Text || is_disabled(*node) || is_readonly(*node)) return false;
    std::string accepted;
    if (is_number(*node)) {
        for (char c : text) if (std::isdigit(static_cast<unsigned char>(c)) || c == '+' || c == '-' || c == '.' || c == 'e' || c == 'E') accepted += c;
    } else {
        accepted = text;
    }
    std::string value = attr(*node, "value");
    if (size_t limit = max_length(*node)) {
        size_t room = limit > count_code_points(value) ? limit - count_code_points(value) : 0;
        size_t pos = 0;
        for (size_t i = 0; i < room && pos < accepted.size(); ++i) pos = next_boundary(accepted, pos);
        accepted = accepted.substr(0, pos);
    }
    if (accepted.empty()) return false;

    TextState& st = state_of(*node);
    value.insert(st.caret, accepted);
    st.caret += accepted.size();
    node->data.attributes["value"] = value;
    touch(st);
    emit(node, "input");
    return true;
}

bool delete_backward(const std::shared_ptr<Node>& node) {
    if (!node || kind_of(*node) != Kind::Text || is_disabled(*node) || is_readonly(*node)) return false;
    TextState& st = state_of(*node);
    if (st.caret == 0) return false;
    std::string value = attr(*node, "value");
    size_t from = prev_boundary(value, st.caret);
    value.erase(from, st.caret - from);
    st.caret = from;
    node->data.attributes["value"] = value;
    touch(st);
    emit(node, "input");
    return true;
}

bool delete_forward(const std::shared_ptr<Node>& node) {
    if (!node || kind_of(*node) != Kind::Text || is_disabled(*node) || is_readonly(*node)) return false;
    TextState& st = state_of(*node);
    std::string value = attr(*node, "value");
    if (st.caret >= value.size()) return false;
    value.erase(st.caret, next_boundary(value, st.caret) - st.caret);
    node->data.attributes["value"] = value;
    touch(st);
    emit(node, "input");
    return true;
}

void move_caret(const std::shared_ptr<Node>& node, int code_points) {
    if (!node || kind_of(*node) != Kind::Text) return;
    TextState& st = state_of(*node);
    std::string value = attr(*node, "value");
    for (; code_points < 0 && st.caret > 0; ++code_points) st.caret = prev_boundary(value, st.caret);
    for (; code_points > 0 && st.caret < value.size(); --code_points) st.caret = next_boundary(value, st.caret);
    touch(st);
}

void caret_home(const std::shared_ptr<Node>& node) {
    if (!node || kind_of(*node) != Kind::Text) return;
    TextState& st = state_of(*node);
    st.caret = 0;
    touch(st);
}

void caret_end(const std::shared_ptr<Node>& node) {
    if (!node || kind_of(*node) != Kind::Text) return;
    TextState& st = state_of(*node);
    st.caret = attr(*node, "value").size();
    touch(st);
}

void commit(const std::shared_ptr<Node>& node) {
    if (!node || kind_of(*node) != Kind::Text) return;
    TextState& st = state_of(*node);
    std::string value = attr(*node, "value");
    if (value != st.at_focus) {
        st.at_focus = value;
        emit(node, "change");
    }
}

// ---- keyboard ---------------------------------------------------------------------------------------------------
bool handle_key(const Key& k) {
    if (k.key == "Tab" && !k.ctrl && !k.alt && !k.meta) return focus_next(k.shift);

    std::shared_ptr<Node> node = focus_slot();
    if (!node) return false;
    Kind kind = kind_of(*node);

    if (kind == Kind::Text) {
        if (k.ctrl || k.meta) return false;               // shortcuts are not typing
        if (k.key == "Backspace") { delete_backward(node); return true; }
        if (k.key == "Delete") { delete_forward(node); return true; }
        if (k.key == "ArrowLeft") { move_caret(node, -1); return true; }
        if (k.key == "ArrowRight") { move_caret(node, 1); return true; }
        if (k.key == "Home") { caret_home(node); return true; }
        if (k.key == "End") { caret_end(node); return true; }
        if (k.key == "Enter") { commit(node); return true; }
        unsigned char first = k.key.empty() ? 0 : static_cast<unsigned char>(k.key[0]);
        if (!k.alt && count_code_points(k.key) == 1 && first >= 0x20 && first != 0x7f) {
            insert_text(node, k.key);
            return true;
        }
        return false;
    }

    if (kind == Kind::Select && !is_disabled(*node)) {
        int count = static_cast<int>(options_of(*node).size());
        if (count == 0 || k.ctrl || k.meta || k.alt) return false;
        bool down = k.key == "ArrowDown";
        bool up = k.key == "ArrowUp";
        bool enter = k.key == "Enter" || k.key == " ";
        if (g_popup == node) {
            if (k.key == "Escape") { close_popup(); return true; }
            if (down || up) {
                g_highlight = std::clamp(g_highlight + (down ? 1 : -1), 0, count - 1);
                mark_dirty();
                return true;
            }
            if (enter) {
                int row = g_highlight;
                close_popup();
                choose_option(node, row);
                return true;
            }
            return false;
        }
        if (down || up) {
            choose_option(node, std::clamp(selected_index(*node) + (down ? 1 : -1), 0, count - 1));
            return true;
        }
        if (enter) { open_popup(node); return true; }
        return false;
    }

    if (kind == Kind::Range && !is_disabled(*node) && !is_readonly(*node)) {
        RangeSpec spec = range_spec(*node);
        double step = spec.step > 0 ? spec.step : (spec.max - spec.min) / 100.0;
        long page = std::max(1L, static_cast<long>(std::lround((spec.max - spec.min) / 10.0 / (step > 0 ? step : 1.0))));
        if (k.key == "ArrowLeft" || k.key == "ArrowDown") { range_step(node, -1); return true; }
        if (k.key == "ArrowRight" || k.key == "ArrowUp") { range_step(node, 1); return true; }
        if (k.key == "PageDown") { range_step(node, -page); return true; }
        if (k.key == "PageUp") { range_step(node, page); return true; }
        if (k.key == "Home") { range_to(node, spec.min); return true; }
        if (k.key == "End") { range_to(node, spec.max); return true; }
    }
    return false;
}

// ---- drawing -----------------------------------------------------------------------------------------------------
TextView text_view(const std::shared_ptr<Node>& node, float content_width, float font_size) {
    TextView view;
    if (!node || kind_of(*node) != Kind::Text) return view;
    TextState& st = state_of(*node);
    std::string value = attr(*node, "value");
    bool has_focus = focus_slot() == node;
    view.password = is_password(*node);

    double phase = std::fmod(now_ms() - st.last_edit, kBlinkMs * 2.0);
    bool caret_on = has_focus && phase < kBlinkMs;

    if (value.empty()) {
        std::string placeholder = attr(*node, "placeholder");
        view.text = placeholder;
        view.placeholder = !placeholder.empty();
        view.show_caret = caret_on;
        view.caret_x = 0;
        return view;
    }

    std::string shown = shown_text(*node, value);
    size_t caret = shown_offset(*node, value, st.caret);
    st.first = clamp_boundary(shown, st.first);
    if (st.first > caret) st.first = caret;
    while (st.first < caret && measure(shown.substr(st.first, caret - st.first), font_size) > content_width) {
        st.first = next_boundary(shown, st.first);
    }
    size_t end = st.first;
    while (end < shown.size()) {
        size_t next = next_boundary(shown, end);
        if (measure(shown.substr(st.first, next - st.first), font_size) > content_width) break;
        end = next;
    }
    view.text = shown.substr(st.first, end - st.first);
    view.caret_x = measure(shown.substr(st.first, caret - st.first), font_size);
    view.show_caret = caret_on;
    return view;
}

// ---- mouse ----------------------------------------------------------------------------------------------------------
RangeGeometry range_geometry(const Rect& content, double fraction) {
    RangeGeometry g;
    float r = content.h / 2.0f;
    float track_h = std::min(content.h, std::max(4.0f, content.h * 0.3f));
    g.thumb_r = r;
    g.thumb_x = content.x + r + static_cast<float>(fraction) * std::max(0.0f, content.w - 2.0f * r);
    g.thumb_y = content.y + r;
    g.track = {content.x, content.y + (content.h - track_h) / 2.0f, content.w, track_h};
    g.fill = {content.x, g.track.y, std::max(0.0f, g.thumb_x - content.x), track_h};
    return g;
}

double range_fraction_at(const Rect& content, float mouse_x) {
    float r = content.h / 2.0f;
    float travel = content.w - 2.0f * r;
    if (travel <= 0) return 0.0;
    return std::min(1.0, std::max(0.0, static_cast<double>((mouse_x - (content.x + r)) / travel)));
}

bool mouse_press(const std::shared_ptr<Node>& node, const Rect& content, float font_size, float mouse_x) {
    if (!node) return false;
    Kind kind = kind_of(*node);
    if (kind != Kind::Text && kind != Kind::Range && kind != Kind::Select) return false;
    if (is_disabled(*node)) return true;

    set_focus(node);
    if (kind == Kind::Select) {
        remember_box(node, content, font_size);
        if (g_popup == node) close_popup();
        else open_popup(node);
        return true;
    }
    if (kind == Kind::Text) {
        TextState& st = state_of(*node);
        std::string value = attr(*node, "value");
        std::string shown = shown_text(*node, value);
        size_t first = clamp_boundary(shown, st.first);
        float wanted = mouse_x - content.x;
        size_t best = first;
        float best_distance = std::abs(wanted);
        for (size_t at = first; at < shown.size();) {
            at = next_boundary(shown, at);
            float distance = std::abs(measure(shown.substr(first, at - first), font_size) - wanted);
            if (distance < best_distance) { best_distance = distance; best = at; }
        }
        // back from the shown text to the value (a password shows one mask per code point)
        size_t caret = best;
        if (is_password(*node)) {
            size_t index = best / kMaskLength;
            caret = 0;
            for (size_t i = 0; i < index; ++i) caret = next_boundary(value, caret);
        }
        st.caret = caret;
        touch(st);
        return true;
    }

    if (is_readonly(*node)) return true;
    RangeSpec spec = range_spec(*node);
    g_drag.node = node;
    g_drag.content = content;
    g_drag.start_value = format_number(spec.value);
    if (store_range_value(node, spec.min + range_fraction_at(content, mouse_x) * (spec.max - spec.min))) emit(node, "input");
    return true;
}

void mouse_drag(float mouse_x) {
    if (!g_drag.node) return;
    RangeSpec spec = range_spec(*g_drag.node);
    if (store_range_value(g_drag.node, spec.min + range_fraction_at(g_drag.content, mouse_x) * (spec.max - spec.min))) {
        emit(g_drag.node, "input");
    }
}

void mouse_release() {
    if (!g_drag.node) return;
    std::shared_ptr<Node> node = g_drag.node;
    std::string start = g_drag.start_value;
    g_drag = Drag();
    if (format_number(range_spec(*node).value) != start) emit(node, "change");
}

bool dragging() {
    return g_drag.node != nullptr;
}

} // namespace forms
} // namespace linweb
