#pragma once

namespace linweb {

enum class Key {
    Unknown = 0,
    A = 1, B, C, D, E, F, G, H, I, J, K, L, M,
    N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    Num0 = 27, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,
    Enter = 37,
    Escape = 38,
    Space = 39,
    Backspace = 40,
    Left = 41,
    Up = 42,
    Right = 43,
    Down = 44,
    Tab = 45,
    LeftShift = 46,
    RightShift = 47,
    LeftControl = 48,
    RightControl = 49,
    LeftAlt = 50,
    RightAlt = 51,
    F2 = 52,
};

enum class Action {
    Release = 0,
    Press = 1,
    Repeat = 2,
};

enum class MouseButton {
    Left = 0,
    Right = 1,
    Middle = 2,
};

} // namespace linweb
