// DependencyNode, DimensionDependency, the WidgetRun base and RunGroup (constraintlayout-core
// analyzer).
#include "kiln/constraintlayout/core/widgets/analyzer/WidgetRuns.h"

#include "kiln/constraintlayout/core/widgets/ConstraintWidgetContainer.h"

#include <algorithm>

namespace kiln::constraintlayout::core {

    using DB = ConstraintWidget::DimensionBehaviour;
    using CW = ConstraintWidget;

    // --- DependencyNode -------------------------------------------------------------------------

    void DependencyNode::resolve(int v) {
        if (resolved) return;
        resolved = true;
        value = v;
        // By index: an update may add to other nodes' lists, never to this one's (Java's iterator
        // would throw).
        for (std::size_t i = 0; i < mDependencies.size(); i++) mDependencies[i]->update(mDependencies[i]);
    }

    void DependencyNode::update(Dependency* /*node*/) {
        for (DependencyNode* target : mTargets)
            if (!target->resolved) return;
        readyToSolve = true;
        if (updateDelegate != nullptr) updateDelegate->update(this);
        if (delegateToWidgetRun) {
            mRun->update(this);
            return;
        }
        DependencyNode* target = nullptr;
        int numTargets = 0;
        for (DependencyNode* t : mTargets) {
            if (t->isDimension()) continue;
            target = t;
            numTargets++;
        }
        if (target != nullptr && numTargets == 1 && target->resolved) {
            if (mMarginDependency != nullptr) {
                if (mMarginDependency->resolved) mMargin = mMarginFactor * mMarginDependency->value;
                else return;
            }
            resolve(target->value + mMargin);
        }
        if (updateDelegate != nullptr) updateDelegate->update(this);
    }

    void DependencyNode::addDependency(Dependency* dependency) {
        mDependencies.push_back(dependency);
        if (resolved) dependency->update(dependency);
    }

    void DependencyNode::clear() {
        mTargets.clear();
        mDependencies.clear();
        resolved = false;
        value = 0;
        readyToSolve = false;
        delegateToWidgetRun = false;
    }

    std::string DependencyNode::toString() const {
        return mRun->mWidget->getDebugName() + ":" + std::to_string(int(mType)) + "(" + (resolved ? std::to_string(value) : std::string("unresolved")) +
               ") <t=" + std::to_string(mTargets.size()) + ":d=" + std::to_string(mDependencies.size()) + ">";
    }

    DimensionDependency::DimensionDependency(WidgetRun* run) : DependencyNode(run) {
        mType = run->mKind == WidgetRun::Kind::HORIZONTAL ? Type::HORIZONTAL_DIMENSION : Type::VERTICAL_DIMENSION;
    }

    // --- WidgetRun --------------------------------------------------------------------------------

    bool WidgetRun::isCenterConnection() const {
        int connections = 0;
        for (const DependencyNode* dependency : start.mTargets)
            if (dependency->mRun != this) connections++;
        for (const DependencyNode* dependency : end.mTargets)
            if (dependency->mRun != this) connections++;
        return connections >= 2;
    }

    long long WidgetRun::wrapSize(int direction) const {
        if (mDimension.resolved) {
            long long size = mDimension.value;
            if (isCenterConnection()) {
                size += start.mMargin - end.mMargin;
            } else {
                if (direction == RunGroup::START) size += start.mMargin;
                else size -= end.mMargin;
            }
            return size;
        }
        return 0;
    }

    DependencyNode* WidgetRun::getTarget(ConstraintAnchor* anchor) {
        if (anchor->mTarget == nullptr) return nullptr;
        ConstraintWidget* targetWidget = anchor->mTarget->mOwner;
        switch (anchor->mTarget->mType) {
            case ConstraintAnchor::Type::LEFT: return &targetWidget->mHorizontalRun->start;
            case ConstraintAnchor::Type::RIGHT: return &targetWidget->mHorizontalRun->end;
            case ConstraintAnchor::Type::TOP: return &targetWidget->mVerticalRun->start;
            case ConstraintAnchor::Type::BASELINE: return &targetWidget->mVerticalRun->baseline;
            case ConstraintAnchor::Type::BOTTOM: return &targetWidget->mVerticalRun->end;
            default: return nullptr;
        }
    }

    DependencyNode* WidgetRun::getTarget(ConstraintAnchor* anchor, int orient) {
        if (anchor->mTarget == nullptr) return nullptr;
        ConstraintWidget* targetWidget = anchor->mTarget->mOwner;
        WidgetRun* run = orient == CW::HORIZONTAL ? static_cast<WidgetRun*>(targetWidget->mHorizontalRun.get())
                                                  : static_cast<WidgetRun*>(targetWidget->mVerticalRun.get());
        switch (anchor->mTarget->mType) {
            case ConstraintAnchor::Type::TOP:
            case ConstraintAnchor::Type::LEFT: return &run->start;
            case ConstraintAnchor::Type::BOTTOM:
            case ConstraintAnchor::Type::RIGHT: return &run->end;
            default: return nullptr;
        }
    }

    void WidgetRun::updateRunCenter(Dependency* /*dependency*/, ConstraintAnchor* startAnchor, ConstraintAnchor* endAnchor, int orient) {
        DependencyNode* startTarget = getTarget(startAnchor);
        DependencyNode* endTarget = getTarget(endAnchor);
        if (!(startTarget->resolved && endTarget->resolved)) return;
        int startPos = startTarget->value + startAnchor->getMargin();
        int endPos = endTarget->value - endAnchor->getMargin();
        const int distance = endPos - startPos;
        if (!mDimension.resolved && mDimensionBehavior == DB::MATCH_CONSTRAINT) resolveDimension(orient, distance);
        if (!mDimension.resolved) return;
        if (mDimension.value == distance) {
            start.resolve(startPos);
            end.resolve(endPos);
            return;
        }
        float bias = orient == CW::HORIZONTAL ? mWidget->getHorizontalBiasPercent() : mWidget->getVerticalBiasPercent();
        if (startTarget == endTarget) {
            startPos = startTarget->value;
            endPos = endTarget->value;
            bias = 0.5f;
        }
        const int availableDistance = endPos - startPos - mDimension.value;
        start.resolve(JavaInt(0.5f + float(startPos) + float(availableDistance) * bias));
        end.resolve(start.value + mDimension.value);
    }

    void WidgetRun::resolveDimension(int orient, int distance) {
        switch (matchConstraintsType) {
            case CW::MATCH_CONSTRAINT_SPREAD: mDimension.resolve(getLimitedDimension(distance, orient)); break;
            case CW::MATCH_CONSTRAINT_PERCENT: {
                ConstraintWidget* parent = mWidget->getParent();
                if (parent != nullptr) {
                    WidgetRun* run = orient == CW::HORIZONTAL ? static_cast<WidgetRun*>(parent->mHorizontalRun.get())
                                                              : static_cast<WidgetRun*>(parent->mVerticalRun.get());
                    if (run->mDimension.resolved) {
                        const float percent = orient == CW::HORIZONTAL ? mWidget->mMatchConstraintPercentWidth : mWidget->mMatchConstraintPercentHeight;
                        const int targetDimensionValue = run->mDimension.value;
                        const int size = JavaInt(0.5f + float(targetDimensionValue) * percent);
                        mDimension.resolve(getLimitedDimension(size, orient));
                    }
                }
                break;
            }
            case CW::MATCH_CONSTRAINT_WRAP: {
                const int wrapValue = getLimitedDimension(mDimension.wrapValue, orient);
                mDimension.resolve(std::min(wrapValue, distance));
                break;
            }
            case CW::MATCH_CONSTRAINT_RATIO: {
                if (mWidget->mHorizontalRun->mDimensionBehavior == DB::MATCH_CONSTRAINT &&
                    mWidget->mHorizontalRun->matchConstraintsType == CW::MATCH_CONSTRAINT_RATIO &&
                    mWidget->mVerticalRun->mDimensionBehavior == DB::MATCH_CONSTRAINT &&
                    mWidget->mVerticalRun->matchConstraintsType == CW::MATCH_CONSTRAINT_RATIO) {
                    // Both sides are ratios: nothing to resolve here.
                } else {
                    WidgetRun* run = orient == CW::HORIZONTAL ? static_cast<WidgetRun*>(mWidget->mVerticalRun.get())
                                                              : static_cast<WidgetRun*>(mWidget->mHorizontalRun.get());
                    if (run->mDimension.resolved) {
                        const float ratio = mWidget->getDimensionRatio();
                        int v;
                        if (orient == CW::VERTICAL) v = JavaInt(0.5f + float(run->mDimension.value) / ratio);
                        else v = JavaInt(0.5f + ratio * float(run->mDimension.value));
                        mDimension.resolve(v);
                    }
                }
                break;
            }
            default: break;
        }
    }

    int WidgetRun::getLimitedDimension(int dimension, int orient) const {
        // Upstream's max branch takes min(max, dimension), dropping the min; kept as is.
        if (orient == CW::HORIZONTAL) {
            const int max = mWidget->mMatchConstraintMaxWidth;
            const int min = mWidget->mMatchConstraintMinWidth;
            int v = std::max(min, dimension);
            if (max > 0) v = std::min(max, dimension);
            if (v != dimension) dimension = v;
        } else {
            const int max = mWidget->mMatchConstraintMaxHeight;
            const int min = mWidget->mMatchConstraintMinHeight;
            int v = std::max(min, dimension);
            if (max > 0) v = std::min(max, dimension);
            if (v != dimension) dimension = v;
        }
        return dimension;
    }

    void WidgetRun::addTarget(DependencyNode* node, DependencyNode* target, int margin) {
        node->mTargets.push_back(target);
        node->mMargin = margin;
        target->mDependencies.push_back(node);
    }

    void WidgetRun::addTarget(DependencyNode* node, DependencyNode* target, int marginFactor, DimensionDependency* dimensionDependency) {
        node->mTargets.push_back(target);
        node->mTargets.push_back(&mDimension);
        node->mMarginFactor = marginFactor;
        node->mMarginDependency = dimensionDependency;
        target->mDependencies.push_back(node);
        dimensionDependency->mDependencies.push_back(node);
    }

    // --- RunGroup ---------------------------------------------------------------------------------

    long long RunGroup::traverseStart(DependencyNode* node, long long startPosition) {
        WidgetRun* run = node->mRun;
        if (run->mKind == WidgetRun::Kind::HELPER) return startPosition;
        long long position = startPosition;
        const std::size_t count = node->mDependencies.size();
        for (std::size_t i = 0; i < count; i++) {
            Dependency* dependency = node->mDependencies[i];
            if (dependency->isNode()) {
                auto* nextNode = static_cast<DependencyNode*>(dependency);
                if (nextNode->mRun == run) continue;
                position = std::max(position, traverseStart(nextNode, startPosition + nextNode->mMargin));
            }
        }
        if (node == &run->start) {
            const long long dimension = run->getWrapDimension();
            position = std::max(position, traverseStart(&run->end, startPosition + dimension));
            position = std::max(position, startPosition + dimension - run->end.mMargin);
        }
        return position;
    }

    long long RunGroup::traverseEnd(DependencyNode* node, long long startPosition) {
        WidgetRun* run = node->mRun;
        if (run->mKind == WidgetRun::Kind::HELPER) return startPosition;
        long long position = startPosition;
        const std::size_t count = node->mDependencies.size();
        for (std::size_t i = 0; i < count; i++) {
            Dependency* dependency = node->mDependencies[i];
            if (dependency->isNode()) {
                auto* nextNode = static_cast<DependencyNode*>(dependency);
                if (nextNode->mRun == run) continue;
                position = std::min(position, traverseEnd(nextNode, startPosition + nextNode->mMargin));
            }
        }
        if (node == &run->end) {
            const long long dimension = run->getWrapDimension();
            position = std::min(position, traverseEnd(&run->start, startPosition - dimension));
            position = std::min(position, startPosition - dimension - run->start.mMargin);
        }
        return position;
    }

    long long RunGroup::computeWrapSize(ConstraintWidgetContainer* container, int orientation) {
        if (mFirstRun->mKind == WidgetRun::Kind::CHAIN) {
            if (mFirstRun->orientation != orientation) return 0;
        } else {
            if (orientation == CW::HORIZONTAL) {
                if (mFirstRun->mKind != WidgetRun::Kind::HORIZONTAL) return 0;
            } else {
                if (mFirstRun->mKind != WidgetRun::Kind::VERTICAL) return 0;
            }
        }
        DependencyNode* containerStart = orientation == CW::HORIZONTAL ? &container->mHorizontalRun->start : &container->mVerticalRun->start;
        DependencyNode* containerEnd = orientation == CW::HORIZONTAL ? &container->mHorizontalRun->end : &container->mVerticalRun->end;

        const auto& st = mFirstRun->start.mTargets;
        const auto& et = mFirstRun->end.mTargets;
        const bool runWithStartTarget = std::find(st.begin(), st.end(), containerStart) != st.end();
        const bool runWithEndTarget = std::find(et.begin(), et.end(), containerEnd) != et.end();

        long long dimension = mFirstRun->getWrapDimension();

        if (runWithStartTarget && runWithEndTarget) {
            const long long maxPosition = traverseStart(&mFirstRun->start, 0);
            const long long minPosition = traverseEnd(&mFirstRun->end, 0);
            long long endGap = maxPosition - dimension;
            if (endGap >= -mFirstRun->end.mMargin) endGap += mFirstRun->end.mMargin;
            long long startGap = -minPosition - dimension - mFirstRun->start.mMargin;
            if (startGap >= mFirstRun->start.mMargin) startGap -= mFirstRun->start.mMargin;
            const float bias = mFirstRun->mWidget->getBiasPercent(orientation);
            long long gap = 0;
            // long / float is float in Java.
            if (bias > 0) gap = JavaLong((float(startGap) / bias) + (float(endGap) / (1.0f - bias)));
            startGap = JavaLong(0.5f + (float(gap) * bias));
            endGap = JavaLong(0.5f + (float(gap) * (1.0f - bias)));
            const long long runDimension = startGap + dimension + endGap;
            dimension = mFirstRun->start.mMargin + runDimension - mFirstRun->end.mMargin;
        } else if (runWithStartTarget) {
            const long long maxPosition = traverseStart(&mFirstRun->start, mFirstRun->start.mMargin);
            const long long runDimension = mFirstRun->start.mMargin + dimension;
            dimension = std::max(maxPosition, runDimension);
        } else if (runWithEndTarget) {
            const long long minPosition = traverseEnd(&mFirstRun->end, mFirstRun->end.mMargin);
            const long long runDimension = -mFirstRun->end.mMargin + dimension;
            dimension = std::max(-minPosition, runDimension);
        } else {
            dimension = mFirstRun->start.mMargin + mFirstRun->getWrapDimension() - mFirstRun->end.mMargin;
        }
        return dimension;
    }

    bool RunGroup::defineTerminalWidget(WidgetRun* run, int orientation) {
        if (!run->mWidget->isTerminalWidget[std::size_t(orientation)]) return false;
        auto visit = [&](DependencyNode& side) {
            for (std::size_t i = 0; i < side.mDependencies.size(); i++) {
                Dependency* dependency = side.mDependencies[i];
                if (!dependency->isNode()) continue;
                auto* node = static_cast<DependencyNode*>(dependency);
                if (node->mRun == run) continue;
                if (node == &node->mRun->start) {
                    if (run->mKind == WidgetRun::Kind::CHAIN) {
                        for (WidgetRun* widgetChainRun : static_cast<ChainRun*>(run)->mWidgets) defineTerminalWidget(widgetChainRun, orientation);
                    } else {
                        if (run->mKind != WidgetRun::Kind::HELPER) run->mWidget->isTerminalWidget[std::size_t(orientation)] = false;
                    }
                    defineTerminalWidget(node->mRun, orientation);
                }
            }
        };
        visit(run->start);
        visit(run->end);
        return false;
    }

    void RunGroup::defineTerminalWidgets(bool horizontalCheck, bool verticalCheck) {
        if (horizontalCheck && mFirstRun->mKind == WidgetRun::Kind::HORIZONTAL) defineTerminalWidget(mFirstRun, CW::HORIZONTAL);
        if (verticalCheck && mFirstRun->mKind == WidgetRun::Kind::VERTICAL) defineTerminalWidget(mFirstRun, CW::VERTICAL);
    }

}
