#pragma once
#include "scripting/js_engine.h"

// Registers every native function of the OS and the `linweb` object of the scripts (storage, gamepad, system, wifi, network, ...).
void register_system_natives(linweb::JSEngine& js);
