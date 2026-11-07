cmake_minimum_required(VERSION 3.10)
project(LinwebSystemOS C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# The generic engine (a copy of the Linweb-web-engine repository made by sync-engine.sh). It is never edited here.
set(LINWEB_ENGINE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../linweb-engine" CACHE PATH "The generic Linweb engine")
if(NOT EXISTS "${LINWEB_ENGINE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "The Linweb engine was not found at ${LINWEB_ENGINE_DIR}: run sync-engine.sh or pass -DLINWEB_ENGINE_DIR=...")
endif()
# WESTON_PLATFORM / XORG_PLATFORM are options of the engine: pass them on the command line (-DWESTON_PLATFORM=ON)
add_subdirectory(${LINWEB_ENGINE_DIR} engine EXCLUDE_FROM_ALL)

file(GLOB MODULE_SOURCES
    src/*.cpp
    src/system/*.cpp
    src/scripting/*.cpp)

add_executable(linweb-system-os ${MODULE_SOURCES})
target_include_directories(linweb-system-os PRIVATE src)
target_link_libraries(linweb-system-os PRIVATE linweb_engine)
if(UNIX)
    target_link_libraries(linweb-system-os PRIVATE m)
endif()
add_custom_command(TARGET linweb-system-os POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${LINWEB_ENGINE_DIR}/assets/fonts/arial.ttf"
        "${LINWEB_ENGINE_DIR}/assets/fonts/arialbd.ttf"
        "${LINWEB_ENGINE_DIR}/assets/fonts/monospace.ttf"
        "$<TARGET_FILE_DIR:linweb-system-os>"
    COMMENT "Copying renderer fonts")
