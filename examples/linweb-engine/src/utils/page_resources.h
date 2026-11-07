#pragma once
#include "core/dom.h"

namespace linweb {
// Resolve local image/inline-style paths and collect styles in document order.
// Expands downloaded CSS imports; never issues network requests.
std::string prepare_page_resources(const std::shared_ptr<Node>& root, const std::string& directory);

}
