#pragma once
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "core/dom.h"

namespace linweb {
namespace forms {

// Form controls: <input> (text, password, search, email, url, tel, number), <input type="range">, <progress> and
// <select> with its <option> children. The value of an input lives in its `value` attribute; a select's value is the
// value of the option with the `selected` attribute (the first one when none has it). Text editing, caret, keyboard
// and mouse behaviour is here and knows nothing about OpenGL or JavaScript: the engine connects it through the
// callbacks below, so it can be tested on its own.

enum class Kind { None, Text, Range, Progress, Select };

Kind kind_of(const Node& node);
bool is_disabled(const Node& node);
bool is_focusable(const Node& node);   // text, range and select controls that are not disabled

struct Rect {
    float x = 0, y = 0, w = 0, h = 0;
};

// ---- select ---------------------------------------------------------------------------------------------------
std::vector<std::shared_ptr<Node>> options_of(const Node& select);
int selected_index(const Node& select);          // -1 when there are no options
std::string option_text(const Node& option);
// The list that opens below a select: the drawn area for `rows` options. Also used by the renderer.
float popup_row_height(float font_size);
Rect popup_area(const Rect& content, size_t rows, float font_size);
// The select whose list is open (nullptr when none) and the highlighted row of that list.
const std::shared_ptr<Node>& popup_node();
int popup_highlight();
// The renderer tells the forms where a select was drawn, so clicks and keys can open its list where it is shown.
void remember_box(const std::shared_ptr<Node>& node, const Rect& content, float font_size);
// A press while a list is open: chooses a row (returns true), or closes the list and lets the press go on (false).
bool popup_press(float x, float y);
// Opens the list of a select as a script's showPicker() asks for (focuses it first).
void open_picker(const std::shared_ptr<Node>& node);

// ---- wiring ---------------------------------------------------------------------------------------------------
// "input", "change", "focus" and "blur" are reported through the emitter (the engine turns them into JS events).
using Emitter = std::function<void(const std::shared_ptr<Node>& node, const std::string& type)>;
// Width of a text in pixels at a font size.
using Measure = std::function<float(const std::string& text, float font_size)>;
// Whether a control is on screen (it has a layout box); hidden controls are skipped by Tab.
using Visibility = std::function<bool(const std::shared_ptr<Node>& node)>;

void set_emitter(Emitter emitter);
void set_measure(Measure measure);
void set_visibility(Visibility visibility);
void bind_focus(std::shared_ptr<Node>* slot);   // the variable that holds the focused node (:focus reads it)
void bind_dirty(bool* flag);                    // set when a change needs a new style pass (focus changes)
void set_root(const std::shared_ptr<Node>& root);

// ---- values ---------------------------------------------------------------------------------------------------
std::string value_of(const Node& node);
// Sets the value as a script would: text is cut to maxlength, a range is clamped to min/max and rounded to step. The
// caret goes to the end. Does not emit events. Returns true when the value changed.
bool set_value(const std::shared_ptr<Node>& node, const std::string& value);

struct RangeSpec {
    double min = 0, max = 100, step = 1, value = 50;
};
RangeSpec range_spec(const Node& node);
double range_fraction(const Node& node);       // 0..1
// -1 when a <progress> has no value (indeterminate), otherwise value/max clamped to 0..1.
double progress_fraction(const Node& node);
std::string format_number(double value);

// ---- focus ----------------------------------------------------------------------------------------------------
std::shared_ptr<Node> focused();
// Focuses any node (the previous control sees a change event if its text changed, then blur). Controls get their
// caret at the end.
void set_focus(const std::shared_ptr<Node>& node);
void blur();
// Tab / Shift+Tab among the visible focusable controls in document order. Returns true when focus moved.
bool focus_next(bool backwards);

// ---- keyboard -------------------------------------------------------------------------------------------------
struct Key {
    std::string key;   // DOM key name: "a", "€", "Backspace", "ArrowLeft", ...
    bool ctrl = false, alt = false, shift = false, meta = false;
};
// The default action of a key press on the focused control. Returns true when the control used the key.
bool handle_key(const Key& key);

// ---- text editing (also used by handle_key) ---------------------------------------------------------------------
bool insert_text(const std::shared_ptr<Node>& node, const std::string& text);
bool delete_backward(const std::shared_ptr<Node>& node);
bool delete_forward(const std::shared_ptr<Node>& node);
void move_caret(const std::shared_ptr<Node>& node, int code_points);
void caret_home(const std::shared_ptr<Node>& node);
void caret_end(const std::shared_ptr<Node>& node);
size_t caret_of(const Node& node);             // in bytes, on a code point boundary
// Commits the text: emits "change" when it differs from when the control got focus (Enter, blur).
void commit(const std::shared_ptr<Node>& node);

// What to draw in a text control.
struct TextView {
    std::string text;       // visible part (password masked, placeholder when empty and unfocused or empty)
    bool placeholder = false;
    bool password = false;
    float caret_x = 0;      // from the left edge of the content box
    bool show_caret = false;
};
TextView text_view(const std::shared_ptr<Node>& node, float content_width, float font_size);

// ---- mouse ----------------------------------------------------------------------------------------------------
struct RangeGeometry {
    Rect track;
    Rect fill;
    float thumb_x = 0, thumb_y = 0, thumb_r = 0;
};
RangeGeometry range_geometry(const Rect& content, double fraction);
double range_fraction_at(const Rect& content, float mouse_x);

// A press on a control: focuses it, puts the caret where the text was clicked or starts dragging a slider. Returns
// true when the node is a control.
bool mouse_press(const std::shared_ptr<Node>& node, const Rect& content, float font_size, float mouse_x);
void mouse_drag(float mouse_x);
void mouse_release();
bool dragging();

// ---- caret blink ------------------------------------------------------------------------------------------------
// The caret blinks while the control has focus and restarts after each edit; tests can set a fixed clock (ms).
void set_clock(std::function<double()> clock_ms);

// Forgets everything (tests).
void reset();

} // namespace forms
} // namespace linweb
