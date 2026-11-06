#pragma once

// Drawing during the draw pass (spec 03 §ui-graphics, DrawScope). M2 draws straight into the
// frame's QuadRenderer with an accumulated translation; retained layers come with GraphicsLayer.

#include "kiln/ui/Geometry.h"

#include <functional>

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

        // Translation stack, managed by the node tree while it draws.
        Offset Translation() const { return m_Translate; }
        void Translate(Offset o) { m_Translate = m_Translate + o; }
        void SetSize(Size s) { m_Size = s; }

    private:
        LayoutDirection m_Dir;
        DrawTarget m_Target;
        Offset m_Translate;
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
