#pragma once

// The text toolbar's entries in tests: their platform ids, and clicking one by id.

#include "kiln/ui/LayoutNode.h"

#include <vector>

namespace kiln::tests {

    inline std::vector<ui::TextToolbarItemId> ToolbarIds(const ui::Owner& owner) {
        std::vector<ui::TextToolbarItemId> out;
        for (const auto& e : owner.TextToolbarEntries()) out.push_back(e.id);
        return out;
    }

    inline bool ClickToolbar(ui::Owner& owner, ui::TextToolbarItemId id) {
        const auto& entries = owner.TextToolbarEntries();
        for (std::size_t i = 0; i < entries.size(); ++i)
            if (entries[i].id == id) { owner.OnTextToolbarItem(i); return true; }
        return false;
    }

}
