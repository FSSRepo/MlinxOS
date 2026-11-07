#include "network/page_downloader.h"
#include "network/http_client.h"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace linweb {
namespace {
std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
// Media files are streamed when they play: downloading one with the page would take as long as the whole file (and the
// downloader refuses more than 64 MiB). That is the case of what an <audio> or <video> plays, and of any resource whose
// file name says it is audio or video.
bool is_streamed(const std::string& tag, const std::string& value) {
    if (tag == "audio" || tag == "video") return true;
    std::string path = lower(value.substr(0, value.find_first_of("?#")));
    const size_t dot = path.rfind('.');
    if (dot == std::string::npos) return false;
    static const char* const media[] = {".mp3", ".mp4", ".m4a", ".m4v", ".aac", ".ogg", ".oga", ".ogv", ".opus", ".wav",
                                        ".flac", ".webm", ".mkv", ".mov", ".avi"};
    const std::string extension = path.substr(dot);
    for (const char* known : media) if (extension == known) return true;
    return false;
}
std::string trim(const std::string& s) {
    size_t first = s.find_first_not_of(" \t\r\n");
    return first == std::string::npos ? "" : s.substr(first, s.find_last_not_of(" \t\r\n") - first + 1);
}
std::string decode_attribute(std::string s) {
    // URL attributes commonly escape query separators. Numeric ASCII entities
    // are also accepted; leave other entities untouched.
    static const std::regex entities(R"(&(amp|quot|apos|lt|gt|#[0-9]+|#x[0-9a-fA-F]+);)");
    std::string result;
    size_t pos = 0;
    for (std::sregex_iterator it(s.begin(), s.end(), entities), end; it != end; ++it) {
        const auto& m = *it;
        result += s.substr(pos, m.position() - pos);
        std::string e = m[1];
        if (e == "amp") result += '&';
        else if (e == "quot") result += '"';
        else if (e == "apos") result += '\'';
        else if (e == "lt") result += '<';
        else if (e == "gt") result += '>';
        else {
            try {
                unsigned long c = std::stoul(e.substr(e[1] == 'x' ? 2 : 1), nullptr, e[1] == 'x' ? 16 : 10);
                if (c > 0 && c < 128) result += static_cast<char>(c);
                else result += m.str();
            } catch (...) { result += m.str(); }
        }
        pos = m.position() + m.length();
    }
    return result + s.substr(pos);
}
struct Patch { size_t start, length; std::string value; };
std::string patched(std::string text, std::vector<Patch> patches) {
    std::sort(patches.begin(), patches.end(), [](const Patch& a, const Patch& b) { return a.start > b.start; });
    for (const auto& p : patches) text.replace(p.start, p.length, p.value);
    return text;
}
struct Attribute { std::string name, value; size_t start, length; };
struct Tag {
    std::string name;
    std::vector<Attribute> attributes;
    size_t content_start = 0, content_length = 0;
    std::string get(const std::string& name) const {
        for (const auto& a : attributes) if (a.name == name) return a.value;
        return "";
    }
};
bool space(char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; }
std::vector<Tag> scan_tags(const std::string& html) {
    std::vector<Tag> tags;
    std::string folded = lower(html);
    size_t pos = 0;
    while ((pos = html.find('<', pos)) != std::string::npos) {
        if (html.compare(pos, 4, "<!--") == 0) {
            size_t end = html.find("-->", pos + 4);
            pos = end == std::string::npos ? html.size() : end + 3;
            continue;
        }
        ++pos;
        if (pos == html.size()) break;
        if (!std::isalpha(static_cast<unsigned char>(html[pos]))) continue;
        Tag tag;
        size_t start = pos;
        while (pos < html.size() && !space(html[pos]) && html[pos] != '/' && html[pos] != '>') ++pos;
        tag.name = folded.substr(start, pos - start);
        while (pos < html.size() && html[pos] != '>') {
            if (space(html[pos]) || html[pos] == '/') { ++pos; continue; }
            start = pos;
            while (pos < html.size() && !space(html[pos]) && html[pos] != '=' && html[pos] != '>') ++pos;
            std::string name = folded.substr(start, pos - start);
            while (pos < html.size() && space(html[pos])) ++pos;
            if (pos == html.size() || html[pos] != '=') continue;
            ++pos;
            while (pos < html.size() && space(html[pos])) ++pos;
            if (pos == html.size()) break;
            char quote = (html[pos] == '\'' || html[pos] == '"') ? html[pos++] : 0;
            start = pos;
            while (pos < html.size() && (quote ? html[pos] != quote : !space(html[pos]) && html[pos] != '>')) ++pos;
            tag.attributes.push_back({name, decode_attribute(html.substr(start, pos - start)), start, pos - start});
            if (quote && pos < html.size()) ++pos;
        }
        if (pos < html.size()) ++pos;
        // Avoid treating markup-like JavaScript strings as actual resources.
        if (tag.name == "script" || tag.name == "style" || tag.name == "textarea" || tag.name == "title") {
            tag.content_start = pos;
            size_t end = folded.find("</" + tag.name, pos);
            tag.content_length = (end == std::string::npos ? html.size() : end) - pos;
            pos += tag.content_length;
        }
        tags.push_back(std::move(tag));
    }
    return tags;
}
void write_file(const std::filesystem::path& path, const std::string& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out || !out.write(bytes.data(), static_cast<std::streamsize>(bytes.size())))
        throw std::runtime_error("Cannot write " + path.string());
}
std::string safe_field(std::string s) {
    for (char& c : s) if (c == '\t' || c == '\r' || c == '\n') c = ' ';
    return s;
}
std::string filename(const std::string& url, bool css) {
    uint64_t hash = 14695981039346656037ull;
    for (unsigned char c : url) { hash ^= c; hash *= 1099511628211ull; }
    std::string path = url.substr(0, url.find_first_of("?#")), extension;
    size_t dot = path.find_last_of('.'), slash = path.find_last_of('/');
    if (dot != std::string::npos && dot > slash && path.size() - dot <= 10) {
        extension = lower(path.substr(dot));
        if (extension.find_first_not_of(".abcdefghijklmnopqrstuvwxyz0123456789") != std::string::npos) extension.clear();
    }
    if (css) extension = ".css";
    if (extension.empty()) extension = ".bin";
    std::ostringstream name;
    name << "resource-" << std::hex << std::setw(16) << std::setfill('0') << hash << extension;
    return name.str();
}
struct Entry { std::string url, file, error; size_t bytes = 0; };
class Downloader {
    HttpClient client;
    std::filesystem::path directory;
    std::map<std::string, Entry> entries;
    size_t total_bytes = 0;
    size_t failures = 0;
    size_t downloaded = 0;

    void account(size_t bytes) {
        constexpr size_t max_total = 256 * 1024 * 1024;
        if (bytes > max_total - total_bytes) throw std::runtime_error("Page download exceeds 256 MiB");
        total_bytes += bytes;
    }
    std::string resource(const std::string& base, const std::string& reference, bool css, unsigned depth) {
        std::string ref = trim(reference);
        if (ref.empty() || ref.front() == '#' || lower(ref).compare(0, 5, "data:") == 0) return reference;
        std::string url;
        try { url = resolve_url(base, ref); }
        catch (const std::exception&) { return reference; } // blob:, javascript:, etc.
        size_t fragment_pos = url.find('#');
        std::string fragment = fragment_pos == std::string::npos ? "" : url.substr(fragment_pos);
        url = url.substr(0, fragment_pos);
        auto found = entries.find(url);
        if (found != entries.end()) return found->second.error.empty() ? found->second.file + fragment : url + fragment;
        Entry entry{url, filename(url, css), "", 0};
        // Reserve before recursion so cyclic CSS imports terminate.
        auto& saved = entries.emplace(url, entry).first->second;
        try {
            if (entries.size() > 512 || depth > 16) throw std::runtime_error("Static dependency limit reached (512 resources / 16 levels)");
            std::cout << "GET " << url << std::endl;
            HttpResponse response = client.get(url);
            saved.bytes = response.body.size();
            account(saved.bytes);
            auto content_type = response.headers.find("content-type");
            bool stylesheet = css || (content_type != response.headers.end() && lower(content_type->second).find("text/css") != std::string::npos);
            // Extensionless SVG/image URLs still need an appropriate local
            // extension for Linweb's file-based resource dispatch.
            if (saved.file.size() >= 4 && saved.file.compare(saved.file.size() - 4, 4, ".bin") == 0 &&
                content_type != response.headers.end()) {
                static const std::map<std::string, std::string> extensions = {
                    {"image/svg+xml", ".svg"}, {"image/png", ".png"}, {"image/jpeg", ".jpg"},
                    {"image/gif", ".gif"}, {"image/webp", ".webp"}, {"text/css", ".css"},
                    {"text/javascript", ".js"}, {"application/javascript", ".js"},
                    {"font/woff", ".woff"}, {"font/woff2", ".woff2"}
                };
                auto extension = extensions.find(lower(trim(content_type->second.substr(0, content_type->second.find(';')))));
                if (extension != extensions.end()) saved.file.replace(saved.file.size() - 4, 4, extension->second);
            }
            if (stylesheet) response.body = rewrite_css(response.body, response.url, depth + 1);
            write_file(directory / saved.file, response.body);
            ++downloaded;
            return saved.file + fragment;
        } catch (const std::exception& e) {
            saved.error = e.what();
            ++failures;
            std::cerr << "Resource failed: " << url << ": " << saved.error << std::endl;
            return url + fragment;
        }
    }
    std::string rewrite_css(const std::string& css, const std::string& base, unsigned depth) {
        // Skip comments and quoted strings; only url() and quoted @import
        // references trigger downloads. JavaScript is never inspected.
        std::vector<Patch> patches;
        std::string folded = lower(css);
        size_t pos = 0;
        while (pos < css.size()) {
            if (css.compare(pos, 2, "/*") == 0) {
                size_t end = css.find("*/", pos + 2);
                pos = end == std::string::npos ? css.size() : end + 2;
                continue;
            }
            bool import = folded.compare(pos, 7, "@import") == 0;
            bool url = folded.compare(pos, 3, "url") == 0 &&
                (pos == 0 || (!std::isalnum(static_cast<unsigned char>(css[pos - 1])) && css[pos - 1] != '-' && css[pos - 1] != '_'));
            size_t cursor = pos + (import ? 7 : 3);
            if (import || url) {
                while (cursor < css.size() && space(css[cursor])) ++cursor;
                if (import && folded.compare(cursor, 3, "url") == 0) {
                    url = true;
                    cursor += 3;
                    while (cursor < css.size() && space(css[cursor])) ++cursor;
                }
                if (url && cursor < css.size() && css[cursor] == '(') {
                    ++cursor;
                    while (cursor < css.size() && space(css[cursor])) ++cursor;
                } else if (!import) url = false;
                if ((import || url) && cursor < css.size()) {
                    char quote = css[cursor] == '\'' || css[cursor] == '"' ? css[cursor++] : 0;
                    if (url || quote) {
                        size_t start = cursor;
                        while (cursor < css.size() && (quote ? css[cursor] != quote : css[cursor] != ')')) {
                            if (css[cursor] == '\\' && cursor + 1 < css.size()) ++cursor;
                            ++cursor;
                        }
                        std::string ref = trim(css.substr(start, cursor - start));
                        // Escaped CSS URLs need a full CSS tokenizer; retain them.
                        if (ref.find('\\') == std::string::npos) {
                            std::string replacement = resource(base, ref, import, depth);
                            // Quote unquoted URL replacements so failed absolute
                            // URLs with parentheses remain valid CSS.
                            patches.push_back({start, cursor - start, quote ? replacement : "\"" + replacement + "\""});
                        }
                        pos = cursor < css.size() ? cursor + 1 : cursor;
                        continue;
                    }
                }
            }
            if (css[pos] == '\'' || css[pos] == '"') {
                char quote = css[pos++];
                while (pos < css.size() && css[pos] != quote) {
                    if (css[pos] == '\\' && pos + 1 < css.size()) ++pos;
                    ++pos;
                }
            }
            if (pos < css.size()) ++pos;
        }
        return patched(css, std::move(patches));
    }
    std::string rewrite_srcset(const std::string& value, const std::string& base) {
        // data: URLs contain commas; preserve those lists rather than split them.
        if (lower(value).find("data:") != std::string::npos) return value;
        std::istringstream candidates(value);
        std::string item, result;
        while (std::getline(candidates, item, ',')) {
            item = trim(item);
            size_t end = item.find_first_of(" \t\r\n");
            if (!result.empty()) result += ", ";
            result += resource(base, item.substr(0, end), false, 0);
            if (end != std::string::npos) result += item.substr(end);
        }
        return result;
    }
public:
    explicit Downloader(const std::string& output) : directory(std::filesystem::absolute(output)) {
        std::filesystem::create_directories(directory);
    }
    DownloadSummary run(const std::string& url) {
        std::cout << "GET " << url << std::endl;
        HttpResponse response = client.get(url);
        account(response.body.size());
        auto tags = scan_tags(response.body);
        std::string base = response.url;
        for (const auto& tag : tags) {
            if (tag.name == "base" && !tag.get("href").empty()) { base = resolve_url(base, tag.get("href")); break; }
        }
        std::vector<Patch> patches;
        for (const auto& tag : tags) {
            std::string rel = " " + lower(tag.get("rel")) + " ";
            std::replace(rel.begin(), rel.end(), '\t', ' ');
            std::replace(rel.begin(), rel.end(), '\n', ' ');
            bool stylesheet = rel.find(" stylesheet ") != std::string::npos;
            bool link_resource = stylesheet || rel.find(" icon ") != std::string::npos ||
                rel.find(" preload ") != std::string::npos || rel.find(" modulepreload ") != std::string::npos;
            for (const auto& attr : tag.attributes) {
                std::string replacement;
                bool change = true;
                if (tag.name == "base" && attr.name == "href") replacement = ".";
                else if (attr.name == "style") replacement = rewrite_css(attr.value, base, 0);
                else if ((tag.name == "img" || tag.name == "source") && attr.name == "srcset") replacement = rewrite_srcset(attr.value, base);
                else if ((attr.name == "src" && (tag.name == "img" || tag.name == "script" || tag.name == "source" ||
                         tag.name == "audio" || tag.name == "video" || tag.name == "track" || tag.name == "embed" ||
                         (tag.name == "input" && lower(tag.get("type")) == "image"))) ||
                         (tag.name == "video" && attr.name == "poster") ||
                         (tag.name == "object" && attr.name == "data") ||
                         (tag.name == "link" && attr.name == "href" && link_resource))
                    // what is streamed when it is used (audio, video) keeps its address, made absolute because the page
                    // is loaded from a local copy; the rest of the files the page depends on are downloaded
                    replacement = is_streamed(tag.name, attr.value) ? resolve_url(base, attr.value)
                        : resource(base, attr.value, tag.name == "link" && (stylesheet || lower(tag.get("as")) == "style"), 0);
                else change = false;
                if (change) {
                    // All values are quoted, including originally unquoted
                    // attributes; use entity escaping for retained URL values.
                    std::string escaped;
                    for (char c : replacement) {
                        if (c == '&') escaped += "&amp;";
                        else if (c == '"') escaped += "&quot;";
                        else if (c == '\'') escaped += "&#39;";
                        else if (c == '<') escaped += "&lt;";
                        else escaped += c;
                    }
                    bool quoted = attr.start > 0 && (response.body[attr.start - 1] == '\'' || response.body[attr.start - 1] == '"');
                    patches.push_back({attr.start, attr.length, quoted ? escaped : "\"" + escaped + "\""});
                }
            }
            if (tag.name == "style") patches.push_back({tag.content_start, tag.content_length,
                rewrite_css(response.body.substr(tag.content_start, tag.content_length), base, 0)});
        }
        write_file(directory / "index.html", patched(response.body, std::move(patches)));
        std::ostringstream manifest;
        manifest << "url\tfile\tbytes\tstatus\n" << safe_field(response.url) << "\tindex.html\t" << response.body.size() << "\tok\n";
        for (const auto& pair : entries) {
            const auto& entry = pair.second;
            manifest << safe_field(entry.url) << '\t' << (entry.error.empty() ? entry.file : "") << '\t' << entry.bytes << '\t'
                     << (entry.error.empty() ? "ok" : safe_field(entry.error)) << '\n';
        }
        write_file(directory / "manifest.tsv", manifest.str());
        return {(directory / "index.html").string(), response.url, downloaded + 1, failures};
    }
};
} // namespace
DownloadSummary download_page(const std::string& url, const std::string& output_directory) {
    return Downloader(output_directory).run(url);
}
} // namespace linweb
