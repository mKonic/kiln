#pragma once

// The C++ declarative layer over the runtime (architecture 3.7): composable functions emit
// LayoutNodes into the current composition. Every function takes an optional Key, defaulting to
// the caller's source location, so conditionals around a call keep its identity.

#include "kiln/foundation/Layouts.h"

#include <string>

namespace kiln::foundation {

    using rt::Key;

    // A raw layout: the base of everything below.
    LayoutNode& Layout(const Modifier& modifier, std::shared_ptr<MeasurePolicy> policy,
                       std::function<void()> content = {}, Key key = {});

    void Box(const Modifier& modifier = {}, Alignment contentAlignment = Alignment::TopStart(),
             bool propagateMinConstraints = false, std::function<void()> content = {}, Key key = {});
    void Row(const Modifier& modifier, Arrangement horizontal, AlignmentV vertical,
             std::function<void()> content, Key key = {});
    void Column(const Modifier& modifier, Arrangement vertical, AlignmentH horizontal,
                std::function<void()> content, Key key = {});
    inline void Row(const Modifier& modifier, std::function<void()> content, Key key = {}) {
        Row(modifier, Arrangement::Start(), AlignmentV::Top(), std::move(content), key);
    }
    inline void Column(const Modifier& modifier, std::function<void()> content, Key key = {}) {
        Column(modifier, Arrangement::Top(), AlignmentH::Start(), std::move(content), key);
    }
    void Spacer(const Modifier& modifier, Key key = {});

    struct TextStyle {
        float fontSizeSp = 14;
        Color color{ 0xFF000000u };
        bool operator==(const TextStyle&) const = default;
    };
    // Plain text laid out by the platform text path (spec 07), wrapping to the max width.
    void BasicText(const std::string& text, const Modifier& modifier = {}, TextStyle style = {}, Key key = {});

    // Shortcuts to the composition's remember/state helpers.
    template<typename T> rt::State<T>& RememberState(T initial, Key key = {}) {
        return rt::Composition::Current()->RememberState(std::move(initial), key);
    }

}
