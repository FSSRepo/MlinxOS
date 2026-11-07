#pragma once
#include <cstddef>
#include <functional>
#include <string>

namespace linweb {
struct DownloadSummary {
    std::string html_path;
    std::string final_url;
    size_t downloaded = 0;
    size_t failed = 0;
};

// Downloads the page and its static dependencies. Each file goes to `sink` (its name next to the page: "index.html",
// "resource-<hash>.png", "manifest.tsv", ...); nothing is written to disk. Downloaded HTML/CSS references are rewritten to
// those names; JavaScript is not executed. The html_path of the summary is "index.html".
using FileSink = std::function<void(const std::string& name, const std::string& bytes)>;
DownloadSummary download_page(const std::string& url, const FileSink& sink);
} // namespace linweb
