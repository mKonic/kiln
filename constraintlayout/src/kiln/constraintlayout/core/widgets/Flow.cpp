#include "kiln/constraintlayout/core/widgets/Flow.h"

#include "kiln/constraintlayout/core/widgets/ConstraintWidgetContainer.h"

#include <algorithm>
#include <cmath>

namespace kiln::constraintlayout::core {

    using DB = ConstraintWidget::DimensionBehaviour;

    // Flow.WidgetsList: one row (or column) of the flow, a chain of widgets.
    class Flow::WidgetsList {
    public:
        WidgetsList(Flow* flow, int orientation, ConstraintAnchor* left, ConstraintAnchor* top, ConstraintAnchor* right,
                    ConstraintAnchor* bottom, int max)
            : mFlow(flow), mOrientation(orientation), mLeft(left), mTop(top), mRight(right), mBottom(bottom),
              mPaddingLeft(flow->getPaddingLeft()), mPaddingTop(flow->getPaddingTop()), mPaddingRight(flow->getPaddingRight()),
              mPaddingBottom(flow->getPaddingBottom()), mMax(max) {}

        void setup(int orientation, ConstraintAnchor* left, ConstraintAnchor* top, ConstraintAnchor* right, ConstraintAnchor* bottom,
                   int paddingLeft, int paddingTop, int paddingRight, int paddingBottom, int max) {
            mOrientation = orientation;
            mLeft = left;
            mTop = top;
            mRight = right;
            mBottom = bottom;
            mPaddingLeft = paddingLeft;
            mPaddingTop = paddingTop;
            mPaddingRight = paddingRight;
            mPaddingBottom = paddingBottom;
            mMax = max;
        }

        void clear() {
            mBiggestDimension = 0;
            mBiggest = nullptr;
            mWidth = 0;
            mHeight = 0;
            mStartIndex = 0;
            mCount = 0;
            mNbMatchConstraintsWidgets = 0;
        }

        void setStartIndex(int value) { mStartIndex = value; }

        int getWidth() const {
            if (mOrientation == HORIZONTAL) return mWidth - mFlow->mHorizontalGap;
            return mWidth;
        }

        int getHeight() const {
            if (mOrientation == VERTICAL) return mHeight - mFlow->mVerticalGap;
            return mHeight;
        }

        void add(ConstraintWidget* widget) {
            Flow& f = *mFlow;
            if (mOrientation == HORIZONTAL) {
                int width = f.getWidgetWidth(widget, mMax);
                if (widget->getHorizontalDimensionBehaviour() == DB::MATCH_CONSTRAINT) {
                    mNbMatchConstraintsWidgets++;
                    width = 0;
                }
                int gap = f.mHorizontalGap;
                if (widget->getVisibility() == GONE) gap = 0;
                mWidth += width + gap;
                int height = f.getWidgetHeight(widget, mMax);
                if (mBiggest == nullptr || mBiggestDimension < height) {
                    mBiggest = widget;
                    mBiggestDimension = height;
                    mHeight = height;
                }
            } else {
                int width = f.getWidgetWidth(widget, mMax);
                int height = f.getWidgetHeight(widget, mMax);
                if (widget->getVerticalDimensionBehaviour() == DB::MATCH_CONSTRAINT) {
                    mNbMatchConstraintsWidgets++;
                    height = 0;
                }
                int gap = f.mVerticalGap;
                if (widget->getVisibility() == GONE) gap = 0;
                mHeight += height + gap;
                if (mBiggest == nullptr || mBiggestDimension < width) {
                    mBiggest = widget;
                    mBiggestDimension = width;
                    mWidth = width;
                }
            }
            mCount++;
        }

        void createConstraints(bool isInRtl, int chainIndex, bool isLastChain) {
            Flow& f = *mFlow;
            const int count = mCount;
            for (int i = 0; i < count; i++) {
                if (mStartIndex + i >= f.mDisplayedWidgetsCount) break;
                ConstraintWidget* widget = f.mDisplayedWidgets[std::size_t(mStartIndex + i)];
                if (widget != nullptr) widget->resetAnchors();
            }
            if (count == 0 || mBiggest == nullptr) return;

            bool singleChain = isLastChain && chainIndex == 0;
            int firstVisible = -1;
            int lastVisible = -1;
            for (int i = 0; i < count; i++) {
                int index = i;
                if (isInRtl) index = count - 1 - i;
                if (mStartIndex + index >= f.mDisplayedWidgetsCount) break;
                ConstraintWidget* widget = f.mDisplayedWidgets[std::size_t(mStartIndex + index)];
                if (widget != nullptr && widget->getVisibility() == VISIBLE) {
                    if (firstVisible == -1) firstVisible = i;
                    lastVisible = i;
                }
            }

            ConstraintWidget* previous = nullptr;
            if (mOrientation == HORIZONTAL) {
                ConstraintWidget* verticalWidget = mBiggest;
                verticalWidget->setVerticalChainStyle(f.mVerticalStyle);
                int padding = mPaddingTop;
                if (chainIndex > 0) padding += f.mVerticalGap;
                verticalWidget->mTop.connect(mTop, padding);
                if (isLastChain) verticalWidget->mBottom.connect(mBottom, mPaddingBottom);
                if (chainIndex > 0) {
                    ConstraintAnchor* bottom = &mTop->mOwner->mBottom;
                    bottom->connect(&verticalWidget->mTop, 0);
                }

                ConstraintWidget* baselineVerticalWidget = verticalWidget;
                if (f.mVerticalAlign == VERTICAL_ALIGN_BASELINE && !verticalWidget->hasBaseline()) {
                    for (int i = 0; i < count; i++) {
                        int index = i;
                        if (isInRtl) index = count - 1 - i;
                        if (mStartIndex + index >= f.mDisplayedWidgetsCount) break;
                        ConstraintWidget* widget = f.mDisplayedWidgets[std::size_t(mStartIndex + index)];
                        if (widget->hasBaseline()) {
                            baselineVerticalWidget = widget;
                            break;
                        }
                    }
                }

                for (int i = 0; i < count; i++) {
                    int index = i;
                    if (isInRtl) index = count - 1 - i;
                    if (mStartIndex + index >= f.mDisplayedWidgetsCount) break;
                    ConstraintWidget* widget = f.mDisplayedWidgets[std::size_t(mStartIndex + index)];
                    if (widget == nullptr) continue;
                    if (i == 0) widget->connect(&widget->mLeft, mLeft, mPaddingLeft);
                    if (index == 0) {
                        int style = f.mHorizontalStyle;
                        float bias = isInRtl ? (1 - f.mHorizontalBias) : f.mHorizontalBias;
                        if (mStartIndex == 0 && f.mFirstHorizontalStyle != UNKNOWN) {
                            style = f.mFirstHorizontalStyle;
                            bias = isInRtl ? (1 - f.mFirstHorizontalBias) : f.mFirstHorizontalBias;
                        } else if (isLastChain && f.mLastHorizontalStyle != UNKNOWN) {
                            style = f.mLastHorizontalStyle;
                            bias = isInRtl ? (1 - f.mLastHorizontalBias) : f.mLastHorizontalBias;
                        }
                        widget->setHorizontalChainStyle(style);
                        widget->setHorizontalBiasPercent(bias);
                    }
                    if (i == count - 1) widget->connect(&widget->mRight, mRight, mPaddingRight);
                    if (previous != nullptr) {
                        widget->mLeft.connect(&previous->mRight, f.mHorizontalGap);
                        if (i == firstVisible) widget->mLeft.setGoneMargin(mPaddingLeft);
                        previous->mRight.connect(&widget->mLeft, 0);
                        if (i == lastVisible + 1) previous->mRight.setGoneMargin(mPaddingRight);
                    }
                    if (widget != verticalWidget) {
                        if (f.mVerticalAlign == VERTICAL_ALIGN_BASELINE && baselineVerticalWidget->hasBaseline() &&
                            widget != baselineVerticalWidget && widget->hasBaseline()) {
                            widget->mBaseline.connect(&baselineVerticalWidget->mBaseline, 0);
                        } else {
                            switch (f.mVerticalAlign) {
                                case VERTICAL_ALIGN_TOP:
                                    widget->mTop.connect(&verticalWidget->mTop, 0);
                                    break;
                                case VERTICAL_ALIGN_BOTTOM:
                                    widget->mBottom.connect(&verticalWidget->mBottom, 0);
                                    break;
                                case VERTICAL_ALIGN_CENTER:
                                default:
                                    if (singleChain) {
                                        widget->mTop.connect(mTop, mPaddingTop);
                                        widget->mBottom.connect(mBottom, mPaddingBottom);
                                    } else {
                                        widget->mTop.connect(&verticalWidget->mTop, 0);
                                        widget->mBottom.connect(&verticalWidget->mBottom, 0);
                                    }
                            }
                        }
                    }
                    previous = widget;
                }
            } else {
                ConstraintWidget* horizontalWidget = mBiggest;
                horizontalWidget->setHorizontalChainStyle(f.mHorizontalStyle);
                int padding = mPaddingLeft;
                if (chainIndex > 0) padding += f.mHorizontalGap;
                if (isInRtl) {
                    horizontalWidget->mRight.connect(mRight, padding);
                    if (isLastChain) horizontalWidget->mLeft.connect(mLeft, mPaddingRight);
                    if (chainIndex > 0) {
                        ConstraintAnchor* left = &mRight->mOwner->mLeft;
                        left->connect(&horizontalWidget->mRight, 0);
                    }
                } else {
                    horizontalWidget->mLeft.connect(mLeft, padding);
                    if (isLastChain) horizontalWidget->mRight.connect(mRight, mPaddingRight);
                    if (chainIndex > 0) {
                        ConstraintAnchor* right = &mLeft->mOwner->mRight;
                        right->connect(&horizontalWidget->mLeft, 0);
                    }
                }

                for (int i = 0; i < count; i++) {
                    if (mStartIndex + i >= f.mDisplayedWidgetsCount) break;
                    ConstraintWidget* widget = f.mDisplayedWidgets[std::size_t(mStartIndex + i)];
                    if (widget == nullptr) continue;
                    if (i == 0) {
                        widget->connect(&widget->mTop, mTop, mPaddingTop);
                        int style = f.mVerticalStyle;
                        float bias = f.mVerticalBias;
                        if (mStartIndex == 0 && f.mFirstVerticalStyle != UNKNOWN) {
                            style = f.mFirstVerticalStyle;
                            bias = f.mFirstVerticalBias;
                        } else if (isLastChain && f.mLastVerticalStyle != UNKNOWN) {
                            style = f.mLastVerticalStyle;
                            bias = f.mLastVerticalBias;
                        }
                        widget->setVerticalChainStyle(style);
                        widget->setVerticalBiasPercent(bias);
                    }
                    if (i == count - 1) widget->connect(&widget->mBottom, mBottom, mPaddingBottom);
                    if (previous != nullptr) {
                        widget->mTop.connect(&previous->mBottom, f.mVerticalGap);
                        if (i == firstVisible) widget->mTop.setGoneMargin(mPaddingTop);
                        previous->mBottom.connect(&widget->mTop, 0);
                        if (i == lastVisible + 1) previous->mBottom.setGoneMargin(mPaddingBottom);
                    }
                    if (widget != horizontalWidget) {
                        if (isInRtl) {
                            switch (f.mHorizontalAlign) {
                                case HORIZONTAL_ALIGN_START:
                                    widget->mRight.connect(&horizontalWidget->mRight, 0);
                                    break;
                                case HORIZONTAL_ALIGN_CENTER:
                                    widget->mLeft.connect(&horizontalWidget->mLeft, 0);
                                    widget->mRight.connect(&horizontalWidget->mRight, 0);
                                    break;
                                case HORIZONTAL_ALIGN_END:
                                    widget->mLeft.connect(&horizontalWidget->mLeft, 0);
                                    break;
                            }
                        } else {
                            switch (f.mHorizontalAlign) {
                                case HORIZONTAL_ALIGN_START:
                                    widget->mLeft.connect(&horizontalWidget->mLeft, 0);
                                    break;
                                case HORIZONTAL_ALIGN_CENTER:
                                    if (singleChain) {
                                        widget->mLeft.connect(mLeft, mPaddingLeft);
                                        widget->mRight.connect(mRight, mPaddingRight);
                                    } else {
                                        widget->mLeft.connect(&horizontalWidget->mLeft, 0);
                                        widget->mRight.connect(&horizontalWidget->mRight, 0);
                                    }
                                    break;
                                case HORIZONTAL_ALIGN_END:
                                    widget->mRight.connect(&horizontalWidget->mRight, 0);
                                    break;
                            }
                        }
                    }
                    previous = widget;
                }
            }
        }

        void measureMatchConstraints(int availableSpace) {
            if (mNbMatchConstraintsWidgets == 0) return;
            Flow& f = *mFlow;
            const int count = mCount;
            int widgetSize = availableSpace / mNbMatchConstraintsWidgets;
            for (int i = 0; i < count; i++) {
                if (mStartIndex + i >= f.mDisplayedWidgetsCount) break;
                ConstraintWidget* widget = f.mDisplayedWidgets[std::size_t(mStartIndex + i)];
                if (mOrientation == HORIZONTAL) {
                    if (widget != nullptr && widget->getHorizontalDimensionBehaviour() == DB::MATCH_CONSTRAINT) {
                        if (widget->mMatchConstraintDefaultWidth == MATCH_CONSTRAINT_SPREAD) {
                            f.measure(widget, DB::FIXED, widgetSize, widget->getVerticalDimensionBehaviour(), widget->getHeight());
                        }
                    }
                } else {
                    if (widget != nullptr && widget->getVerticalDimensionBehaviour() == DB::MATCH_CONSTRAINT) {
                        if (widget->mMatchConstraintDefaultHeight == MATCH_CONSTRAINT_SPREAD) {
                            f.measure(widget, widget->getHorizontalDimensionBehaviour(), widget->getWidth(), DB::FIXED, widgetSize);
                        }
                    }
                }
            }
            recomputeDimensions();
        }

        Flow* const mFlow;
        int mOrientation = HORIZONTAL;
        ConstraintWidget* mBiggest = nullptr;
        int mBiggestDimension = 0;

    private:
        void recomputeDimensions() {
            Flow& f = *mFlow;
            mWidth = 0;
            mHeight = 0;
            mBiggest = nullptr;
            mBiggestDimension = 0;
            const int count = mCount;
            for (int i = 0; i < count; i++) {
                if (mStartIndex + i >= f.mDisplayedWidgetsCount) break;
                ConstraintWidget* widget = f.mDisplayedWidgets[std::size_t(mStartIndex + i)];
                if (mOrientation == HORIZONTAL) {
                    int width = widget->getWidth();
                    int gap = f.mHorizontalGap;
                    if (widget->getVisibility() == GONE) gap = 0;
                    mWidth += width + gap;
                    int height = f.getWidgetHeight(widget, mMax);
                    if (mBiggest == nullptr || mBiggestDimension < height) {
                        mBiggest = widget;
                        mBiggestDimension = height;
                        mHeight = height;
                    }
                } else {
                    int width = f.getWidgetWidth(widget, mMax);
                    int height = f.getWidgetHeight(widget, mMax);
                    int gap = f.mVerticalGap;
                    if (widget->getVisibility() == GONE) gap = 0;
                    mHeight += height + gap;
                    if (mBiggest == nullptr || mBiggestDimension < width) {
                        mBiggest = widget;
                        mBiggestDimension = width;
                        mWidth = width;
                    }
                }
            }
        }

        ConstraintAnchor* mLeft;
        ConstraintAnchor* mTop;
        ConstraintAnchor* mRight;
        ConstraintAnchor* mBottom;
        int mPaddingLeft = 0;
        int mPaddingTop = 0;
        int mPaddingRight = 0;
        int mPaddingBottom = 0;
        int mWidth = 0;
        int mHeight = 0;
        int mStartIndex = 0;
        int mCount = 0;
        int mNbMatchConstraintsWidgets = 0;
        int mMax = 0;
    };

    Flow::Flow() { mKind |= KIND_FLOW; }
    Flow::~Flow() = default;

    void Flow::copy(const ConstraintWidget& src, const WidgetMap& map) {
        VirtualLayout::copy(src, map);
        const auto& srcFLow = static_cast<const Flow&>(src);
        mHorizontalStyle = srcFLow.mHorizontalStyle;
        mVerticalStyle = srcFLow.mVerticalStyle;
        mFirstHorizontalStyle = srcFLow.mFirstHorizontalStyle;
        mFirstVerticalStyle = srcFLow.mFirstVerticalStyle;
        mLastHorizontalStyle = srcFLow.mLastHorizontalStyle;
        mLastVerticalStyle = srcFLow.mLastVerticalStyle;
        mHorizontalBias = srcFLow.mHorizontalBias;
        mVerticalBias = srcFLow.mVerticalBias;
        mFirstHorizontalBias = srcFLow.mFirstHorizontalBias;
        mFirstVerticalBias = srcFLow.mFirstVerticalBias;
        mLastHorizontalBias = srcFLow.mLastHorizontalBias;
        mLastVerticalBias = srcFLow.mLastVerticalBias;
        mHorizontalGap = srcFLow.mHorizontalGap;
        mVerticalGap = srcFLow.mVerticalGap;
        mHorizontalAlign = srcFLow.mHorizontalAlign;
        mVerticalAlign = srcFLow.mVerticalAlign;
        mWrapMode = srcFLow.mWrapMode;
        mMaxElementsWrap = srcFLow.mMaxElementsWrap;
        mOrientation = srcFLow.mOrientation;
    }

    int Flow::getWidgetWidth(ConstraintWidget* widget, int max) {
        if (widget == nullptr) return 0;
        if (widget->getHorizontalDimensionBehaviour() == DB::MATCH_CONSTRAINT) {
            if (widget->mMatchConstraintDefaultWidth == MATCH_CONSTRAINT_SPREAD) {
                return 0;
            } else if (widget->mMatchConstraintDefaultWidth == MATCH_CONSTRAINT_PERCENT) {
                int value = JavaInt(widget->mMatchConstraintPercentWidth * float(max));
                if (value != widget->getWidth()) {
                    widget->setMeasureRequested(true);
                    measure(widget, DB::FIXED, value, widget->getVerticalDimensionBehaviour(), widget->getHeight());
                }
                return value;
            } else if (widget->mMatchConstraintDefaultWidth == MATCH_CONSTRAINT_WRAP) {
                return widget->getWidth();
            } else if (widget->mMatchConstraintDefaultWidth == MATCH_CONSTRAINT_RATIO) {
                return JavaInt(float(widget->getHeight()) * widget->mDimensionRatio + 0.5f);
            }
        }
        return widget->getWidth();
    }

    int Flow::getWidgetHeight(ConstraintWidget* widget, int max) {
        if (widget == nullptr) return 0;
        if (widget->getVerticalDimensionBehaviour() == DB::MATCH_CONSTRAINT) {
            if (widget->mMatchConstraintDefaultHeight == MATCH_CONSTRAINT_SPREAD) {
                return 0;
            } else if (widget->mMatchConstraintDefaultHeight == MATCH_CONSTRAINT_PERCENT) {
                int value = JavaInt(widget->mMatchConstraintPercentHeight * float(max));
                if (value != widget->getHeight()) {
                    widget->setMeasureRequested(true);
                    measure(widget, widget->getHorizontalDimensionBehaviour(), widget->getWidth(), DB::FIXED, value);
                }
                return value;
            } else if (widget->mMatchConstraintDefaultHeight == MATCH_CONSTRAINT_WRAP) {
                return widget->getHeight();
            } else if (widget->mMatchConstraintDefaultHeight == MATCH_CONSTRAINT_RATIO) {
                return JavaInt(float(widget->getWidth()) * widget->mDimensionRatio + 0.5f);
            }
        }
        return widget->getHeight();
    }

    void Flow::measure(int widthMode, int widthSize, int heightMode, int heightSize) {
        if (mWidgetsCount > 0 && !measureChildren()) {
            setMeasure(0, 0);
            needsCallbackFromSolver(false);
            return;
        }

        int width = 0;
        int height = 0;
        int paddingLeft = getPaddingLeft();
        int paddingRight = getPaddingRight();
        int paddingTop = getPaddingTop();
        int paddingBottom = getPaddingBottom();

        int measured[2] = { 0, 0 };
        int max = widthSize - paddingLeft - paddingRight;
        if (mOrientation == VERTICAL) max = heightSize - paddingTop - paddingBottom;

        if (mOrientation == HORIZONTAL) {
            if (mHorizontalStyle == UNKNOWN) mHorizontalStyle = CHAIN_SPREAD;
            if (mVerticalStyle == UNKNOWN) mVerticalStyle = CHAIN_SPREAD;
        } else {
            if (mHorizontalStyle == UNKNOWN) mHorizontalStyle = CHAIN_SPREAD;
            if (mVerticalStyle == UNKNOWN) mVerticalStyle = CHAIN_SPREAD;
        }

        std::vector<ConstraintWidget*> widgets = mWidgets;
        int gone = 0;
        for (int i = 0; i < mWidgetsCount; i++) {
            ConstraintWidget* widget = mWidgets[std::size_t(i)];
            if (widget->getVisibility() == GONE) gone++;
        }
        int count = mWidgetsCount;
        if (gone > 0) {
            widgets.assign(std::size_t(mWidgetsCount - gone), nullptr);
            int j = 0;
            for (int i = 0; i < mWidgetsCount; i++) {
                ConstraintWidget* widget = mWidgets[std::size_t(i)];
                if (widget->getVisibility() != GONE) {
                    widgets[std::size_t(j)] = widget;
                    j++;
                }
            }
            count = j;
        }
        mDisplayedWidgets = widgets;
        mDisplayedWidgetsCount = count;
        switch (mWrapMode) {
            case WRAP_ALIGNED:
                measureAligned(widgets, count, mOrientation, max, measured);
                break;
            case WRAP_CHAIN:
                measureChainWrap(widgets, count, mOrientation, max, measured);
                break;
            case WRAP_NONE:
                measureNoWrap(widgets, count, mOrientation, max, measured);
                break;
            case WRAP_CHAIN_NEW:
                measureChainWrap_new(widgets, count, mOrientation, max, measured);
                break;
        }

        width = measured[HORIZONTAL] + paddingLeft + paddingRight;
        height = measured[VERTICAL] + paddingTop + paddingBottom;

        int measuredWidth = 0;
        int measuredHeight = 0;
        if (widthMode == BasicMeasure::EXACTLY) measuredWidth = widthSize;
        else if (widthMode == BasicMeasure::AT_MOST) measuredWidth = std::min(width, widthSize);
        else if (widthMode == BasicMeasure::UNSPECIFIED) measuredWidth = width;

        if (heightMode == BasicMeasure::EXACTLY) measuredHeight = heightSize;
        else if (heightMode == BasicMeasure::AT_MOST) measuredHeight = std::min(height, heightSize);
        else if (heightMode == BasicMeasure::UNSPECIFIED) measuredHeight = height;

        setMeasure(measuredWidth, measuredHeight);
        setWidth(measuredWidth);
        setHeight(measuredHeight);
        needsCallbackFromSolver(mWidgetsCount > 0);
    }

    void Flow::measureChainWrap(const std::vector<ConstraintWidget*>& widgets, int count, int orientation, int max, int* measured) {
        if (count == 0) return;

        mChainList.clear();
        WidgetsList* list = mChainList.emplace_back(std::make_unique<WidgetsList>(this, orientation, &mLeft, &mTop, &mRight, &mBottom, max)).get();

        int nbMatchConstraintsWidgets = 0;
        if (orientation == HORIZONTAL) {
            int width = 0;
            for (int i = 0; i < count; i++) {
                ConstraintWidget* widget = widgets[std::size_t(i)];
                int w = getWidgetWidth(widget, max);
                if (widget->getHorizontalDimensionBehaviour() == DB::MATCH_CONSTRAINT) nbMatchConstraintsWidgets++;
                bool doWrap = (width == max || (width + mHorizontalGap + w) > max) && list->mBiggest != nullptr;
                if (!doWrap && i > 0 && mMaxElementsWrap > 0 && (i % mMaxElementsWrap == 0)) doWrap = true;
                if (doWrap) {
                    width = w;
                    list = mChainList.emplace_back(std::make_unique<WidgetsList>(this, orientation, &mLeft, &mTop, &mRight, &mBottom, max)).get();
                    list->setStartIndex(i);
                } else {
                    if (i > 0) width += mHorizontalGap + w;
                    else width = w;
                }
                list->add(widget);
            }
        } else {
            int height = 0;
            for (int i = 0; i < count; i++) {
                ConstraintWidget* widget = widgets[std::size_t(i)];
                int h = getWidgetHeight(widget, max);
                if (widget->getVerticalDimensionBehaviour() == DB::MATCH_CONSTRAINT) nbMatchConstraintsWidgets++;
                bool doWrap = (height == max || (height + mVerticalGap + h) > max) && list->mBiggest != nullptr;
                if (!doWrap && i > 0 && mMaxElementsWrap > 0 && (i % mMaxElementsWrap == 0)) doWrap = true;
                if (doWrap) {
                    height = h;
                    list = mChainList.emplace_back(std::make_unique<WidgetsList>(this, orientation, &mLeft, &mTop, &mRight, &mBottom, max)).get();
                    list->setStartIndex(i);
                } else {
                    if (i > 0) height += mVerticalGap + h;
                    else height = h;
                }
                list->add(widget);
            }
        }

        const int listCount = int(mChainList.size());

        ConstraintAnchor* left = &mLeft;
        ConstraintAnchor* top = &mTop;
        ConstraintAnchor* right = &mRight;
        ConstraintAnchor* bottom = &mBottom;

        int paddingLeft = getPaddingLeft();
        int paddingTop = getPaddingTop();
        int paddingRight = getPaddingRight();
        int paddingBottom = getPaddingBottom();

        int maxWidth = 0;
        int maxHeight = 0;

        bool needInternalMeasure =
            getHorizontalDimensionBehaviour() == DB::WRAP_CONTENT || getVerticalDimensionBehaviour() == DB::WRAP_CONTENT;

        if (nbMatchConstraintsWidgets > 0 && needInternalMeasure) {
            for (int i = 0; i < listCount; i++) {
                WidgetsList* current = mChainList[std::size_t(i)].get();
                if (orientation == HORIZONTAL) current->measureMatchConstraints(max - current->getWidth());
                else current->measureMatchConstraints(max - current->getHeight());
            }
        }

        for (int i = 0; i < listCount; i++) {
            WidgetsList* current = mChainList[std::size_t(i)].get();
            if (orientation == HORIZONTAL) {
                if (i < listCount - 1) {
                    WidgetsList* next = mChainList[std::size_t(i + 1)].get();
                    bottom = &next->mBiggest->mTop;
                    paddingBottom = 0;
                } else {
                    bottom = &mBottom;
                    paddingBottom = getPaddingBottom();
                }
                ConstraintAnchor* currentBottom = &current->mBiggest->mBottom;
                current->setup(orientation, left, top, right, bottom, paddingLeft, paddingTop, paddingRight, paddingBottom, max);
                top = currentBottom;
                paddingTop = 0;
                maxWidth = std::max(maxWidth, current->getWidth());
                maxHeight += current->getHeight();
                if (i > 0) maxHeight += mVerticalGap;
            } else {
                if (i < listCount - 1) {
                    WidgetsList* next = mChainList[std::size_t(i + 1)].get();
                    right = &next->mBiggest->mLeft;
                    paddingRight = 0;
                } else {
                    right = &mRight;
                    paddingRight = getPaddingRight();
                }
                ConstraintAnchor* currentRight = &current->mBiggest->mRight;
                current->setup(orientation, left, top, right, bottom, paddingLeft, paddingTop, paddingRight, paddingBottom, max);
                left = currentRight;
                paddingLeft = 0;
                maxWidth += current->getWidth();
                maxHeight = std::max(maxHeight, current->getHeight());
                if (i > 0) maxWidth += mHorizontalGap;
            }
        }
        measured[HORIZONTAL] = maxWidth;
        measured[VERTICAL] = maxHeight;
    }

    void Flow::measureChainWrap_new(const std::vector<ConstraintWidget*>& widgets, int count, int orientation, int max, int* measured) {
        if (count == 0) return;

        mChainList.clear();
        WidgetsList* list = mChainList.emplace_back(std::make_unique<WidgetsList>(this, orientation, &mLeft, &mTop, &mRight, &mBottom, max)).get();

        int nbMatchConstraintsWidgets = 0;
        if (orientation == HORIZONTAL) {
            int width = 0;
            int col = 0;
            for (int i = 0; i < count; i++) {
                col++;
                ConstraintWidget* widget = widgets[std::size_t(i)];
                int w = getWidgetWidth(widget, max);
                if (widget->getHorizontalDimensionBehaviour() == DB::MATCH_CONSTRAINT) nbMatchConstraintsWidgets++;
                bool doWrap = (width == max || (width + mHorizontalGap + w) > max) && list->mBiggest != nullptr;
                if (!doWrap && i > 0 && mMaxElementsWrap > 0 && (col > mMaxElementsWrap)) doWrap = true;
                if (doWrap) {
                    col = 1;
                    width = w;
                    list = mChainList.emplace_back(std::make_unique<WidgetsList>(this, orientation, &mLeft, &mTop, &mRight, &mBottom, max)).get();
                    list->setStartIndex(i);
                } else {
                    if (i > 0) width += mHorizontalGap + w;
                    else width = w;
                }
                list->add(widget);
            }
        } else {
            int height = 0;
            int row = 0;
            for (int i = 0; i < count; i++) {
                row++;
                ConstraintWidget* widget = widgets[std::size_t(i)];
                int h = getWidgetHeight(widget, max);
                if (widget->getVerticalDimensionBehaviour() == DB::MATCH_CONSTRAINT) nbMatchConstraintsWidgets++;
                bool doWrap = (height == max || (height + mVerticalGap + h) > max) && list->mBiggest != nullptr;
                if (!doWrap && i > 0 && mMaxElementsWrap > 0 && (row > mMaxElementsWrap)) doWrap = true;
                if (doWrap) {
                    row = 1;
                    height = h;
                    list = mChainList.emplace_back(std::make_unique<WidgetsList>(this, orientation, &mLeft, &mTop, &mRight, &mBottom, max)).get();
                    list->setStartIndex(i);
                } else {
                    if (i > 0) height += mVerticalGap + h;
                    else height = h;
                }
                list->add(widget);
            }
        }

        const int listCount = int(mChainList.size());

        ConstraintAnchor* left = &mLeft;
        ConstraintAnchor* top = &mTop;
        ConstraintAnchor* right = &mRight;
        ConstraintAnchor* bottom = &mBottom;

        int paddingLeft = getPaddingLeft();
        int paddingTop = getPaddingTop();
        int paddingRight = getPaddingRight();
        int paddingBottom = getPaddingBottom();

        int maxWidth = 0;
        int maxHeight = 0;

        bool needInternalMeasure =
            getHorizontalDimensionBehaviour() == DB::WRAP_CONTENT || getVerticalDimensionBehaviour() == DB::WRAP_CONTENT;

        if (nbMatchConstraintsWidgets > 0 && needInternalMeasure) {
            for (int i = 0; i < listCount; i++) {
                WidgetsList* current = mChainList[std::size_t(i)].get();
                if (orientation == HORIZONTAL) current->measureMatchConstraints(max - current->getWidth());
                else current->measureMatchConstraints(max - current->getHeight());
            }
        }

        for (int i = 0; i < listCount; i++) {
            WidgetsList* current = mChainList[std::size_t(i)].get();
            if (orientation == HORIZONTAL) {
                if (i < listCount - 1) {
                    WidgetsList* next = mChainList[std::size_t(i + 1)].get();
                    bottom = &next->mBiggest->mTop;
                    paddingBottom = 0;
                } else {
                    bottom = &mBottom;
                    paddingBottom = getPaddingBottom();
                }
                ConstraintAnchor* currentBottom = &current->mBiggest->mBottom;
                current->setup(orientation, left, top, right, bottom, paddingLeft, paddingTop, paddingRight, paddingBottom, max);
                top = currentBottom;
                paddingTop = 0;
                maxWidth = std::max(maxWidth, current->getWidth());
                maxHeight += current->getHeight();
                if (i > 0) maxHeight += mVerticalGap;
            } else {
                if (i < listCount - 1) {
                    WidgetsList* next = mChainList[std::size_t(i + 1)].get();
                    right = &next->mBiggest->mLeft;
                    paddingRight = 0;
                } else {
                    right = &mRight;
                    paddingRight = getPaddingRight();
                }
                ConstraintAnchor* currentRight = &current->mBiggest->mRight;
                current->setup(orientation, left, top, right, bottom, paddingLeft, paddingTop, paddingRight, paddingBottom, max);
                left = currentRight;
                paddingLeft = 0;
                maxWidth += current->getWidth();
                maxHeight = std::max(maxHeight, current->getHeight());
                if (i > 0) maxWidth += mHorizontalGap;
            }
        }
        measured[HORIZONTAL] = maxWidth;
        measured[VERTICAL] = maxHeight;
    }

    void Flow::measureNoWrap(const std::vector<ConstraintWidget*>& widgets, int count, int orientation, int max, int* measured) {
        if (count == 0) return;
        WidgetsList* list = nullptr;
        if (mChainList.empty()) {
            list = mChainList.emplace_back(std::make_unique<WidgetsList>(this, orientation, &mLeft, &mTop, &mRight, &mBottom, max)).get();
        } else {
            list = mChainList[0].get();
            list->clear();
            list->setup(orientation, &mLeft, &mTop, &mRight, &mBottom, getPaddingLeft(), getPaddingTop(), getPaddingRight(),
                        getPaddingBottom(), max);
        }
        for (int i = 0; i < count; i++) list->add(widgets[std::size_t(i)]);
        measured[HORIZONTAL] = list->getWidth();
        measured[VERTICAL] = list->getHeight();
    }

    void Flow::measureAligned(const std::vector<ConstraintWidget*>& widgets, int count, int orientation, int max, int* measured) {
        bool done = false;
        int rows = 0;
        int cols = 0;

        if (orientation == HORIZONTAL) {
            cols = mMaxElementsWrap;
            if (cols <= 0) {
                int w = 0;
                cols = 0;
                for (int i = 0; i < count; i++) {
                    if (i > 0) w += mHorizontalGap;
                    ConstraintWidget* widget = widgets[std::size_t(i)];
                    if (widget == nullptr) continue;
                    w += getWidgetWidth(widget, max);
                    if (w > max) break;
                    cols++;
                }
            }
        } else {
            rows = mMaxElementsWrap;
            if (rows <= 0) {
                int h = 0;
                rows = 0;
                for (int i = 0; i < count; i++) {
                    if (i > 0) h += mVerticalGap;
                    ConstraintWidget* widget = widgets[std::size_t(i)];
                    if (widget == nullptr) continue;
                    h += getWidgetHeight(widget, max);
                    if (h > max) break;
                    rows++;
                }
            }
        }

        if (mAlignedDimensions.empty()) mAlignedDimensions.assign(2, 0);

        if ((rows == 0 && orientation == VERTICAL) || (cols == 0 && orientation == HORIZONTAL)) done = true;

        while (!done) {
            if (orientation == HORIZONTAL) rows = JavaInt(std::ceil(float(count) / float(cols)));
            else cols = JavaInt(std::ceil(float(count) / float(rows)));

            if (mAlignedBiggestElementsInCols.empty() || int(mAlignedBiggestElementsInCols.size()) < cols) {
                mAlignedBiggestElementsInCols.assign(std::size_t(cols), nullptr);
            } else {
                std::fill(mAlignedBiggestElementsInCols.begin(), mAlignedBiggestElementsInCols.end(), nullptr);
            }
            if (mAlignedBiggestElementsInRows.empty() || int(mAlignedBiggestElementsInRows.size()) < rows) {
                mAlignedBiggestElementsInRows.assign(std::size_t(rows), nullptr);
            } else {
                std::fill(mAlignedBiggestElementsInRows.begin(), mAlignedBiggestElementsInRows.end(), nullptr);
            }

            for (int i = 0; i < cols; i++) {
                for (int j = 0; j < rows; j++) {
                    int index = j * cols + i;
                    if (orientation == VERTICAL) index = i * rows + j;
                    if (index >= int(widgets.size())) continue;
                    ConstraintWidget* widget = widgets[std::size_t(index)];
                    if (widget == nullptr) continue;
                    int w = getWidgetWidth(widget, max);
                    ConstraintWidget*& biggestInCol = mAlignedBiggestElementsInCols[std::size_t(i)];
                    if (biggestInCol == nullptr || biggestInCol->getWidth() < w) biggestInCol = widget;
                    int h = getWidgetHeight(widget, max);
                    ConstraintWidget*& biggestInRow = mAlignedBiggestElementsInRows[std::size_t(j)];
                    if (biggestInRow == nullptr || biggestInRow->getHeight() < h) biggestInRow = widget;
                }
            }

            int w = 0;
            for (int i = 0; i < cols; i++) {
                ConstraintWidget* widget = mAlignedBiggestElementsInCols[std::size_t(i)];
                if (widget != nullptr) {
                    if (i > 0) w += mHorizontalGap;
                    w += getWidgetWidth(widget, max);
                }
            }
            int h = 0;
            for (int j = 0; j < rows; j++) {
                ConstraintWidget* widget = mAlignedBiggestElementsInRows[std::size_t(j)];
                if (widget != nullptr) {
                    if (j > 0) h += mVerticalGap;
                    h += getWidgetHeight(widget, max);
                }
            }
            measured[HORIZONTAL] = w;
            measured[VERTICAL] = h;

            if (orientation == HORIZONTAL) {
                if (w > max) {
                    if (cols > 1) cols--;
                    else done = true;
                } else {
                    done = true;
                }
            } else {
                if (h > max) {
                    if (rows > 1) rows--;
                    else done = true;
                } else {
                    done = true;
                }
            }
        }
        mAlignedDimensions[HORIZONTAL] = cols;
        mAlignedDimensions[VERTICAL] = rows;
    }

    void Flow::createAlignedConstraints(bool isInRtl) {
        if (mAlignedDimensions.empty() || mAlignedBiggestElementsInCols.empty() || mAlignedBiggestElementsInRows.empty()) return;

        for (int i = 0; i < mDisplayedWidgetsCount; i++) mDisplayedWidgets[std::size_t(i)]->resetAnchors();

        int cols = mAlignedDimensions[HORIZONTAL];
        int rows = mAlignedDimensions[VERTICAL];

        ConstraintWidget* previous = nullptr;
        float horizontalBias = mHorizontalBias;
        for (int i = 0; i < cols; i++) {
            int index = i;
            if (isInRtl) {
                index = cols - i - 1;
                horizontalBias = 1 - mHorizontalBias;
            }
            ConstraintWidget* widget = mAlignedBiggestElementsInCols[std::size_t(index)];
            if (widget == nullptr || widget->getVisibility() == GONE) continue;
            if (i == 0) {
                widget->connect(&widget->mLeft, &mLeft, getPaddingLeft());
                widget->setHorizontalChainStyle(mHorizontalStyle);
                widget->setHorizontalBiasPercent(horizontalBias);
            }
            if (i == cols - 1) widget->connect(&widget->mRight, &mRight, getPaddingRight());
            if (i > 0 && previous != nullptr) {
                widget->connect(&widget->mLeft, &previous->mRight, mHorizontalGap);
                previous->connect(&previous->mRight, &widget->mLeft, 0);
            }
            previous = widget;
        }
        for (int j = 0; j < rows; j++) {
            ConstraintWidget* widget = mAlignedBiggestElementsInRows[std::size_t(j)];
            if (widget == nullptr || widget->getVisibility() == GONE) continue;
            if (j == 0) {
                widget->connect(&widget->mTop, &mTop, getPaddingTop());
                widget->setVerticalChainStyle(mVerticalStyle);
                widget->setVerticalBiasPercent(mVerticalBias);
            }
            if (j == rows - 1) widget->connect(&widget->mBottom, &mBottom, getPaddingBottom());
            if (j > 0 && previous != nullptr) {
                widget->connect(&widget->mTop, &previous->mBottom, mVerticalGap);
                previous->connect(&previous->mBottom, &widget->mTop, 0);
            }
            previous = widget;
        }

        for (int i = 0; i < cols; i++) {
            for (int j = 0; j < rows; j++) {
                int index = j * cols + i;
                if (mOrientation == VERTICAL) index = i * rows + j;
                if (index >= int(mDisplayedWidgets.size())) continue;
                ConstraintWidget* widget = mDisplayedWidgets[std::size_t(index)];
                if (widget == nullptr || widget->getVisibility() == GONE) continue;
                ConstraintWidget* biggestInCol = mAlignedBiggestElementsInCols[std::size_t(i)];
                ConstraintWidget* biggestInRow = mAlignedBiggestElementsInRows[std::size_t(j)];
                if (widget != biggestInCol) {
                    widget->connect(&widget->mLeft, &biggestInCol->mLeft, 0);
                    widget->connect(&widget->mRight, &biggestInCol->mRight, 0);
                }
                if (widget != biggestInRow) {
                    widget->connect(&widget->mTop, &biggestInRow->mTop, 0);
                    widget->connect(&widget->mBottom, &biggestInRow->mBottom, 0);
                }
            }
        }
    }

    void Flow::addToSolver(LinearSystem& system, bool optimize) {
        VirtualLayout::addToSolver(system, optimize);

        bool isInRtl = getParent() != nullptr && static_cast<ConstraintWidgetContainer*>(getParent())->isRtl();
        switch (mWrapMode) {
            case WRAP_CHAIN:
            case WRAP_CHAIN_NEW: {
                const int count = int(mChainList.size());
                for (int i = 0; i < count; i++) mChainList[std::size_t(i)]->createConstraints(isInRtl, i, i == count - 1);
                break;
            }
            case WRAP_NONE:
                if (!mChainList.empty()) mChainList[0]->createConstraints(isInRtl, 0, true);
                break;
            case WRAP_ALIGNED:
                createAlignedConstraints(isInRtl);
                break;
        }
        needsCallbackFromSolver(false);
    }

}
