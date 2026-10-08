#include "window.h"
#include "gl_wrapper.h"
#include <GLFW/glfw3.h>
#include <stdexcept>

namespace linweb {

struct Window::Impl {
    GLFWwindow* window = nullptr;
};

static Key map_glfw_key(int glfw_key) {
    if (glfw_key >= GLFW_KEY_A && glfw_key <= GLFW_KEY_Z)
        return static_cast<Key>(static_cast<int>(Key::A) + (glfw_key - GLFW_KEY_A));
    if (glfw_key >= GLFW_KEY_0 && glfw_key <= GLFW_KEY_9)
        return static_cast<Key>(static_cast<int>(Key::Num0) + (glfw_key - GLFW_KEY_0));
    switch (glfw_key) {
        case GLFW_KEY_ENTER: return Key::Enter;
        case GLFW_KEY_ESCAPE: return Key::Escape;
        case GLFW_KEY_SPACE: return Key::Space;
        case GLFW_KEY_BACKSPACE: return Key::Backspace;
        case GLFW_KEY_LEFT: return Key::Left;
        case GLFW_KEY_UP: return Key::Up;
        case GLFW_KEY_RIGHT: return Key::Right;
        case GLFW_KEY_DOWN: return Key::Down;
        case GLFW_KEY_TAB: return Key::Tab;
        case GLFW_KEY_LEFT_SHIFT: return Key::LeftShift;
        case GLFW_KEY_RIGHT_SHIFT: return Key::RightShift;
        case GLFW_KEY_LEFT_CONTROL: return Key::LeftControl;
        case GLFW_KEY_RIGHT_CONTROL: return Key::RightControl;
        case GLFW_KEY_LEFT_ALT: return Key::LeftAlt;
        case GLFW_KEY_RIGHT_ALT: return Key::RightAlt;
        case GLFW_KEY_F2: return Key::F2;
        default: return Key::Unknown;
    }
}

static void glfw_scroll_adapter(GLFWwindow* glfw_win, double x, double y) {
    Window* win = static_cast<Window*>(glfwGetWindowUserPointer(glfw_win));
    if (win && win->get_scroll_callback()) win->get_scroll_callback()(win, x, y);
}

static void glfw_key_adapter(GLFWwindow* glfw_win, int key, int scancode, int action, int mods) {
    Window* win = static_cast<Window*>(glfwGetWindowUserPointer(glfw_win));
    if (win && win->get_key_callback()) {
        Action a = (action == GLFW_PRESS) ? Action::Press : (action == GLFW_RELEASE) ? Action::Release : Action::Repeat;
        win->get_key_callback()(win, map_glfw_key(key), scancode, a, mods);
    }
}

static void glfw_mouse_button_adapter(GLFWwindow* glfw_win, int button, int action, int mods) {
    Window* win = static_cast<Window*>(glfwGetWindowUserPointer(glfw_win));
    if (win && win->get_mouse_button_callback()) {
        MouseButton mb = (button == GLFW_MOUSE_BUTTON_LEFT) ? MouseButton::Left :
                         (button == GLFW_MOUSE_BUTTON_RIGHT) ? MouseButton::Right : MouseButton::Middle;
        Action a = (action == GLFW_PRESS) ? Action::Press : Action::Release;
        win->get_mouse_button_callback()(win, mb, a, mods);
    }
}

static void glfw_cursor_pos_adapter(GLFWwindow* glfw_win, double x, double y) {
    Window* win = static_cast<Window*>(glfwGetWindowUserPointer(glfw_win));
    if (win && win->get_cursor_pos_callback()) win->get_cursor_pos_callback()(win, x, y);
}

Window::Window() : impl_(new Impl()), width_(800), height_(600), title_("Linweb"),
    scroll_cb_(nullptr), key_cb_(nullptr), mouse_btn_cb_(nullptr), cursor_pos_cb_(nullptr) {}

Window::~Window() {
    shutdown();
    delete impl_;
}

bool Window::init(int width, int height, const std::string& title) {
    width_ = width;
    height_ = height;
    title_ = title;

    if (!glfwInit()) return false;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

    impl_->window = glfwCreateWindow(width_, height_, title_.c_str(), NULL, NULL);
    if (!impl_->window) {
        glfwTerminate();
        return false;
    }

    glfwMakeContextCurrent(impl_->window);
    glfwSetWindowUserPointer(impl_->window, this);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        glfwDestroyWindow(impl_->window);
        glfwTerminate();
        return false;
    }

    return true;
}

void Window::shutdown() {
    if (impl_->window) {
        glfwDestroyWindow(impl_->window);
        impl_->window = nullptr;
    }
    glfwTerminate();
}

bool Window::should_close() const {
    return impl_->window ? glfwWindowShouldClose(impl_->window) : true;
}

void Window::swap_buffers() const {
    if (impl_->window) glfwSwapBuffers(impl_->window);
}

void Window::poll_events() const {
    glfwPollEvents();
}

void Window::get_framebuffer_size(int& width, int& height) const {
    if (impl_->window) {
        glfwGetFramebufferSize(impl_->window, &width, &height);
    } else {
        width = 0;
        height = 0;
    }
}

void Window::get_window_size(int& width, int& height) const {
    if (impl_->window) {
        glfwGetWindowSize(impl_->window, &width, &height);
    } else {
        width = 0;
        height = 0;
    }
}

void Window::get_cursor_pos(double& x, double& y) const {
    if (impl_->window) {
        glfwGetCursorPos(impl_->window, &x, &y);
    } else {
        x = 0.0;
        y = 0.0;
    }
}

bool Window::is_mouse_button_pressed(MouseButton button) const {
    if (!impl_->window) return false;
    int b = (button == MouseButton::Left) ? GLFW_MOUSE_BUTTON_LEFT :
            (button == MouseButton::Right) ? GLFW_MOUSE_BUTTON_RIGHT : GLFW_MOUSE_BUTTON_MIDDLE;
    return glfwGetMouseButton(impl_->window, b) == GLFW_PRESS;
}

double Window::get_time() const {
    return glfwGetTime();
}

void Window::set_scroll_callback(ScrollCallback callback) {
    scroll_cb_ = callback;
    if (impl_->window) glfwSetScrollCallback(impl_->window, glfw_scroll_adapter);
}

void Window::set_key_callback(KeyCallback callback) {
    key_cb_ = callback;
    if (impl_->window) glfwSetKeyCallback(impl_->window, glfw_key_adapter);
}

void Window::set_mouse_button_callback(MouseButtonCallback callback) {
    mouse_btn_cb_ = callback;
    if (impl_->window) glfwSetMouseButtonCallback(impl_->window, glfw_mouse_button_adapter);
}

void Window::set_cursor_pos_callback(CursorPosCallback callback) {
    cursor_pos_cb_ = callback;
    if (impl_->window) glfwSetCursorPosCallback(impl_->window, glfw_cursor_pos_adapter);
}

void Window::make_context_current() {
    if (impl_->window) glfwMakeContextCurrent(impl_->window);
}

void Window::enable_blend() {
    glEnable(GL_BLEND);
}

void* Window::get_native_window() const {
    return impl_->window;
}

double get_platform_time() {
    return glfwGetTime();
}

} // namespace linweb
