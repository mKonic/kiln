#pragma once

// Flow (constraintlayout-core): a virtual layout that places its widgets in a row or column,
// wrapping them into chains or an aligned grid when they don't fit.

#include "kiln/constraintlayout/core/widgets/VirtualLayout.h"

#include <memory>
#include <vector>

namespace kiln::constraintlayout::core {

    class Flow : public VirtualLayout {
    public:
        static constexpr int HORIZONTAL_ALIGN_START = 0;
        static constexpr int HORIZONTAL_ALIGN_END = 1;
        static constexpr int HORIZONTAL_ALIGN_CENTER = 2;
        static constexpr int VERTICAL_ALIGN_TOP = 0;
        static constexpr int VERTICAL_ALIGN_BOTTOM = 1;
        static constexpr int VERTICAL_ALIGN_CENTER = 2;
        static constexpr int VERTICAL_ALIGN_BASELINE = 3;
        static constexpr int WRAP_NONE = 0;
        static constexpr int WRAP_CHAIN = 1;
        static constexpr int WRAP_ALIGNED = 2;
        static constexpr int WRAP_CHAIN_NEW = 3;

        Flow();
        ~Flow() override;

        void copy(const ConstraintWidget& src, const WidgetMap& map) override;
        void setOrientation(int value) { mOrientation = value; }
        void setFirstHorizontalStyle(int value) { mFirstHorizontalStyle = value; }
        void setFirstVerticalStyle(int value) { mFirstVerticalStyle = value; }
        void setLastHorizontalStyle(int value) { mLastHorizontalStyle = value; }
        void setLastVerticalStyle(int value) { mLastVerticalStyle = value; }
        void setHorizontalStyle(int value) { mHorizontalStyle = value; }
        void setVerticalStyle(int value) { mVerticalStyle = value; }
        void setHorizontalBias(float value) { mHorizontalBias = value; }
        void setVerticalBias(float value) { mVerticalBias = value; }
        void setFirstHorizontalBias(float value) { mFirstHorizontalBias = value; }
        void setFirstVerticalBias(float value) { mFirstVerticalBias = value; }
        void setLastHorizontalBias(float value) { mLastHorizontalBias = value; }
        void setLastVerticalBias(float value) { mLastVerticalBias = value; }
        void setHorizontalAlign(int value) { mHorizontalAlign = value; }
        void setVerticalAlign(int value) { mVerticalAlign = value; }
        void setWrapMode(int value) { mWrapMode = value; }
        void setHorizontalGap(int value) { mHorizontalGap = value; }
        void setVerticalGap(int value) { mVerticalGap = value; }
        void setMaxElementsWrap(int value) { mMaxElementsWrap = value; }
        float getMaxElementsWrap() const { return float(mMaxElementsWrap); }

        void measure(int widthMode, int widthSize, int heightMode, int heightSize) override;
        void addToSolver(LinearSystem& system, bool optimize) override;

    protected:
        using VirtualLayout::measure;

    private:
        class WidgetsList;

        int getWidgetWidth(ConstraintWidget* widget, int max);
        int getWidgetHeight(ConstraintWidget* widget, int max);
        void measureChainWrap(const std::vector<ConstraintWidget*>& widgets, int count, int orientation, int max, int* measured);
        void measureChainWrap_new(const std::vector<ConstraintWidget*>& widgets, int count, int orientation, int max, int* measured);
        void measureNoWrap(const std::vector<ConstraintWidget*>& widgets, int count, int orientation, int max, int* measured);
        void measureAligned(const std::vector<ConstraintWidget*>& widgets, int count, int orientation, int max, int* measured);
        void createAlignedConstraints(bool isInRtl);

        int mHorizontalStyle = UNKNOWN;
        int mVerticalStyle = UNKNOWN;
        int mFirstHorizontalStyle = UNKNOWN;
        int mFirstVerticalStyle = UNKNOWN;
        int mLastHorizontalStyle = UNKNOWN;
        int mLastVerticalStyle = UNKNOWN;
        float mHorizontalBias = 0.5f;
        float mVerticalBias = 0.5f;
        float mFirstHorizontalBias = 0.5f;
        float mFirstVerticalBias = 0.5f;
        float mLastHorizontalBias = 0.5f;
        float mLastVerticalBias = 0.5f;
        int mHorizontalGap = 0;
        int mVerticalGap = 0;
        int mHorizontalAlign = HORIZONTAL_ALIGN_CENTER;
        int mVerticalAlign = VERTICAL_ALIGN_CENTER;
        int mWrapMode = WRAP_NONE;
        int mMaxElementsWrap = UNKNOWN;
        int mOrientation = HORIZONTAL;
        std::vector<std::unique_ptr<WidgetsList>> mChainList;
        // Java arrays: empty stands in for null.
        std::vector<ConstraintWidget*> mAlignedBiggestElementsInRows;
        std::vector<ConstraintWidget*> mAlignedBiggestElementsInCols;
        std::vector<int> mAlignedDimensions;
        std::vector<ConstraintWidget*> mDisplayedWidgets;
        int mDisplayedWidgetsCount = 0;
    };

}
