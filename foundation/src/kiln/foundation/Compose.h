#pragma once

// The C++ declarative layer over the runtime (architecture 3.7): composable functions emit
// LayoutNodes into the current composition. Every function takes an optional Key, defaulting to
// the caller's source location, so conditionals around a call keep its identity.

#include "kiln/foundation/Layouts.h"
#include "kiln/foundation/text/BasicText.h"

#include <string>
#include <vector>

namespace kiln::foundation {

    using rt::Key;

    // A raw layout: the base of everything below.
    LayoutNode& Layout(const Modifier& modifier, std::shared_ptr<MeasurePolicy> policy,
                       std::function<void()> content = {}, Key key = {});

    // MultiContentMeasurePolicy: measures the children of each content separately (in the
    // order of the contents).
    class MultiContentMeasurePolicy {
    public:
        virtual ~MultiContentMeasurePolicy() = default;
        virtual MeasureResult Measure(MeasureScope& scope, const std::vector<std::vector<Measurable*>>& contents, const Constraints& c) = 0;
    };
    // Layout(contents, modifier, measurePolicy): each content's nodes are this layout's children,
    // handed to the policy grouped by content.
    LayoutNode& Layout(const Modifier& modifier, std::vector<std::function<void()>> contents, std::shared_ptr<MultiContentMeasurePolicy> policy,
                       Key key = {});

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

    // Shortcuts to the composition's remember/state helpers.
    template<typename T> rt::State<T>& RememberState(T initial, Key key = {}) {
        return rt::Composition::Current()->RememberState(std::move(initial), key);
    }

}
