#include "kiln/constraintlayout/core/state/GridReference.h"

#include "kiln/constraintlayout/core/widgets/GridCore.h"

#include <cctype>

namespace kiln::constraintlayout::core::state {

    GridReference::GridReference(State& state, int type) : HelperReference(state, type) {
        if (type == State::ROW) mRowsSet = 1;
        else if (type == State::COLUMN) mColumnsSet = 1;
    }

    void GridReference::setFlags(const std::string& flags) {
        if (flags.empty()) return;
        mFlags = 0;
        std::size_t start = 0;
        for (;;) {
            std::size_t at = flags.find('|', start);
            std::string str = flags.substr(start, at == std::string::npos ? std::string::npos : at - start);
            for (char& c : str) c = char(std::tolower(static_cast<unsigned char>(c)));
            if (str == "subgridbycolrow") mFlags |= 1;
            else if (str == "spansrespectwidgetorder") mFlags |= 2;
            if (at == std::string::npos) break;
            start = at + 1;
        }
    }

    HelperWidget* GridReference::getHelperWidget() {
        if (mGrid == nullptr) mGrid = mHelperState.adopt(std::make_shared<GridCore>());
        return mGrid;
    }

    void GridReference::setHelperWidget(HelperWidget* widget) {
        mGrid = widget != nullptr && widget->is(ConstraintWidget::KIND_GRID) ? static_cast<GridCore*>(widget) : nullptr;
    }

    void GridReference::apply() {
        getHelperWidget();
        mGrid->setOrientation(mOrientation);
        if (mRowsSet != 0) mGrid->setRows(mRowsSet);
        if (mColumnsSet != 0) mGrid->setColumns(mColumnsSet);
        if (mHorizontalGaps != 0) mGrid->setHorizontalGaps(mHorizontalGaps);
        if (mVerticalGaps != 0) mGrid->setVerticalGaps(mVerticalGaps);
        if (!mRowWeights.empty()) mGrid->setRowWeights(mRowWeights);
        if (!mColumnWeights.empty()) mGrid->setColumnWeights(mColumnWeights);
        if (!mSpans.empty()) mGrid->setSpans(mSpans);
        if (!mSkips.empty()) mGrid->setSkips(mSkips);
        mGrid->setFlags(mFlags);
        mGrid->setPaddingStart(mPaddingStart);
        mGrid->setPaddingEnd(mPaddingEnd);
        mGrid->setPaddingTop(mPaddingTop);
        mGrid->setPaddingBottom(mPaddingBottom);
        applyBase();
    }

}
