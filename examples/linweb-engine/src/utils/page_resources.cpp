#include "utils/page_resources.h"
#include "utils/file_utils.h"
#include "utils/log.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iostream>
#include <set>
#include <sstream>

namespace linweb {
namespace {
bool space(char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; }
std::string local_path(const std::string& base, const std::string& ref) {
    if (ref.empty() || ref[0] == '#' || ref.find(":") != std::string::npos || ref.compare(0, 2, "//") == 0) return ref;
    std::string resolved = resolve_resource_path(base, ref);
    if (!file_exists(resolved)) return ref;
    return std::filesystem::absolute(resolved).generic_string();
}
struct UrlToken { size_t end = 0; std::string ref; };
UrlToken url_token(const std::string& css, size_t pos, bool bare) {
    size_t cursor = pos;
    if (!bare) {
        cursor += 3;
        while (cursor < css.size() && space(css[cursor])) ++cursor;
        if (cursor == css.size() || css[cursor++] != '(') return {};
        while (cursor < css.size() && space(css[cursor])) ++cursor;
    }
    if (cursor == css.size()) return {};
    char quote = css[cursor] == '\'' || css[cursor] == '"' ? css[cursor++] : 0;
    if (bare && !quote) return {};
    size_t start = cursor;
    while (cursor < css.size() && (quote ? css[cursor] != quote : css[cursor] != ')')) {
        if (css[cursor] == '\\') return {}; // Leave escaped CSS URLs intact.
        ++cursor;
    }
    if (cursor == css.size()) return {};
    std::string ref = css.substr(start, cursor - start);
    if (quote) ++cursor;
    if (!bare) {
        while (cursor < css.size() && space(css[cursor])) ++cursor;
        if (cursor == css.size() || css[cursor++] != ')') return {};
    }
    while (!ref.empty() && space(ref.back())) ref.pop_back();
    return {cursor, ref};
}
std::string process_css(const std::string& css, const std::string& base, std::set<std::string>& active, unsigned depth) {
    std::string out;
    for (size_t pos = 0; pos < css.size();) {
        if (css.compare(pos, 2, "/*") == 0) {
            size_t end = css.find("*/", pos + 2);
            end = end == std::string::npos ? css.size() : end + 2;
            out.append(css, pos, end - pos); pos = end; continue;
        }
        if (css.compare(pos, 7, "@import") == 0) {
            size_t start = pos + 7;
            while (start < css.size() && space(css[start])) ++start;
            bool url = css.compare(start, 3, "url") == 0;
            auto token = url_token(css, start, !url);
            size_t end = token.end ? css.find(';', token.end) : std::string::npos;
            if (end != std::string::npos && depth < 16) {
                std::string path = local_path(base, token.ref);
                if (file_exists(path)) {
                    std::string expanded;
                    if (active.insert(path).second) {
                        expanded = process_css(read_file(path), get_directory(path), active, depth + 1);
                        active.erase(path);
                    }
                    std::string media = css.substr(token.end, end - token.end);
                    if (media.find_first_not_of(" \t\r\n") != std::string::npos) out += "@media " + media + "{" + expanded + "}";
                    else out += expanded;
                    pos = end + 1; continue;
                }
            }
        }
        bool boundary = pos == 0 || (!std::isalnum(static_cast<unsigned char>(css[pos - 1])) && css[pos - 1] != '-' && css[pos - 1] != '_');
        if (boundary && css.compare(pos, 3, "url") == 0) {
            auto token = url_token(css, pos, false);
            if (token.end) {
                std::string path = local_path(base, token.ref);
                if (path.find_first_of("\"\\") == std::string::npos) out += "url(\"" + path + "\")";
                else out.append(css, pos, token.end - pos);
                pos = token.end; continue;
            }
        }
        char c = css[pos++];
        out += c;
        if (c == '\'' || c == '"') {
            while (pos < css.size()) {
                char next = css[pos++]; out += next;
                if (next == '\\' && pos < css.size()) out += css[pos++];
                else if (next == c) break;
            }
        }
    }
    return out;
}
void visit(const std::shared_ptr<Node>& node, const std::string& base, std::string& css, std::set<std::string>& active) {
    if (!node->is_element()) return;
    auto& attrs = node->data.attributes;
    if (node->tag_name == "img" && attrs.count("src")) attrs["src"] = local_path(base, attrs["src"]);
    if (attrs.count("style")) attrs["style"] = process_css(attrs["style"], base, active, 0);
    if (node->tag_name == "link" && attrs.count("href")) {
        std::istringstream rel(attrs.count("rel") ? attrs["rel"] : "");
        std::string word;
        bool stylesheet = false;
        while (rel >> word) if (word == "stylesheet") stylesheet = true;
        if (stylesheet) {
            std::string path = local_path(base, attrs["href"]);
            // a browser refuses a stylesheet whose file is not served as CSS; a local file is served as CSS by its extension
            std::string extension = std::filesystem::path(path).extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (file_exists(path) && extension != ".css") {
                log_event("css", "stylesheet skipped, not a .css file: " + path);
            } else if (file_exists(path)) {
                active.insert(path);
                std::string sheet = process_css(read_file(path), get_directory(path), active, 0);
                active.erase(path);
                css += attrs.count("media") && !attrs["media"].empty() ? "@media " + attrs["media"] + "{" + sheet + "}\n" : sheet + "\n";
            } else std::cerr << "Stylesheet not found: " << path << '\n';
        }
    }
    if (node->tag_name == "style") {
        for (const auto& child : node->children) if (child->is_text()) css += process_css(std::static_pointer_cast<TextNode>(child)->data, base, active, 0) + "\n";
    }
    for (const auto& child : node->children) visit(child, base, css, active);
}
}
std::string prepare_page_resources(const std::shared_ptr<Node>& root, const std::string& directory) {
    std::set<std::string> active;
    std::string css;
    visit(root, directory, css, active);
    return css;
}


}
