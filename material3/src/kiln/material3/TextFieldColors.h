#pragma once

// TextFieldDefaults.kt: TextFieldColors (every colour by state: disabled, error, focused,
// unfocused) and TextFieldLabelPosition (where a text field's label sits).

#include "kiln/foundation/text/Selection.h"
#include <kiln/Function.h>
#include "kiln/ui/Geometry.h"

#include <cstdint>
#include <functional>

namespace kiln::material3 {

    // The colours, each for focused, unfocused, disabled and error.
#define KILN_TEXT_FIELD_COLORS(X)                                                                            \
    X(Text) X(Container) X(Indicator) X(LeadingIcon) X(TrailingIcon) X(Label) X(Placeholder) X(SupportingText) \
    X(Prefix) X(Suffix)

    struct TextFieldColors {
#define KILN_TFC_FIELDS(Name) ui::Color focused##Name##Color, unfocused##Name##Color, disabled##Name##Color, error##Name##Color;
        KILN_TEXT_FIELD_COLORS(KILN_TFC_FIELDS)
#undef KILN_TFC_FIELDS
        ui::Color cursorColor, errorCursorColor;
        foundation::TextSelectionColors textSelectionColors;

        // Which colour applies: disabled first, then error, then focused.
#define KILN_TFC_QUERY(Name)                                                          \
    ui::Color Name##Color(bool enabled, bool isError, bool focused) const {           \
        return !enabled ? disabled##Name##Color : isError ? error##Name##Color          \
             : focused ? focused##Name##Color : unfocused##Name##Color;                 \
    }
        KILN_TEXT_FIELD_COLORS(KILN_TFC_QUERY)
#undef KILN_TFC_QUERY
        ui::Color CursorColor(bool isError) const { return isError ? errorCursorColor : cursorColor; }
        bool operator==(const TextFieldColors&) const = default;
    };

    // Where the label sits: Inside the container (filled fields), in the outline's Cutout
    // (outlined fields), or Above the field. Inside and Cutout labels sit expanded in the input
    // area while the field is empty and unfocused, unless alwaysMinimized.
    struct TextFieldLabelPosition {
        enum class Kind : std::uint8_t { Inside, Cutout, Above };
        Kind kind = Kind::Inside;
        bool alwaysMinimized = false;
        ui::AlignmentH minimizedAlignment = ui::AlignmentH::Start();
        ui::AlignmentH expandedAlignment = ui::AlignmentH::Start();   // Above: the same as minimized

        static TextFieldLabelPosition Inside(bool alwaysMinimized = false, ui::AlignmentH minimized = ui::AlignmentH::Start(),
                                             ui::AlignmentH expanded = ui::AlignmentH::Start()) {
            return { Kind::Inside, alwaysMinimized, minimized, expanded };
        }
        static TextFieldLabelPosition Cutout(bool alwaysMinimized = false, ui::AlignmentH minimized = ui::AlignmentH::Start(),
                                             ui::AlignmentH expanded = ui::AlignmentH::Start()) {
            return { Kind::Cutout, alwaysMinimized, minimized, expanded };
        }
        static TextFieldLabelPosition Above(ui::AlignmentH alignment = ui::AlignmentH::Start()) { return { Kind::Above, true, alignment, alignment }; }
        // showExpandedLabel
        bool ShowsExpanded() const { return kind != Kind::Above && !alwaysMinimized; }
        bool operator==(const TextFieldLabelPosition&) const = default;
    };

    // TextFieldLabelScope: what a label may read while it composes.
    struct TextFieldLabelScope {
        kiln::Function<float()> progress;
        // 0 while the label is expanded in the input area, 1 once minimized (reading it subscribes).
        float LabelMinimizedProgress() const { return progress ? progress() : 1.0f; }
    };

}
