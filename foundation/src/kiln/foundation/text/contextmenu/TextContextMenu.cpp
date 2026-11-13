#include "kiln/foundation/text/contextmenu/TextContextMenu.h"
#include <kiln/Function.h>
#include "kiln/foundation/text/contextmenu/TextContextMenuProvider.h"

#include "kiln/runtime/Channel.h"
#include "kiln/ui/LayoutNode.h"

#include <algorithm>

namespace kiln::foundation {

    namespace TextContextMenuKeys {
        namespace {
            // Distinct objects: only their addresses matter.
            char kCut, kCopy, kPaste, kSelectAll, kAutofill, kLookUp, kProcessText;
        }
        const TextContextMenuKey Cut{ &kCut }, Copy{ &kCopy }, Paste{ &kPaste }, SelectAll{ &kSelectAll }, Autofill{ &kAutofill }, LookUp{ &kLookUp };
        TextContextMenuKey ProcessText(int index) { return { &kProcessText, index }; }
        ui::TextToolbarItemId PlatformId(const TextContextMenuKey& key) {
            using Id = ui::TextToolbarItemId;
            if (key == Cut) return Id::Cut;
            if (key == Copy) return Id::Copy;
            if (key == Paste) return Id::Paste;
            if (key == SelectAll) return Id::SelectAll;
            if (key == Autofill) return Id::Autofill;
            if (key == LookUp) return Id::LookUp;
            return Id::None;
        }
    }

    std::optional<TextContextMenuKey> KeyOf(const TextContextMenuComponent& c) {
        if (const auto* i = std::get_if<TextContextMenuItem>(&c)) return i->key;
        if (const auto* i = std::get_if<TextContextMenuTextClassificationItem>(&c)) return i->key;
        return std::nullopt;
    }

    TextContextMenuData TextContextMenuBuilderScope::Build() const {
        TextContextMenuData out;
        bool headIsSeparator = true;
        const TextContextMenuComponent* previous = nullptr;
        for (const auto& current : m_Components) {
            const bool separator = IsSeparator(current);
            if (headIsSeparator && separator) continue;
            headIsSeparator = false;
            if (separator && previous && IsSeparator(*previous)) continue;
            if (!separator && std::any_of(m_Filters.begin(), m_Filters.end(), [&](const auto& f) { return !f(current); })) continue;
            out.components.push_back(current);
            previous = &current;
        }
        if (!out.components.empty() && IsSeparator(out.components.back())) out.components.pop_back();
        return out;
    }

    // --- the add / filter modifiers ------------------------------------------------------------

    namespace {
        // The TextContextMenuDataTraverseKey nodes.
        class DataNode {
        public:
            virtual ~DataNode() = default;
            virtual void Contribute(TextContextMenuBuilderScope& scope) const = 0;
        };

        struct AddParams {
            std::shared_ptr<const TextContextMenuBuilder> builder;
            bool operator==(const AddParams& o) const { return builder == o.builder; }
        };
        struct AddNode final : ModifierNode, DataNode {
            AddParams params;
            explicit AddNode(AddParams p) : params(std::move(p)) {}
            void OnParamsChanged() {}
            void Contribute(TextContextMenuBuilderScope& scope) const override { if (*params.builder) (*params.builder)(scope); }
        };

        struct FilterParams {
            std::shared_ptr<const kiln::Function<bool(const TextContextMenuComponent&)>> filter;
            bool operator==(const FilterParams& o) const { return filter == o.filter; }
        };
        struct FilterNode final : ModifierNode, DataNode {
            FilterParams params;
            explicit FilterNode(FilterParams p) : params(std::move(p)) {}
            void OnParamsChanged() {}
            void Contribute(TextContextMenuBuilderScope& scope) const override {
                auto f = params.filter;
                scope.AddFilter([f](const TextContextMenuComponent& c) { return !*f || (*f)(c); });
            }
        };

        const DataNode* DataNodeOf(const ui::ModifierNode* n) { return dynamic_cast<const DataNode*>(n); }
    }

    Modifier AppendTextContextMenuComponents(Modifier m, TextContextMenuBuilder builder) {
        return m.With<AddNode>(AddParams{ std::make_shared<const TextContextMenuBuilder>(std::move(builder)) });
    }

    Modifier FilterTextContextMenuComponents(Modifier m, kiln::Function<bool(const TextContextMenuComponent&)> filter) {
        return m.With<FilterNode>(FilterParams{ std::make_shared<const kiln::Function<bool(const TextContextMenuComponent&)>>(std::move(filter)) });
    }

    TextContextMenuData CollectTextContextMenuData(const ui::ModifierNode& node) {
        TextContextMenuBuilderScope scope;
        // traverseAncestors: the nearest first.
        const ui::LayoutNode* owner = node.Owner();
        if (owner) {
            const auto mine = owner->ModifierNodes();
            auto at = std::find(mine.begin(), mine.end(), &node);
            for (auto it = at; it != mine.begin();)
                if (const DataNode* d = DataNodeOf(*--it)) d->Contribute(scope);
            for (const ui::LayoutNode* p = owner->Parent(); p; p = p->Parent()) {
                const auto nodes = p->ModifierNodes();
                for (auto it = nodes.rbegin(); it != nodes.rend(); ++it)
                    if (const DataNode* d = DataNodeOf(*it)) d->Contribute(scope);
            }
        }
        return scope.Build();
    }

    std::vector<ui::TextToolbarEntry> ToolbarEntriesOf(const TextContextMenuData& data) {
        std::vector<ui::TextToolbarEntry> out;
        int group = 1;
        for (const auto& c : data.components) {
            if (IsSeparator(c)) { ++group; continue; }
            ui::TextToolbarEntry e;
            e.group = group;
            if (const auto* item = std::get_if<TextContextMenuItem>(&c)) {
                e.id = TextContextMenuKeys::PlatformId(item->key);
                e.label = item->label;
            } else {
                const auto& ci = std::get<TextContextMenuTextClassificationItem>(c);
                e.kind = ui::TextToolbarEntry::Kind::Classification;
                e.classification = ci.classification;
                e.index = ci.index;
            }
            out.push_back(std::move(e));
        }
        return out;
    }

    // --- the toolbar handler -------------------------------------------------------------------

    struct TextContextMenuToolbarHandler::Session final : TextContextMenuSession {
        rt::Channel<int> closed{ 1 };
        void Close() override { closed.TrySend(0); }
    };

    TextContextMenuToolbarHandler::~TextContextMenuToolbarHandler() {
        m_Alive.reset();
        m_Job.Cancel();
        if (m_Owner) m_Owner->HideTextToolbar(this);
    }

    void TextContextMenuToolbarHandler::Attach(ui::Owner* owner, const ui::ModifierNode* anchor) {
        if (m_Owner && m_Owner != owner) Detach();
        m_Owner = owner;
        m_Anchor = anchor;
    }

    void TextContextMenuToolbarHandler::Detach() {
        Hide();
        m_Owner = nullptr;
        m_Anchor = nullptr;
    }

    rt::Task<> TextContextMenuToolbarHandler::Run(TextContextMenuToolbarHandler* self, std::shared_ptr<Session> session, std::weak_ptr<char> alive) {
        // try { onShow; provider.showTextContextMenu } finally { onHide }
        struct Finally {
            TextContextMenuToolbarHandler* self;
            std::weak_ptr<char> alive;
            ~Finally() {
                if (alive.expired()) return;
                self->m_Active = false;
                if (self->m_Session) self->m_Session.reset();
                if (self->m_Owner) self->m_Owner->HideTextToolbar(self);
                if (self->onHide) self->onHide();
            }
        } finally{ self, alive };
        if (self->onShow) {
            co_await self->onShow();
            if (alive.expired()) co_return;
        }
        if (!self->m_Owner) co_return;
        if (TextContextMenuProvider* provider = self->toolbarProvider) {
            // A toolbar provider shows it instead of the platform, until its session ends.
            struct ToolbarData final : TextContextMenuDataProvider {
                TextContextMenuToolbarHandler* self;
                explicit ToolbarData(TextContextMenuToolbarHandler* h) : self(h) {}
                Offset PositionInRoot() override { const Rect r = self->ContentBounds(); return { (r.left + r.right) / 2, r.top }; }
                Rect ContentBoundsInRoot() override { return self->ContentBounds(); }
                TextContextMenuData Data() override {
                    self->m_Data = self->m_Anchor ? CollectTextContextMenuData(*self->m_Anchor) : TextContextMenuData{};
                    return self->m_Data;
                }
            };
            co_await provider->ShowTextContextMenu(std::make_shared<ToolbarData>(self));
            co_return;
        }
        self->m_Owner->ShowTextToolbar(self);
        if (session->closed.Closed()) co_return;
        if (!self->m_Owner || self->m_Owner->CurrentTextToolbarClient() != self) co_return;   // nothing to show
        co_await session->closed.Next();
    }

    void TextContextMenuToolbarHandler::Show() {
        if (!m_Owner || m_Active) return;
        m_Active = true;
        m_Session = std::make_shared<Session>();
        m_Job = rt::Launch(rt::Dispatcher::Main(), Run(this, m_Session, m_Alive));
    }

    void TextContextMenuToolbarHandler::Hide() {
        if (!m_Active) return;
        m_Job.Cancel();
    }

    Rect TextContextMenuToolbarHandler::ContentBounds() {
        m_BoundsVersion.Get();
        if (!m_Owner || !computeContentBounds) return m_PreviousContentBounds;
        const auto r = computeContentBounds();
        if (!r) return m_PreviousContentBounds;
        m_PreviousContentBounds = *r;
        return *r;
    }

    std::vector<ui::TextToolbarEntry> TextContextMenuToolbarHandler::Entries() {
        m_Data = m_Anchor ? CollectTextContextMenuData(*m_Anchor) : TextContextMenuData{};
        return ToolbarEntriesOf(m_Data);
    }

    void TextContextMenuToolbarHandler::OnEntryClicked(std::size_t index) {
        // The entries are the data's non-separator components, in order.
        std::size_t i = 0;
        for (const auto& c : m_Data.components) {
            if (IsSeparator(c)) continue;
            if (i++ != index) continue;
            const auto* item = std::get_if<TextContextMenuItem>(&c);
            if (item && item->onClick && m_Session) {
                const auto keep = m_Session;
                const auto onClick = item->onClick;   // the click may rebuild the data
                onClick(*keep);
            }
            return;
        }
    }

    void TextContextMenuToolbarHandler::OnClosed() {
        if (m_Session) m_Session->Close();
    }

}
