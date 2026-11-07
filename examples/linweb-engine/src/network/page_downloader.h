#pragma once
#include <cstddef>
#include <string>

namespace linweb {
struct DownloadSummary {
    std::string html_path;
    std::string final_url;
    size_t downloaded = 0;
    size_t failed = 0;
};

// Saves index.html, static dependencies and manifest.tsv in output_directory.
// Rewrites downloaded HTML/CSS references to local files; does not execute JS.
DownloadSummary download_page(const std::string& url, const std::string& output_directory);
} // namespace linweb
