#pragma once
#include <vector>
#include "app/options.h"
#include "scripting/js_engine.h"

namespace linweb {

// Presses the keys --keys schedules, as keydown and keyup events for the scripts, once their time has come.
class ScriptedKeys {
public:
    explicit ScriptedKeys(std::vector<ScriptedKey> keys);
    void update(JSEngine* js);

private:
    std::vector<ScriptedKey> keys_;
    size_t next_ = 0;
    double start_;
};

} // namespace linweb
