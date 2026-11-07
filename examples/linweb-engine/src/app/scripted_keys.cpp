#include "app/scripted_keys.h"
#include <algorithm>
#include <cctype>
#include <map>
#include "platform/platform_config.h"

namespace linweb {

ScriptedKeys::ScriptedKeys(std::vector<ScriptedKey> keys) : keys_(std::move(keys)), start_(get_platform_time())
{
    std::sort(keys_.begin(), keys_.end(), [](const ScriptedKey& a, const ScriptedKey& b) { return a.at < b.at; });
}

void ScriptedKeys::update(JSEngine* js)
{
    while (next_ < keys_.size() && get_platform_time() - start_ >= keys_[next_].at) {
        const std::string& name = keys_[next_++].name;
        static const std::map<std::string, int> codes = {{"Enter", 13}, {"Escape", 27}, {"Backspace", 8}, {"Tab", 9}, {" ", 32},
            {"ArrowLeft", 37}, {"ArrowUp", 38}, {"ArrowRight", 39}, {"ArrowDown", 40}};
        auto code = codes.find(name);
        const int key_code = code != codes.end() ? code->second : (name.size() == 1 ? std::toupper(static_cast<unsigned char>(name[0])) : 0);
        const std::string data = "{ key: '" + name + "', code: '" + name + "', keyCode: " + std::to_string(key_code) + ", which: " + std::to_string(key_code) +
            ", shiftKey: false, ctrlKey: false, altKey: false, metaKey: false, repeat: false, preventDefault: function() {} }";
        if (js) {
            js->dispatch_event("document", "keydown", data);
            js->dispatch_event("window", "keydown", data);
            js->dispatch_event("document", "keyup", data);
            js->dispatch_event("window", "keyup", data);
        }
    }
}

} // namespace linweb
