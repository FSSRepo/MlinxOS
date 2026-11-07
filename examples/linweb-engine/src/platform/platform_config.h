#pragma once

#ifdef WESTON_PLATFORM
    #define LINWEB_PLATFORM_WESTON 1
#elif defined(XORG_PLATFORM)
    #define LINWEB_PLATFORM_XORG 1
#else
    #define LINWEB_PLATFORM_GLFW 1
#endif

namespace linweb {

double get_platform_time();

} // namespace linweb
