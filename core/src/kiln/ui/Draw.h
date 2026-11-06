#pragma once

// Drawing during the draw pass (spec 03 §ui-graphics, DrawScope). M2 draws straight into the
// frame's QuadRenderer with an accumulated translation; retained layers come with GraphicsLayer.

#include "kiln/ui/Geometry.h"

#include <functional>
#include <vector>

namespace kiln { class QuadRenderer; struct ParagraphData; class AtlasPage; }

namespace kiln::ui {

    struct DrawTarget {
        QuadRenderer* quads = nullptr;
        std::function<const AtlasPage*(std::uint32_t)> page;
    };

    class DrawScope : public Density {
    public:
        DrawScope(Density d, LayoutDirection dir, DrawTarget target) : Density(d), m_Dir(dir), m_Target(target) {}

        Size GetSize() const { return m_Size; }
        LayoutDirection Direction() const { return m_Dir; }

        void DrawRect(Color color, Offset topLeft, Size size);
        void DrawRoundRect(Color color, Offset topLeft, Size size, float cornerRadius);
        void DrawParagraph(const ParagraphData& paragraph, Offset topLeft, Color color);

        // Transform: device = origin + scale * local (axis-aligned; rotation needs the retained
        // layer renderer). Translate moves the local origin.
        void Translate(Offset o) { m_State.origin = m_State.origin + Offset{ o.x * m_State.sx, o.y * m_State.sy }; }
        // Scales about `pivot` (local coordinates).
        void Scale(float sx, float sy, Offset pivot);
        void MultiplyAlpha(float a) { m_State.alpha *= a; }
        // Intersects the clip with a local rect, rounded by `radius` (local px). Nested rounded
        // clips keep only the innermost rounding.
        void ClipRect(Rect local, float radius = 0);
        void Save() { m_Stack.push_back(m_State); }
        void Restore();
        void SetSize(Size s) { m_Size = s; }

    private:
        struct State {
            Offset origin;
            float sx = 1, sy = 1;
            float alpha = 1;
            bool clip = false;
            Rect clipRect;
            float clipRadius = 0;
        };
        Rect ToDevice(Offset p, Size s) const {
            return { m_State.origin.x + p.x * m_State.sx, m_State.origin.y + p.y * m_State.sy,
                     m_State.origin.x + (p.x + s.width) * m_State.sx, m_State.origin.y + (p.y + s.height) * m_State.sy };
        }
        void ApplyClip();
        LayoutDirection m_Dir;
        DrawTarget m_Target;
        State m_State;
        std::vector<State> m_Stack;
        Size m_Size;
    };

    class ContentDrawScope : public DrawScope {
    public:
        using DrawScope::DrawScope;
        void DrawContent() { if (m_Content) m_Content(); }
        void SetContent(std::function<void()> c) { m_Content = std::move(c); }
    private:
        std::function<void()> m_Content;
    };

}
