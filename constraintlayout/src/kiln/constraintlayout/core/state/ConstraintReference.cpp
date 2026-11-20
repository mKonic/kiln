#include "kiln/constraintlayout/core/state/State.h"

#include "kiln/constraintlayout/core/widgets/HelperWidget.h"

namespace kiln::constraintlayout::core::state {

    using T = ConstraintAnchor::Type;

    void ConstraintReference::setView(void* view) {
        mView = view;
        if (mConstraintWidget != nullptr) mConstraintWidget->setCompanionWidget(mView);
    }

    void ConstraintReference::setFacade(std::shared_ptr<Facade> facade) {
        mFacade = std::move(facade);
        if (mFacade != nullptr) setConstraintWidget(mFacade->getConstraintWidget());
    }

    void ConstraintReference::setConstraintWidget(ConstraintWidget* widget) {
        if (widget == nullptr) return;
        mConstraintWidget = widget;
        mConstraintWidget->setCompanionWidget(mView);
    }

    ConstraintWidget* ConstraintReference::getConstraintWidget() {
        if (mConstraintWidget == nullptr) {
            mConstraintWidget = mState.adopt(createConstraintWidget());
            mConstraintWidget->setCompanionWidget(mView);
        }
        return mConstraintWidget;
    }

    std::shared_ptr<ConstraintWidget> ConstraintReference::createConstraintWidget() {
        return std::make_shared<ConstraintWidget>(getWidth().getValue(), getHeight().getValue());
    }

    void ConstraintReference::validate() const {
        std::vector<std::string> errors;
        if (isSet(mLeftToLeft) && isSet(mLeftToRight)) errors.emplace_back("LeftToLeft and LeftToRight both defined");
        if (isSet(mRightToLeft) && isSet(mRightToRight)) errors.emplace_back("RightToLeft and RightToRight both defined");
        if (isSet(mStartToStart) && isSet(mStartToEnd)) errors.emplace_back("StartToStart and StartToEnd both defined");
        if (isSet(mEndToStart) && isSet(mEndToEnd)) errors.emplace_back("EndToStart and EndToEnd both defined");
        if ((isSet(mLeftToLeft) || isSet(mLeftToRight) || isSet(mRightToLeft) || isSet(mRightToRight)) &&
            (isSet(mStartToStart) || isSet(mStartToEnd) || isSet(mEndToStart) || isSet(mEndToEnd))) {
            errors.emplace_back("Both left/right and start/end constraints defined");
        }
        if (!errors.empty()) {
            std::string message = "IncorrectConstraintException: [";
            for (std::size_t i = 0; i < errors.size(); i++) message += (i ? ", " : "") + errors[i];
            throw std::logic_error(message + "]");
        }
    }

    RefTarget ConstraintReference::get(const RefTarget& reference) const {
        if (const auto* key = std::get_if<std::string>(&reference)) {
            Reference* found = mState.reference(*key);
            return found != nullptr ? RefTarget(found) : RefTarget();
        }
        return reference;
    }

    ConstraintReference& ConstraintReference::clearVertical() {
        top().clear();
        baseline().clear();
        bottom().clear();
        return *this;
    }

    ConstraintReference& ConstraintReference::clearHorizontal() {
        start().clear();
        end().clear();
        left().clear();
        right().clear();
        return *this;
    }

    ConstraintReference& ConstraintReference::left() {
        mLast = isSet(mLeftToLeft) ? Constraint::LEFT_TO_LEFT : Constraint::LEFT_TO_RIGHT;
        return *this;
    }
    ConstraintReference& ConstraintReference::right() {
        mLast = isSet(mRightToLeft) ? Constraint::RIGHT_TO_LEFT : Constraint::RIGHT_TO_RIGHT;
        return *this;
    }
    ConstraintReference& ConstraintReference::start() {
        mLast = isSet(mStartToStart) ? Constraint::START_TO_START : Constraint::START_TO_END;
        return *this;
    }
    ConstraintReference& ConstraintReference::end() {
        mLast = isSet(mEndToStart) ? Constraint::END_TO_START : Constraint::END_TO_END;
        return *this;
    }
    ConstraintReference& ConstraintReference::top() {
        mLast = isSet(mTopToTop) ? Constraint::TOP_TO_TOP : Constraint::TOP_TO_BOTTOM;
        return *this;
    }
    ConstraintReference& ConstraintReference::bottom() {
        mLast = isSet(mBottomToTop) ? Constraint::BOTTOM_TO_TOP : Constraint::BOTTOM_TO_BOTTOM;
        return *this;
    }
    ConstraintReference& ConstraintReference::baseline() {
        mLast = Constraint::BASELINE_TO_BASELINE;
        return *this;
    }

    void ConstraintReference::dereference() {
        mLeftToLeft = get(mLeftToLeft);
        mLeftToRight = get(mLeftToRight);
        mRightToLeft = get(mRightToLeft);
        mRightToRight = get(mRightToRight);
        mStartToStart = get(mStartToStart);
        mStartToEnd = get(mStartToEnd);
        mEndToStart = get(mEndToStart);
        mEndToEnd = get(mEndToEnd);
        mTopToTop = get(mTopToTop);
        mTopToBottom = get(mTopToBottom);
        mBottomToTop = get(mBottomToTop);
        mBottomToBottom = get(mBottomToBottom);
        mBaselineToBaseline = get(mBaselineToBaseline);
        mBaselineToTop = get(mBaselineToTop);
        mBaselineToBottom = get(mBaselineToBottom);
    }

#define KILN_CL_SETTER(name, field, constraint)                                \
    ConstraintReference& ConstraintReference::name(RefTarget reference) {      \
        mLast = Constraint::constraint;                                         \
        field = std::move(reference);                                           \
        return *this;                                                           \
    }
    KILN_CL_SETTER(leftToLeft, mLeftToLeft, LEFT_TO_LEFT)
    KILN_CL_SETTER(leftToRight, mLeftToRight, LEFT_TO_RIGHT)
    KILN_CL_SETTER(rightToLeft, mRightToLeft, RIGHT_TO_LEFT)
    KILN_CL_SETTER(rightToRight, mRightToRight, RIGHT_TO_RIGHT)
    KILN_CL_SETTER(startToStart, mStartToStart, START_TO_START)
    KILN_CL_SETTER(startToEnd, mStartToEnd, START_TO_END)
    KILN_CL_SETTER(endToStart, mEndToStart, END_TO_START)
    KILN_CL_SETTER(endToEnd, mEndToEnd, END_TO_END)
    KILN_CL_SETTER(topToTop, mTopToTop, TOP_TO_TOP)
    KILN_CL_SETTER(topToBottom, mTopToBottom, TOP_TO_BOTTOM)
    KILN_CL_SETTER(topToBaseline, mTopToBaseline, TOP_TO_BASELINE)
    KILN_CL_SETTER(bottomToTop, mBottomToTop, BOTTOM_TO_TOP)
    KILN_CL_SETTER(bottomToBottom, mBottomToBottom, BOTTOM_TO_BOTTOM)
    KILN_CL_SETTER(bottomToBaseline, mBottomToBaseline, BOTTOM_TO_BASELINE)
    KILN_CL_SETTER(baselineToBaseline, mBaselineToBaseline, BASELINE_TO_BASELINE)
    KILN_CL_SETTER(baselineToTop, mBaselineToTop, BASELINE_TO_TOP)
    KILN_CL_SETTER(baselineToBottom, mBaselineToBottom, BASELINE_TO_BOTTOM)
#undef KILN_CL_SETTER

    ConstraintReference& ConstraintReference::centerHorizontally(RefTarget reference) {
        RefTarget ref = get(reference);
        mStartToStart = ref;
        mEndToEnd = ref;
        mLast = Constraint::CENTER_HORIZONTALLY;
        mHorizontalBias = 0.5f;
        return *this;
    }

    ConstraintReference& ConstraintReference::centerVertically(RefTarget reference) {
        RefTarget ref = get(reference);
        mTopToTop = ref;
        mBottomToBottom = ref;
        mLast = Constraint::CENTER_VERTICALLY;
        mVerticalBias = 0.5f;
        return *this;
    }

    ConstraintReference& ConstraintReference::circularConstraint(RefTarget reference, float angle, float distance) {
        mCircularConstraint = get(reference);
        mCircularAngle = angle;
        mCircularDistance = distance;
        mLast = Constraint::CIRCULAR_CONSTRAINT;
        return *this;
    }

    ConstraintReference& ConstraintReference::margin(float marginValue) { return margin(mState.convertDimension(marginValue)); }
    ConstraintReference& ConstraintReference::marginGone(float marginGoneValue) { return marginGone(mState.convertDimension(marginGoneValue)); }

    ConstraintReference& ConstraintReference::margin(int value) {
        if (mLast) {
            switch (*mLast) {
                case Constraint::LEFT_TO_LEFT:
                case Constraint::LEFT_TO_RIGHT:
                    mMarginLeft = value;
                    break;
                case Constraint::RIGHT_TO_LEFT:
                case Constraint::RIGHT_TO_RIGHT:
                    mMarginRight = value;
                    break;
                case Constraint::START_TO_START:
                case Constraint::START_TO_END:
                    mMarginStart = value;
                    break;
                case Constraint::END_TO_START:
                case Constraint::END_TO_END:
                    mMarginEnd = value;
                    break;
                case Constraint::TOP_TO_TOP:
                case Constraint::TOP_TO_BOTTOM:
                case Constraint::TOP_TO_BASELINE:
                    mMarginTop = value;
                    break;
                case Constraint::BOTTOM_TO_TOP:
                case Constraint::BOTTOM_TO_BOTTOM:
                case Constraint::BOTTOM_TO_BASELINE:
                    mMarginBottom = value;
                    break;
                case Constraint::BASELINE_TO_BOTTOM:
                case Constraint::BASELINE_TO_TOP:
                case Constraint::BASELINE_TO_BASELINE:
                    mMarginBaseline = value;
                    break;
                case Constraint::CIRCULAR_CONSTRAINT:
                    mCircularDistance = float(value);
                    break;
                default:
                    break;
            }
        } else {
            mMarginLeft = value;
            mMarginRight = value;
            mMarginStart = value;
            mMarginEnd = value;
            mMarginTop = value;
            mMarginBottom = value;
        }
        return *this;
    }

    ConstraintReference& ConstraintReference::marginGone(int value) {
        if (mLast) {
            switch (*mLast) {
                case Constraint::LEFT_TO_LEFT:
                case Constraint::LEFT_TO_RIGHT:
                    mMarginLeftGone = value;
                    break;
                case Constraint::RIGHT_TO_LEFT:
                case Constraint::RIGHT_TO_RIGHT:
                    mMarginRightGone = value;
                    break;
                case Constraint::START_TO_START:
                case Constraint::START_TO_END:
                    mMarginStartGone = value;
                    break;
                case Constraint::END_TO_START:
                case Constraint::END_TO_END:
                    mMarginEndGone = value;
                    break;
                case Constraint::TOP_TO_TOP:
                case Constraint::TOP_TO_BOTTOM:
                case Constraint::TOP_TO_BASELINE:
                    mMarginTopGone = value;
                    break;
                case Constraint::BOTTOM_TO_TOP:
                case Constraint::BOTTOM_TO_BOTTOM:
                case Constraint::BOTTOM_TO_BASELINE:
                    mMarginBottomGone = value;
                    break;
                case Constraint::BASELINE_TO_TOP:
                case Constraint::BASELINE_TO_BOTTOM:
                case Constraint::BASELINE_TO_BASELINE:
                    mMarginBaselineGone = value;
                    break;
                default:
                    break;
            }
        } else {
            mMarginLeftGone = value;
            mMarginRightGone = value;
            mMarginStartGone = value;
            mMarginEndGone = value;
            mMarginTopGone = value;
            mMarginBottomGone = value;
        }
        return *this;
    }

    ConstraintReference& ConstraintReference::bias(float value) {
        if (!mLast) return *this;
        switch (*mLast) {
            case Constraint::CENTER_HORIZONTALLY:
            case Constraint::LEFT_TO_LEFT:
            case Constraint::LEFT_TO_RIGHT:
            case Constraint::RIGHT_TO_LEFT:
            case Constraint::RIGHT_TO_RIGHT:
            case Constraint::START_TO_START:
            case Constraint::START_TO_END:
            case Constraint::END_TO_START:
            case Constraint::END_TO_END:
                mHorizontalBias = value;
                break;
            case Constraint::CENTER_VERTICALLY:
            case Constraint::TOP_TO_TOP:
            case Constraint::TOP_TO_BOTTOM:
            case Constraint::TOP_TO_BASELINE:
            case Constraint::BOTTOM_TO_TOP:
            case Constraint::BOTTOM_TO_BOTTOM:
            case Constraint::BOTTOM_TO_BASELINE:
                mVerticalBias = value;
                break;
            default:
                break;
        }
        return *this;
    }

    ConstraintReference& ConstraintReference::clearAll() {
        mLeftToLeft = {};
        mLeftToRight = {};
        mMarginLeft = 0;
        mRightToLeft = {};
        mRightToRight = {};
        mMarginRight = 0;
        mStartToStart = {};
        mStartToEnd = {};
        mMarginStart = 0;
        mEndToStart = {};
        mEndToEnd = {};
        mMarginEnd = 0;
        mTopToTop = {};
        mTopToBottom = {};
        mMarginTop = 0;
        mBottomToTop = {};
        mBottomToBottom = {};
        mMarginBottom = 0;
        mBaselineToBaseline = {};
        mCircularConstraint = {};
        mHorizontalBias = 0.5f;
        mVerticalBias = 0.5f;
        mMarginLeftGone = 0;
        mMarginRightGone = 0;
        mMarginStartGone = 0;
        mMarginEndGone = 0;
        mMarginTopGone = 0;
        mMarginBottomGone = 0;
        return *this;
    }

    ConstraintReference& ConstraintReference::clear() {
        if (mLast) {
            switch (*mLast) {
                case Constraint::LEFT_TO_LEFT:
                case Constraint::LEFT_TO_RIGHT:
                    mLeftToLeft = {};
                    mLeftToRight = {};
                    mMarginLeft = 0;
                    mMarginLeftGone = 0;
                    break;
                case Constraint::RIGHT_TO_LEFT:
                case Constraint::RIGHT_TO_RIGHT:
                    mRightToLeft = {};
                    mRightToRight = {};
                    mMarginRight = 0;
                    mMarginRightGone = 0;
                    break;
                case Constraint::START_TO_START:
                case Constraint::START_TO_END:
                    mStartToStart = {};
                    mStartToEnd = {};
                    mMarginStart = 0;
                    mMarginStartGone = 0;
                    break;
                case Constraint::END_TO_START:
                case Constraint::END_TO_END:
                    mEndToStart = {};
                    mEndToEnd = {};
                    mMarginEnd = 0;
                    mMarginEndGone = 0;
                    break;
                case Constraint::TOP_TO_TOP:
                case Constraint::TOP_TO_BOTTOM:
                case Constraint::TOP_TO_BASELINE:
                    mTopToTop = {};
                    mTopToBottom = {};
                    mTopToBaseline = {};
                    mMarginTop = 0;
                    mMarginTopGone = 0;
                    break;
                case Constraint::BOTTOM_TO_TOP:
                case Constraint::BOTTOM_TO_BOTTOM:
                case Constraint::BOTTOM_TO_BASELINE:
                    mBottomToTop = {};
                    mBottomToBottom = {};
                    mBottomToBaseline = {};
                    mMarginBottom = 0;
                    mMarginBottomGone = 0;
                    break;
                case Constraint::BASELINE_TO_BASELINE:
                    mBaselineToBaseline = {};
                    break;
                case Constraint::CIRCULAR_CONSTRAINT:
                    mCircularConstraint = {};
                    break;
                default:
                    break;
            }
        } else {
            clearAll();
        }
        return *this;
    }

    ConstraintWidget* ConstraintReference::getTarget(const RefTarget& target) {
        if (auto* const* reference = std::get_if<Reference*>(&target)) return (*reference)->getConstraintWidget();
        return nullptr;
    }

    void ConstraintReference::applyConnection(ConstraintWidget* widget, const RefTarget& opaqueTarget, Constraint type) {
        ConstraintWidget* target = getTarget(opaqueTarget);
        if (target == nullptr) return;
        switch (type) {
            case Constraint::START_TO_START:
                widget->getAnchor(T::LEFT)->connect(target->getAnchor(T::LEFT), mMarginStart, mMarginStartGone, false);
                break;
            case Constraint::START_TO_END:
                widget->getAnchor(T::LEFT)->connect(target->getAnchor(T::RIGHT), mMarginStart, mMarginStartGone, false);
                break;
            case Constraint::END_TO_START:
                widget->getAnchor(T::RIGHT)->connect(target->getAnchor(T::LEFT), mMarginEnd, mMarginEndGone, false);
                break;
            case Constraint::END_TO_END:
                widget->getAnchor(T::RIGHT)->connect(target->getAnchor(T::RIGHT), mMarginEnd, mMarginEndGone, false);
                break;
            case Constraint::LEFT_TO_LEFT:
                widget->getAnchor(T::LEFT)->connect(target->getAnchor(T::LEFT), mMarginLeft, mMarginLeftGone, false);
                break;
            case Constraint::LEFT_TO_RIGHT:
                widget->getAnchor(T::LEFT)->connect(target->getAnchor(T::RIGHT), mMarginLeft, mMarginLeftGone, false);
                break;
            case Constraint::RIGHT_TO_LEFT:
                widget->getAnchor(T::RIGHT)->connect(target->getAnchor(T::LEFT), mMarginRight, mMarginRightGone, false);
                break;
            case Constraint::RIGHT_TO_RIGHT:
                widget->getAnchor(T::RIGHT)->connect(target->getAnchor(T::RIGHT), mMarginRight, mMarginRightGone, false);
                break;
            case Constraint::TOP_TO_TOP:
                widget->getAnchor(T::TOP)->connect(target->getAnchor(T::TOP), mMarginTop, mMarginTopGone, false);
                break;
            case Constraint::TOP_TO_BOTTOM:
                widget->getAnchor(T::TOP)->connect(target->getAnchor(T::BOTTOM), mMarginTop, mMarginTopGone, false);
                break;
            case Constraint::TOP_TO_BASELINE:
                widget->immediateConnect(T::TOP, target, T::BASELINE, mMarginTop, mMarginTopGone);
                break;
            case Constraint::BOTTOM_TO_TOP:
                widget->getAnchor(T::BOTTOM)->connect(target->getAnchor(T::TOP), mMarginBottom, mMarginBottomGone, false);
                break;
            case Constraint::BOTTOM_TO_BOTTOM:
                widget->getAnchor(T::BOTTOM)->connect(target->getAnchor(T::BOTTOM), mMarginBottom, mMarginBottomGone, false);
                break;
            case Constraint::BOTTOM_TO_BASELINE:
                widget->immediateConnect(T::BOTTOM, target, T::BASELINE, mMarginBottom, mMarginBottomGone);
                break;
            case Constraint::BASELINE_TO_BASELINE:
                widget->immediateConnect(T::BASELINE, target, T::BASELINE, mMarginBaseline, mMarginBaselineGone);
                break;
            case Constraint::BASELINE_TO_TOP:
                widget->immediateConnect(T::BASELINE, target, T::TOP, mMarginBaseline, mMarginBaselineGone);
                break;
            case Constraint::BASELINE_TO_BOTTOM:
                widget->immediateConnect(T::BASELINE, target, T::BOTTOM, mMarginBaseline, mMarginBaselineGone);
                break;
            case Constraint::CIRCULAR_CONSTRAINT:
                widget->connectCircularConstraint(target, mCircularAngle, JavaInt(mCircularDistance));
                break;
            default:
                break;
        }
    }

    void ConstraintReference::applyWidgetConstraints() {
        applyConnection(mConstraintWidget, mLeftToLeft, Constraint::LEFT_TO_LEFT);
        applyConnection(mConstraintWidget, mLeftToRight, Constraint::LEFT_TO_RIGHT);
        applyConnection(mConstraintWidget, mRightToLeft, Constraint::RIGHT_TO_LEFT);
        applyConnection(mConstraintWidget, mRightToRight, Constraint::RIGHT_TO_RIGHT);
        applyConnection(mConstraintWidget, mStartToStart, Constraint::START_TO_START);
        applyConnection(mConstraintWidget, mStartToEnd, Constraint::START_TO_END);
        applyConnection(mConstraintWidget, mEndToStart, Constraint::END_TO_START);
        applyConnection(mConstraintWidget, mEndToEnd, Constraint::END_TO_END);
        applyConnection(mConstraintWidget, mTopToTop, Constraint::TOP_TO_TOP);
        applyConnection(mConstraintWidget, mTopToBottom, Constraint::TOP_TO_BOTTOM);
        applyConnection(mConstraintWidget, mTopToBaseline, Constraint::TOP_TO_BASELINE);
        applyConnection(mConstraintWidget, mBottomToTop, Constraint::BOTTOM_TO_TOP);
        applyConnection(mConstraintWidget, mBottomToBottom, Constraint::BOTTOM_TO_BOTTOM);
        applyConnection(mConstraintWidget, mBottomToBaseline, Constraint::BOTTOM_TO_BASELINE);
        applyConnection(mConstraintWidget, mBaselineToBaseline, Constraint::BASELINE_TO_BASELINE);
        applyConnection(mConstraintWidget, mBaselineToTop, Constraint::BASELINE_TO_TOP);
        applyConnection(mConstraintWidget, mBaselineToBottom, Constraint::BASELINE_TO_BOTTOM);
        applyConnection(mConstraintWidget, mCircularConstraint, Constraint::CIRCULAR_CONSTRAINT);
    }

    void ConstraintReference::apply() {
        if (mConstraintWidget == nullptr) return;
        if (mFacade != nullptr) mFacade->apply();
        mHorizontalDimension.apply(mState, mConstraintWidget, ConstraintWidget::HORIZONTAL);
        mVerticalDimension.apply(mState, mConstraintWidget, ConstraintWidget::VERTICAL);
        dereference();
        applyWidgetConstraints();
        if (mHorizontalChainStyle != ConstraintWidget::CHAIN_SPREAD) mConstraintWidget->setHorizontalChainStyle(mHorizontalChainStyle);
        if (mVerticalChainStyle != ConstraintWidget::CHAIN_SPREAD) mConstraintWidget->setVerticalChainStyle(mVerticalChainStyle);
        if (mHorizontalChainWeight != float(ConstraintWidget::UNKNOWN)) mConstraintWidget->setHorizontalWeight(mHorizontalChainWeight);
        if (mVerticalChainWeight != float(ConstraintWidget::UNKNOWN)) mConstraintWidget->setVerticalWeight(mVerticalChainWeight);
        mConstraintWidget->setHorizontalBiasPercent(mHorizontalBias);
        mConstraintWidget->setVerticalBiasPercent(mVerticalBias);
        WidgetFrame& frame = mConstraintWidget->frame;
        frame.pivotX = mPivotX;
        frame.pivotY = mPivotY;
        frame.rotationX = mRotationX;
        frame.rotationY = mRotationY;
        frame.rotationZ = mRotationZ;
        frame.translationX = mTranslationX;
        frame.translationY = mTranslationY;
        frame.translationZ = mTranslationZ;
        frame.scaleX = mScaleX;
        frame.scaleY = mScaleY;
        frame.alpha = mAlpha;
        frame.visibility = mVisibility;
        mConstraintWidget->setVisibility(mVisibility);
    }

    // HelperReference

    HelperReference::HelperReference(State& state, int type) : ConstraintReference(state), mHelperState(state), mType(type) {}

    ConstraintWidget* HelperReference::getConstraintWidget() { return getHelperWidget(); }

}
