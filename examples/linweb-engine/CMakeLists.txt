cmake_minimum_required(VERSION 3.10)
project(LinWebEngine C CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(LINWEB_BUILD_ENGINE "Build the OpenGL renderer in addition to the downloader" ON)

# Portable HTTP/TLS library. net_sockets.c uses Winsock on Windows; no POSIX
# wrapper from node-lite is required. Keep the CA bundle inside the executable.
file(GLOB LINWEB_MBEDTLS_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/external/mbedtls/library/*.c")
add_library(linweb_mbedtls STATIC ${LINWEB_MBEDTLS_SOURCES})
target_include_directories(linweb_mbedtls PUBLIC external/mbedtls/include PRIVATE external/mbedtls/library)
if(WIN32)
    target_compile_definitions(linweb_mbedtls PRIVATE _WIN32_WINNT=0x0601 _CRT_SECURE_NO_WARNINGS)
    target_link_libraries(linweb_mbedtls PUBLIC ws2_32 bcrypt)
endif()

set(LINWEB_CA_FILE "${CMAKE_CURRENT_SOURCE_DIR}/external/ca/cacert.pem")
file(READ "${LINWEB_CA_FILE}" LINWEB_CA_BUNDLE)
string(REGEX MATCHALL "-----BEGIN CERTIFICATE-----[^-]*-----END CERTIFICATE-----" LINWEB_CA_CERTS "${LINWEB_CA_BUNDLE}")
string(REPLACE ";" "\n" LINWEB_CA_PEM "${LINWEB_CA_CERTS}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/linweb-ca.pem" "${LINWEB_CA_PEM}\n")
file(READ "${CMAKE_CURRENT_BINARY_DIR}/linweb-ca.pem" LINWEB_CA_HEX HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," LINWEB_CA_ARRAY "${LINWEB_CA_HEX}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/linweb_ca.c"
    "const unsigned char linweb_ca_pem[] = {${LINWEB_CA_ARRAY}0x00};\nconst unsigned long linweb_ca_pem_len = sizeof(linweb_ca_pem) - 1;\n")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${LINWEB_CA_FILE}")

add_library(linweb_network STATIC src/network/http_client.cpp src/network/page_downloader.cpp
    "${CMAKE_CURRENT_BINARY_DIR}/linweb_ca.c")
target_include_directories(linweb_network PUBLIC src)
target_link_libraries(linweb_network PUBLIC linweb_mbedtls)
add_executable(linweb-download src/network/download_main.cpp)
target_link_libraries(linweb-download PRIVATE linweb_network)

# Media: MP3 (OpenMP3), MP4 demuxer + H.264 (FFmpeg's decoder) + AAC (FDK), and the sound device through miniaudio.
# The headers of the decoders use generic names (types.h, mp4.h, config.h, ...), so their include paths stay private.
find_package(Threads REQUIRED)
add_subdirectory(external/ffmpeg)
add_subdirectory(external/libaac)
# The bytes of the media files: a file, or an http(s) URL read in blocks (used by the MP3 and the MP4 parsers)
add_library(linweb_io STATIC src/media/byte_reader.cpp)
target_include_directories(linweb_io PUBLIC src)
target_link_libraries(linweb_io PUBLIC linweb_network Threads::Threads)
add_library(linweb_mp3 STATIC
    external/openmp3/huffman.cpp
    external/openmp3/requantize.cpp
    external/openmp3/stereo.cpp
    external/openmp3/synthesis.cpp
    external/openmp3/tables.cpp
    src/media/mp3/decode.cpp
    src/media/mp3/mp3_stream.cpp)
target_include_directories(linweb_mp3 PRIVATE external/openmp3 src/media/mp3)
target_link_libraries(linweb_mp3 PUBLIC linweb_io)
add_library(linweb_media STATIC
    src/media/audio_output.cpp
    src/media/audio_player.cpp
    src/media/video_player.cpp
    src/media/mp4/mp4.cpp
    src/media/mp4/yuv.cpp)
target_include_directories(linweb_media PUBLIC src PRIVATE src/media/mp4 external external/ffmpeg external/libaac/include)
target_link_libraries(linweb_media PUBLIC linweb_io linweb_mp3 h264 aac Threads::Threads ${CMAKE_DL_LIBS})
if(MSVC)
    target_compile_definitions(linweb_media PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
endif()
add_executable(linweb-media-test src/media/media_test.cpp)
target_link_libraries(linweb-media-test PRIVATE linweb_media)

if(NOT LINWEB_BUILD_ENGINE)
    return()
endif()

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
    src/app/app_state.cpp
    src/app/module.cpp
    src/app/run.cpp
    src/app/frame.cpp
    src/app/js_api.cpp
    src/app/options.cpp
    src/app/scripted_keys.cpp
    src/app/startup_report.cpp
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
    src/layout/table_layout.cpp
    src/layout/positioned_layout.cpp
    src/renderer/renderer_core.cpp
    src/renderer/renderer.cpp
    src/renderer/primitive_renderer.cpp
    src/renderer/image_renderer.cpp
    src/renderer/mask_renderer.cpp
    src/renderer/shadow_blur.cpp
    src/renderer/background_renderer.cpp
    src/renderer/text_renderer.cpp
    src/renderer/custom_font.cpp
    src/renderer/svg_renderer.cpp
    src/renderer/shader_manager.cpp
    src/renderer/font_manager.cpp
    src/renderer/texture_manager.cpp
    src/renderer/svg_loader.cpp
    src/renderer/video_surface.cpp
    src/renderer/scrollbar.cpp
    src/scripting/js_engine.cpp
    src/scripting/js_runtime.cpp
    src/scripting/js_context.cpp
    src/scripting/dom_bindings.cpp
    src/scripting/style_bindings.cpp
    src/scripting/event_bindings.cpp
    src/scripting/timer_bindings.cpp
    src/scripting/console_bindings.cpp
    src/scripting/layout_bindings.cpp
    src/scripting/window_bindings.cpp
    src/scripting/media_bindings.cpp
    src/utils/file_utils.cpp
    src/utils/log.cpp
    src/utils/page_resources.cpp
    src/utils/dom_traversal.cpp
    src/utils/layout_traversal.cpp
    src/utils/scroll_manager.cpp
    src/platform/input_handler.cpp
    src/platform/scroll_handler.cpp
    src/platform/keyboard_handler.cpp
    src/platform/key_event.cpp
    src/forms/form_controls.cpp
    src/forms/form_bindings.cpp
    src/renderer/form_renderer.cpp
    src/platform/mouse_handler.cpp
    src/debug/debug_visualizer.cpp
    src/debug/frame_capture.cpp
    src/debug/node_tree_view.cpp
    src/debug/anim_panel.cpp
    src/debug/fps_counter.cpp
    src/debug/profiler.cpp
    src/debug/layout_log.cpp
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

# The engine is a library; the executable below only calls linweb::run(). A program with native modules (see src/app/module.h)
# links linweb_engine as well, registers its modules and calls linweb::run().
set(LINWEB_DIR ${CMAKE_CURRENT_SOURCE_DIR})
add_library(linweb_engine STATIC ${LINWEB_SOURCES})
target_link_libraries(linweb_engine PUBLIC linweb_network linweb_media)   # linweb_media: <audio> and <video>; linweb_network: http(s) URLs

target_include_directories(linweb_engine PUBLIC
    ${LINWEB_DIR}/src
    ${LINWEB_DIR}/external
    ${LINWEB_DIR}/external/glad
    ${LINWEB_DIR}/external/quickjs
    ${LINWEB_DIR}/external/stb
    ${LINWEB_DIR}/external/nanosvg
)

if(WESTON_PLATFORM)
    target_include_directories(linweb_engine PUBLIC ${LINWEB_DIR})   # xdg-shell-client-protocol.h
endif()

# Link libraries
if(WESTON_PLATFORM)
    target_link_libraries(linweb_engine PUBLIC GLESv2 wayland-client wayland-egl EGL xkbcommon qjs)
    target_compile_definitions(linweb_engine PUBLIC WESTON_PLATFORM)
elseif(XORG_PLATFORM)
    target_link_libraries(linweb_engine PUBLIC OpenGL::GL X11 EGL qjs)
    target_compile_definitions(linweb_engine PUBLIC XORG_PLATFORM)
else()
    target_link_libraries(linweb_engine PUBLIC glfw OpenGL::GL qjs)
    target_compile_definitions(linweb_engine PUBLIC GLFW_INCLUDE_NONE)
endif()

# Compile definitions for QuickJS
target_compile_definitions(linweb_engine PRIVATE
    CONFIG_VERSION="2021-03-27"
    _GNU_SOURCE
)

# Math library for UNIX
if(UNIX)
    target_link_libraries(linweb_engine PUBLIC m)
endif()

# The generic executable: the engine with no modules
add_executable(linweb src/main.cpp)
target_link_libraries(linweb PRIVATE linweb_engine)
add_custom_command(TARGET linweb POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${LINWEB_DIR}/assets/fonts/arial.ttf"
        "${LINWEB_DIR}/assets/fonts/arialbd.ttf"
        "${LINWEB_DIR}/assets/fonts/monospace.ttf"
        "$<TARGET_FILE_DIR:linweb>"
    COMMENT "Copying renderer fonts")
