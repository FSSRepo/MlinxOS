#include "app/module.h"

// The generic engine, with no modules. A program with native modules registers them (linweb::register_module) and then calls linweb::run.
int main(int argc, char** argv) {
    return linweb::run(argc, argv);
}
