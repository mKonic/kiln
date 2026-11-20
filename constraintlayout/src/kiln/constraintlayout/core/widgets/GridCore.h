#pragma once

// GridCore (constraintlayout-core utils): a virtual layout that places its widgets in a grid of
// rows and columns, with weights, gaps, spans and skips. It lays out invisible box widgets, one
// per row and column, in chains, and constrains its widgets to them.

#include "kiln/constraintlayout/core/widgets/VirtualLayout.h"

#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

namespace kiln::constraintlayout::core {

    class ConstraintWidgetContainer;

    class GridCore : public VirtualLayout {
    public:
        static constexpr int HORIZONTAL = 0;
        static constexpr int VERTICAL = 1;
        static constexpr int SUB_GRID_BY_COL_ROW = 1;
        static constexpr int SPANS_RESPECT_WIDGET_ORDER = 2;

        GridCore();
        GridCore(int rows, int columns);
        ~GridCore() override;

        ConstraintWidgetContainer* getContainer() const { return mContainer; }
        void setContainer(ConstraintWidgetContainer* container) { mContainer = container; }
        void setSpans(const std::string& spans);
        void setSkips(const std::string& skips);
        float getHorizontalGaps() const { return mHorizontalGaps; }
        void setHorizontalGaps(float horizontalGaps);
        float getVerticalGaps() const { return mVerticalGaps; }
        void setVerticalGaps(float verticalGaps);
        const std::string& getRowWeights() const { return mRowWeights; }
        void setRowWeights(const std::string& rowWeights) { mRowWeights = rowWeights; }
        const std::string& getColumnWeights() const { return mColumnWeights; }
        void setColumnWeights(const std::string& columnWeights) { mColumnWeights = columnWeights; }
        int getOrientation() const { return mOrientation; }
        void setOrientation(int orientation);
        void setRows(int rows);
        void setColumns(int columns);
        int getFlags() const { return mFlags; }
        void setFlags(int flags) { mFlags = flags; }

        void measure(int widthMode, int widthSize, int heightMode, int heightSize) override;
        void addToSolver(LinearSystem& system, bool optimize) override;

    protected:
        using VirtualLayout::measure;

    private:
        using Matrix = std::vector<std::vector<int>>;
        static constexpr int DEFAULT_SIZE = 3;
        static constexpr int MAX_ROWS = 50;
        static constexpr int MAX_COLUMNS = 50;

        void handleSpans(const Matrix& spansMatrix);
        void arrangeWidgets();
        void setupGrid(bool isUpdate);
        int getRowByIndex(int index) const;
        int getColByIndex(int index) const;
        void handleSkips(const Matrix& skipsMatrix);
        bool invalidatePositions(int startRow, int startColumn, int rowSpan, int columnSpan);
        std::vector<float> parseWeights(int size, const std::string& str) const;
        int getNextPosition();
        void updateActualRowsAndColumns();
        std::unique_ptr<ConstraintWidget> makeNewWidget();
        void connectWidget(ConstraintWidget* widget, int row, int column, int rowSpan, int columnSpan);
        void setBoxWidgetHorizontalChains();
        void setBoxWidgetVerticalChains();
        void addConstraints();
        void createBoxes();
        static void clearVerticalAttributes(ConstraintWidget* widget);
        static void clearHorizontalAttributes(ConstraintWidget* widget);
        void initVariables();
        // Empty when the string does not parse (Java's null).
        Matrix parseSpans(const std::string& str, bool isSpans);
        void fillConstraintMatrix(bool isUpdate);
        void initMatrices();
        bool isSubGridByColRow() const { return (mFlags & SUB_GRID_BY_COL_ROW) > 0; }
        bool isSpansRespectWidgetOrder() const { return (mFlags & SPANS_RESPECT_WIDGET_ORDER) > 0; }

        ConstraintWidgetContainer* mContainer = nullptr;
        std::vector<std::unique_ptr<ConstraintWidget>> mBoxWidgets;
        bool mHasBoxWidgets = false;   // Java's mBoxWidgets != null
        // Boxes dropped when the grid shrank: the container's solver may still index them.
        std::vector<std::unique_ptr<ConstraintWidget>> mRetiredBoxes;
        bool mExtraSpaceHandled = false;
        int mRows = 0;
        int mRowsSet = 0;
        int mColumns = 0;
        int mColumnsSet = 0;
        float mHorizontalGaps = 0;
        float mVerticalGaps = 0;
        std::string mRowWeights;
        std::string mColumnWeights;
        std::string mSpans;
        bool mHasSpans = false;
        std::string mSkips;
        bool mHasSkips = false;
        int mOrientation = 0;
        int mNextAvailableIndex = 0;
        std::vector<std::vector<bool>> mPositionMatrix;
        std::unordered_set<std::string> mSpanIds;
        Matrix mConstraintMatrix;
        bool mHasConstraintMatrix = false;
        int mFlags = 0;
        Matrix mSpanMatrix;
        bool mHasSpanMatrix = false;
        int mSpanIndex = 0;
    };

}
