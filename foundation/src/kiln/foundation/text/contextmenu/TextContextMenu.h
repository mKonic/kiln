#pragma once

// The text context menu (Compose's text/contextmenu): the menu's data (items, platform text
// classification items, separators), the builder scope that tidies it, the modifiers that add
// components to or filter them out of every menu below them, and the toolbar handler a text
// component shows its floating toolbar through (TextContextMenuToolbarHandlerNode with its
// ToolbarRequester; the provider is the owner's text toolbar).

#include "kiln/foundation/Compose.h"
#include <kiln/Function.h>
#include "kiln/runtime/Task.h"
#include "kiln/ui/TextToolbar.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace kiln::ui { class Owner; class ModifierNode; }

namespace kiln::foundation {

    // A component's identity (Compose keys are any object): a tag's address plus an index.
    struct TextContextMenuKey {
        const void* tag = nullptr;
        int index = 0;
        bool operator==(const TextContextMenuKey&) const = default;
    };

    // TextContextMenuKeys, and ProcessTextKey(index) for the PROCESS_TEXT activities.
    namespace TextContextMenuKeys {
        extern const TextContextMenuKey Cut, Copy, Paste, SelectAll, Autofill, LookUp;
        TextContextMenuKey ProcessText(int index);
        // The keys the platform toolbar knows (android.R.id.cut, ...).
        ui::TextToolbarItemId PlatformId(const TextContextMenuKey& key);
    }

    // TextContextMenuSession: closes the menu.
    class TextContextMenuSession {
    public:
        virtual ~TextContextMenuSession() = default;
        virtual void Close() = 0;
    };

    struct TextContextMenuItem {
        TextContextMenuKey key;
        std::string label;   // empty: the platform's string for a standard key
        int leadingIcon = 0;   // a drawable resource (the dropdown menu's icon)
        kiln::Function<void(TextContextMenuSession&)> onClick;
    };
    // TextContextMenuTextClassificationItem: an action of a TextClassification the host keeps
    // (index 0 is the primary one; -1 the legacy label and intent).
    struct TextContextMenuTextClassificationItem {
        TextContextMenuKey key;
        int classification = 0;
        int index = 0;
    };
    struct TextContextMenuSeparator {};

    using TextContextMenuComponent = std::variant<TextContextMenuSeparator, TextContextMenuItem, TextContextMenuTextClassificationItem>;
    inline bool IsSeparator(const TextContextMenuComponent& c) { return std::holds_alternative<TextContextMenuSeparator>(c); }
    // The key of an item (none for a separator).
    std::optional<TextContextMenuKey> KeyOf(const TextContextMenuComponent& c);

    struct TextContextMenuData {
        std::vector<TextContextMenuComponent> components;
    };

    class TextContextMenuBuilderScope {
    public:
        void Separator() { m_Components.emplace_back(TextContextMenuSeparator{}); }
        void Item(TextContextMenuKey key, std::string label, kiln::Function<void(TextContextMenuSession&)> onClick, int leadingIcon = 0) {
            m_Components.emplace_back(TextContextMenuItem{ key, std::move(label), leadingIcon, std::move(onClick) });
        }
        void TextClassificationItem(TextContextMenuKey key, int classification, int index) {
            m_Components.emplace_back(TextContextMenuTextClassificationItem{ key, classification, index });
        }
        void AddComponent(TextContextMenuComponent c) { m_Components.push_back(std::move(c)); }
        void AddFilter(kiln::Function<bool(const TextContextMenuComponent&)> filter) { m_Filters.push_back(std::move(filter)); }
        // No separators first, last or twice in a row; items every filter keeps.
        TextContextMenuData Build() const;

    private:
        std::vector<TextContextMenuComponent> m_Components;
        std::vector<kiln::Function<bool(const TextContextMenuComponent&)>> m_Filters;
    };

    using TextContextMenuBuilder = kiln::Function<void(TextContextMenuBuilderScope&)>;
    // appendTextContextMenuComponents / filterTextContextMenuComponents: for the menus of the
    // text components inside (nearest builders first).
    Modifier AppendTextContextMenuComponents(Modifier m, TextContextMenuBuilder builder);
    Modifier FilterTextContextMenuComponents(Modifier m, kiln::Function<bool(const TextContextMenuComponent&)> filter);
    // collectTextContextMenuData: the modifiers before `node` in its chain, then its ancestors'.
    TextContextMenuData CollectTextContextMenuData(const ui::ModifierNode& node);

    // The toolbar entries AndroidTextContextMenuToolbarProvider makes of the data (a separator
    // starts a new group).
    std::vector<ui::TextToolbarEntry> ToolbarEntriesOf(const TextContextMenuData& data);

    // TextContextMenuToolbarHandlerNode for a node (`anchor`, where the data is collected):
    // Show runs onShow, then shows the owner's toolbar until the session closes or Hide;
    // onHide follows either way.
    class TextContextMenuToolbarHandler final : public ui::TextToolbarClient {
    public:
        TextContextMenuToolbarHandler() = default;
        TextContextMenuToolbarHandler(const TextContextMenuToolbarHandler&) = delete;
        TextContextMenuToolbarHandler& operator=(const TextContextMenuToolbarHandler&) = delete;
        ~TextContextMenuToolbarHandler() override;

        kiln::Function<rt::Task<>()> onShow;
        kiln::Function<void()> onHide;
        // LocalTextContextMenuToolbarProvider where the component is: shows the toolbar in place
        // of the owner's platform toolbar (null: the platform's).
        class TextContextMenuProvider* toolbarProvider = nullptr;
        // The content bounds in root pixels (null: keep the previous ones).
        kiln::Function<std::optional<Rect>()> computeContentBounds;

        void Attach(ui::Owner* owner, const ui::ModifierNode* anchor);
        void Detach();
        bool Attached() const { return m_Owner != nullptr; }
        void Show();
        void Hide();
        bool Active() const { return m_Active; }
        // What computeContentBounds reads changed: the toolbar moves.
        void InvalidateContentBounds() { m_BoundsVersion.Set(m_BoundsVersion.Peek() + 1); }

        // TextToolbarClient
        Rect ContentBounds() override;
        std::vector<ui::TextToolbarEntry> Entries() override;
        void OnEntryClicked(std::size_t index) override;
        void OnClosed() override;

    private:
        struct Session;
        static rt::Task<> Run(TextContextMenuToolbarHandler* self, std::shared_ptr<Session> session, std::weak_ptr<char> alive);
        ui::Owner* m_Owner = nullptr;
        const ui::ModifierNode* m_Anchor = nullptr;
        rt::Job m_Job;
        bool m_Active = false;
        std::shared_ptr<Session> m_Session;
        TextContextMenuData m_Data;
        Rect m_PreviousContentBounds;
        rt::State<int> m_BoundsVersion{ 0 };
        std::shared_ptr<char> m_Alive = std::make_shared<char>(0);
    };

}
