#include "kiln/constraintlayout/core/state/StateHelpers.h"

#include "kiln/constraintlayout/core/widgets/Barrier.h"
#include "kiln/constraintlayout/core/widgets/Flow.h"
#include "kiln/constraintlayout/core/widgets/Guideline.h"

namespace kiln::constraintlayout::core::state {

    // GuidelineReference

    GuidelineReference& GuidelineReference::start(float margin) { return start(mState.convertDimension(margin)); }
    GuidelineReference& GuidelineReference::start(int margin) {
        mStart = mState.convertDimension(margin);
        mEnd = -1;
        mPercent = 0;
        return *this;
    }
    GuidelineReference& GuidelineReference::end(float margin) { return end(mState.convertDimension(margin)); }
    GuidelineReference& GuidelineReference::end(int margin) {
        mStart = -1;
        mEnd = mState.convertDimension(margin);
        mPercent = 0;
        return *this;
    }

    void GuidelineReference::apply() {
        mGuidelineWidget->setOrientation(mOrientation);
        if (mStart != -1) mGuidelineWidget->setGuideBegin(mStart);
        else if (mEnd != -1) mGuidelineWidget->setGuideEnd(mEnd);
        else mGuidelineWidget->setGuidePercent(mPercent);
    }

    ConstraintWidget* GuidelineReference::getConstraintWidget() {
        if (mGuidelineWidget == nullptr) mGuidelineWidget = mState.adopt(std::make_shared<Guideline>());
        return mGuidelineWidget;
    }

    void GuidelineReference::setConstraintWidget(ConstraintWidget* widget) {
        mGuidelineWidget = widget != nullptr && widget->is(ConstraintWidget::KIND_GUIDELINE) ? static_cast<Guideline*>(widget) : nullptr;
    }

    // BarrierReference

    HelperWidget* BarrierReference::getHelperWidget() {
        if (mBarrierWidget == nullptr) mBarrierWidget = mHelperState.adopt(std::make_shared<Barrier>());
        return mBarrierWidget;
    }

    void BarrierReference::apply() {
        getHelperWidget();
        int direction = Barrier::LEFT;
        switch (mDirection) {
            case State::Direction::LEFT:
            case State::Direction::START:
                break;
            case State::Direction::RIGHT:
            case State::Direction::END:
                direction = Barrier::RIGHT;
                break;
            case State::Direction::TOP:
                direction = Barrier::TOP;
                break;
            case State::Direction::BOTTOM:
                direction = Barrier::BOTTOM;
                break;
        }
        mBarrierWidget->setBarrierType(direction);
        mBarrierWidget->setMargin(mMargin);
    }

    // Align references

    void AlignHorizontallyReference::apply() {
        for (const std::string& key : mReferences) {
            ConstraintReference* reference = mHelperState.constraints(key);
            reference->clearHorizontal();
            if (isSet(mStartToStart)) reference->startToStart(mStartToStart);
            else if (isSet(mStartToEnd)) reference->startToEnd(mStartToEnd);
            else reference->startToStart(State::PARENT);
            if (isSet(mEndToStart)) reference->endToStart(mEndToStart);
            else if (isSet(mEndToEnd)) reference->endToEnd(mEndToEnd);
            else reference->endToEnd(State::PARENT);
            if (mBias != 0.5f) reference->horizontalBias(mBias);
        }
    }

    void AlignVerticallyReference::apply() {
        for (const std::string& key : mReferences) {
            ConstraintReference* reference = mHelperState.constraints(key);
            reference->clearVertical();
            if (isSet(mTopToTop)) reference->topToTop(mTopToTop);
            else if (isSet(mTopToBottom)) reference->topToBottom(mTopToBottom);
            else reference->topToTop(State::PARENT);
            if (isSet(mBottomToTop)) reference->bottomToTop(mBottomToTop);
            else if (isSet(mBottomToBottom)) reference->bottomToBottom(mBottomToBottom);
            else reference->bottomToBottom(State::PARENT);
            if (mBias != 0.5f) reference->verticalBias(mBias);
        }
    }

    // ChainReference

    void ChainReference::addChainElement(const std::string& id, float weight, float preMargin, float postMargin, float preGoneMargin,
                                         float postGoneMargin) {
        HelperReference::add(id);
        const std::string& idString = id;
        if (!std::isnan(weight)) mMapWeights.put(idString, weight);
        if (!std::isnan(preMargin)) mMapPreMargin.put(idString, preMargin);
        if (!std::isnan(postMargin)) mMapPostMargin.put(idString, postMargin);
        if (!std::isnan(preGoneMargin)) mMapPreGoneMargin.put(idString, preGoneMargin);
        if (!std::isnan(postGoneMargin)) mMapPostGoneMargin.put(idString, postGoneMargin);
    }

    float ChainReference::getWeight(const std::string& id) const {
        const float* v = mMapWeights.find(id);
        return v ? *v : float(ConstraintWidget::UNKNOWN);
    }
    float ChainReference::getPostMargin(const std::string& id) const {
        const float* v = mMapPostMargin.find(id);
        return v ? *v : 0;
    }
    float ChainReference::getPreMargin(const std::string& id) const {
        const float* v = mMapPreMargin.find(id);
        return v ? *v : 0;
    }
    float ChainReference::getPostGoneMargin(const std::string& id) const {
        const float* v = mMapPostGoneMargin.find(id);
        return v ? *v : 0;
    }
    float ChainReference::getPreGoneMargin(const std::string& id) const {
        const float* v = mMapPreGoneMargin.find(id);
        return v ? *v : 0;
    }

    void HorizontalChainReference::apply() {
        ConstraintReference* first = nullptr;
        ConstraintReference* previous = nullptr;
        for (const std::string& key : mReferences) mHelperState.constraints(key)->clearHorizontal();

        for (const std::string& key : mReferences) {
            ConstraintReference* reference = mHelperState.constraints(key);
            if (first == nullptr) {
                first = reference;
                if (isSet(mStartToStart)) first->startToStart(mStartToStart).margin(mMarginStart).marginGone(mMarginStartGone);
                else if (isSet(mStartToEnd)) first->startToEnd(mStartToEnd).margin(mMarginStart).marginGone(mMarginStartGone);
                else if (isSet(mLeftToLeft)) first->startToStart(mLeftToLeft).margin(mMarginLeft).marginGone(mMarginLeftGone);
                else if (isSet(mLeftToRight)) first->startToEnd(mLeftToRight).margin(mMarginLeft).marginGone(mMarginLeftGone);
                else {
                    std::string refKey = State::keyToString(reference->getKey());
                    first->startToStart(State::PARENT).margin(getPreMargin(refKey)).marginGone(getPreGoneMargin(refKey));
                }
            }
            if (previous != nullptr) {
                std::string preKey = State::keyToString(previous->getKey());
                std::string refKey = State::keyToString(reference->getKey());
                previous->endToStart(reference->getKey()).margin(getPostMargin(preKey)).marginGone(getPostGoneMargin(preKey));
                reference->startToEnd(previous->getKey()).margin(getPreMargin(refKey)).marginGone(getPreGoneMargin(refKey));
            }
            float weight = getWeight(State::keyToString(key));
            if (weight != float(ConstraintWidget::UNKNOWN)) reference->setHorizontalChainWeight(weight);
            previous = reference;
        }

        if (previous != nullptr) {
            if (isSet(mEndToStart)) previous->endToStart(mEndToStart).margin(mMarginEnd).marginGone(mMarginEndGone);
            else if (isSet(mEndToEnd)) previous->endToEnd(mEndToEnd).margin(mMarginEnd).marginGone(mMarginEndGone);
            else if (isSet(mRightToLeft)) previous->endToStart(mRightToLeft).margin(mMarginRight).marginGone(mMarginRightGone);
            else if (isSet(mRightToRight)) previous->endToEnd(mRightToRight).margin(mMarginRight).marginGone(mMarginRightGone);
            else {
                std::string preKey = State::keyToString(previous->getKey());
                previous->endToEnd(State::PARENT).margin(getPostMargin(preKey)).marginGone(getPostGoneMargin(preKey));
            }
        }

        if (first == nullptr) return;
        if (mBias != 0.5f) first->horizontalBias(mBias);
        switch (mStyle) {
            case State::Chain::SPREAD:
                first->setHorizontalChainStyle(ConstraintWidget::CHAIN_SPREAD);
                break;
            case State::Chain::SPREAD_INSIDE:
                first->setHorizontalChainStyle(ConstraintWidget::CHAIN_SPREAD_INSIDE);
                break;
            case State::Chain::PACKED:
                first->setHorizontalChainStyle(ConstraintWidget::CHAIN_PACKED);
                break;
        }
    }

    void VerticalChainReference::apply() {
        ConstraintReference* first = nullptr;
        ConstraintReference* previous = nullptr;
        for (const std::string& key : mReferences) mHelperState.constraints(key)->clearVertical();

        for (const std::string& key : mReferences) {
            ConstraintReference* reference = mHelperState.constraints(key);
            if (first == nullptr) {
                first = reference;
                if (isSet(mTopToTop)) first->topToTop(mTopToTop).margin(mMarginTop).marginGone(mMarginTopGone);
                else if (isSet(mTopToBottom)) first->topToBottom(mTopToBottom).margin(mMarginTop).marginGone(mMarginTopGone);
                else {
                    std::string refKey = State::keyToString(reference->getKey());
                    first->topToTop(State::PARENT).margin(getPreMargin(refKey)).marginGone(getPreGoneMargin(refKey));
                }
            }
            if (previous != nullptr) {
                std::string preKey = State::keyToString(previous->getKey());
                std::string refKey = State::keyToString(reference->getKey());
                previous->bottomToTop(reference->getKey()).margin(getPostMargin(preKey)).marginGone(getPostGoneMargin(preKey));
                reference->topToBottom(previous->getKey()).margin(getPreMargin(refKey)).marginGone(getPreGoneMargin(refKey));
            }
            float weight = getWeight(State::keyToString(key));
            if (weight != float(ConstraintWidget::UNKNOWN)) reference->setVerticalChainWeight(weight);
            previous = reference;
        }

        if (previous != nullptr) {
            if (isSet(mBottomToTop)) previous->bottomToTop(mBottomToTop).margin(mMarginBottom).marginGone(mMarginBottomGone);
            else if (isSet(mBottomToBottom)) previous->bottomToBottom(mBottomToBottom).margin(mMarginBottom).marginGone(mMarginBottomGone);
            else {
                std::string preKey = State::keyToString(previous->getKey());
                previous->bottomToBottom(State::PARENT).margin(getPostMargin(preKey)).marginGone(getPostGoneMargin(preKey));
            }
        }

        if (first == nullptr) return;
        if (mBias != 0.5f) first->verticalBias(mBias);
        switch (mStyle) {
            case State::Chain::SPREAD:
                first->setVerticalChainStyle(ConstraintWidget::CHAIN_SPREAD);
                break;
            case State::Chain::SPREAD_INSIDE:
                first->setVerticalChainStyle(ConstraintWidget::CHAIN_SPREAD_INSIDE);
                break;
            case State::Chain::PACKED:
                first->setVerticalChainStyle(ConstraintWidget::CHAIN_PACKED);
                break;
        }
    }

    // FlowReference

    FlowReference::FlowReference(State& state, int type) : HelperReference(state, type) {
        if (type == State::VERTICAL_FLOW) mOrientation = ConstraintWidget::VERTICAL;
    }

    void FlowReference::addFlowElement(const std::string& id, float weight, float preMargin, float postMargin) {
        HelperReference::add(id);
        if (!std::isnan(weight)) mMapWeights.put(id, weight);
        if (!std::isnan(preMargin)) mMapPreMargin.put(id, preMargin);
        if (!std::isnan(postMargin)) mMapPostMargin.put(id, postMargin);
    }

    float FlowReference::getWeight(const std::string& id) const {
        const float* v = mMapWeights.find(id);
        return v ? *v : float(ConstraintWidget::UNKNOWN);
    }
    // Upstream reads the maps crossed over; kept.
    float FlowReference::getPostMargin(const std::string& id) const {
        const float* v = mMapPreMargin.find(id);
        return v ? *v : 0;
    }
    float FlowReference::getPreMargin(const std::string& id) const {
        const float* v = mMapPostMargin.find(id);
        return v ? *v : 0;
    }

    HelperWidget* FlowReference::getHelperWidget() {
        if (mFlow == nullptr) mFlow = mHelperState.adopt(std::make_shared<Flow>());
        return mFlow;
    }

    void FlowReference::setHelperWidget(HelperWidget* widget) {
        mFlow = widget != nullptr && widget->is(ConstraintWidget::KIND_FLOW) ? static_cast<Flow*>(widget) : nullptr;
    }

    void FlowReference::apply() {
        getHelperWidget();
        setConstraintWidget(mFlow);
        mFlow->setOrientation(mOrientation);
        mFlow->setWrapMode(mWrapMode);
        if (mMaxElementsWrap != ConstraintWidget::UNKNOWN) mFlow->setMaxElementsWrap(mMaxElementsWrap);
        if (mPaddingLeft != 0) mFlow->setPaddingLeft(mPaddingLeft);
        if (mPaddingTop != 0) mFlow->setPaddingTop(mPaddingTop);
        if (mPaddingRight != 0) mFlow->setPaddingRight(mPaddingRight);
        if (mPaddingBottom != 0) mFlow->setPaddingBottom(mPaddingBottom);
        if (mHorizontalGap != 0) mFlow->setHorizontalGap(mHorizontalGap);
        if (mVerticalGap != 0) mFlow->setVerticalGap(mVerticalGap);
        if (mHorizontalBias != 0.5f) mFlow->setHorizontalBias(mHorizontalBias);
        if (mFirstHorizontalBias != 0.5f) mFlow->setFirstHorizontalBias(mFirstHorizontalBias);
        if (mLastHorizontalBias != 0.5f) mFlow->setLastHorizontalBias(mLastHorizontalBias);
        if (mVerticalBias != 0.5f) mFlow->setVerticalBias(mVerticalBias);
        if (mFirstVerticalBias != 0.5f) mFlow->setFirstVerticalBias(mFirstVerticalBias);
        if (mLastVerticalBias != 0.5f) mFlow->setLastVerticalBias(mLastVerticalBias);
        if (mHorizontalAlign != Flow::HORIZONTAL_ALIGN_CENTER) mFlow->setHorizontalAlign(mHorizontalAlign);
        if (mVerticalAlign != Flow::VERTICAL_ALIGN_CENTER) mFlow->setVerticalAlign(mVerticalAlign);
        if (mVerticalStyle != ConstraintWidget::UNKNOWN) mFlow->setVerticalStyle(mVerticalStyle);
        if (mFirstVerticalStyle != ConstraintWidget::UNKNOWN) mFlow->setFirstVerticalStyle(mFirstVerticalStyle);
        if (mLastVerticalStyle != ConstraintWidget::UNKNOWN) mFlow->setLastVerticalStyle(mLastVerticalStyle);
        if (mHorizontalStyle != ConstraintWidget::UNKNOWN) mFlow->setHorizontalStyle(mHorizontalStyle);
        if (mFirstHorizontalStyle != ConstraintWidget::UNKNOWN) mFlow->setFirstHorizontalStyle(mFirstHorizontalStyle);
        if (mLastHorizontalStyle != ConstraintWidget::UNKNOWN) mFlow->setLastHorizontalStyle(mLastHorizontalStyle);
        applyBase();
    }

}
