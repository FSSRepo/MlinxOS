#pragma once
#include <vector>
#include <string>

namespace linweb {

/**
 * @brief Navigation system between local HTML pages.
 *
 * Maintains a history of visited URLs and allows navigating
 * forward and backward.
 */
class Navigator {
private:
    std::vector<std::string> history;
    size_t current_index;
    std::string current_url;

public:
    Navigator();

    /** Loads a new HTML page and adds it to the history */
    void load_page(const std::string& url);

    /** Navigates back in the history */
    void go_back();

    /** Navigates forward in the history */
    void go_forward();

    /** Returns the current URL */
    std::string get_current_url() const;

    /** Returns the full navigation history */
    const std::vector<std::string>& get_history() const;

    /** Checks whether it can go back */
    bool can_go_back() const;

    /** Checks whether it can go forward */
    bool can_go_forward() const;
};

} // namespace linweb
