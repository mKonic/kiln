#pragma once

// GridReference (constraintlayout-core state helpers): a grid, row or column of references,
// laid out by a GridCore.

#include "kiln/constraintlayout/core/state/State.h"

namespace kiln::constraintlayout::core {
    class GridCore;
}

namespace kiln::constraintlayout::core::state {


    class GridReference : public HelperReference {
    public:
        GridReference(State& state, int type);
        int getPaddingStart() const { return mPaddingStart; }
        void setPaddingStart(int paddingStart) { mPaddingStart = paddingStart; }
        int getPaddingEnd() const { return mPaddingEnd; }
        void setPaddingEnd(int paddingEnd) { mPaddingEnd = paddingEnd; }
        int getPaddingTop() const { return mPaddingTop; }
        void setPaddingTop(int paddingTop) { mPaddingTop = paddingTop; }
        int getPaddingBottom() const { return mPaddingBottom; }
        void setPaddingBottom(int paddingBottom) { mPaddingBottom = paddingBottom; }
        int getFlags() const { return mFlags; }
        void setFlags(int flags) { mFlags = flags; }
        // "subgridbycolrow|spansrespectwidgetorder", case ignored.
        void setFlags(const std::string& flags);
        int getRowsSet() const { return mRowsSet; }
        void setRowsSet(int rowsSet) {
            if (mType == State::COLUMN) return;
            mRowsSet = rowsSet;
        }
        int getColumnsSet() const { return mColumnsSet; }
        void setColumnsSet(int columnsSet) {
            if (mType == State::ROW) return;
            mColumnsSet = columnsSet;
        }
        float getHorizontalGaps() const { return mHorizontalGaps; }
        void setHorizontalGaps(float horizontalGaps) { mHorizontalGaps = horizontalGaps; }
        float getVerticalGaps() const { return mVerticalGaps; }
        void setVerticalGaps(float verticalGaps) { mVerticalGaps = verticalGaps; }
        const std::string& getRowWeights() const { return mRowWeights; }
        void setRowWeights(const std::string& rowWeights) { mRowWeights = rowWeights; }
        const std::string& getColumnWeights() const { return mColumnWeights; }
        void setColumnWeights(const std::string& columnWeights) { mColumnWeights = columnWeights; }
        const std::string& getSpans() const { return mSpans; }
        void setSpans(const std::string& spans) { mSpans = spans; }
        const std::string& getSkips() const { return mSkips; }
        void setSkips(const std::string& skips) { mSkips = skips; }
        HelperWidget* getHelperWidget() override;
        void setHelperWidget(HelperWidget* widget) override;
        int getOrientation() const { return mOrientation; }
        void setOrientation(int orientation) { mOrientation = orientation; }
        void apply() override;
        GridReference* asGridReference() override { return this; }

    private:
        GridCore* mGrid = nullptr;
        int mPaddingStart = 0;
        int mPaddingEnd = 0;
        int mPaddingTop = 0;
        int mPaddingBottom = 0;
        int mOrientation = 0;
        int mRowsSet = 0;
        int mColumnsSet = 0;
        float mHorizontalGaps = 0;
        float mVerticalGaps = 0;
        std::string mRowWeights;
        std::string mColumnWeights;
        std::string mSpans;
        std::string mSkips;
        int mFlags = 0;
    };

}
