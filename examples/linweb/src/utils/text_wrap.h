#pragma once
#include <functional>
#include <string>
#include <vector>

namespace linweb {

// Breaks a text into lines at its spaces. The first line has `first_width` available, the others `width`. A word wider
// than its line stays whole on a line of its own. The spaces at a break are dropped; those at the ends of the text stay,
// because the text may continue in the next box.
inline std::vector<std::string> wrap_text(const std::string& text, float first_width, float width,
                                          const std::function<float(const std::string&)>& measure)
{
    std::vector<std::string> words;
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find(' ', start);
        if (end == std::string::npos) end = text.size();
        words.push_back(text.substr(start, end - start));
        start = end + 1;
    }
    const bool leading_space = !text.empty() && text.front() == ' ';
    std::vector<std::string> lines;
    std::string line;
    float limit = first_width;
    for (size_t i = 0; i < words.size(); ++i) {
        const std::string& word = words[i];
        if (word.empty()) continue;   // the space between two spaces, or at an end of the text
        std::string candidate;
        if (line.empty()) candidate = (lines.empty() && leading_space) ? " " + word : word;
        else candidate = line + " " + word;
        if (line.empty() || measure(candidate) <= limit) {
            line = candidate;
        } else {
            lines.push_back(line);
            line = word;
            limit = width;
        }
    }
    if (!text.empty() && text.back() == ' ' && !line.empty() && line.back() != ' ') line += ' ';
    if (!line.empty() || lines.empty()) lines.push_back(line);
    return lines;
}

} // namespace linweb
