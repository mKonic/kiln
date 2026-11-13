#pragma once

// Autofill (Compose's AndroidAutofillManager and PopulateViewStructure): the platform
// AutofillManager hears which autofillable node is focused, the values and visibility of
// autofill-related nodes as their semantics change, and commits once every node with a content
// type has gone. The host builds the virtual view structure from Structure() and hands filled
// values to PerformAutofill.

#include "kiln/ui/Accessibility.h"
#include <kiln/Function.h>
#include "kiln/ui/Semantics.h"

#include <functional>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace kiln::ui {

    class Owner;
    class LayoutNode;

    // PlatformAutofillManager: android.view.autofill.AutofillManager for the host view's virtual
    // children (by semantics id, bounds in root pixels).
    struct AutofillHost {
        kiln::Function<void(int id, IntRect bounds)> notifyViewEntered;
        kiln::Function<void(int id)> notifyViewExited;
        kiln::Function<void(int id, const FillableData& value)> notifyValueChanged;
        kiln::Function<void(int id, bool visible)> notifyViewVisibilityChanged;
        kiln::Function<void()> commit;
        kiln::Function<void()> cancel;
        kiln::Function<void(int id, IntRect bounds)> requestAutofill;
        // CredentialRequest nodes are autofill nodes (API 34+).
        bool credentialRequestsSupported = false;
    };

    // One ViewStructure node (ViewStructure.populate).
    struct AutofillStructureNode {
        int id = a11y::kHostViewId;
        int parent = -1;                       // index of the parent node; -1 for the root
        std::optional<int> autofillType;       // View.AUTOFILL_TYPE_*
        std::optional<FillableData> value;
        std::vector<std::string> hints;
        IntRect bounds;                        // in root pixels
        std::optional<std::string> contentDescription;
        std::optional<std::string> hint;       // HintText (ViewStructure.setHint)
        std::optional<bool> focused;
        bool enabled = true, clickable = false, longClickable = false, focusable = false;
        std::optional<bool> selected;
        bool checkable = false, checked = false;
        bool sensitive = true;
        std::optional<std::string> text;       // the merged text, one line per entry
        std::optional<std::string> className;
        std::optional<int> maxTextLength;
        bool passwordInputType = false;        // TYPE_CLASS_TEXT | TYPE_TEXT_VARIATION_PASSWORD
        std::shared_ptr<const CredentialRequestData> credentialRequest;   // setPendingCredentialRequest
    };

    class AutofillManager {
    public:
        AutofillManager(Owner& owner, AutofillHost host) : m_Owner(owner), m_Host(std::move(host)) {}

        void Commit() { if (m_Host.commit) m_Host.commit(); }
        void Cancel() { if (m_Host.cancel) m_Host.cancel(); }
        // FocusListener.onFocusChanged.
        void OnFocusChanged(LayoutNode* previous, LayoutNode* current);
        // After each layout pass: the semantics changes since the last one (onSemanticsChanged,
        // onPostAttach, onDetach), then onEndApplyChanges' commit.
        void OnEndApplyChanges();
        void RequestAutofill(LayoutNode& node);
        // populateViewStructure: the root first, every node after its parent.
        std::vector<AutofillStructureNode> Structure();
        // performAutofill: one value for the node with this semantics id.
        void PerformAutofill(int id, const FillableData& value);
        // The pending credential request's outcome for the node (from the last Structure()).
        void OnCredentialResult(int id, const Credential& credential);
        void OnCredentialError(int id, const std::string& type, const std::string& message);

    private:
        struct Snapshot {
            std::optional<int> contentDataType;
            std::optional<std::string> inputText;
            std::optional<int> toggleableState;
            std::optional<FillableData> fillableData;
            bool relatedToAutoCommit = false;
        };
        void SyncSemantics();
        void Notify(int id, const Snapshot* previous, const Snapshot& current);
        std::optional<IntRect> RectOf(int id);

        Owner& m_Owner;
        AutofillHost m_Host;
        std::unordered_map<int, Snapshot> m_Snapshots;
        std::uint64_t m_SyncedVersion = ~0ull;
        std::set<int> m_CurrentlyDisplayedIds;
        std::unordered_map<int, std::shared_ptr<const CredentialRequestData>> m_CredentialRequests;
        bool m_PendingAutofillCommit = false;
    };

}
