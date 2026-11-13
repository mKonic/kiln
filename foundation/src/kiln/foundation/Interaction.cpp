#include "kiln/foundation/Interaction.h"
#include <kiln/Function.h>

#include "kiln/ui/Draw.h"

#include <algorithm>

namespace kiln::foundation {

    using namespace ui;

    std::uint64_t MutableInteractionSource::Start(Interaction::Kind kind, Offset position) {
        const std::uint64_t id = m_NextId++;
        m_Active.push_back({ id, kind });
        auto& c = m_Count[int(kind)];
        c.Set(c.Peek() + 1);
        Emit({ kind, Interaction::Phase::Start, id, position });
        return id;
    }

    void MutableInteractionSource::End(Interaction::Kind kind, std::uint64_t id) {
        auto it = std::find_if(m_Active.begin(), m_Active.end(), [id](const auto& a) { return a.first == id; });
        if (it == m_Active.end()) return;
        m_Active.erase(it);
        auto& c = m_Count[int(kind)];
        c.Set(std::max(0, c.Peek() - 1));
        Emit({ kind, Interaction::Phase::End, id, {} });
    }

    void MutableInteractionSource::Cancel(Interaction::Kind kind, std::uint64_t id) {
        auto it = std::find_if(m_Active.begin(), m_Active.end(), [id](const auto& a) { return a.first == id; });
        if (it == m_Active.end()) return;
        m_Active.erase(it);
        auto& c = m_Count[int(kind)];
        c.Set(std::max(0, c.Peek() - 1));
        Emit({ kind, Interaction::Phase::Cancel, id, {} });
    }

    void MutableInteractionSource::Emit(const Interaction& i) {
        auto subs = m_Subscribers;   // a subscriber may unsubscribe while notified
        for (auto& [token, fn] : subs) if (fn) fn(i);
    }

    std::uint64_t MutableInteractionSource::Subscribe(kiln::Function<void(const Interaction&)> fn) {
        m_Subscribers.push_back({ m_NextToken, std::move(fn) });
        return m_NextToken++;
    }

    void MutableInteractionSource::Unsubscribe(std::uint64_t token) {
        std::erase_if(m_Subscribers, [token](const auto& s) { return s.first == token; });
    }

    std::shared_ptr<MutableInteractionSource> RememberInteractionSource(rt::Key key) {
        return rt::Composition::Current()->Remember<std::shared_ptr<MutableInteractionSource>>(
            [] { return std::make_shared<MutableInteractionSource>(); }, {}, key);
    }

    namespace {
        struct DebugParams {
            std::shared_ptr<MutableInteractionSource> source;
            bool operator==(const DebugParams& o) const { return source == o.source; }
        };
        struct DebugNode final : ModifierNode, DrawModifier {
            DebugParams params;
            explicit DebugNode(DebugParams p) : params(std::move(p)) {}
            void OnParamsChanged() { InvalidateDraw(); }
            DrawModifier* AsDraw() override { return this; }
            void Draw(ContentDrawScope& s) override {
                s.DrawContent();
                if (!params.source) return;
                // Reads subscribe the draw phase: the overlay follows the interactions.
                if (params.source->IsPressed()) s.DrawRect(Color(0x4D000000u), {}, s.GetSize());
                else if (params.source->IsHovered() || params.source->IsFocused()) s.DrawRect(Color(0x1A000000u), {}, s.GetSize());
            }
        };
        class DebugIndication final : public Indication {
        public:
            Modifier Create(Modifier m, std::shared_ptr<MutableInteractionSource> source) const override {
                return m.With<DebugNode>(DebugParams{ std::move(source) });
            }
        };
    }

    const IndicationPtr& DefaultDebugIndication() {
        static const IndicationPtr i = std::make_shared<DebugIndication>();
        return i;
    }

}
