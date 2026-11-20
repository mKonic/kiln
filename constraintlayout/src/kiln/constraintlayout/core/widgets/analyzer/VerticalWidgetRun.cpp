#include "kiln/constraintlayout/core/widgets/analyzer/WidgetRuns.h"

#include <algorithm>

namespace kiln::constraintlayout::core {

    using DB = ConstraintWidget::DimensionBehaviour;
    using CW = ConstraintWidget;

    VerticalWidgetRun::VerticalWidgetRun(ConstraintWidget* widget) : WidgetRun(widget, Kind::VERTICAL) {
        start.mType = DependencyNode::Type::TOP;
        end.mType = DependencyNode::Type::BOTTOM;
        baseline.mType = DependencyNode::Type::BASELINE;
        orientation = CW::VERTICAL;
    }

    // Upstream adds through a null baseline dimension where it never made one (a crash there);
    // here it is made on first use.
    DimensionDependency* VerticalWidgetRun::baselineDimension() {
        if (!mBaselineDimension) mBaselineDimension = std::make_unique<DimensionDependency>(this);
        return mBaselineDimension.get();
    }

    void VerticalWidgetRun::retireBaselineDimension() {
        if (mBaselineDimension) mRetiredBaselineDimensions.push_back(std::move(mBaselineDimension));
        std::erase_if(mRetiredBaselineDimensions, [&](const std::unique_ptr<DimensionDependency>& d) {
            return d.get() != start.mMarginDependency && d.get() != end.mMarginDependency && d.get() != baseline.mMarginDependency;
        });
    }

    void VerticalWidgetRun::clear() {
        mRunGroup = nullptr;
        start.clear();
        end.clear();
        baseline.clear();
        mDimension.clear();
        mResolved = false;
    }

    void VerticalWidgetRun::reset() {
        mResolved = false;
        start.clear();
        start.resolved = false;
        end.clear();
        end.resolved = false;
        baseline.clear();
        baseline.resolved = false;
        mDimension.resolved = false;
    }

    bool VerticalWidgetRun::supportsWrapComputation() const {
        if (mDimensionBehavior == DB::MATCH_CONSTRAINT) return mWidget->mMatchConstraintDefaultHeight == CW::MATCH_CONSTRAINT_SPREAD;
        return true;
    }

    void VerticalWidgetRun::update(Dependency* dependency) {
        CW* w = mWidget;
        switch (mRunType) {
            case RunType::START: updateRunStart(dependency); break;
            case RunType::END: updateRunEnd(dependency); break;
            case RunType::CENTER: updateRunCenter(dependency, &w->mTop, &w->mBottom, CW::VERTICAL); return;
            default: break;
        }
        // FORCE_USE is true upstream.
        if (mDimension.readyToSolve && !mDimension.resolved) {
            if (mDimensionBehavior == DB::MATCH_CONSTRAINT) {
                switch (w->mMatchConstraintDefaultHeight) {
                    case CW::MATCH_CONSTRAINT_RATIO: {
                        if (w->mHorizontalRun->mDimension.resolved) {
                            int size = 0;
                            switch (w->getDimensionRatioSide()) {
                                case CW::HORIZONTAL: size = JavaInt(0.5f + float(w->mHorizontalRun->mDimension.value) * w->getDimensionRatio()); break;
                                case CW::VERTICAL: size = JavaInt(0.5f + float(w->mHorizontalRun->mDimension.value) / w->getDimensionRatio()); break;
                                case CW::UNKNOWN: size = JavaInt(0.5f + float(w->mHorizontalRun->mDimension.value) / w->getDimensionRatio()); break;
                                default: break;
                            }
                            mDimension.resolve(size);
                        }
                        break;
                    }
                    case CW::MATCH_CONSTRAINT_PERCENT: {
                        CW* parent = w->getParent();
                        if (parent != nullptr && parent->mVerticalRun->mDimension.resolved) {
                            const float percent = w->mMatchConstraintPercentHeight;
                            const int targetDimensionValue = parent->mVerticalRun->mDimension.value;
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
            !w->isInVerticalChain()) {
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
                if (availableSpace < mDimension.wrapValue) mDimension.resolve(availableSpace);
                else mDimension.resolve(mDimension.wrapValue);
            }
        }
        if (!mDimension.resolved) return;
        if (!start.mTargets.empty() && !end.mTargets.empty()) {
            DependencyNode* startTarget = start.mTargets[0];
            DependencyNode* endTarget = end.mTargets[0];
            int startPos = startTarget->value + start.mMargin;
            int endPos = endTarget->value + end.mMargin;
            float bias = w->getVerticalBiasPercent();
            if (startTarget == endTarget) {
                startPos = startTarget->value;
                endPos = endTarget->value;
                bias = 0.5f;
            }
            const int distance = endPos - startPos - mDimension.value;
            start.resolve(JavaInt(0.5f + float(startPos) + float(distance) * bias));
            end.resolve(start.value + mDimension.value);
        }
    }

    void VerticalWidgetRun::apply() {
        CW* w = mWidget;
        if (w->measured) mDimension.resolve(w->getHeight());
        if (!mDimension.resolved) {
            mDimensionBehavior = w->getVerticalDimensionBehaviour();
            if (w->hasBaseline()) {
                retireBaselineDimension();
                mBaselineDimension = std::make_unique<DimensionDependency>(this);
            }
            if (mDimensionBehavior != DB::MATCH_CONSTRAINT) {
                if (mDimensionBehavior == DB::MATCH_PARENT) {
                    CW* parent = w->getParent();
                    if (parent != nullptr && parent->getVerticalDimensionBehaviour() == DB::FIXED) {
                        const int resolvedDimension = parent->getHeight() - w->mTop.getMargin() - w->mBottom.getMargin();
                        addTarget(&start, &parent->mVerticalRun->start, w->mTop.getMargin());
                        addTarget(&end, &parent->mVerticalRun->end, -w->mBottom.getMargin());
                        mDimension.resolve(resolvedDimension);
                        return;
                    }
                }
                if (mDimensionBehavior == DB::FIXED) mDimension.resolve(w->getHeight());
            }
        } else {
            if (mDimensionBehavior == DB::MATCH_PARENT) {
                CW* parent = w->getParent();
                if (parent != nullptr && parent->getVerticalDimensionBehaviour() == DB::FIXED) {
                    addTarget(&start, &parent->mVerticalRun->start, w->mTop.getMargin());
                    addTarget(&end, &parent->mVerticalRun->end, -w->mBottom.getMargin());
                    return;
                }
            }
        }

        ConstraintAnchor* top = w->mListAnchors[CW::ANCHOR_TOP];
        ConstraintAnchor* bottom = w->mListAnchors[CW::ANCHOR_BOTTOM];
        ConstraintAnchor* base = w->mListAnchors[CW::ANCHOR_BASELINE];
        if (mDimension.resolved && w->measured) {
            if (top->mTarget != nullptr && bottom->mTarget != nullptr) {
                if (w->isInVerticalChain()) {
                    start.mMargin = top->getMargin();
                    end.mMargin = -bottom->getMargin();
                } else {
                    if (DependencyNode* startTarget = getTarget(top)) addTarget(&start, startTarget, top->getMargin());
                    if (DependencyNode* endTarget = getTarget(bottom)) addTarget(&end, endTarget, -bottom->getMargin());
                    start.delegateToWidgetRun = true;
                    end.delegateToWidgetRun = true;
                }
                if (w->hasBaseline()) addTarget(&baseline, &start, w->getBaselineDistance());
            } else if (top->mTarget != nullptr) {
                if (DependencyNode* target = getTarget(top)) {
                    addTarget(&start, target, top->getMargin());
                    addTarget(&end, &start, mDimension.value);
                    if (w->hasBaseline()) addTarget(&baseline, &start, w->getBaselineDistance());
                }
            } else if (bottom->mTarget != nullptr) {
                if (DependencyNode* target = getTarget(bottom)) {
                    addTarget(&end, target, -bottom->getMargin());
                    addTarget(&start, &end, -mDimension.value);
                }
                if (w->hasBaseline()) addTarget(&baseline, &start, w->getBaselineDistance());
            } else if (base->mTarget != nullptr) {
                if (DependencyNode* target = getTarget(base)) {
                    addTarget(&baseline, target, 0);
                    addTarget(&start, &baseline, -w->getBaselineDistance());
                    addTarget(&end, &start, mDimension.value);
                }
            } else {
                if (!w->is(CW::KIND_HELPER_WIDGET) && w->getParent() != nullptr && w->getAnchor(ConstraintAnchor::Type::CENTER)->mTarget == nullptr) {
                    DependencyNode* t = &w->getParent()->mVerticalRun->start;
                    addTarget(&start, t, w->getY());
                    addTarget(&end, &start, mDimension.value);
                    if (w->hasBaseline()) addTarget(&baseline, &start, w->getBaselineDistance());
                }
            }
        } else {
            if (!mDimension.resolved && mDimensionBehavior == DB::MATCH_CONSTRAINT) {
                switch (w->mMatchConstraintDefaultHeight) {
                    case CW::MATCH_CONSTRAINT_RATIO: {
                        if (!w->isInVerticalChain()) {
                            if (w->mMatchConstraintDefaultWidth == CW::MATCH_CONSTRAINT_RATIO) break;
                            DependencyNode* targetDimension = &w->mHorizontalRun->mDimension;
                            mDimension.mTargets.push_back(targetDimension);
                            targetDimension->mDependencies.push_back(&mDimension);
                            mDimension.delegateToWidgetRun = true;
                            mDimension.mDependencies.push_back(&start);
                            mDimension.mDependencies.push_back(&end);
                        }
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
            } else {
                mDimension.addDependency(this);
            }
            if (top->mTarget != nullptr && bottom->mTarget != nullptr) {
                if (w->isInVerticalChain()) {
                    start.mMargin = top->getMargin();
                    end.mMargin = -bottom->getMargin();
                } else {
                    DependencyNode* startTarget = getTarget(top);
                    DependencyNode* endTarget = getTarget(bottom);
                    if (startTarget != nullptr) startTarget->addDependency(this);
                    if (endTarget != nullptr) endTarget->addDependency(this);
                    mRunType = RunType::CENTER;
                }
                if (w->hasBaseline()) addTarget(&baseline, &start, 1, baselineDimension());
            } else if (top->mTarget != nullptr) {
                if (DependencyNode* target = getTarget(top)) {
                    addTarget(&start, target, top->getMargin());
                    addTarget(&end, &start, 1, &mDimension);
                    if (w->hasBaseline()) addTarget(&baseline, &start, 1, baselineDimension());
                    if (mDimensionBehavior == DB::MATCH_CONSTRAINT && w->getDimensionRatio() > 0 &&
                        w->mHorizontalRun->mDimensionBehavior == DB::MATCH_CONSTRAINT) {
                        w->mHorizontalRun->mDimension.mDependencies.push_back(&mDimension);
                        mDimension.mTargets.push_back(&w->mHorizontalRun->mDimension);
                        mDimension.updateDelegate = this;
                    }
                }
            } else if (bottom->mTarget != nullptr) {
                if (DependencyNode* target = getTarget(bottom)) {
                    addTarget(&end, target, -bottom->getMargin());
                    addTarget(&start, &end, -1, &mDimension);
                    if (w->hasBaseline()) addTarget(&baseline, &start, 1, baselineDimension());
                }
            } else if (base->mTarget != nullptr) {
                if (DependencyNode* target = getTarget(base)) {
                    addTarget(&baseline, target, 0);
                    addTarget(&start, &baseline, -1, baselineDimension());
                    addTarget(&end, &start, 1, &mDimension);
                }
            } else {
                if (!w->is(CW::KIND_HELPER_WIDGET) && w->getParent() != nullptr) {
                    DependencyNode* t = &w->getParent()->mVerticalRun->start;
                    addTarget(&start, t, w->getY());
                    addTarget(&end, &start, 1, &mDimension);
                    if (w->hasBaseline()) addTarget(&baseline, &start, 1, baselineDimension());
                    if (mDimensionBehavior == DB::MATCH_CONSTRAINT && w->getDimensionRatio() > 0 &&
                        w->mHorizontalRun->mDimensionBehavior == DB::MATCH_CONSTRAINT) {
                        w->mHorizontalRun->mDimension.mDependencies.push_back(&mDimension);
                        mDimension.mTargets.push_back(&w->mHorizontalRun->mDimension);
                        mDimension.updateDelegate = this;
                    }
                }
            }
            if (mDimension.mTargets.empty()) mDimension.readyToSolve = true;
        }
    }

    void VerticalWidgetRun::applyToWidget() {
        if (start.resolved) mWidget->setY(start.value);
    }

}
