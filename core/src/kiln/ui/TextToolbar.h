#pragma once

// The platform side of the text context menu (Compose's text/contextmenu on Android): the floating
// toolbar's entries as AndroidTextContextMenuToolbarProvider puts them in the ActionMode menu,
// the client that supplies them (TextContextMenuDataProvider), and the TextClassifier, process
// text and Look Up services the platform items come from (PlatformSelectionBehaviors,
// ProcessText, LookUpText).

#include "kiln/ui/Geometry.h"
#include <kiln/Function.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace kiln::ui {

    // The platform menu item ids the toolbar gives the standard items (android.R.id.cut, ...).
    enum class TextToolbarItemId : std::uint8_t { None, Cut, Copy, Paste, SelectAll, Autofill, LookUp };

    struct TextToolbarEntry {
        enum class Kind : std::uint8_t { Item, Classification };
        Kind kind = Kind::Item;
        TextToolbarItemId id = TextToolbarItemId::None;
        // The title; empty for a standard item: the platform's string for its id.
        std::string label;
        int group = 1;   // separators start a new group
        // A classification item: the TextClassification's handle and action index (-1: the
        // legacy label and intent).
        int classification = 0, index = 0;
        bool operator==(const TextToolbarEntry&) const = default;
    };

    // What the toolbar shows: its entries and the content bounds in root pixels, both read
    // observed (a change updates the menu or moves it). A click goes to the entry; the toolbar
    // closing on its own (dismissed, or nothing to show) ends the session with OnClosed.
    class TextToolbarClient {
    public:
        virtual ~TextToolbarClient() = default;
        virtual Rect ContentBounds() = 0;
        virtual std::vector<TextToolbarEntry> Entries() = 0;
        virtual void OnEntryClicked(std::size_t index) = 0;
        virtual void OnClosed() = 0;
    };

    // TextClassification as the menu needs it: the handle the host keeps the object under, the
    // action titles, and whether it has a legacy assist item.
    struct TextClassificationInfo {
        int handle = 0;
        std::vector<std::string> actions;
        bool legacyAssistItem = false;
    };

    enum class SelectedTextType : std::uint8_t { EditableText, StaticText };

    // An icon the host drew (a theme drawable or a classification action's icon) as RGBA,
    // premultiplied, tightly packed.
    struct HostIcon {
        int width = 0, height = 0;
        std::vector<std::uint8_t> rgba;
    };

    // What the dropdown text context menu needs from the platform (DefaultTextContextMenuDropdown):
    // the standard items' strings and action mode icons, a custom item's icon (its leadingIcon
    // theme attribute), the classification items' titles, icons and actions, and the popup menu
    // theme's colours (Widget.PopupMenu's colorBackground, TextAppearance.Widget.PopupMenu.Large's
    // textColorPrimary enabled / disabled; 0 for the default).
    struct ContextMenuHost {
        kiln::Function<std::string(TextToolbarItemId)> standardLabel;
        kiln::Function<std::optional<HostIcon>(TextToolbarItemId, int sizePx)> standardIcon;
        kiln::Function<std::optional<HostIcon>(int themeAttr, int sizePx)> themeIcon;
        kiln::Function<std::string(int classification, int index)> classificationLabel;
        kiln::Function<std::optional<HostIcon>(int classification, int index, int sizePx)> classificationIcon;
        kiln::Function<void(int classification, int index)> performClassification;
        struct Colors { std::uint32_t background = 0, text = 0, disabledText = 0; };
        kiln::Function<Colors()> colors;
    };

    // PlatformSelectionBehaviorsImpl's platform calls. Requests run off the main thread with
    // Compose's timeouts (300 ms to create the session, 200 ms to classify); `done` runs once,
    // on the main thread, with nothing after a timeout or a failure.
    struct TextClassifierHost {
        // An id for a TextClassifier session of the widget type, made on first use; 0: none.
        kiln::Function<int(SelectedTextType)> createSession;
        kiln::Function<void(int session)> destroySession;
        // suggestSelection (with the classification from API 31).
        kiln::Function<void(int session, const std::u16string& text, int start, int end, const std::vector<std::string>& locales,
                           kiln::Function<void(std::optional<std::pair<int, int>> range, std::optional<TextClassificationInfo>)> done)>
            suggestSelection;
        kiln::Function<void(int session, const std::u16string& text, int start, int end, const std::vector<std::string>& locales,
                           kiln::Function<void(std::optional<TextClassificationInfo>)> done)>
            classifyText;
        kiln::Function<void(int handle)> releaseClassification;
        // ACTION_PROCESS_TEXT activities the app may start (their labels, in order), and starting
        // one of the last query with the selected text.
        kiln::Function<std::vector<std::string>()> processTextActivities;
        kiln::Function<void(int index, bool editable, const std::u16string& selected)> processText;
        // LookUpText (API 37): whether an activity handles the text, and starting it.
        kiln::Function<bool(const std::u16string&)> canLookUp;
        kiln::Function<void(const std::u16string&)> lookUp;
    };

}
