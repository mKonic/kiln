#pragma once

// TextFieldImpl.kt's measure policies: TextFieldMeasurePolicy (labels inside or above a filled
// field) and OutlinedTextFieldMeasurePolicy (labels in the outline's cutout or above it). The
// children are found by their layoutId (the ids below). Placeholder, prefix and suffix fade
// through a graphics layer on themselves (Compose places them with one).

#include "kiln/foundation/Layouts.h"
#include "kiln/ui/Layout.h"

#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>

namespace kiln::material3::internal {

    inline constexpr const char* PlaceholderId = "Hint";
    inline constexpr const char* LabelId = "Label";
    inline constexpr const char* LeadingId = "Leading";
    inline constexpr const char* TrailingId = "Trailing";
    inline constexpr const char* PrefixId = "Prefix";
    inline constexpr const char* SuffixId = "Suffix";
    inline constexpr const char* SupportingId = "Supporting";
    inline constexpr const char* ContainerId = "Container";
    inline constexpr const char* TextFieldId = "TextField";

    enum class LabelPlacement : std::uint8_t { Inside, Above, Cutout };

    struct TextFieldLayoutParams {
        bool singleLine = false;
        LabelPlacement placement = LabelPlacement::Inside;
        // TextFieldLabelPosition's alignments while minimized and expanded.
        ui::AlignmentH minimizedAlignment = ui::AlignmentH::Start(), expandedAlignment = ui::AlignmentH::Start();
        std::function<float()> labelProgress;   // 0 expanded .. 1 minimized
        foundation::PaddingValues padding;
        ui::Dp minimizedLabelHalfHeight{ 0 };   // filled fields
        ui::Dp horizontalIconPadding{ 0 };      // outlined fields
        // Outlined fields: the label's size times its progress, for the outline's cutout.
        std::function<void(ui::Size)> onLabelMeasured;
    };

    std::shared_ptr<ui::MeasurePolicy> TextFieldMeasurePolicy(TextFieldLayoutParams p);
    std::shared_ptr<ui::MeasurePolicy> OutlinedTextFieldMeasurePolicy(TextFieldLayoutParams p);

    // androidx.compose.ui.util.lerp for ints.
    inline int LerpInt(int start, int stop, float fraction) { return start + int(std::floor(double(stop - start) * double(fraction) + 0.5)); }

}
