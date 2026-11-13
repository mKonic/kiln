#include "kiln/foundation/content/ReceiveContent.h"
#include <kiln/Function.h>

#include "kiln/ui/LayoutNode.h"

#include <algorithm>
#include <cctype>

namespace kiln::foundation {

    TransferableContentPtr TransferableContent::Consume(const kiln::Function<bool(const ui::ClipItem&)>& predicate,
                                                        const TransferableContentPtr& self) const {
        const auto& items = clipEntry.items;
        if (items.size() == 1) return predicate(items[0]) ? nullptr : self;
        std::vector<ui::ClipItem> remaining;
        for (const auto& item : items) if (!predicate(item)) remaining.push_back(item);
        if (remaining.empty()) return nullptr;
        if (remaining.size() == items.size()) return self;
        return std::make_shared<TransferableContent>(ui::ClipEntry{ std::move(remaining) }, clipMetadata, source, linkUri, permission);
    }

    std::optional<std::string> TransferableContent::ReadPlainText() const {
        std::optional<std::string> out;
        for (const auto& item : clipEntry.items) {
            if (!item.text) continue;
            if (out) *out += '\n';
            else out.emplace();
            *out += *item.text;
        }
        return out;
    }

    std::shared_ptr<ReceiveContentListener> ReceiveContentListenerOf(kiln::Function<TransferableContentPtr(TransferableContentPtr)> onReceive) {
        struct Fn final : ReceiveContentListener {
            kiln::Function<TransferableContentPtr(TransferableContentPtr)> fn;
            TransferableContentPtr OnReceive(TransferableContentPtr c) override { return fn(std::move(c)); }
        };
        auto l = std::make_shared<Fn>();
        l->fn = std::move(onReceive);
        return l;
    }

    bool ClipContainsContentUri(const ui::ClipEntry& clip) {
        for (const auto& item : clip.items) {
            if (!item.uri) continue;
            const std::string& u = *item.uri;
            if (u.size() >= 8 && std::equal(u.begin(), u.begin() + 8, "content:", [](char a, char b) { return std::tolower(a) == b; })) return true;
        }
        return false;
    }

    void DragAndDropRequestPermission(const ui::DragAndDropEvent& e) {
        if (!e.clip || !ClipContainsContentUri(*e.clip)) return;
        if (e.requestPermissions) e.requestPermissions();
    }

    TransferableContentPtr TransferableContentOf(const ui::DragAndDropEvent& e) {
        return std::make_shared<TransferableContent>(e.clip.value_or(ui::ClipEntry{}), e.metadata.value_or(ui::ClipMetadata{}),
                                                     TransferableContent::Source::DragAndDrop);
    }

    namespace {
        class Provider {
        public:
            virtual ~Provider() = default;
            virtual ReceiveContentConfiguration* Configuration() = 0;
        };

        struct ReceiveParams {
            std::shared_ptr<ReceiveContentListener> listener;
            bool operator==(const ReceiveParams&) const = default;
        };
        // ReceiveContentNode with its DynamicReceiveContentConfiguration: this receiver first,
        // then the ones further out with what is left.
        // ReceiveContentDragAndDropNode's target: drags go to the configuration's listener.
        struct ReceiveDropTarget final : ui::DragAndDropTarget {
            ReceiveContentConfiguration* configuration = nullptr;
            ui::ModifierNode* node = nullptr;
            void OnStarted(const ui::DragAndDropEvent&) override { configuration->Listener().OnDragStart(); }
            void OnEnded(const ui::DragAndDropEvent&) override { configuration->Listener().OnDragEnd(); }
            void OnEntered(const ui::DragAndDropEvent&) override { configuration->Listener().OnDragEnter(); }
            void OnExited(const ui::DragAndDropEvent&) override { configuration->Listener().OnDragExit(); }
            bool OnDrop(const ui::DragAndDropEvent& e) override {
                DragAndDropRequestPermission(e);
                auto original = TransferableContentOf(e);
                const TransferableContentPtr remaining = configuration->Listener().OnReceive(original);
                return original != remaining;
            }
        };

        struct ReceiveContentNode final : ui::DragAndDropNode, Provider, ReceiveContentConfiguration, ReceiveContentListener {
            ReceiveParams params;
            explicit ReceiveContentNode(ReceiveParams p) : params(std::move(p)) {
                m_DropTarget.configuration = this;
                m_DropTarget.node = this;
                onDropTargetValidate = [this](const ui::DragAndDropEvent&) -> ui::DragAndDropTarget* { return &m_DropTarget; };
            }
            ReceiveDropTarget m_DropTarget;
            void OnParamsChanged() {}
            ReceiveContentConfiguration* Configuration() override { return this; }
            ReceiveContentListener& Listener() override { return *this; }

            ReceiveContentListener* Parent() const {
                ReceiveContentConfiguration* c = FindReceiveContentConfiguration(*this);
                return c ? &c->Listener() : nullptr;
            }
            void OnDragStart() override { m_EnterCount = 0; params.listener->OnDragStart(); }
            void OnDragEnd() override { params.listener->OnDragEnd(); m_EnterCount = 0; }
            void OnDragEnter() override {
                if (++m_EnterCount == 1) params.listener->OnDragEnter();
                if (auto* p = Parent()) p->OnDragEnter();
            }
            void OnDragExit() override {
                const int previous = m_EnterCount;
                m_EnterCount = std::max(0, m_EnterCount - 1);
                if (m_EnterCount == 0 && previous > 0) params.listener->OnDragExit();
                if (auto* p = Parent()) p->OnDragExit();
            }
            TransferableContentPtr OnReceive(TransferableContentPtr content) override {
                TransferableContentPtr remaining = params.listener->OnReceive(std::move(content));
                if (!remaining) return nullptr;
                ReceiveContentListener* parent = Parent();
                return parent ? parent->OnReceive(std::move(remaining)) : remaining;
            }
            int m_EnterCount = 0;
        };

        ReceiveContentConfiguration* ProviderOf(const ui::ModifierNode* n) {
            auto* p = dynamic_cast<const Provider*>(n);
            return p ? const_cast<Provider*>(p)->Configuration() : nullptr;
        }
    }

    Modifier ContentReceiver(Modifier m, std::shared_ptr<ReceiveContentListener> listener) {
        return m.With<ReceiveContentNode>(ReceiveParams{ std::move(listener) });
    }

    ReceiveContentConfiguration* FindReceiveContentConfiguration(const ui::ModifierNode& node) {
        const ui::LayoutNode* owner = node.Owner();
        if (!owner || !owner->GetOwner()) return nullptr;
        const auto mine = owner->ModifierNodes();
        auto at = std::find(mine.begin(), mine.end(), &node);
        for (auto it = at; it != mine.begin();)
            if (auto* c = ProviderOf(*--it)) return c;
        for (const ui::LayoutNode* p = owner->Parent(); p; p = p->Parent()) {
            const auto nodes = p->ModifierNodes();
            for (auto it = nodes.rbegin(); it != nodes.rend(); ++it)
                if (auto* c = ProviderOf(*it)) return c;
        }
        return nullptr;
    }

}
