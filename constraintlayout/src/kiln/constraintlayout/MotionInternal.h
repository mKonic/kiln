#pragma once

// MotionLayout's internals shared by its files: TransitionImpl (Transition.kt) and
// MotionMeasurer (MotionMeasurer.kt).

#include "kiln/constraintlayout/MeasurerInternal.h"
#include "kiln/constraintlayout/MotionLayout.h"

#include "kiln/constraintlayout/core/parser/CLParser.h"
#include "kiln/constraintlayout/core/state/Transition.h"

#include <memory>
#include <optional>

namespace kiln::constraintlayout::detail {

    class TransitionImpl final : public Transition {
    public:
        explicit TransitionImpl(std::shared_ptr<core::CLObject> parsedTransition);
        void ApplyAllTo(core::state::Transition& transition) const;
        void ApplyKeyFramesTo(core::state::Transition& transition) const;
        std::string GetStartConstraintSetId() const override;
        std::string GetEndConstraintSetId() const override;
        bool Equals(const Transition& other) const override;
        static const std::shared_ptr<TransitionImpl>& Empty();

    private:
        std::shared_ptr<core::CLObject> m_Parsed;
    };

    // Where the measure pass came from: the content composing (remeasure from both sets), or
    // something else (a progress change only interpolates).
    enum class CompositionSource : std::uint8_t { Unknown, Content };

    using ShouldInvalidateCallback = kiln::Function<bool(const ui::Constraints& old, const ui::Constraints& now)>;

    class MotionMeasurer final : public Measurer {
    public:
        explicit MotionMeasurer(float density);

        ui::IntSize PerformInterpolationMeasure(const ui::Constraints& constraints, ui::LayoutDirection layoutDirection, ConstraintSet& constraintSetStart,
                                                ConstraintSet& constraintSetEnd, const TransitionImpl& transition,
                                                std::span<ui::Measurable* const> measurables, int optimizationLevel, float progress,
                                                CompositionSource compositionSource, const ShouldInvalidateCallback* invalidateOnConstraintsCallback);
        Color GetCustomColor(const std::string& id, const std::string& name, float progress);
        float GetCustomFloat(const std::string& id, const std::string& name, float progress);
        void ClearConstraintSets();
        void InitWith(ConstraintSet& start, ConstraintSet& end, ui::LayoutDirection layoutDirection, const TransitionImpl& transition, float progress);
        void DrawDebug(ui::DrawScope& scope, bool drawBounds, bool drawPaths, bool drawKeyPositions);
        int LayoutCurrentWidth() const { return m_Root.getWidth(); }
        int LayoutCurrentHeight() const { return m_Root.getHeight(); }

        core::state::Transition transition;

    private:
        void MeasureConstraintSet(int optimizationLevel, ConstraintSet& constraintSet, std::span<ui::Measurable* const> measurables,
                                  const ui::Constraints& constraints);
        bool NeedsRemeasure(const ui::Constraints& constraints, CompositionSource source, const ShouldInvalidateCallback* invalidateOnConstraintsCallback);
        void RecalculateInterpolation(const ui::Constraints& constraints, ui::LayoutDirection layoutDirection, ConstraintSet& constraintSetStart,
                                      ConstraintSet& constraintSetEnd, const TransitionImpl* transition, std::span<ui::Measurable* const> measurables,
                                      int optimizationLevel, float progress, bool remeasure);
        void ApplyRootSize(const ui::Constraints& constraints);

        float m_LastProgressInInterpolation = 0;
        std::optional<ui::Constraints> m_OldConstraints;
    };

}
