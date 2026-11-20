#pragma once

// Guideline (constraintlayout-core): a line at a fixed offset from the parent's start or end, or a
// percentage of it, that widgets attach to.

#include "kiln/constraintlayout/core/widgets/ConstraintWidget.h"

namespace kiln::constraintlayout::core {

    class Guideline : public ConstraintWidget {
    public:
        static constexpr int HORIZONTAL = 0, VERTICAL = 1;
        static constexpr int RELATIVE_PERCENT = 0, RELATIVE_BEGIN = 1, RELATIVE_END = 2, RELATIVE_UNKNOWN = -1;

        Guideline();
        void copy(const ConstraintWidget& src, const WidgetMap& map) override;
        bool allowedInBarrier() const override { return true; }
        int getRelativeBehaviour() const;
        void setOrientation(int orientation);
        ConstraintAnchor* getAnchor() const { return mAnchor; }
        std::string getType() const override { return "Guideline"; }
        int getOrientation() const { return mOrientation; }
        void setMinimumPosition(int minimum) { mMinimumPosition = minimum; }
        int getMinimumPosition() const { return mMinimumPosition; }
        ConstraintAnchor* getAnchor(ConstraintAnchor::Type anchorType) override;
        void setGuidePercent(int value) { setGuidePercent(float(value) / 100.0f); }
        void setGuidePercent(float value);
        void setGuideBegin(int value);
        void setGuideEnd(int value);
        float getRelativePercent() const { return mRelativePercent; }
        int getRelativeBegin() const { return mRelativeBegin; }
        int getRelativeEnd() const { return mRelativeEnd; }
        void setFinalValue(int position) { mAnchor->setFinalValue(position); mResolved = true; }
        bool isResolvedHorizontally() const override { return mResolved; }
        bool isResolvedVertically() const override { return mResolved; }
        void addToSolver(LinearSystem& system, bool optimize) override;
        void updateFromSolver(LinearSystem& system, bool optimize) override;
        void cyclePosition();
        bool isPercent() const { return mRelativePercent != -1 && mRelativeBegin == -1 && mRelativeEnd == -1; }

        float mRelativePercent = -1;
        int mRelativeBegin = -1;
        int mRelativeEnd = -1;
        bool mGuidelineUseRtl = true;

    private:
        void inferRelativePercentPosition();
        void inferRelativeBeginPosition();
        void inferRelativeEndPosition();
        ConstraintAnchor* mAnchor = &mTop;
        int mOrientation = HORIZONTAL;
        int mMinimumPosition = 0;
        bool mResolved = false;
    };

}
