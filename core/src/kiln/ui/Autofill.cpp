#include "kiln/ui/Autofill.h"

#include "kiln/ui/LayoutNode.h"

#include <algorithm>
#include <cmath>

namespace kiln::ui {

    namespace {
        const char* kPasswordHint = "password";   // View.AUTOFILL_HINT_PASSWORD

        // isAutofillable: a data type other than None, and a fill action.
        bool IsAutofillable(const SemanticsConfiguration& c) {
            if (c.contentDataType == autofill::kTypeNone) return false;
            return bool(c.onFillData);
        }
        bool IsRelatedToAutoCommit(const SemanticsConfiguration& c) { return c.contentType.has_value(); }
        bool IsRelatedToAutofill(const SemanticsConfiguration& c, bool credentialRequests) {
            return bool(c.onFillData) || c.contentType.has_value() || c.contentDataType.has_value() || c.hintText.has_value() ||
                   (credentialRequests && c.credentialRequest);
        }
        IntRect ToIntRect(const Rect& r) {
            return { int(std::lround(r.left)), int(std::lround(r.top)), int(std::lround(r.right)), int(std::lround(r.bottom)) };
        }
        // Role.toLegacyClassName.
        const char* LegacyClassName(Role r) {
            switch (r) {
                case Role::Button: return "android.widget.Button";
                case Role::Checkbox: return "android.widget.CheckBox";
                case Role::RadioButton: return "android.widget.RadioButton";
                case Role::Image: return "android.widget.ImageView";
                case Role::DropdownList: return "android.widget.Spinner";
                case Role::ValuePicker: return "android.widget.NumberPicker";
                default: return nullptr;
            }
        }
    }

    std::optional<IntRect> AutofillManager::RectOf(int id) {
        const AccessibilityNode* n = m_Owner.Accessibility().FindAnyNode(id);
        if (!n) return std::nullopt;
        return ToIntRect(n->unclippedBoundsInRoot);
    }

    void AutofillManager::OnFocusChanged(LayoutNode* previous, LayoutNode* current) {
        if (previous) {
            SemanticsConfiguration c;
            if (CalculateSemanticsConfiguration(*previous, c) && IsAutofillable(c) && m_Host.notifyViewExited)
                m_Host.notifyViewExited(previous->SemanticsId());
        }
        if (current) {
            SemanticsConfiguration c;
            if (CalculateSemanticsConfiguration(*current, c) && IsAutofillable(c) && m_Host.notifyViewEntered) {
                const int id = current->SemanticsId();
                m_Host.notifyViewEntered(id, ToIntRect(current->BoundsInRoot()));
            }
        }
    }

    // notifyAutofillManager: data type, text, toggle and fillable data changes as visibility and
    // value notifications.
    void AutofillManager::Notify(int id, const Snapshot* prev, const Snapshot& cur) {
        auto visible = [&](bool v) { if (m_Host.notifyViewVisibilityChanged) m_Host.notifyViewVisibilityChanged(id, v); };
        auto value = [&](const FillableData& d) { if (m_Host.notifyValueChanged) m_Host.notifyValueChanged(id, d); };
        const std::optional<int> previousType = prev ? prev->contentDataType : std::nullopt;
        if (cur.contentDataType == autofill::kTypeNone) {
            if (previousType != autofill::kTypeNone) visible(false);
            return;
        }
        if (previousType == autofill::kTypeNone && cur.contentDataType != autofill::kTypeNone) visible(true);

        const std::optional<std::string> previousText = prev ? prev->inputText : std::nullopt;
        if (previousText != cur.inputText) {
            if (!previousText) visible(true);
            else if (!cur.inputText) visible(false);
            else if (cur.contentDataType == autofill::kTypeText) {
                FillableData d;
                d.kind = FillableData::Kind::Text;
                d.text = *cur.inputText;
                value(d);
            }
        }
        const std::optional<int> previousToggle = prev ? prev->toggleableState : std::nullopt;
        if (previousToggle != cur.toggleableState) {
            if (!previousToggle) visible(true);
            else if (!cur.toggleableState) visible(false);
            else if (cur.contentDataType == autofill::kTypeToggle && *cur.toggleableState != 2) {
                FillableData d;
                d.kind = FillableData::Kind::Toggle;
                d.toggle = *cur.toggleableState == 1;
                value(d);
            }
        }
        const std::optional<FillableData> previousFillable = prev ? prev->fillableData : std::nullopt;
        if (previousFillable != cur.fillableData) {
            if (!previousFillable) visible(true);
            else if (!cur.fillableData) visible(false);
            else value(*cur.fillableData);
        }
    }

    void AutofillManager::SyncSemantics() {
        if (m_SyncedVersion == m_Owner.SemanticsVersion()) return;
        m_SyncedVersion = m_Owner.SemanticsVersion();
        const auto& nodes = m_Owner.Accessibility().Nodes();
        std::unordered_map<int, Snapshot> next;
        for (const auto& [id, node] : nodes) {
            if (node.fake) continue;
            const SemanticsConfiguration& c = node.config;
            Snapshot s{ c.contentDataType, c.inputText, c.toggleableState, c.fillableData, IsRelatedToAutoCommit(c) };
            const auto it = m_Snapshots.find(id);
            const Snapshot* prev = it == m_Snapshots.end() ? nullptr : &it->second;
            if (!prev && s.relatedToAutoCommit) {
                // onPostAttach
                m_CurrentlyDisplayedIds.insert(id);
                if (m_Host.notifyViewVisibilityChanged) m_Host.notifyViewVisibilityChanged(id, true);
            }
            const bool changed = !prev || prev->contentDataType != s.contentDataType || prev->inputText != s.inputText ||
                                 prev->toggleableState != s.toggleableState || prev->fillableData != s.fillableData ||
                                 prev->relatedToAutoCommit != s.relatedToAutoCommit;
            if (changed && (s.contentDataType || s.inputText || s.toggleableState || s.fillableData || prev)) {
                // onSemanticsChanged
                Notify(id, prev, s);
                const bool wasRelated = prev && prev->relatedToAutoCommit;
                if (wasRelated != s.relatedToAutoCommit) {
                    if (s.relatedToAutoCommit) m_CurrentlyDisplayedIds.insert(id);
                    else m_CurrentlyDisplayedIds.erase(id);
                }
            }
            next.emplace(id, std::move(s));
        }
        // onDetach
        for (const auto& [id, s] : m_Snapshots)
            if (!next.count(id) && m_CurrentlyDisplayedIds.erase(id) && m_Host.notifyViewVisibilityChanged)
                m_Host.notifyViewVisibilityChanged(id, false);
        m_Snapshots = std::move(next);
    }

    void AutofillManager::OnEndApplyChanges() {
        SyncSemantics();
        if (m_CurrentlyDisplayedIds.empty() && m_PendingAutofillCommit) {
            Commit();
            m_PendingAutofillCommit = false;
        }
        if (!m_CurrentlyDisplayedIds.empty()) m_PendingAutofillCommit = true;
    }

    void AutofillManager::RequestAutofill(LayoutNode& node) {
        if (!m_Host.requestAutofill) return;
        m_Host.requestAutofill(node.SemanticsId(), ToIntRect(node.BoundsInRoot()));
    }

    std::vector<AutofillStructureNode> AutofillManager::Structure() {
        AccessibilityDelegate& a11y = m_Owner.Accessibility();
        const auto& nodes = a11y.Nodes();
        std::vector<AutofillStructureNode> out;
        m_CredentialRequests.clear();
        auto populate = [&](const AccessibilityNode& n, int parentIndex) {
            AutofillStructureNode s;
            s.id = n.id;
            s.parent = parentIndex;
            const SemanticsConfiguration& c = n.config;
            if (!c.contentDescription.empty()) s.contentDescription = c.contentDescription.front();
            if (c.hintText) s.hint = c.hintText;
            s.focused = c.focused;
            if (c.onClick) s.clickable = true;
            if (c.onLongClick) s.longClickable = true;
            if (c.requestFocus) s.focusable = true;
            const SemanticsConfiguration merged = a11y.MergedConfig(n);
            if (merged.disabled) s.enabled = false;
            const bool hasSetTextAction = bool(c.setText);
            if (c.contentDataType) s.autofillType = *c.contentDataType;
            else if (hasSetTextAction) s.autofillType = autofill::kTypeText;
            else if (c.toggleableState) s.autofillType = autofill::kTypeToggle;
            if (c.editableText) {
                FillableData d;
                d.text = *c.editableText;
                s.value = d;
            }
            if (c.fillableData) s.value = c.fillableData;
            if (c.contentType) s.hints = *c.contentType;
            s.bounds = ToIntRect(n.unclippedBoundsInRoot);
            s.selected = c.selected;
            if (c.toggleableState) {
                s.checkable = true;
                s.checked = *c.toggleableState == 1;
            } else if (c.selected && c.role != Role::Tab) {
                s.checkable = true;
                s.checked = *c.selected;
            }
            const bool contentTypePassword = c.contentType && std::find(c.contentType->begin(), c.contentType->end(), kPasswordHint) != c.contentType->end();
            const bool isPassword = c.password || contentTypePassword;
            s.sensitive = isPassword || c.isSensitiveData.value_or(true);
            if (!merged.text.empty()) {
                std::string joined;
                for (const auto& t : merged.text) joined += t + "\n";
                s.text = joined;
                s.className = "android.widget.TextView";
            }
            if (n.children.empty())
                if (const char* name = LegacyClassName(c.role)) s.className = name;
            if (hasSetTextAction) {
                s.className = "android.widget.EditText";
                s.maxTextLength = c.maxTextLength;
                s.passwordInputType = isPassword;
            }
            if (m_Host.credentialRequestsSupported && c.credentialRequest) {
                s.credentialRequest = c.credentialRequest;
                m_CredentialRequests[n.id] = c.credentialRequest;
            }
            out.push_back(std::move(s));
            return int(out.size()) - 1;
        };
        const auto rootIt = nodes.find(a11y::kHostViewId);
        if (rootIt == nodes.end()) return out;
        populate(rootIt->second, -1);
        // Children in order; a node unrelated to autofill is skipped but its children are not.
        std::vector<std::pair<const AccessibilityNode*, int>> stack{ { &rootIt->second, 0 } };
        while (!stack.empty()) {
            const auto [parent, parentIndex] = stack.back();
            stack.pop_back();
            for (int childId : parent->children) {
                const auto it = nodes.find(childId);
                if (it == nodes.end() || it->second.fake) continue;
                const AccessibilityNode& child = it->second;
                if (!IsRelatedToAutofill(child.config, m_Host.credentialRequestsSupported)) {
                    stack.push_back({ &child, parentIndex });
                    continue;
                }
                stack.push_back({ &child, populate(child, parentIndex) });
            }
        }
        return out;
    }

    void AutofillManager::OnCredentialResult(int id, const Credential& credential) {
        const auto it = m_CredentialRequests.find(id);
        if (it == m_CredentialRequests.end()) return;
        const auto request = it->second;   // the callback may rebuild the structure
        if (request->onResult) request->onResult(credential);
    }

    void AutofillManager::OnCredentialError(int id, const std::string& type, const std::string& message) {
        const auto it = m_CredentialRequests.find(id);
        if (it == m_CredentialRequests.end()) return;
        const auto request = it->second;
        if (request->onError) request->onError(type, message);
    }

    void AutofillManager::PerformAutofill(int id, const FillableData& value) {
        const AccessibilityNode* n = m_Owner.Accessibility().FindAnyNode(id);
        if (n && n->config.onFillData) n->config.onFillData(value);
    }

}
