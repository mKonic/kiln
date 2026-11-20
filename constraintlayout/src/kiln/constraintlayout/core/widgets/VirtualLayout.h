#pragma once

// VirtualLayout (constraintlayout-core): a helper that lays out its widgets itself (Flow,
// Placeholder), measured before the solver.

#include "kiln/constraintlayout/core/widgets/HelperWidget.h"
#include "kiln/constraintlayout/core/widgets/analyzer/BasicMeasure.h"

#include <vector>

namespace kiln::constraintlayout::core {

    class VirtualLayout : public HelperWidget {
    public:
        VirtualLayout() { mKind |= KIND_VIRTUAL_LAYOUT; }

        void setPadding(int value) {
            mPaddingLeft = mPaddingTop = mPaddingRight = mPaddingBottom = mPaddingStart = mPaddingEnd = value;
        }
        void setPaddingStart(int value) {
            mPaddingStart = value;
            mResolvedPaddingLeft = value;
            mResolvedPaddingRight = value;
        }
        void setPaddingEnd(int value) { mPaddingEnd = value; }
        void setPaddingLeft(int value) {
            mPaddingLeft = value;
            mResolvedPaddingLeft = value;
        }
        void applyRtl(bool isRtl);
        void setPaddingTop(int value) { mPaddingTop = value; }
        void setPaddingRight(int value) {
            mPaddingRight = value;
            mResolvedPaddingRight = value;
        }
        void setPaddingBottom(int value) { mPaddingBottom = value; }
        int getPaddingTop() const { return mPaddingTop; }
        int getPaddingBottom() const { return mPaddingBottom; }
        int getPaddingLeft() const { return mResolvedPaddingLeft; }
        int getPaddingRight() const { return mResolvedPaddingRight; }
        bool needSolverPass() const { return mNeedsCallFromSolver; }
        virtual void measure(int /*widthMode*/, int /*widthSize*/, int /*heightMode*/, int /*heightSize*/) {}
        void updateConstraints(ConstraintWidgetContainer* /*container*/) override { captureWidgets(); }
        void captureWidgets();
        int getMeasuredWidth() const { return mMeasuredWidth; }
        int getMeasuredHeight() const { return mMeasuredHeight; }
        void setMeasure(int width, int height) {
            mMeasuredWidth = width;
            mMeasuredHeight = height;
        }
        bool contains(const std::vector<ConstraintWidget*>& widgets) const;

    protected:
        void needsCallbackFromSolver(bool value) { mNeedsCallFromSolver = value; }
        bool measureChildren();
        void measure(ConstraintWidget* widget, DimensionBehaviour horizontalBehavior, int horizontalDimension, DimensionBehaviour verticalBehavior,
                     int verticalDimension);
        BasicMeasure::Measure mMeasure;
        BasicMeasure::Measurer* mMeasurer = nullptr;

    private:
        int mPaddingTop = 0;
        int mPaddingBottom = 0;
        int mPaddingLeft = 0;
        int mPaddingRight = 0;
        int mPaddingStart = 0;
        int mPaddingEnd = 0;
        int mResolvedPaddingLeft = 0;
        int mResolvedPaddingRight = 0;
        bool mNeedsCallFromSolver = false;
        int mMeasuredWidth = 0;
        int mMeasuredHeight = 0;
    };

    class Placeholder : public VirtualLayout {
    public:
        Placeholder() { mKind |= KIND_PLACEHOLDER; }
        void measure(int widthMode, int widthSize, int heightMode, int heightSize) override;
        void addToSolver(LinearSystem& system, bool optimize) override;
    };

}
