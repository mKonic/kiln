#pragma once

#include <string>

namespace kiln::gallery {
    // `only`: a section's key ("split", "menus", ...) to show it alone; empty for all of them.
    void MaterialComponents(const std::string& only = {});
    // The section to show alone: debug.kiln.gallery.section on Android, KILN_GALLERY_SECTION on the host.
    std::string RequestedSection();
}
