#pragma once

// Interactions and indication (foundation/interaction, Indication.kt): components report press,
// hover, focus and drag interactions to an interaction source; an Indication draws them (a ripple
// in Material, DefaultDebugIndication here).

#include "kiln/runtime/Composition.h"
#include "kiln/runtime/State.h"
#include "kiln/ui/Geometry.h"
#include "kiln/ui/LayoutNode.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace kiln::foundation {

    using ui::Modifier;
    using ui::Offset;

    struct Interaction {
        enum class Kind : std::uint8_t { Press, Hover, Focus, Drag, DragAndDropHover };
        enum class Phase : std::uint8_t { Start, End, Cancel };   // Press Release = End
        Kind kind;
        Phase phase;
        std::uint64_t id;          // pairs the start with its end
        Offset position;           // Press: where (local px)
    };

    class MutableInteractionSource {
    public:
        // Starts an interaction; returns its id for End/Cancel.
        std::uint64_t Start(Interaction::Kind kind, Offset position = {});
        void End(Interaction::Kind kind, std::uint64_t id);
        void Cancel(Interaction::Kind kind, std::uint64_t id);

        // collectIsPressedAsState etc.: observable.
        bool IsPressed() const { return m_Count[0].Get() > 0; }
        bool IsHovered() const { return m_Count[1].Get() > 0; }
        bool IsFocused() const { return m_Count[2].Get() > 0; }
        bool IsDragged() const { return m_Count[3].Get() > 0; }
        // collectIsDragAndDropHoveredAsState: a drag and drop session is over the component.
        bool IsDragAndDropHovered() const { return m_Count[4].Get() > 0; }

        // Every event, in order (indications that need positions and ends, like the ripple).
        std::uint64_t Subscribe(std::function<void(const Interaction&)> fn);
        void Unsubscribe(std::uint64_t token);

    private:
        void Emit(const Interaction& i);
        rt::State<int> m_Count[5] = { rt::State<int>(0), rt::State<int>(0), rt::State<int>(0), rt::State<int>(0), rt::State<int>(0) };
        std::vector<std::pair<std::uint64_t, Interaction::Kind>> m_Active;
        std::vector<std::pair<std::uint64_t, std::function<void(const Interaction&)>>> m_Subscribers;
        std::uint64_t m_NextId = 1, m_NextToken = 1;
    };

    // remember { MutableInteractionSource() }
    std::shared_ptr<MutableInteractionSource> RememberInteractionSource(rt::Key key = {});

    // Indication: draws a component's interactions (IndicationNodeFactory).
    class Indication {
    public:
        virtual ~Indication() = default;
        virtual Modifier Create(Modifier m, std::shared_ptr<MutableInteractionSource> source) const = 0;
    };
    using IndicationPtr = std::shared_ptr<const Indication>;
    // DefaultDebugIndication: 30% black while pressed, 10% while hovered or focused.
    const IndicationPtr& DefaultDebugIndication();
    inline rt::Local<IndicationPtr> LocalIndication{ nullptr };   // null reads as DefaultDebugIndication

}
