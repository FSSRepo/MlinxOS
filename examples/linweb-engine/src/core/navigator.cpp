#include "core/navigator.h"

namespace linweb {

Navigator::Navigator() : current_index(0) {}

void Navigator::load_page(const std::string& url) {
    // Remove any forward history if we're not at the end
    if (current_index < history.size()) {
        history.resize(current_index);
    }
    history.push_back(url);
    current_url = url;
    current_index = history.size();
}

void Navigator::go_back() {
    if (can_go_back()) {
        --current_index;
        current_url = history[current_index - 1];
    }
}

void Navigator::go_forward() {
    if (can_go_forward()) {
        ++current_index;
        current_url = history[current_index - 1];
    }
}

std::string Navigator::get_current_url() const {
    return current_url;
}

const std::vector<std::string>& Navigator::get_history() const {
    return history;
}

bool Navigator::can_go_back() const {
    return current_index > 1;
}

bool Navigator::can_go_forward() const {
    return current_index < history.size();
}

} // namespace linweb
