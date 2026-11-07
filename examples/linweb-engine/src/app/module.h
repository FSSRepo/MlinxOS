#pragma once
#include <memory>
#include <string>

namespace linweb {

class JSEngine;
class KeyboardHandler;

// A native module extends the engine without being part of it: a program that links the engine registers its modules and
// calls run(). The engine knows nothing about what a module does; it only calls these hooks at fixed points.
class Module {
public:
    virtual ~Module() = default;
    virtual const char* name() const = 0;
    // After the engine's own bindings are installed: add natives with js.add_function("__x", fn) and run scripts with js.execute(...).
    virtual void install_js(JSEngine& js) { (void)js; }
    // Once, when the page is parsed; html_base_dir is the folder of the page ("" for the working directory).
    virtual void on_page_loaded(const std::string& html_base_dir) { (void)html_base_dir; }
    // Every iteration of the main loop, before the page is styled and drawn. It may inject keys
    // (KeyboardHandler::handle_key_event) and dispatch events to scripts (dispatch_event of scripting/event_bindings.h).
    virtual void on_frame(KeyboardHandler& keyboard) { (void)keyboard; }
    // True: the next frame is drawn even when nothing else changed (a stick is pushed, an overlay moves).
    virtual bool wants_frame() const { return false; }
    virtual void on_shutdown() {}
};

// Call before run(). The engine owns the module from then on.
void register_module(std::unique_ptr<Module> module);

// The engine: parses the command line, opens the window, loads the page and runs the main loop until the window closes.
// This was the main() of the engine; its executable is `int main(int argc, char** argv) { return linweb::run(argc, argv); }`.
int run(int argc, char** argv);

// The F3 overlay of the engine (a module can bind it to a button of its own).
void toggle_profiler();

// Used by run(): each calls the hook on every registered module, in the order they were registered.
void modules_install_js(JSEngine& js);
void modules_on_page_loaded(const std::string& html_base_dir);
void modules_on_frame(KeyboardHandler& keyboard);
bool modules_want_frame();
void modules_shutdown();

} // namespace linweb
