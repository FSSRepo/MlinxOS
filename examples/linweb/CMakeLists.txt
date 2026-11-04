cmake_minimum_required(VERSION 3.10)
project(OpenGLExample C CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Platform selection
option(WESTON_PLATFORM "Build with Wayland/Weston support instead of GLFW" OFF)
option(XORG_PLATFORM "Build with X11/EGL support instead of GLFW" OFF)

if(NOT WESTON_PLATFORM AND NOT XORG_PLATFORM)
    # Find GLFW3
    find_package(glfw3 CONFIG REQUIRED)
endif()

# Find OpenGL (the Weston build uses GLESv2/EGL instead)
if(NOT WESTON_PLATFORM)
    find_package(OpenGL REQUIRED)
endif()

# Add QuickJS
add_subdirectory(external/quickjs)

# Collect all source files
set(LINWEB_SOURCES
    src/main.cpp
    src/core/dom.cpp
    src/core/dom_helpers.cpp
    src/core/navigator.cpp
    src/parser/html/html_parser.cpp
    src/parser/html/html_tokenizer.cpp
    src/parser/html/html_element_parser.cpp
    src/parser/html/html_text_parser.cpp
    src/parser/css/css_parser.cpp
    src/parser/css/css_tokenizer.cpp
    src/parser/css/css_selector_parser.cpp
    src/parser/css/css_declaration_parser.cpp
    src/parser/css/css_keyframe_parser.cpp
    src/style/style_engine.cpp
    src/style/selector_matcher.cpp
    src/style/property_resolver.cpp
    src/style/transform_parser.cpp
    src/style/transition_engine.cpp
    src/style/animation_engine.cpp
    src/layout/layout_engine.cpp
    src/layout/box_resolver.cpp
    src/layout/block_layout.cpp
    src/layout/inline_layout.cpp
    src/layout/flex_layout.cpp
    src/layout/grid_layout.cpp
    src/layout/positioned_layout.cpp
    src/renderer/renderer_core.cpp
    src/renderer/renderer.cpp
    src/renderer/primitive_renderer.cpp
    src/renderer/image_renderer.cpp
    src/renderer/text_renderer.cpp
    src/renderer/svg_renderer.cpp
    src/renderer/shader_manager.cpp
    src/renderer/font_manager.cpp
    src/renderer/texture_manager.cpp
    src/renderer/svg_loader.cpp
    src/renderer/scrollbar.cpp
    src/scripting/js_engine.cpp
    src/scripting/js_runtime.cpp
    src/scripting/js_context.cpp
    src/scripting/dom_bindings.cpp
    src/scripting/style_bindings.cpp
    src/scripting/event_bindings.cpp
    src/scripting/timer_bindings.cpp
    src/scripting/console_bindings.cpp
    src/scripting/storage_bindings.cpp
    src/system/storage.cpp
    src/utils/file_utils.cpp
    src/utils/dom_traversal.cpp
    src/utils/layout_traversal.cpp
    src/utils/scroll_manager.cpp
    src/platform/input_handler.cpp
    src/platform/scroll_handler.cpp
    src/platform/keyboard_handler.cpp
    src/platform/mouse_handler.cpp
    src/debug/debug_visualizer.cpp
    src/debug/node_tree_view.cpp
)

if(NOT WESTON_PLATFORM)
    list(APPEND LINWEB_SOURCES external/glad/glad.c)
endif()

if(WESTON_PLATFORM)
    list(APPEND LINWEB_SOURCES src/platform/window_weston.cpp xdg-shell-client-protocol.c)
elseif(XORG_PLATFORM)
    list(APPEND LINWEB_SOURCES src/platform/window_xorg.cpp)
else()
    list(APPEND LINWEB_SOURCES src/platform/window.cpp)
endif()

# Create the executable
add_executable(linweb ${LINWEB_SOURCES})

# Include directories
target_include_directories(linweb PRIVATE
    src
    external
    external/glad
    external/quickjs
    external/stb
    external/nanosvg
)

if(WESTON_PLATFORM)
    target_include_directories(linweb PRIVATE ${CMAKE_SOURCE_DIR})
endif()

# Link libraries
if(WESTON_PLATFORM)
    target_link_libraries(linweb PRIVATE GLESv2 wayland-client wayland-egl EGL qjs)
    target_compile_definitions(linweb PRIVATE WESTON_PLATFORM)
elseif(XORG_PLATFORM)
    target_link_libraries(linweb PRIVATE OpenGL::GL X11 EGL qjs)
    target_compile_definitions(linweb PRIVATE XORG_PLATFORM)
else()
    target_link_libraries(linweb PRIVATE glfw OpenGL::GL qjs)
    target_compile_definitions(linweb PRIVATE GLFW_INCLUDE_NONE)
endif()

# Compile definitions for QuickJS
target_compile_definitions(linweb PRIVATE
    CONFIG_VERSION="2021-03-27"
    _GNU_SOURCE
)

# Math library for UNIX
if(UNIX)
    target_link_libraries(linweb PRIVATE m)
endif()