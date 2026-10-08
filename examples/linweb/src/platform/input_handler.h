#pragma once

namespace linweb {

class Window;

class InputHandler {
public:
    virtual ~InputHandler() = default;

    virtual void setup(Window* window) {}
    virtual void update() {}
};

} // namespace linweb
