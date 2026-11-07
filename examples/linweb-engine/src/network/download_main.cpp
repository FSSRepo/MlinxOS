#include "network/page_downloader.h"
#include <exception>
#include <iostream>

int main(int argc, char** argv) {
    if (argc < 2 || argc > 3) {
        std::cerr << "Usage: linweb-download URL [output-directory]\n";
        return 1;
    }
    try {
        auto result = linweb::download_page(argv[1], argc == 3 ? argv[2] : "downloads/page");
        std::cout << "Saved: " << result.html_path << "\nDownloaded: " << result.downloaded
                  << ", failed: " << result.failed << "\n";
        return result.failed ? 2 : 0;
    } catch (const std::exception& e) {
        std::cerr << "Download failed: " << e.what() << '\n';
        return 1;
    }
}
