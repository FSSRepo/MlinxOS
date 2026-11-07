#pragma once
#include "core/dom.h"

namespace linweb {
// Resolve local image/inline-style paths and collect styles in document order.
// Expands downloaded CSS imports; never issues network requests.
std::string prepare_page_resources(const std::shared_ptr<Node>& root, const std::string& directory);

// downloads/<host and path of the URL>, so each page keeps its own copy of its files.
std::string download_directory_for(const std::string& url);
}
