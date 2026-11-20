// ChainRun, GuidelineReference and HelperReferences (constraintlayout-core analyzer).
#include "kiln/constraintlayout/core/widgets/analyzer/WidgetRuns.h"

#include "kiln/constraintlayout/core/widgets/Barrier.h"
#include "kiln/constraintlayout/core/widgets/ConstraintWidgetContainer.h"
#include "kiln/constraintlayout/core/widgets/Guideline.h"

#include <algorithm>

namespace kiln::constraintlayout::core {

    using DB = ConstraintWidget::DimensionBehaviour;
    using CW = ConstraintWidget;

    // --- ChainRun ---------------------------------------------------------------------------------

    ChainRun::ChainRun(ConstraintWidget* widget, int orient) : WidgetRun(widget, Kind::CHAIN) {
        orientation = orient;
        build();
    }

    void ChainRun::attach(const std::shared_ptr<ChainRun>& run) {
        for (WidgetRun* r : run->mWidgets) {
            if (run->orientation == CW::HORIZONTAL) r->mWidget->horizontalChainRun = run;
            else if (run->orientation == CW::VERTICAL) r->mWidget->verticalChainRun = run;
        }
    }

    std::string ChainRun::toString() const {
        std::string log = std::string("ChainRun ") + (orientation == CW::HORIZONTAL ? "horizontal : " : "vertical : ");
        for (WidgetRun* run : mWidgets) log += "<" + run->mWidget->getDebugName() + "> ";
        return log;
    }

    bool ChainRun::supportsWrapComputation() const {
        for (WidgetRun* run : mWidgets)
            if (!run->supportsWrapComputation()) return false;
        return true;
    }

    long long ChainRun::getWrapDimension() const {
        long long wrapDimension = 0;
        for (WidgetRun* run : mWidgets) {
            wrapDimension += run->start.mMargin;
            wrapDimension += run->getWrapDimension();
            wrapDimension += run->end.mMargin;
        }
        return wrapDimension;
    }

    void ChainRun::build() {
        ConstraintWidget* current = mWidget;
        ConstraintWidget* previous = current->getPreviousChainMember(orientation);
        while (previous != nullptr) {
            current = previous;
            previous = current->getPreviousChainMember(orientation);
        }
        mWidget = current;
        mWidgets.push_back(current->getRun(orientation));
        ConstraintWidget* next = current->getNextChainMember(orientation);
        while (next != nullptr) {
            current = next;
            mWidgets.push_back(current->getRun(orientation));
            next = current->getNextChainMember(orientation);
        }
        // The members' chain run fields are set by attach(), once this run is shared.
        const bool isInRtl = orientation == CW::HORIZONTAL && static_cast<ConstraintWidgetContainer*>(mWidget->getParent())->isRtl();
        if (isInRtl && mWidgets.size() > 1) mWidget = mWidgets.back()->mWidget;
        mChainStyle = orientation == CW::HORIZONTAL ? mWidget->getHorizontalChainStyle() : mWidget->getVerticalChainStyle();
    }

    void ChainRun::clear() {
        mRunGroup = nullptr;
        for (WidgetRun* run : mWidgets) run->clear();
    }

    void ChainRun::reset() {
        start.resolved = false;
        end.resolved = false;
    }

    void ChainRun::update(Dependency* /*dependency*/) {
        if (!(start.resolved && end.resolved)) return;
        ConstraintWidget* parent = mWidget->getParent();
        bool isInRtl = false;
        if (parent != nullptr && parent->is(CW::KIND_CONSTRAINT_WIDGET_CONTAINER)) isInRtl = static_cast<ConstraintWidgetContainer*>(parent)->isRtl();
        const int distance = end.value - start.value;
        int size = 0;
        int numMatchConstraints = 0;
        float weights = 0;
        int numVisibleWidgets = 0;
        const int count = int(mWidgets.size());
        int firstVisibleWidget = -1;
        for (int i = 0; i < count; i++) {
            if (mWidgets[std::size_t(i)]->mWidget->getVisibility() == CW::GONE) continue;
            firstVisibleWidget = i;
            break;
        }
        int lastVisibleWidget = -1;
        for (int i = count - 1; i >= 0; i--) {
            if (mWidgets[std::size_t(i)]->mWidget->getVisibility() == CW::GONE) continue;
            lastVisibleWidget = i;
            break;
        }
        for (int j = 0; j < 2; j++) {
            for (int i = 0; i < count; i++) {
                WidgetRun* run = mWidgets[std::size_t(i)];
                if (run->mWidget->getVisibility() == CW::GONE) continue;
                numVisibleWidgets++;
                if (i > 0 && i >= firstVisibleWidget) size += run->start.mMargin;
                int dimension = run->mDimension.value;
                bool treatAsFixed = run->mDimensionBehavior != DB::MATCH_CONSTRAINT;
                if (treatAsFixed) {
                    if (orientation == CW::HORIZONTAL && !run->mWidget->mHorizontalRun->mDimension.resolved) return;
                    if (orientation == CW::VERTICAL && !run->mWidget->mVerticalRun->mDimension.resolved) return;
                } else if (run->matchConstraintsType == CW::MATCH_CONSTRAINT_WRAP && j == 0) {
                    treatAsFixed = true;
                    dimension = run->mDimension.wrapValue;
                    numMatchConstraints++;
                } else if (run->mDimension.resolved) {
                    treatAsFixed = true;
                }
                if (!treatAsFixed) {
                    numMatchConstraints++;
                    const float weight = run->mWidget->mWeight[std::size_t(orientation)];
                    if (weight >= 0) weights += weight;
                } else {
                    size += dimension;
                }
                if (i < count - 1 && i < lastVisibleWidget) size += -run->end.mMargin;
            }
            if (size < distance || numMatchConstraints == 0) break;
            numVisibleWidgets = 0;
            numMatchConstraints = 0;
            size = 0;
            weights = 0;
        }

        int position = start.value;
        if (isInRtl) position = end.value;
        if (size > distance) {
            if (isInRtl) position += JavaInt(0.5f + float(size - distance) / 2.0f);
            else position -= JavaInt(0.5f + float(size - distance) / 2.0f);
        }
        int matchConstraintsDimension = 0;
        if (numMatchConstraints > 0) {
            matchConstraintsDimension = JavaInt(0.5f + float(distance - size) / float(numMatchConstraints));
            int appliedLimits = 0;
            for (int i = 0; i < count; i++) {
                WidgetRun* run = mWidgets[std::size_t(i)];
                if (run->mWidget->getVisibility() == CW::GONE) continue;
                if (run->mDimensionBehavior == DB::MATCH_CONSTRAINT && !run->mDimension.resolved) {
                    int dimension = matchConstraintsDimension;
                    if (weights > 0) {
                        const float weight = run->mWidget->mWeight[std::size_t(orientation)];
                        // weight * (distance - size) / weights, left to right in float.
                        dimension = JavaInt(0.5f + weight * float(distance - size) / weights);
                    }
                    int max;
                    int min;
                    int v = dimension;
                    if (orientation == CW::HORIZONTAL) {
                        max = run->mWidget->mMatchConstraintMaxWidth;
                        min = run->mWidget->mMatchConstraintMinWidth;
                    } else {
                        max = run->mWidget->mMatchConstraintMaxHeight;
                        min = run->mWidget->mMatchConstraintMinHeight;
                    }
                    if (run->matchConstraintsType == CW::MATCH_CONSTRAINT_WRAP) v = std::min(v, run->mDimension.wrapValue);
                    v = std::max(min, v);
                    if (max > 0) v = std::min(max, v);
                    if (v != dimension) {
                        appliedLimits++;
                        dimension = v;
                    }
                    run->mDimension.resolve(dimension);
                }
            }
            if (appliedLimits > 0) {
                numMatchConstraints -= appliedLimits;
                size = 0;
                for (int i = 0; i < count; i++) {
                    WidgetRun* run = mWidgets[std::size_t(i)];
                    if (run->mWidget->getVisibility() == CW::GONE) continue;
                    if (i > 0 && i >= firstVisibleWidget) size += run->start.mMargin;
                    size += run->mDimension.value;
                    if (i < count - 1 && i < lastVisibleWidget) size += -run->end.mMargin;
                }
            }
            if (mChainStyle == CW::CHAIN_PACKED && appliedLimits == 0) mChainStyle = CW::CHAIN_SPREAD;
        }

        if (size > distance) mChainStyle = CW::CHAIN_PACKED;
        if (numVisibleWidgets > 0 && numMatchConstraints == 0 && firstVisibleWidget == lastVisibleWidget) mChainStyle = CW::CHAIN_PACKED;

        auto runAt = [&](int i) { return mWidgets[std::size_t(isInRtl ? count - (i + 1) : i)]; };
        if (mChainStyle == CW::CHAIN_SPREAD_INSIDE) {
            int gap = 0;
            if (numVisibleWidgets > 1) gap = (distance - size) / (numVisibleWidgets - 1);
            else if (numVisibleWidgets == 1) gap = (distance - size) / 2;
            if (numMatchConstraints > 0) gap = 0;
            for (int i = 0; i < count; i++) {
                WidgetRun* run = runAt(i);
                if (run->mWidget->getVisibility() == CW::GONE) {
                    run->start.resolve(position);
                    run->end.resolve(position);
                    continue;
                }
                if (i > 0) position += isInRtl ? -gap : gap;
                if (i > 0 && i >= firstVisibleWidget) position += isInRtl ? -run->start.mMargin : run->start.mMargin;
                if (isInRtl) run->end.resolve(position);
                else run->start.resolve(position);
                int dimension = run->mDimension.value;
                if (run->mDimensionBehavior == DB::MATCH_CONSTRAINT && run->matchConstraintsType == CW::MATCH_CONSTRAINT_WRAP) dimension = run->mDimension.wrapValue;
                position += isInRtl ? -dimension : dimension;
                if (isInRtl) run->start.resolve(position);
                else run->end.resolve(position);
                run->mResolved = true;
                if (i < count - 1 && i < lastVisibleWidget) position += isInRtl ? run->end.mMargin : -run->end.mMargin;
            }
        } else if (mChainStyle == CW::CHAIN_SPREAD) {
            int gap = (distance - size) / (numVisibleWidgets + 1);
            if (numMatchConstraints > 0) gap = 0;
            for (int i = 0; i < count; i++) {
                WidgetRun* run = runAt(i);
                if (run->mWidget->getVisibility() == CW::GONE) {
                    run->start.resolve(position);
                    run->end.resolve(position);
                    continue;
                }
                position += isInRtl ? -gap : gap;
                if (i > 0 && i >= firstVisibleWidget) position += isInRtl ? -run->start.mMargin : run->start.mMargin;
                if (isInRtl) run->end.resolve(position);
                else run->start.resolve(position);
                int dimension = run->mDimension.value;
                if (run->mDimensionBehavior == DB::MATCH_CONSTRAINT && run->matchConstraintsType == CW::MATCH_CONSTRAINT_WRAP)
                    dimension = std::min(dimension, run->mDimension.wrapValue);
                position += isInRtl ? -dimension : dimension;
                if (isInRtl) run->start.resolve(position);
                else run->end.resolve(position);
                if (i < count - 1 && i < lastVisibleWidget) position += isInRtl ? run->end.mMargin : -run->end.mMargin;
            }
        } else if (mChainStyle == CW::CHAIN_PACKED) {
            float bias = orientation == CW::HORIZONTAL ? mWidget->getHorizontalBiasPercent() : mWidget->getVerticalBiasPercent();
            if (isInRtl) bias = 1 - bias;
            int gap = JavaInt(0.5f + float(distance - size) * bias);
            if (gap < 0 || numMatchConstraints > 0) gap = 0;
            position += isInRtl ? -gap : gap;
            for (int i = 0; i < count; i++) {
                WidgetRun* run = runAt(i);
                if (run->mWidget->getVisibility() == CW::GONE) {
                    run->start.resolve(position);
                    run->end.resolve(position);
                    continue;
                }
                if (i > 0 && i >= firstVisibleWidget) position += isInRtl ? -run->start.mMargin : run->start.mMargin;
                if (isInRtl) run->end.resolve(position);
                else run->start.resolve(position);
                int dimension = run->mDimension.value;
                if (run->mDimensionBehavior == DB::MATCH_CONSTRAINT && run->matchConstraintsType == CW::MATCH_CONSTRAINT_WRAP) dimension = run->mDimension.wrapValue;
                position += isInRtl ? -dimension : dimension;
                if (isInRtl) run->start.resolve(position);
                else run->end.resolve(position);
                if (i < count - 1 && i < lastVisibleWidget) position += isInRtl ? run->end.mMargin : -run->end.mMargin;
            }
        }
    }

    void ChainRun::applyToWidget() {
        for (WidgetRun* run : mWidgets) run->applyToWidget();
    }

    ConstraintWidget* ChainRun::getFirstVisibleWidget() const {
        for (WidgetRun* run : mWidgets)
            if (run->mWidget->getVisibility() != CW::GONE) return run->mWidget;
        return nullptr;
    }

    ConstraintWidget* ChainRun::getLastVisibleWidget() const {
        for (auto it = mWidgets.rbegin(); it != mWidgets.rend(); ++it)
            if ((*it)->mWidget->getVisibility() != CW::GONE) return (*it)->mWidget;
        return nullptr;
    }

    void ChainRun::apply() {
        for (WidgetRun* run : mWidgets) run->apply();
        const int count = int(mWidgets.size());
        if (count < 1) return;
        ConstraintWidget* firstWidget = mWidgets.front()->mWidget;
        ConstraintWidget* lastWidget = mWidgets.back()->mWidget;
        if (orientation == CW::HORIZONTAL) {
            ConstraintAnchor* startAnchor = &firstWidget->mLeft;
            ConstraintAnchor* endAnchor = &lastWidget->mRight;
            DependencyNode* startTarget = getTarget(startAnchor, CW::HORIZONTAL);
            int startMargin = startAnchor->getMargin();
            if (ConstraintWidget* firstVisible = getFirstVisibleWidget()) startMargin = firstVisible->mLeft.getMargin();
            if (startTarget != nullptr) addTarget(&start, startTarget, startMargin);
            DependencyNode* endTarget = getTarget(endAnchor, CW::HORIZONTAL);
            int endMargin = endAnchor->getMargin();
            if (ConstraintWidget* lastVisible = getLastVisibleWidget()) endMargin = lastVisible->mRight.getMargin();
            if (endTarget != nullptr) addTarget(&end, endTarget, -endMargin);
        } else {
            ConstraintAnchor* startAnchor = &firstWidget->mTop;
            ConstraintAnchor* endAnchor = &lastWidget->mBottom;
            DependencyNode* startTarget = getTarget(startAnchor, CW::VERTICAL);
            int startMargin = startAnchor->getMargin();
            if (ConstraintWidget* firstVisible = getFirstVisibleWidget()) startMargin = firstVisible->mTop.getMargin();
            if (startTarget != nullptr) addTarget(&start, startTarget, startMargin);
            DependencyNode* endTarget = getTarget(endAnchor, CW::VERTICAL);
            int endMargin = endAnchor->getMargin();
            if (ConstraintWidget* lastVisible = getLastVisibleWidget()) endMargin = lastVisible->mBottom.getMargin();
            if (endTarget != nullptr) addTarget(&end, endTarget, -endMargin);
        }
        start.updateDelegate = this;
        end.updateDelegate = this;
    }

    // --- GuidelineReference -----------------------------------------------------------------------

    GuidelineReference::GuidelineReference(ConstraintWidget* widget) : WidgetRun(widget, Kind::GUIDELINE) {
        widget->mHorizontalRun->clear();
        widget->mVerticalRun->clear();
        orientation = static_cast<Guideline*>(widget)->getOrientation();
    }

    void GuidelineReference::addDependency(DependencyNode* node) {
        start.mDependencies.push_back(node);
        node->mTargets.push_back(&start);
    }

    void GuidelineReference::update(Dependency* /*dependency*/) {
        if (!start.readyToSolve) return;
        if (start.resolved) return;
        DependencyNode* startTarget = start.mTargets[0];
        auto* guideline = static_cast<Guideline*>(mWidget);
        const int startPos = JavaInt(0.5f + float(startTarget->value) * guideline->getRelativePercent());
        start.resolve(startPos);
    }

    void GuidelineReference::apply() {
        auto* guideline = static_cast<Guideline*>(mWidget);
        const int relativeBegin = guideline->getRelativeBegin();
        const int relativeEnd = guideline->getRelativeEnd();
        WidgetRun* parentRun = guideline->getOrientation() == CW::VERTICAL ? static_cast<WidgetRun*>(mWidget->mParent->mHorizontalRun.get())
                                                                          : static_cast<WidgetRun*>(mWidget->mParent->mVerticalRun.get());
        if (relativeBegin != -1) {
            start.mTargets.push_back(&parentRun->start);
            parentRun->start.mDependencies.push_back(&start);
            start.mMargin = relativeBegin;
        } else if (relativeEnd != -1) {
            start.mTargets.push_back(&parentRun->end);
            parentRun->end.mDependencies.push_back(&start);
            start.mMargin = -relativeEnd;
        } else {
            start.delegateToWidgetRun = true;
            start.mTargets.push_back(&parentRun->end);
            parentRun->end.mDependencies.push_back(&start);
        }
        if (guideline->getOrientation() == CW::VERTICAL) {
            addDependency(&mWidget->mHorizontalRun->start);
            addDependency(&mWidget->mHorizontalRun->end);
        } else {
            addDependency(&mWidget->mVerticalRun->start);
            addDependency(&mWidget->mVerticalRun->end);
        }
    }

    void GuidelineReference::applyToWidget() {
        if (static_cast<Guideline*>(mWidget)->getOrientation() == CW::VERTICAL) mWidget->setX(start.value);
        else mWidget->setY(start.value);
    }

    // --- HelperReferences -------------------------------------------------------------------------

    void HelperReferences::addDependency(DependencyNode* node) {
        start.mDependencies.push_back(node);
        node->mTargets.push_back(&start);
    }

    void HelperReferences::apply() {
        if (!mWidget->is(CW::KIND_BARRIER)) return;
        start.delegateToWidgetRun = true;
        auto* barrier = static_cast<Barrier*>(mWidget);
        const int type = barrier->getBarrierType();
        const bool allowsGoneWidget = barrier->getAllowsGoneWidget();
        auto collect = [&](DependencyNode::Type nodeType, bool horizontal, bool useEnd) {
            start.mType = nodeType;
            for (int i = 0; i < barrier->mWidgetsCount; i++) {
                ConstraintWidget* refWidget = barrier->mWidgets[std::size_t(i)];
                if (!allowsGoneWidget && refWidget->getVisibility() == CW::GONE) continue;
                WidgetRun* run = horizontal ? static_cast<WidgetRun*>(refWidget->mHorizontalRun.get()) : static_cast<WidgetRun*>(refWidget->mVerticalRun.get());
                DependencyNode* target = useEnd ? &run->end : &run->start;
                target->mDependencies.push_back(&start);
                start.mTargets.push_back(target);
            }
            WidgetRun* own = horizontal ? static_cast<WidgetRun*>(mWidget->mHorizontalRun.get()) : static_cast<WidgetRun*>(mWidget->mVerticalRun.get());
            addDependency(&own->start);
            addDependency(&own->end);
        };
        switch (type) {
            case Barrier::LEFT: collect(DependencyNode::Type::LEFT, true, false); break;
            case Barrier::RIGHT: collect(DependencyNode::Type::RIGHT, true, true); break;
            case Barrier::TOP: collect(DependencyNode::Type::TOP, false, false); break;
            case Barrier::BOTTOM: collect(DependencyNode::Type::BOTTOM, false, true); break;
            default: break;
        }
    }

    void HelperReferences::update(Dependency* /*dependency*/) {
        auto* barrier = static_cast<Barrier*>(mWidget);
        const int type = barrier->getBarrierType();
        int min = -1;
        int max = 0;
        for (DependencyNode* node : start.mTargets) {
            const int v = node->value;
            if (min == -1 || v < min) min = v;
            if (max < v) max = v;
        }
        if (type == Barrier::LEFT || type == Barrier::TOP) start.resolve(min + barrier->getMargin());
        else start.resolve(max + barrier->getMargin());
    }

    void HelperReferences::applyToWidget() {
        if (mWidget->is(CW::KIND_BARRIER)) {
            const int type = static_cast<Barrier*>(mWidget)->getBarrierType();
            if (type == Barrier::LEFT || type == Barrier::RIGHT) mWidget->setX(start.value);
            else mWidget->setY(start.value);
        }
    }

}
