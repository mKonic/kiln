#include "kiln/foundation/text/PlatformSelectionBehaviors.h"
#include <kiln/Function.h>

#include "kiln/runtime/Channel.h"
#include "kiln/ui/LayoutNode.h"

#include <algorithm>
#include <cwctype>

namespace kiln::foundation {

    namespace {
        // The key every smart selection item shares (AssistantItemKey).
        char kAssistantItemTag;
        const TextContextMenuKey kAssistantItemKey{ &kAssistantItemTag };

        bool IsBlank(const std::u16string& s) {
            return std::all_of(s.begin(), s.end(), [](char16_t c) { return c < 0x80 ? std::iswspace(wint_t(c)) != 0 : c == 0x00A0 || c == 0x2007 || c == 0x202F || (c >= 0x2000 && c <= 0x200A) || c == 0x2028 || c == 0x2029 || c == 0x205F || c == 0x3000 || c == 0x1680; });
        }
    }

    std::unique_ptr<PlatformSelectionBehaviors> PlatformSelectionBehaviors::Create(ui::Owner* owner, ui::SelectedTextType type, std::vector<std::string> locales) {
        if (!owner || !owner->TextClassifier().classifyText) return nullptr;
        return std::make_unique<PlatformSelectionBehaviors>(owner, type, std::move(locales));
    }

    PlatformSelectionBehaviors::PlatformSelectionBehaviors(ui::Owner* owner, ui::SelectedTextType type, std::vector<std::string> locales)
        : m_Owner(owner), m_Type(type), m_Locales(std::move(locales)) {}

    PlatformSelectionBehaviors::~PlatformSelectionBehaviors() {
        m_Alive.reset();
        if (!m_Owner) return;
        const auto& host = m_Owner->TextClassifier();
        if (const auto& r = m_Result.Peek(); r && host.releaseClassification) host.releaseClassification(r->info.handle);
        if (m_Session && host.destroySession) host.destroySession(m_Session);
    }

    int PlatformSelectionBehaviors::Session() {
        if (!m_Session && m_Owner->TextClassifier().createSession) m_Session = m_Owner->TextClassifier().createSession(m_Type);
        return m_Session;
    }

    void PlatformSelectionBehaviors::KeepResult(std::u16string text, TextRange selection, ui::TextClassificationInfo info) {
        const auto previous = m_Result.Peek();
        m_Result.Set(std::make_shared<const Result>(Result{ std::move(text), selection, std::move(info) }));
        if (previous && m_Owner->TextClassifier().releaseClassification) m_Owner->TextClassifier().releaseClassification(previous->info.handle);
    }

    bool PlatformSelectionBehaviors::CanReuse(const std::u16string& text, TextRange selection) const {
        const auto& r = m_Result.Peek();
        return r && r->selection == selection && r->text == text;
    }

    rt::Task<std::optional<TextRange>> PlatformSelectionBehaviors::SuggestSelectionForLongPressOrDoubleClick(std::u16string text, TextRange selection) {
        if (text.empty() || selection.Collapsed()) co_return std::nullopt;
        const auto& host = m_Owner->TextClassifier();
        if (!host.suggestSelection) co_return std::nullopt;
        struct Reply {
            std::optional<std::pair<int, int>> range;
            std::optional<ui::TextClassificationInfo> info;
        };
        auto reply = std::make_shared<rt::Channel<Reply>>(1);
        // TextSelection.Request with the classification (API 31); the host classifies the new
        // range itself when the suggestion comes without one.
        host.suggestSelection(Session(), text, selection.Min(), selection.Max(), m_Locales,
                              [reply](std::optional<std::pair<int, int>> range, std::optional<ui::TextClassificationInfo> info) {
                                  reply->TrySend(Reply{ range, std::move(info) });
                              });
        std::weak_ptr<char> alive = m_Alive;
        auto r = co_await reply->Next();
        if (alive.expired() || !r || !r->range) {
            if (r && r->info && !alive.expired() && host.releaseClassification) host.releaseClassification(r->info->handle);
            co_return std::nullopt;
        }
        const TextRange suggested(r->range->first, r->range->second);
        if (r->info) KeepResult(text, suggested, std::move(*r->info));
        co_return suggested;
    }

    rt::Task<> PlatformSelectionBehaviors::ClassifyText(std::u16string text, TextRange selection) {
        if (CanReuse(text, selection)) co_return;
        const auto& host = m_Owner->TextClassifier();
        auto reply = std::make_shared<rt::Channel<std::optional<ui::TextClassificationInfo>>>(1);
        host.classifyText(Session(), text, selection.Min(), selection.Max(), m_Locales,
                          [reply](std::optional<ui::TextClassificationInfo> info) { reply->TrySend(std::move(info)); });
        std::weak_ptr<char> alive = m_Alive;
        auto r = co_await reply->Next();
        if (!r || !*r) co_return;
        if (alive.expired()) {
            if (host.releaseClassification) host.releaseClassification((*r)->handle);
            co_return;
        }
        KeepResult(std::move(text), selection, std::move(**r));
    }

    rt::Task<> PlatformSelectionBehaviors::OnShowSelectionToolbar(std::u16string text, TextRange selection) {
        if (text.empty() || selection.Collapsed()) co_return;
        co_await ClassifyText(std::move(text), selection);
    }

    void PlatformSelectionBehaviors::AddSmartSelectionTextContextMenuItems(TextContextMenuBuilderScope& scope, const std::u16string& text, TextRange selection,
                                                                           const kiln::Function<void(TextContextMenuBuilderScope&)>& child) {
        // tryGetTextClassificationResult reads the kept result (observed: a new one rebuilds).
        const auto result = m_Result.Get();
        if (!result || result->selection != selection || result->text != text) {
            child(scope);
            return;
        }
        const ui::TextClassificationInfo& info = result->info;
        if (!info.actions.empty()) scope.TextClassificationItem(kAssistantItemKey, info.handle, 0);
        else if (info.legacyAssistItem) scope.TextClassificationItem(kAssistantItemKey, info.handle, -1);
        child(scope);
        for (std::size_t i = 1; i < info.actions.size(); ++i) scope.TextClassificationItem(kAssistantItemKey, info.handle, int(i));
    }

    void AddPlatformTextContextMenuItems(TextContextMenuBuilderScope& scope, ui::Owner* owner, bool editable, const std::u16string* text,
                                         std::optional<TextRange> selection, PlatformSelectionBehaviors* behaviors,
                                         const kiln::Function<void(TextContextMenuBuilderScope&)>& child) {
        if (!text || !selection || !behaviors) {
            child(scope);
            if (text && selection) AddProcessedTextContextMenuItems(scope, owner, editable, *text, *selection);
            return;
        }
        behaviors->AddSmartSelectionTextContextMenuItems(scope, *text, *selection, child);
        AddProcessedTextContextMenuItems(scope, owner, editable, *text, *selection);
    }

    void AddProcessedTextContextMenuItems(TextContextMenuBuilderScope& scope, ui::Owner* owner, bool editable, const std::u16string& text,
                                          TextRange selection) {
        // ComposeFoundationFlags.isSmartSelectionEnabled is on.
        if (!owner || selection.Collapsed() || text.empty()) return;
        const auto& host = owner->TextClassifier();
        if (!host.processTextActivities || !host.processText) return;
        const std::vector<std::string> labels = host.processTextActivities();
        if (labels.empty()) return;
        scope.Separator();
        const std::u16string selected = text.substr(std::size_t(selection.Min()), std::size_t(selection.Length()));
        for (std::size_t i = 0; i < labels.size(); ++i) {
            const int index = int(i);
            scope.Item(TextContextMenuKeys::ProcessText(index), labels[i], [owner, index, editable, selected](TextContextMenuSession& session) {
                owner->TextClassifier().processText(index, editable, selected);
                session.Close();
            });
        }
        scope.Separator();
    }

    bool CanShowLookUp(ui::Owner* owner, const std::u16string& selectedText) {
        if (!owner || !owner->TextClassifier().canLookUp) return false;   // before API 37
        if (IsBlank(selectedText)) return false;
        return owner->TextClassifier().canLookUp(selectedText);
    }

    void StartLookUpActivity(ui::Owner* owner, const std::u16string& selectedText) {
        if (!owner || !owner->TextClassifier().lookUp || IsBlank(selectedText)) return;
        owner->TextClassifier().lookUp(selectedText);
    }

    std::vector<std::string> SplitLocaleList(const std::optional<std::string>& list) {
        std::vector<std::string> out;
        if (!list) return out;
        std::size_t at = 0;
        while (at <= list->size()) {
            const std::size_t comma = list->find(',', at);
            std::string tag = list->substr(at, comma == std::string::npos ? std::string::npos : comma - at);
            tag.erase(0, tag.find_first_not_of(' '));
            tag.erase(tag.find_last_not_of(' ') + 1);
            if (!tag.empty()) out.push_back(std::move(tag));
            if (comma == std::string::npos) break;
            at = comma + 1;
        }
        return out;
    }

}
