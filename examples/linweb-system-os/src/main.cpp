#include "app/module.h"
#include "system_os_module.h"
#include <memory>

// Linweb with the OS module: the generic engine plus everything MLinxOS needs around it.
int main(int argc, char** argv) {
    linweb::register_module(std::make_unique<SystemOsModule>());
    return linweb::run(argc, argv);
}
