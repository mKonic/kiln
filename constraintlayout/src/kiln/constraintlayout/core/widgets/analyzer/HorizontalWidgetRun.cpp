#include "kiln/constraintlayout/core/widgets/analyzer/WidgetRuns.h"

#include <algorithm>

namespace kiln::constraintlayout::core {

    using DB = ConstraintWidget::DimensionBehaviour;
    using CW = ConstraintWidget;

    namespace {
        int sTempDimensions[2] = {};
    }

    HorizontalWidgetRun::HorizontalWidgetRun(ConstraintWidget* widget) : WidgetRun(widget, Kind::HORIZONTAL) {
        start.mType = DependencyNode::Type::LEFT;
        end.mType = DependencyNode::Type::RIGHT;
        orientation = CW::HORIZONTAL;
    }

    void HorizontalWidgetRun::clear() {
        mRunGroup = nullptr;
        start.clear();
        end.clear();
        mDimension.clear();
        mResolved = false;
    }

    void HorizontalWidgetRun::reset() {
        mResolved = false;
        start.clear();
        start.resolved = false;
        end.clear();
        end.resolved = false;
        mDimension.resolved = false;
    }

    bool HorizontalWidgetRun::supportsWrapComputation() const {
        if (mDimensionBehavior == DB::MATCH_CONSTRAINT) return mWidget->mMatchConstraintDefaultWidth == CW::MATCH_CONSTRAINT_SPREAD;
        return true;
    }

    void HorizontalWidgetRun::apply() {
        CW* w = mWidget;
        if (w->measured) mDimension.resolve(w->getWidth());
        if (!mDimension.resolved) {
            mDimensionBehavior = w->getHorizontalDimensionBehaviour();
            if (mDimensionBehavior != DB::MATCH_CONSTRAINT) {
                if (mDimensionBehavior == DB::MATCH_PARENT) {
                    CW* parent = w->getParent();
                    if (parent != nullptr &&
                        (parent->getHorizontalDimensionBehaviour() == DB::FIXED || parent->getHorizontalDimensionBehaviour() == DB::MATCH_PARENT)) {
                        const int resolvedDimension = parent->getWidth() - w->mLeft.getMargin() - w->mRight.getMargin();
                        addTarget(&start, &parent->mHorizontalRun->start, w->mLeft.getMargin());
                        addTarget(&end, &parent->mHorizontalRun->end, -w->mRight.getMargin());
                        mDimension.resolve(resolvedDimension);
                        return;
                    }
                }
                if (mDimensionBehavior == DB::FIXED) mDimension.resolve(w->getWidth());
            }
        } else {
            if (mDimensionBehavior == DB::MATCH_PARENT) {
                CW* parent = w->getParent();
                if (parent != nullptr &&
                    (parent->getHorizontalDimensionBehaviour() == DB::FIXED || parent->getHorizontalDimensionBehaviour() == DB::MATCH_PARENT)) {
                    addTarget(&start, &parent->mHorizontalRun->start, w->mLeft.getMargin());
                    addTarget(&end, &parent->mHorizontalRun->end, -w->mRight.getMargin());
                    return;
                }
            }
        }

        ConstraintAnchor* left = w->mListAnchors[CW::ANCHOR_LEFT];
        ConstraintAnchor* right = w->mListAnchors[CW::ANCHOR_RIGHT];
        if (mDimension.resolved && w->measured) {
            if (left->mTarget != nullptr && right->mTarget != nullptr) {
                if (w->isInHorizontalChain()) {
                    start.mMargin = left->getMargin();
                    end.mMargin = -right->getMargin();
                } else {
                    if (DependencyNode* startTarget = getTarget(left)) addTarget(&start, startTarget, left->getMargin());
                    if (DependencyNode* endTarget = getTarget(right)) addTarget(&end, endTarget, -right->getMargin());
                    start.delegateToWidgetRun = true;
                    end.delegateToWidgetRun = true;
                }
            } else if (left->mTarget != nullptr) {
                if (DependencyNode* target = getTarget(left)) {
                    addTarget(&start, target, left->getMargin());
                    addTarget(&end, &start, mDimension.value);
                }
            } else if (right->mTarget != nullptr) {
                if (DependencyNode* target = getTarget(right)) {
                    addTarget(&end, target, -right->getMargin());
                    addTarget(&start, &end, -mDimension.value);
                }
            } else {
                if (!w->is(CW::KIND_HELPER_WIDGET) && w->getParent() != nullptr && w->getAnchor(ConstraintAnchor::Type::CENTER)->mTarget == nullptr) {
                    DependencyNode* l = &w->getParent()->mHorizontalRun->start;
                    addTarget(&start, l, w->getX());
                    addTarget(&end, &start, mDimension.value);
                }
            }
        } else {
            if (mDimensionBehavior == DB::MATCH_CONSTRAINT) {
                switch (w->mMatchConstraintDefaultWidth) {
                    case CW::MATCH_CONSTRAINT_RATIO: {
                        if (w->mMatchConstraintDefaultHeight == CW::MATCH_CONSTRAINT_RATIO) {
                            start.updateDelegate = this;
                            end.updateDelegate = this;
                            w->mVerticalRun->start.updateDelegate = this;
                            w->mVerticalRun->end.updateDelegate = this;
                            mDimension.updateDelegate = this;
                            if (w->isInVerticalChain()) {
                                mDimension.mTargets.push_back(&w->mVerticalRun->mDimension);
                                w->mVerticalRun->mDimension.mDependencies.push_back(&mDimension);
                                w->mVerticalRun->mDimension.updateDelegate = this;
                                mDimension.mTargets.push_back(&w->mVerticalRun->start);
                                mDimension.mTargets.push_back(&w->mVerticalRun->end);
                                w->mVerticalRun->start.mDependencies.push_back(&mDimension);
                                w->mVerticalRun->end.mDependencies.push_back(&mDimension);
                            } else if (w->isInHorizontalChain()) {
                                w->mVerticalRun->mDimension.mTargets.push_back(&mDimension);
                                mDimension.mDependencies.push_back(&w->mVerticalRun->mDimension);
                            } else {
                                w->mVerticalRun->mDimension.mTargets.push_back(&mDimension);
                            }
                            break;
                        }
                        DependencyNode* targetDimension = &w->mVerticalRun->mDimension;
                        mDimension.mTargets.push_back(targetDimension);
                        targetDimension->mDependencies.push_back(&mDimension);
                        w->mVerticalRun->start.mDependencies.push_back(&mDimension);
                        w->mVerticalRun->end.mDependencies.push_back(&mDimension);
                        mDimension.delegateToWidgetRun = true;
                        mDimension.mDependencies.push_back(&start);
                        mDimension.mDependencies.push_back(&end);
                        start.mTargets.push_back(&mDimension);
                        end.mTargets.push_back(&mDimension);
                        break;
                    }
                    case CW::MATCH_CONSTRAINT_PERCENT: {
                        CW* parent = w->getParent();
                        if (parent == nullptr) break;
                        DependencyNode* targetDimension = &parent->mVerticalRun->mDimension;
                        mDimension.mTargets.push_back(targetDimension);
                        targetDimension->mDependencies.push_back(&mDimension);
                        mDimension.delegateToWidgetRun = true;
                        mDimension.mDependencies.push_back(&start);
                        mDimension.mDependencies.push_back(&end);
                        break;
                    }
                    default: break;
                }
            }
            if (left->mTarget != nullptr && right->mTarget != nullptr) {
                if (w->isInHorizontalChain()) {
                    start.mMargin = left->getMargin();
                    end.mMargin = -right->getMargin();
                } else {
                    DependencyNode* startTarget = getTarget(left);
                    DependencyNode* endTarget = getTarget(right);
                    if (startTarget != nullptr) startTarget->addDependency(this);
                    if (endTarget != nullptr) endTarget->addDependency(this);
                    mRunType = RunType::CENTER;
                }
            } else if (left->mTarget != nullptr) {
                if (DependencyNode* target = getTarget(left)) {
                    addTarget(&start, target, left->getMargin());
                    addTarget(&end, &start, 1, &mDimension);
                }
            } else if (right->mTarget != nullptr) {
                if (DependencyNode* target = getTarget(right)) {
                    addTarget(&end, target, -right->getMargin());
                    addTarget(&start, &end, -1, &mDimension);
                }
            } else {
                if (!w->is(CW::KIND_HELPER_WIDGET) && w->getParent() != nullptr) {
                    DependencyNode* l = &w->getParent()->mHorizontalRun->start;
                    addTarget(&start, l, w->getX());
                    addTarget(&end, &start, 1, &mDimension);
                }
            }
        }
    }

    void HorizontalWidgetRun::computeInsetRatio(int* dimensions, int x1, int x2, int y1, int y2, float ratio, int side) {
        const int dx = x2 - x1;
        const int dy = y2 - y1;
        switch (side) {
            case CW::UNKNOWN: {
                const int candidateX1 = JavaInt(0.5f + float(dy) * ratio);
                const int candidateY1 = dy;
                const int candidateX2 = dx;
                const int candidateY2 = JavaInt(0.5f + float(dx) / ratio);
                if (candidateX1 <= dx && candidateY1 <= dy) {
                    dimensions[CW::HORIZONTAL] = candidateX1;
                    dimensions[CW::VERTICAL] = candidateY1;
                } else if (candidateX2 <= dx && candidateY2 <= dy) {
                    dimensions[CW::HORIZONTAL] = candidateX2;
                    dimensions[CW::VERTICAL] = candidateY2;
                }
                break;
            }
            case CW::HORIZONTAL: {
                dimensions[CW::HORIZONTAL] = JavaInt(0.5f + float(dy) * ratio);
                dimensions[CW::VERTICAL] = dy;
                break;
            }
            case CW::VERTICAL: {
                dimensions[CW::HORIZONTAL] = dx;
                dimensions[CW::VERTICAL] = JavaInt(0.5f + float(dx) * ratio);
                break;
            }
            default: break;
        }
    }

    void HorizontalWidgetRun::update(Dependency* dependency) {
        CW* w = mWidget;
        switch (mRunType) {
            case RunType::START: updateRunStart(dependency); break;
            case RunType::END: updateRunEnd(dependency); break;
            case RunType::CENTER: updateRunCenter(dependency, &w->mLeft, &w->mRight, CW::HORIZONTAL); return;
            default: break;
        }

        if (!mDimension.resolved) {
            if (mDimensionBehavior == DB::MATCH_CONSTRAINT) {
                switch (w->mMatchConstraintDefaultWidth) {
                    case CW::MATCH_CONSTRAINT_RATIO: {
                        if (w->mMatchConstraintDefaultHeight == CW::MATCH_CONSTRAINT_SPREAD || w->mMatchConstraintDefaultHeight == CW::MATCH_CONSTRAINT_RATIO) {
                            DependencyNode& secondStart = w->mVerticalRun->start;
                            DependencyNode& secondEnd = w->mVerticalRun->end;
                            const bool s1 = w->mLeft.mTarget != nullptr;
                            const bool s2 = w->mTop.mTarget != nullptr;
                            const bool e1 = w->mRight.mTarget != nullptr;
                            const bool e2 = w->mBottom.mTarget != nullptr;
                            const int definedSide = w->getDimensionRatioSide();
                            if (s1 && s2 && e1 && e2) {
                                const float ratio = w->getDimensionRatio();
                                if (secondStart.resolved && secondEnd.resolved) {
                                    if (!(start.readyToSolve && end.readyToSolve)) return;
                                    const int x1 = start.mTargets[0]->value + start.mMargin;
                                    const int x2 = end.mTargets[0]->value - end.mMargin;
                                    const int y1 = secondStart.value + secondStart.mMargin;
                                    const int y2 = secondEnd.value - secondEnd.mMargin;
                                    computeInsetRatio(sTempDimensions, x1, x2, y1, y2, ratio, definedSide);
                                    mDimension.resolve(sTempDimensions[CW::HORIZONTAL]);
                                    w->mVerticalRun->mDimension.resolve(sTempDimensions[CW::VERTICAL]);
                                    return;
                                }
                                if (start.resolved && end.resolved) {
                                    if (!(secondStart.readyToSolve && secondEnd.readyToSolve)) return;
                                    const int x1 = start.value + start.mMargin;
                                    const int x2 = end.value - end.mMargin;
                                    const int y1 = secondStart.mTargets[0]->value + secondStart.mMargin;
                                    const int y2 = secondEnd.mTargets[0]->value - secondEnd.mMargin;
                                    computeInsetRatio(sTempDimensions, x1, x2, y1, y2, ratio, definedSide);
                                    mDimension.resolve(sTempDimensions[CW::HORIZONTAL]);
                                    w->mVerticalRun->mDimension.resolve(sTempDimensions[CW::VERTICAL]);
                                }
                                if (!(start.readyToSolve && end.readyToSolve && secondStart.readyToSolve && secondEnd.readyToSolve)) return;
                                const int x1 = start.mTargets[0]->value + start.mMargin;
                                const int x2 = end.mTargets[0]->value - end.mMargin;
                                const int y1 = secondStart.mTargets[0]->value + secondStart.mMargin;
                                const int y2 = secondEnd.mTargets[0]->value - secondEnd.mMargin;
                                computeInsetRatio(sTempDimensions, x1, x2, y1, y2, ratio, definedSide);
                                mDimension.resolve(sTempDimensions[CW::HORIZONTAL]);
                                w->mVerticalRun->mDimension.resolve(sTempDimensions[CW::VERTICAL]);
                            } else if (s1 && e1) {
                                if (!(start.readyToSolve && end.readyToSolve)) return;
                                const float ratio = w->getDimensionRatio();
                                const int x1 = start.mTargets[0]->value + start.mMargin;
                                const int x2 = end.mTargets[0]->value - end.mMargin;
                                switch (definedSide) {
                                    case CW::UNKNOWN:
                                    case CW::HORIZONTAL: {
                                        const int dx = x2 - x1;
                                        int ldx = getLimitedDimension(dx, CW::HORIZONTAL);
                                        const int dy = JavaInt(0.5f + float(ldx) * ratio);
                                        const int ldy = getLimitedDimension(dy, CW::VERTICAL);
                                        if (dy != ldy) ldx = JavaInt(0.5f + float(ldy) / ratio);
                                        mDimension.resolve(ldx);
                                        w->mVerticalRun->mDimension.resolve(ldy);
                                        break;
                                    }
                                    case CW::VERTICAL: {
                                        const int dx = x2 - x1;
                                        int ldx = getLimitedDimension(dx, CW::HORIZONTAL);
                                        const int dy = JavaInt(0.5f + float(ldx) / ratio);
                                        const int ldy = getLimitedDimension(dy, CW::VERTICAL);
                                        if (dy != ldy) ldx = JavaInt(0.5f + float(ldy) * ratio);
                                        mDimension.resolve(ldx);
                                        w->mVerticalRun->mDimension.resolve(ldy);
                                        break;
                                    }
                                    default: break;
                                }
                            } else if (s2 && e2) {
                                if (!(secondStart.readyToSolve && secondEnd.readyToSolve)) return;
                                const float ratio = w->getDimensionRatio();
                                const int y1 = secondStart.mTargets[0]->value + secondStart.mMargin;
                                const int y2 = secondEnd.mTargets[0]->value - secondEnd.mMargin;
                                switch (definedSide) {
                                    case CW::UNKNOWN:
                                    case CW::VERTICAL: {
                                        const int dy = y2 - y1;
                                        int ldy = getLimitedDimension(dy, CW::VERTICAL);
                                        const int dx = JavaInt(0.5f + float(ldy) / ratio);
                                        const int ldx = getLimitedDimension(dx, CW::HORIZONTAL);
                                        if (dx != ldx) ldy = JavaInt(0.5f + float(ldx) * ratio);
                                        mDimension.resolve(ldx);
                                        w->mVerticalRun->mDimension.resolve(ldy);
                                        break;
                                    }
                                    case CW::HORIZONTAL: {
                                        const int dy = y2 - y1;
                                        int ldy = getLimitedDimension(dy, CW::VERTICAL);
                                        const int dx = JavaInt(0.5f + float(ldy) * ratio);
                                        const int ldx = getLimitedDimension(dx, CW::HORIZONTAL);
                                        if (dx != ldx) ldy = JavaInt(0.5f + float(ldx) / ratio);
                                        mDimension.resolve(ldx);
                                        w->mVerticalRun->mDimension.resolve(ldy);
                                        break;
                                    }
                                    default: break;
                                }
                            }
                        } else {
                            int size = 0;
                            const int ratioSide = w->getDimensionRatioSide();
                            switch (ratioSide) {
                                case CW::HORIZONTAL: size = JavaInt(0.5f + float(w->mVerticalRun->mDimension.value) / w->getDimensionRatio()); break;
                                case CW::VERTICAL: size = JavaInt(0.5f + float(w->mVerticalRun->mDimension.value) * w->getDimensionRatio()); break;
                                case CW::UNKNOWN: size = JavaInt(0.5f + float(w->mVerticalRun->mDimension.value) * w->getDimensionRatio()); break;
                                default: break;
                            }
                            mDimension.resolve(size);
                        }
                        break;
                    }
                    case CW::MATCH_CONSTRAINT_PERCENT: {
                        CW* parent = w->getParent();
                        if (parent != nullptr && parent->mHorizontalRun->mDimension.resolved) {
                            const float percent = w->mMatchConstraintPercentWidth;
                            const int targetDimensionValue = parent->mHorizontalRun->mDimension.value;
                            mDimension.resolve(JavaInt(0.5f + float(targetDimensionValue) * percent));
                        }
                        break;
                    }
                    default: break;
                }
            }
        }

        if (!(start.readyToSolve && end.readyToSolve)) return;
        if (start.resolved && end.resolved && mDimension.resolved) return;

        if (!mDimension.resolved && mDimensionBehavior == DB::MATCH_CONSTRAINT && w->mMatchConstraintDefaultWidth == CW::MATCH_CONSTRAINT_SPREAD &&
            !w->isInHorizontalChain()) {
            DependencyNode* startTarget = start.mTargets[0];
            DependencyNode* endTarget = end.mTargets[0];
            const int startPos = startTarget->value + start.mMargin;
            const int endPos = endTarget->value + end.mMargin;
            const int distance = endPos - startPos;
            start.resolve(startPos);
            end.resolve(endPos);
            mDimension.resolve(distance);
            return;
        }

        if (!mDimension.resolved && mDimensionBehavior == DB::MATCH_CONSTRAINT && matchConstraintsType == CW::MATCH_CONSTRAINT_WRAP) {
            if (!start.mTargets.empty() && !end.mTargets.empty()) {
                DependencyNode* startTarget = start.mTargets[0];
                DependencyNode* endTarget = end.mTargets[0];
                const int startPos = startTarget->value + start.mMargin;
                const int endPos = endTarget->value + end.mMargin;
                const int availableSpace = endPos - startPos;
                int v = std::min(availableSpace, mDimension.wrapValue);
                const int max = w->mMatchConstraintMaxWidth;
                const int min = w->mMatchConstraintMinWidth;
                v = std::max(min, v);
                if (max > 0) v = std::min(max, v);
                mDimension.resolve(v);
            }
        }

        if (!mDimension.resolved) return;
        DependencyNode* startTarget = start.mTargets[0];
        DependencyNode* endTarget = end.mTargets[0];
        int startPos = startTarget->value + start.mMargin;
        int endPos = endTarget->value + end.mMargin;
        float bias = w->getHorizontalBiasPercent();
        if (startTarget == endTarget) {
            startPos = startTarget->value;
            endPos = endTarget->value;
            bias = 0.5f;
        }
        const int distance = endPos - startPos - mDimension.value;
        start.resolve(JavaInt(0.5f + float(startPos) + float(distance) * bias));
        end.resolve(start.value + mDimension.value);
    }

    void HorizontalWidgetRun::applyToWidget() {
        if (start.resolved) mWidget->setX(start.value);
    }

}
