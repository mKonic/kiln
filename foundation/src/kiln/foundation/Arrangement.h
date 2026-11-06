#pragma once

// Arrangement (spec 06 §13, foundation-layout Arrangement.kt:296-733).

#include "kiln/ui/Geometry.h"

#include <optional>
#include <span>

namespace kiln::foundation {

    using namespace kiln::ui;

    struct Arrangement {
        enum class Kind : std::uint8_t { Start, End, Center, SpaceEvenly, SpaceBetween, SpaceAround, Spaced,
                                         AbsLeft, AbsRight, AbsCenter, AbsSpaceEvenly, AbsSpaceBetween, AbsSpaceAround };
        Kind kind = Kind::Start;
        Dp space{ 0 };
        bool rtlMirror = true;             // Spaced: mirrors in RTL (Absolute.spacedBy does not)
        std::optional<float> alignBias;    // Spaced: where leftover space goes (-1 start .. 1 end)
        bool alignIsVertical = false;

        // Horizontal (Row): positions depend on direction. Vertical (Column): pass Ltr.
        void Arrange(const Density& d, int total, std::span<const int> sizes, LayoutDirection dir,
                     std::span<int> out) const;
        Dp Spacing() const { return kind == Kind::Spaced ? space : Dp(0); }
        bool operator==(const Arrangement&) const = default;

        static Arrangement Start() { return { Kind::Start }; }
        static Arrangement End() { return { Kind::End }; }
        static Arrangement Top() { return { Kind::Start }; }       // vertical: never mirrored
        static Arrangement Bottom() { return { Kind::End }; }
        static Arrangement Center() { return { Kind::Center }; }
        static Arrangement SpaceEvenly() { return { Kind::SpaceEvenly }; }
        static Arrangement SpaceBetween() { return { Kind::SpaceBetween }; }
        static Arrangement SpaceAround() { return { Kind::SpaceAround }; }
        static Arrangement SpacedBy(Dp s) { Arrangement a{ Kind::Spaced, s }; a.alignBias = -1; return a; }
        static Arrangement SpacedBy(Dp s, AlignmentH a) { Arrangement r{ Kind::Spaced, s }; r.alignBias = a.bias; return r; }
        static Arrangement SpacedBy(Dp s, AlignmentV a) { Arrangement r{ Kind::Spaced, s }; r.alignBias = a.bias; r.alignIsVertical = true; r.rtlMirror = false; return r; }
        static Arrangement AbsoluteLeft() { return { Kind::AbsLeft }; }
        static Arrangement AbsoluteRight() { return { Kind::AbsRight }; }
        static Arrangement AbsoluteCenter() { return { Kind::AbsCenter }; }
    };

}
