#pragma once

// PlatformSelectionBehaviors (PlatformSelectionBehaviors.android.kt): smart selection through
// the platform TextClassifier. A long press or double click's word grows to the entity around
// it (an address, a phone number), and the floating toolbar leads with the classification's
// actions (call, open in Maps). The process text activities (ProcessText.android.kt) and Look
// Up follow the text items.

#include "kiln/foundation/text/contextmenu/TextContextMenu.h"
#include <kiln/Function.h>
#include "kiln/foundation/text/input/TextFieldBuffer.h"
#include "kiln/runtime/State.h"
#include "kiln/runtime/Task.h"
#include "kiln/ui/TextToolbar.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace kiln::ui { class Owner; }

namespace kiln::foundation {

    class PlatformSelectionBehaviors {
    public:
        // Null without a TextClassifier host (rememberPlatformSelectionBehaviors before API 28).
        static std::unique_ptr<PlatformSelectionBehaviors> Create(ui::Owner* owner, ui::SelectedTextType type, std::vector<std::string> locales);
        PlatformSelectionBehaviors(ui::Owner* owner, ui::SelectedTextType type, std::vector<std::string> locales);
        ~PlatformSelectionBehaviors();
        PlatformSelectionBehaviors(const PlatformSelectionBehaviors&) = delete;
        PlatformSelectionBehaviors& operator=(const PlatformSelectionBehaviors&) = delete;

        ui::Owner* GetOwner() const { return m_Owner; }
        ui::SelectedTextType Type() const { return m_Type; }
        const std::vector<std::string>& Locales() const { return m_Locales; }

        // The range TextClassifier suggests for the selection (and its classification, kept);
        // null for none.
        rt::Task<std::optional<TextRange>> SuggestSelectionForLongPressOrDoubleClick(std::u16string text, TextRange selection);
        // Classifies the selection before the toolbar shows, unless the kept result is for it.
        rt::Task<> OnShowSelectionToolbar(std::u16string text, TextRange selection);
        // onShowContextMenu: the same, before the dropdown menu shows (the click is not used).
        rt::Task<> OnShowContextMenu(std::u16string text, TextRange selection, std::optional<Offset> = std::nullopt) {
            return OnShowSelectionToolbar(std::move(text), selection);
        }

        // addSmartSelectionTextContextMenuItems: the primary action (or the legacy assist item)
        // before `child`'s items, the other actions after (all when the kept result is for this
        // text and selection).
        void AddSmartSelectionTextContextMenuItems(TextContextMenuBuilderScope& scope, const std::u16string& text, TextRange selection,
                                                   const kiln::Function<void(TextContextMenuBuilderScope&)>& child);

    private:
        struct Result {
            std::u16string text;
            TextRange selection;
            ui::TextClassificationInfo info;
        };
        int Session();
        void KeepResult(std::u16string text, TextRange selection, ui::TextClassificationInfo info);
        bool CanReuse(const std::u16string& text, TextRange selection) const;
        rt::Task<> ClassifyText(std::u16string text, TextRange selection);

        ui::Owner* m_Owner;
        ui::SelectedTextType m_Type;
        std::vector<std::string> m_Locales;
        int m_Session = 0;
        rt::State<std::shared_ptr<const Result>> m_Result;
        std::shared_ptr<char> m_Alive = std::make_shared<char>(0);
    };

    // addPlatformTextContextMenuItems: the smart selection items around `child` when there are
    // behaviors, then the process text items.
    void AddPlatformTextContextMenuItems(TextContextMenuBuilderScope& scope, ui::Owner* owner, bool editable, const std::u16string* text,
                                         std::optional<TextRange> selection, PlatformSelectionBehaviors* behaviors,
                                         const kiln::Function<void(TextContextMenuBuilderScope&)>& child);
    // addProcessedTextContextMenuItems: one item per ACTION_PROCESS_TEXT activity, between
    // separators, for a non-empty selection.
    void AddProcessedTextContextMenuItems(TextContextMenuBuilderScope& scope, ui::Owner* owner, bool editable, const std::u16string& text,
                                          TextRange selection);
    // canShowLookUp / startLookUpActivity (API 37).
    bool CanShowLookUp(ui::Owner* owner, const std::u16string& selectedText);
    void StartLookUpActivity(ui::Owner* owner, const std::u16string& selectedText);

    // A style's locale list ("en-US,fr") as tags.
    std::vector<std::string> SplitLocaleList(const std::optional<std::string>& list);

}
