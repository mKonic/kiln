#include "kiln/constraintlayout/core/widgets/GridCore.h"

#include "kiln/constraintlayout/core/widgets/ConstraintWidgetContainer.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <stdexcept>

namespace kiln::constraintlayout::core {

    namespace {

        // String.split with a literal one-character separator: trailing empty strings dropped.
        std::vector<std::string> javaSplit(const std::string& s, char sep) {
            std::vector<std::string> parts;
            std::size_t start = 0;
            for (;;) {
                std::size_t at = s.find(sep, start);
                if (at == std::string::npos) {
                    parts.push_back(s.substr(start));
                    break;
                }
                parts.push_back(s.substr(start, at - start));
                start = at + 1;
            }
            while (!parts.empty() && parts.back().empty()) parts.pop_back();
            if (parts.empty()) parts.push_back("");   // "".split gives [""]
            return parts;
        }

        std::string javaTrim(const std::string& s) {
            std::size_t b = 0, e = s.size();
            while (b < e && static_cast<unsigned char>(s[b]) <= ' ') b++;
            while (e > b && static_cast<unsigned char>(s[e - 1]) <= ' ') e--;
            return s.substr(b, e - b);
        }

        // Integer.parseInt: throws on anything but an optionally signed decimal.
        int javaParseInt(const std::string& s) {
            const char* begin = s.data();
            const char* end = s.data() + s.size();
            if (begin != end && *begin == '+') ++begin;
            int value = 0;
            auto [ptr, ec] = std::from_chars(begin, end, value);
            if (begin == end || ec != std::errc() || ptr != end) throw std::invalid_argument(s);
            return value;
        }

        float javaParseFloat(const std::string& s) {
            std::string t = javaTrim(s);
            if (!t.empty() && (t.back() == 'f' || t.back() == 'F' || t.back() == 'd' || t.back() == 'D')) t.pop_back();
            if (t.empty()) throw std::invalid_argument(s);
            char* endPtr = nullptr;
            float value = std::strtof(t.c_str(), &endPtr);
            if (endPtr != t.c_str() + t.size()) throw std::invalid_argument(s);
            return value;
        }

        int sBoxIds = 0;

    }

    GridCore::GridCore() {
        mKind |= KIND_GRID;
        updateActualRowsAndColumns();
        initMatrices();
    }

    GridCore::GridCore(int rows, int columns) {
        mKind |= KIND_GRID;
        mRowsSet = rows;
        mColumnsSet = columns;
        if (rows > MAX_ROWS) mRowsSet = DEFAULT_SIZE;
        if (columns > MAX_COLUMNS) mColumnsSet = DEFAULT_SIZE;
        updateActualRowsAndColumns();
        initMatrices();
    }

    GridCore::~GridCore() = default;

    void GridCore::setSpans(const std::string& spans) {
        if (mHasSpans && mSpans == spans) return;
        mExtraSpaceHandled = false;
        mSpans = spans;
        mHasSpans = true;
    }

    void GridCore::setSkips(const std::string& skips) {
        if (mHasSkips && mSkips == skips) return;
        mExtraSpaceHandled = false;
        mSkips = skips;
        mHasSkips = true;
    }

    void GridCore::setHorizontalGaps(float horizontalGaps) {
        if (horizontalGaps < 0) return;
        mHorizontalGaps = horizontalGaps;
    }

    void GridCore::setVerticalGaps(float verticalGaps) {
        if (verticalGaps < 0) return;
        mVerticalGaps = verticalGaps;
    }

    void GridCore::setOrientation(int orientation) {
        if (!(orientation == HORIZONTAL || orientation == VERTICAL)) return;
        mOrientation = orientation;
    }

    void GridCore::setRows(int rows) {
        if (rows > MAX_ROWS) return;
        if (mRowsSet == rows) return;
        mRowsSet = rows;
        updateActualRowsAndColumns();
        initVariables();
    }

    void GridCore::setColumns(int columns) {
        if (columns > MAX_COLUMNS) return;
        if (mColumnsSet == columns) return;
        mColumnsSet = columns;
        updateActualRowsAndColumns();
        initVariables();
    }

    void GridCore::handleSpans(const Matrix& spansMatrix) {
        if (isSpansRespectWidgetOrder()) return;
        for (std::size_t i = 0; i < spansMatrix.size(); i++) {
            int row = getRowByIndex(spansMatrix[i][0]);
            int col = getColByIndex(spansMatrix[i][0]);
            if (!invalidatePositions(row, col, spansMatrix[i][1], spansMatrix[i][2])) return;
            if (i >= mWidgets.size() || mWidgets[i] == nullptr) return;   // Java would throw
            connectWidget(mWidgets[i], row, col, spansMatrix[i][1], spansMatrix[i][2]);
            mSpanIds.insert(mWidgets[i]->stringId);
        }
    }

    void GridCore::arrangeWidgets() {
        for (int i = 0; i < mWidgetsCount; i++) {
            ConstraintWidget* widget = mWidgets[std::size_t(i)];
            if (mSpanIds.count(widget->stringId)) continue;
            int position = getNextPosition();
            if (position == -1) return;
            int row = getRowByIndex(position);
            int col = getColByIndex(position);
            if (isSpansRespectWidgetOrder() && mHasSpanMatrix) {
                if (mSpanIndex < int(mSpanMatrix.size()) && mSpanMatrix[std::size_t(mSpanIndex)][0] == position) {
                    mPositionMatrix[std::size_t(row)][std::size_t(col)] = true;
                    const auto& span = mSpanMatrix[std::size_t(mSpanIndex)];
                    if (!invalidatePositions(row, col, span[1], span[2])) continue;
                    connectWidget(widget, row, col, span[1], span[2]);
                    mSpanIndex++;
                    continue;
                }
            }
            connectWidget(widget, row, col, 1, 1);
        }
    }

    void GridCore::setupGrid(bool isUpdate) {
        if (mRows < 1 || mColumns < 1) return;
        if (isUpdate) {
            for (auto& row : mPositionMatrix) std::fill(row.begin(), row.end(), true);
            mSpanIds.clear();
        }
        mNextAvailableIndex = 0;
        if (mHasSkips && !javaTrim(mSkips).empty()) {
            Matrix skips = parseSpans(mSkips, false);
            if (!skips.empty()) handleSkips(skips);
        }
        if (mHasSpans && !javaTrim(mSpans).empty()) {
            mSpanMatrix = parseSpans(mSpans, true);
            mHasSpanMatrix = !mSpanMatrix.empty();
        }
        createBoxes();
        if (mHasSpanMatrix) handleSpans(mSpanMatrix);
    }

    int GridCore::getRowByIndex(int index) const {
        if (mOrientation == 1) return index % mRows;
        return index / mColumns;
    }

    int GridCore::getColByIndex(int index) const {
        if (mOrientation == 1) return index / mRows;
        return index % mColumns;
    }

    void GridCore::handleSkips(const Matrix& skipsMatrix) {
        for (const auto& matrix : skipsMatrix) {
            int row = getRowByIndex(matrix[0]);
            int col = getColByIndex(matrix[0]);
            if (!invalidatePositions(row, col, matrix[1], matrix[2])) return;
        }
    }

    bool GridCore::invalidatePositions(int startRow, int startColumn, int rowSpan, int columnSpan) {
        for (int i = startRow; i < startRow + rowSpan; i++) {
            for (int j = startColumn; j < startColumn + columnSpan; j++) {
                if (i < 0 || j < 0 || i >= int(mPositionMatrix.size()) || j >= int(mPositionMatrix[0].size()) ||
                    !mPositionMatrix[std::size_t(i)][std::size_t(j)]) {
                    return false;
                }
                mPositionMatrix[std::size_t(i)][std::size_t(j)] = false;
            }
        }
        return true;
    }

    std::vector<float> GridCore::parseWeights(int size, const std::string& str) const {
        if (javaTrim(str).empty()) return {};
        std::vector<std::string> values = javaSplit(str, ',');
        std::vector<float> arr(std::size_t(std::max(size, 0)));
        for (std::size_t i = 0; i < arr.size(); i++) {
            if (i < values.size()) {
                try {
                    arr[i] = javaParseFloat(values[i]);
                } catch (const std::exception&) {
                    arr[i] = 1.0f;
                }
            } else {
                arr[i] = 1.0f;
            }
        }
        return arr;
    }

    int GridCore::getNextPosition() {
        int position = 0;
        bool positionFound = false;
        while (!positionFound) {
            if (mNextAvailableIndex >= mRows * mColumns) return -1;
            position = mNextAvailableIndex;
            int row = getRowByIndex(mNextAvailableIndex);
            int col = getColByIndex(mNextAvailableIndex);
            if (mPositionMatrix[std::size_t(row)][std::size_t(col)]) {
                mPositionMatrix[std::size_t(row)][std::size_t(col)] = false;
                positionFound = true;
            }
            mNextAvailableIndex++;
        }
        return position;
    }

    void GridCore::updateActualRowsAndColumns() {
        if (mRowsSet == 0 || mColumnsSet == 0) {
            if (mColumnsSet > 0) {
                mColumns = mColumnsSet;
                mRows = (mWidgetsCount + mColumns - 1) / mColumnsSet;
            } else if (mRowsSet > 0) {
                mRows = mRowsSet;
                mColumns = (mWidgetsCount + mRowsSet - 1) / mRowsSet;
            } else {
                mRows = int(1.5 + std::sqrt(double(mWidgetsCount)));
                mColumns = (mWidgetsCount + mRows - 1) / mRows;
            }
        } else {
            mRows = mRowsSet;
            mColumns = mColumnsSet;
        }
    }

    std::unique_ptr<ConstraintWidget> GridCore::makeNewWidget() {
        auto widget = std::make_unique<ConstraintWidget>();
        widget->mListDimensionBehaviors[HORIZONTAL] = DimensionBehaviour::MATCH_CONSTRAINT;
        widget->mListDimensionBehaviors[VERTICAL] = DimensionBehaviour::MATCH_CONSTRAINT;
        // Java uses the widget's identity hash.
        widget->stringId = "__grid_box_" + std::to_string(++sBoxIds);
        return widget;
    }

    void GridCore::connectWidget(ConstraintWidget* widget, int row, int column, int rowSpan, int columnSpan) {
        widget->mLeft.connect(&mBoxWidgets[std::size_t(column)]->mLeft, 0);
        widget->mTop.connect(&mBoxWidgets[std::size_t(row)]->mTop, 0);
        widget->mRight.connect(&mBoxWidgets[std::size_t(column + columnSpan - 1)]->mRight, 0);
        widget->mBottom.connect(&mBoxWidgets[std::size_t(row + rowSpan - 1)]->mBottom, 0);
    }

    void GridCore::setBoxWidgetHorizontalChains() {
        int maxVal = std::max(mRows, mColumns);
        ConstraintWidget* widget = mBoxWidgets[0].get();
        std::vector<float> columnWeights = parseWeights(mColumns, mColumnWeights);
        if (mColumns == 1) {
            clearHorizontalAttributes(widget);
            widget->mLeft.connect(&mLeft, 0);
            widget->mRight.connect(&mRight, 0);
            return;
        }
        for (int i = 0; i < mColumns; i++) {
            widget = mBoxWidgets[std::size_t(i)].get();
            clearHorizontalAttributes(widget);
            if (!columnWeights.empty()) widget->setHorizontalWeight(columnWeights[std::size_t(i)]);
            if (i > 0) widget->mLeft.connect(&mBoxWidgets[std::size_t(i - 1)]->mRight, 0);
            else widget->mLeft.connect(&mLeft, 0);
            if (i < mColumns - 1) widget->mRight.connect(&mBoxWidgets[std::size_t(i + 1)]->mLeft, 0);
            else widget->mRight.connect(&mRight, 0);
            if (i > 0) widget->mLeft.mMargin = JavaInt(mHorizontalGaps);
        }
        for (int i = mColumns; i < maxVal; i++) {
            widget = mBoxWidgets[std::size_t(i)].get();
            clearHorizontalAttributes(widget);
            widget->mLeft.connect(&mLeft, 0);
            widget->mRight.connect(&mRight, 0);
        }
    }

    void GridCore::setBoxWidgetVerticalChains() {
        int maxVal = std::max(mRows, mColumns);
        ConstraintWidget* widget = mBoxWidgets[0].get();
        std::vector<float> rowWeights = parseWeights(mRows, mRowWeights);
        if (mRows == 1) {
            clearVerticalAttributes(widget);
            widget->mTop.connect(&mTop, 0);
            widget->mBottom.connect(&mBottom, 0);
            return;
        }
        for (int i = 0; i < mRows; i++) {
            widget = mBoxWidgets[std::size_t(i)].get();
            clearVerticalAttributes(widget);
            if (!rowWeights.empty()) widget->setVerticalWeight(rowWeights[std::size_t(i)]);
            if (i > 0) widget->mTop.connect(&mBoxWidgets[std::size_t(i - 1)]->mBottom, 0);
            else widget->mTop.connect(&mTop, 0);
            if (i < mRows - 1) widget->mBottom.connect(&mBoxWidgets[std::size_t(i + 1)]->mTop, 0);
            else widget->mBottom.connect(&mBottom, 0);
            if (i > 0) widget->mTop.mMargin = JavaInt(mVerticalGaps);
        }
        for (int i = mRows; i < maxVal; i++) {
            widget = mBoxWidgets[std::size_t(i)].get();
            clearVerticalAttributes(widget);
            widget->mTop.connect(&mTop, 0);
            widget->mBottom.connect(&mBottom, 0);
        }
    }

    void GridCore::addConstraints() {
        setBoxWidgetVerticalChains();
        setBoxWidgetHorizontalChains();
        arrangeWidgets();
    }

    void GridCore::createBoxes() {
        int boxCount = std::max(mRows, mColumns);
        if (!mHasBoxWidgets) {
            mBoxWidgets.clear();
            for (int i = 0; i < boxCount; i++) mBoxWidgets.push_back(makeNewWidget());
            mHasBoxWidgets = true;
        } else if (boxCount != int(mBoxWidgets.size())) {
            for (int j = boxCount; j < int(mBoxWidgets.size()); j++) {
                if (mContainer != nullptr) mContainer->remove(mBoxWidgets[std::size_t(j)].get());
                mRetiredBoxes.push_back(std::move(mBoxWidgets[std::size_t(j)]));
            }
            mBoxWidgets.resize(std::min<std::size_t>(mBoxWidgets.size(), std::size_t(boxCount)));
            while (int(mBoxWidgets.size()) < boxCount) mBoxWidgets.push_back(makeNewWidget());
        }
    }

    void GridCore::clearVerticalAttributes(ConstraintWidget* widget) {
        widget->setVerticalWeight(float(UNKNOWN));
        widget->mTop.reset();
        widget->mBottom.reset();
        widget->mBaseline.reset();
    }

    void GridCore::clearHorizontalAttributes(ConstraintWidget* widget) {
        widget->setHorizontalWeight(float(UNKNOWN));
        widget->mLeft.reset();
        widget->mRight.reset();
    }

    void GridCore::initVariables() {
        mPositionMatrix.assign(std::size_t(std::max(mRows, 0)), std::vector<bool>(std::size_t(std::max(mColumns, 0)), true));
        if (mWidgetsCount > 0) {
            mConstraintMatrix.assign(std::size_t(mWidgetsCount), std::vector<int>(4, -1));
            mHasConstraintMatrix = true;
        }
    }

    GridCore::Matrix GridCore::parseSpans(const std::string& str, bool isSpans) {
        try {
            int extraRows = 0;
            int extraColumns = 0;
            std::vector<std::string> spans = javaSplit(str, ',');
            // Arrays.sort with a comparator is a stable merge sort; its parse errors abort.
            // (A single span is never compared, so its key is not parsed there.)
            if (spans.size() > 1) {
                std::vector<std::pair<int, std::string>> keyed;
                for (const auto& span : spans) keyed.emplace_back(javaParseInt(javaSplit(span, ':')[0]), span);
                std::stable_sort(keyed.begin(), keyed.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
                for (std::size_t i = 0; i < spans.size(); i++) spans[i] = keyed[i].second;
            }
            Matrix spanMatrix(spans.size(), std::vector<int>(3, 0));
            if (mRows == 1 || mColumns == 1) {
                for (std::size_t i = 0; i < spans.size(); i++) {
                    std::vector<std::string> indexAndSpan = javaSplit(javaTrim(spans[i]), ':');
                    spanMatrix[i][0] = javaParseInt(indexAndSpan.at(0));
                    spanMatrix[i][1] = 1;
                    spanMatrix[i][2] = 1;
                    if (mColumns == 1) {
                        spanMatrix[i][1] = javaParseInt(indexAndSpan.at(1));
                        extraRows += spanMatrix[i][1];
                        if (isSpans) extraRows--;
                    }
                    if (mRows == 1) {
                        spanMatrix[i][2] = javaParseInt(indexAndSpan.at(1));
                        extraColumns += spanMatrix[i][2];
                        if (isSpans) extraColumns--;
                    }
                }
                if (extraRows != 0 && !mExtraSpaceHandled) setRows(mRows + extraRows);
                if (extraColumns != 0 && !mExtraSpaceHandled) setColumns(mColumns + extraColumns);
                mExtraSpaceHandled = true;
            } else {
                for (std::size_t i = 0; i < spans.size(); i++) {
                    std::vector<std::string> indexAndSpan = javaSplit(javaTrim(spans[i]), ':');
                    std::vector<std::string> rowAndCol = javaSplit(indexAndSpan.at(1), 'x');
                    spanMatrix[i][0] = javaParseInt(indexAndSpan.at(0));
                    if (isSubGridByColRow()) {
                        spanMatrix[i][1] = javaParseInt(rowAndCol.at(1));
                        spanMatrix[i][2] = javaParseInt(rowAndCol.at(0));
                    } else {
                        spanMatrix[i][1] = javaParseInt(rowAndCol.at(0));
                        spanMatrix[i][2] = javaParseInt(rowAndCol.at(1));
                    }
                }
            }
            return spanMatrix;
        } catch (const std::exception&) {
            return {};
        }
    }

    void GridCore::fillConstraintMatrix(bool isUpdate) {
        if (isUpdate) {
            for (auto& row : mPositionMatrix) std::fill(row.begin(), row.end(), true);
            for (auto& row : mConstraintMatrix) std::fill(row.begin(), row.end(), -1);
        }
        mNextAvailableIndex = 0;
        if (mHasSkips && !javaTrim(mSkips).empty()) {
            Matrix skips = parseSpans(mSkips, false);
            if (!skips.empty()) handleSkips(skips);
        }
        if (mHasSpans && !javaTrim(mSpans).empty()) {
            Matrix spans = parseSpans(mSpans, true);
            if (!spans.empty()) handleSpans(spans);
        }
    }

    void GridCore::initMatrices() {
        bool isUpdate = mHasConstraintMatrix && int(mConstraintMatrix.size()) == mWidgetsCount && int(mPositionMatrix.size()) == mRows &&
                        !mPositionMatrix.empty() && int(mPositionMatrix[0].size()) == mColumns;
        if (!isUpdate) initVariables();
        fillConstraintMatrix(isUpdate);
    }

    void GridCore::measure(int widthMode, int widthSize, int heightMode, int heightSize) {
        VirtualLayout::measure(widthMode, widthSize, heightMode, heightSize);
        mContainer = static_cast<ConstraintWidgetContainer*>(getParent());
        setupGrid(false);
        for (auto& box : mBoxWidgets) mContainer->add(box.get());
    }

    void GridCore::addToSolver(LinearSystem& system, bool optimize) {
        VirtualLayout::addToSolver(system, optimize);
        addConstraints();
    }

}
