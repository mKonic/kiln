#include "kiln/constraintlayout/core/widgets/Chain.h"

#include "kiln/constraintlayout/core/widgets/ConstraintWidgetContainer.h"

#include <algorithm>

namespace kiln::constraintlayout::core {

    using DB = ConstraintWidget::DimensionBehaviour;
    using CW = ConstraintWidget;
    using SV = SolverVariable;

    namespace {
        SolverVariable* Var(const ConstraintAnchor* a) { return a == nullptr ? nullptr : a->getSolverVariable(); }
        SolverVariable* TargetVar(const ConstraintAnchor* a) { return a->mTarget != nullptr ? a->mTarget->getSolverVariable() : nullptr; }
    }

    void Chain::applyChainConstraints(ConstraintWidgetContainer* container, LinearSystem& system, const std::vector<ConstraintWidget*>* widgets,
                                      int orientation) {
        int offset = 0;
        int chainsSize = 0;
        std::vector<std::unique_ptr<ChainHead>>* chainsArray = nullptr;
        if (orientation == CW::HORIZONTAL) {
            offset = 0;
            chainsSize = container->mHorizontalChainsSize;
            chainsArray = &container->mHorizontalChainsArray;
        } else {
            offset = 2;
            chainsSize = container->mVerticalChainsSize;
            chainsArray = &container->mVerticalChainsArray;
        }
        for (int i = 0; i < chainsSize; i++) {
            ChainHead& first = *(*chainsArray)[std::size_t(i)];
            first.define();
            if (widgets == nullptr || std::find(widgets->begin(), widgets->end(), first.mFirst) != widgets->end())
                applyChainConstraints(container, system, orientation, offset, first);
        }
    }

    void Chain::applyChainConstraints(ConstraintWidgetContainer* container, LinearSystem& system, int orientation, int offset, ChainHead& chainHead) {
        const std::size_t o = std::size_t(orientation);
        const std::size_t ob = std::size_t(offset), oe = std::size_t(offset + 1);
        ConstraintWidget* first = chainHead.mFirst;
        ConstraintWidget* last = chainHead.mLast;
        ConstraintWidget* firstVisibleWidget = chainHead.mFirstVisibleWidget;
        ConstraintWidget* lastVisibleWidget = chainHead.mLastVisibleWidget;
        ConstraintWidget* head = chainHead.mHead;
        ConstraintWidget* widget = first;
        ConstraintWidget* next = nullptr;
        bool done = false;
        float totalWeights = chainHead.mTotalWeight;

        const bool isWrapContent = container->mListDimensionBehaviors[o] == DB::WRAP_CONTENT;
        bool isChainSpread = false;
        bool isChainSpreadInside = false;
        bool isChainPacked = false;
        if (orientation == CW::HORIZONTAL) {
            isChainSpread = head->mHorizontalChainStyle == CW::CHAIN_SPREAD;
            isChainSpreadInside = head->mHorizontalChainStyle == CW::CHAIN_SPREAD_INSIDE;
            isChainPacked = head->mHorizontalChainStyle == CW::CHAIN_PACKED;
        } else {
            isChainSpread = head->mVerticalChainStyle == CW::CHAIN_SPREAD;
            isChainSpreadInside = head->mVerticalChainStyle == CW::CHAIN_SPREAD_INSIDE;
            isChainPacked = head->mVerticalChainStyle == CW::CHAIN_PACKED;
        }
        // USE_CHAIN_OPTIMIZATION (Direct.solveChain) is off upstream.

        while (!done) {
            ConstraintAnchor* begin = widget->mListAnchors[ob];
            int strength = SV::STRENGTH_HIGHEST;
            if (isChainPacked) strength = SV::STRENGTH_LOW;
            int margin = begin->getMargin();
            const bool isSpreadOnly =
                widget->mListDimensionBehaviors[o] == DB::MATCH_CONSTRAINT && widget->mResolvedMatchConstraintDefault[o] == CW::MATCH_CONSTRAINT_SPREAD;
            if (begin->mTarget != nullptr && widget != first) margin += begin->mTarget->getMargin();
            if (isChainPacked && widget != first && widget != firstVisibleWidget) strength = SV::STRENGTH_FIXED;
            if (begin->mTarget != nullptr) {
                if (widget == firstVisibleWidget) system.addGreaterThan(Var(begin), Var(begin->mTarget), margin, SV::STRENGTH_BARRIER);
                else system.addGreaterThan(Var(begin), Var(begin->mTarget), margin, SV::STRENGTH_FIXED);
                if (isSpreadOnly && !isChainPacked) strength = SV::STRENGTH_EQUALITY;
                if (widget == firstVisibleWidget && isChainPacked && widget->isInBarrier(orientation)) strength = SV::STRENGTH_EQUALITY;
                system.addEquality(Var(begin), Var(begin->mTarget), margin, strength);
            }
            if (isWrapContent) {
                if (widget->getVisibility() != CW::GONE && widget->mListDimensionBehaviors[o] == DB::MATCH_CONSTRAINT)
                    system.addGreaterThan(Var(widget->mListAnchors[oe]), Var(widget->mListAnchors[ob]), 0, SV::STRENGTH_EQUALITY);
                system.addGreaterThan(Var(widget->mListAnchors[ob]), Var(container->mListAnchors[ob]), 0, SV::STRENGTH_FIXED);
            }
            ConstraintAnchor* nextAnchor = widget->mListAnchors[oe]->mTarget;
            if (nextAnchor != nullptr) {
                next = nextAnchor->mOwner;
                if (next->mListAnchors[ob]->mTarget == nullptr || next->mListAnchors[ob]->mTarget->mOwner != widget) next = nullptr;
            } else {
                next = nullptr;
            }
            if (next != nullptr) widget = next;
            else done = true;
        }

        if (lastVisibleWidget != nullptr && last->mListAnchors[oe]->mTarget != nullptr) {
            ConstraintAnchor* end = lastVisibleWidget->mListAnchors[oe];
            const bool isSpreadOnly = lastVisibleWidget->mListDimensionBehaviors[o] == DB::MATCH_CONSTRAINT &&
                                      lastVisibleWidget->mResolvedMatchConstraintDefault[o] == CW::MATCH_CONSTRAINT_SPREAD;
            if (isSpreadOnly && !isChainPacked && end->mTarget->mOwner == container)
                system.addEquality(Var(end), Var(end->mTarget), -end->getMargin(), SV::STRENGTH_EQUALITY);
            else if (isChainPacked && end->mTarget->mOwner == container)
                system.addEquality(Var(end), Var(end->mTarget), -end->getMargin(), SV::STRENGTH_HIGHEST);
            system.addLowerThan(Var(end), Var(last->mListAnchors[oe]->mTarget), -end->getMargin(), SV::STRENGTH_BARRIER);
        }

        if (isWrapContent)
            system.addGreaterThan(Var(container->mListAnchors[oe]), Var(last->mListAnchors[oe]), last->mListAnchors[oe]->getMargin(), SV::STRENGTH_FIXED);

        const std::vector<ConstraintWidget*>& listMatchConstraints = chainHead.mWeightedMatchConstraintsWidgets;
        const int count = int(listMatchConstraints.size());
        if (count > 1) {
            ConstraintWidget* lastMatch = nullptr;
            float lastWeight = 0;
            if (chainHead.mHasUndefinedWeights && !chainHead.mHasComplexMatchWeights) totalWeights = float(chainHead.mWidgetsMatchCount);
            for (int i = 0; i < count; i++) {
                ConstraintWidget* match = listMatchConstraints[std::size_t(i)];
                float currentWeight = match->mWeight[o];
                if (currentWeight < 0) {
                    if (chainHead.mHasComplexMatchWeights) {
                        system.addEquality(Var(match->mListAnchors[oe]), Var(match->mListAnchors[ob]), 0, SV::STRENGTH_HIGHEST);
                        continue;
                    }
                    currentWeight = 1;
                }
                if (currentWeight == 0) {
                    system.addEquality(Var(match->mListAnchors[oe]), Var(match->mListAnchors[ob]), 0, SV::STRENGTH_FIXED);
                    continue;
                }
                if (lastMatch != nullptr) {
                    SolverVariable* begin = Var(lastMatch->mListAnchors[ob]);
                    SolverVariable* end = Var(lastMatch->mListAnchors[oe]);
                    SolverVariable* nextBegin = Var(match->mListAnchors[ob]);
                    SolverVariable* nextEnd = Var(match->mListAnchors[oe]);
                    ArrayRow* row = system.createRow();
                    row->createRowEqualMatchDimensions(lastWeight, totalWeights, currentWeight, begin, end, nextBegin, nextEnd);
                    system.addConstraint(row);
                }
                lastMatch = match;
                lastWeight = currentWeight;
            }
        }

        if (firstVisibleWidget != nullptr && (firstVisibleWidget == lastVisibleWidget || isChainPacked)) {
            ConstraintAnchor* begin = first->mListAnchors[ob];
            ConstraintAnchor* end = last->mListAnchors[oe];
            SolverVariable* beginTarget = TargetVar(begin);
            SolverVariable* endTarget = TargetVar(end);
            begin = firstVisibleWidget->mListAnchors[ob];
            if (lastVisibleWidget != nullptr) end = lastVisibleWidget->mListAnchors[oe];
            if (beginTarget != nullptr && endTarget != nullptr) {
                const float bias = orientation == CW::HORIZONTAL ? head->mHorizontalBiasPercent : head->mVerticalBiasPercent;
                const int beginMargin = begin->getMargin();
                const int endMargin = end->getMargin();
                system.addCentering(Var(begin), beginTarget, beginMargin, bias, endTarget, Var(end), endMargin, SV::STRENGTH_CENTERING);
            }
        } else if (isChainSpread && firstVisibleWidget != nullptr) {
            widget = firstVisibleWidget;
            ConstraintWidget* previousVisibleWidget = firstVisibleWidget;
            const bool applyFixedEquality = chainHead.mWidgetsMatchCount > 0 && (chainHead.mWidgetsCount == chainHead.mWidgetsMatchCount);
            while (widget != nullptr) {
                next = widget->mNextChainWidget[o];
                while (next != nullptr && next->getVisibility() == CW::GONE) next = next->mNextChainWidget[o];
                if (next != nullptr || widget == lastVisibleWidget) {
                    ConstraintAnchor* beginAnchor = widget->mListAnchors[ob];
                    SolverVariable* begin = Var(beginAnchor);
                    SolverVariable* beginTarget = TargetVar(beginAnchor);
                    if (previousVisibleWidget != widget) beginTarget = Var(previousVisibleWidget->mListAnchors[oe]);
                    else if (widget == firstVisibleWidget) beginTarget = TargetVar(first->mListAnchors[ob]);
                    ConstraintAnchor* beginNextAnchor = nullptr;
                    SolverVariable* beginNext = nullptr;
                    SolverVariable* beginNextTarget = nullptr;
                    int beginMargin = beginAnchor->getMargin();
                    int nextMargin = widget->mListAnchors[oe]->getMargin();
                    if (next != nullptr) {
                        beginNextAnchor = next->mListAnchors[ob];
                        beginNext = Var(beginNextAnchor);
                    } else {
                        beginNextAnchor = last->mListAnchors[oe]->mTarget;
                        if (beginNextAnchor != nullptr) beginNext = Var(beginNextAnchor);
                    }
                    beginNextTarget = Var(widget->mListAnchors[oe]);
                    if (beginNextAnchor != nullptr) nextMargin += beginNextAnchor->getMargin();
                    beginMargin += previousVisibleWidget->mListAnchors[oe]->getMargin();
                    if (begin != nullptr && beginTarget != nullptr && beginNext != nullptr && beginNextTarget != nullptr) {
                        int margin1 = beginMargin;
                        if (widget == firstVisibleWidget) margin1 = firstVisibleWidget->mListAnchors[ob]->getMargin();
                        int margin2 = nextMargin;
                        if (widget == lastVisibleWidget) margin2 = lastVisibleWidget->mListAnchors[oe]->getMargin();
                        int strength = SV::STRENGTH_EQUALITY;
                        if (applyFixedEquality) strength = SV::STRENGTH_FIXED;
                        system.addCentering(begin, beginTarget, margin1, 0.5f, beginNext, beginNextTarget, margin2, strength);
                    }
                }
                if (widget->getVisibility() != CW::GONE) previousVisibleWidget = widget;
                widget = next;
            }
        } else if (isChainSpreadInside && firstVisibleWidget != nullptr) {
            widget = firstVisibleWidget;
            ConstraintWidget* previousVisibleWidget = firstVisibleWidget;
            const bool applyFixedEquality = chainHead.mWidgetsMatchCount > 0 && (chainHead.mWidgetsCount == chainHead.mWidgetsMatchCount);
            while (widget != nullptr) {
                next = widget->mNextChainWidget[o];
                while (next != nullptr && next->getVisibility() == CW::GONE) next = next->mNextChainWidget[o];
                if (widget != firstVisibleWidget && widget != lastVisibleWidget && next != nullptr) {
                    if (next == lastVisibleWidget) next = nullptr;
                    ConstraintAnchor* beginAnchor = widget->mListAnchors[ob];
                    SolverVariable* begin = Var(beginAnchor);
                    SolverVariable* beginTarget = Var(previousVisibleWidget->mListAnchors[oe]);
                    ConstraintAnchor* beginNextAnchor = nullptr;
                    SolverVariable* beginNext = nullptr;
                    SolverVariable* beginNextTarget = nullptr;
                    int beginMargin = beginAnchor->getMargin();
                    int nextMargin = widget->mListAnchors[oe]->getMargin();
                    if (next != nullptr) {
                        beginNextAnchor = next->mListAnchors[ob];
                        beginNext = Var(beginNextAnchor);
                        beginNextTarget = TargetVar(beginNextAnchor);
                    } else {
                        beginNextAnchor = lastVisibleWidget->mListAnchors[ob];
                        if (beginNextAnchor != nullptr) beginNext = Var(beginNextAnchor);
                        beginNextTarget = Var(widget->mListAnchors[oe]);
                    }
                    if (beginNextAnchor != nullptr) nextMargin += beginNextAnchor->getMargin();
                    beginMargin += previousVisibleWidget->mListAnchors[oe]->getMargin();
                    int strength = SV::STRENGTH_HIGHEST;
                    if (applyFixedEquality) strength = SV::STRENGTH_FIXED;
                    if (begin != nullptr && beginTarget != nullptr && beginNext != nullptr && beginNextTarget != nullptr)
                        system.addCentering(begin, beginTarget, beginMargin, 0.5f, beginNext, beginNextTarget, nextMargin, strength);
                }
                if (widget->getVisibility() != CW::GONE) previousVisibleWidget = widget;
                widget = next;
            }
            ConstraintAnchor* begin = firstVisibleWidget->mListAnchors[ob];
            ConstraintAnchor* beginTarget = first->mListAnchors[ob]->mTarget;
            ConstraintAnchor* end = lastVisibleWidget->mListAnchors[oe];
            ConstraintAnchor* endTarget = last->mListAnchors[oe]->mTarget;
            const int endPointsStrength = SV::STRENGTH_EQUALITY;
            if (beginTarget != nullptr) {
                if (firstVisibleWidget != lastVisibleWidget) {
                    system.addEquality(Var(begin), Var(beginTarget), begin->getMargin(), endPointsStrength);
                } else if (endTarget != nullptr) {
                    system.addCentering(Var(begin), Var(beginTarget), begin->getMargin(), 0.5f, Var(end), Var(endTarget), end->getMargin(), endPointsStrength);
                }
            }
            if (endTarget != nullptr && (firstVisibleWidget != lastVisibleWidget))
                system.addEquality(Var(end), Var(endTarget), -end->getMargin(), endPointsStrength);
        }

        if ((isChainSpread || isChainSpreadInside) && firstVisibleWidget != nullptr && firstVisibleWidget != lastVisibleWidget) {
            ConstraintAnchor* begin = firstVisibleWidget->mListAnchors[ob];
            if (lastVisibleWidget == nullptr) lastVisibleWidget = firstVisibleWidget;
            ConstraintAnchor* end = lastVisibleWidget->mListAnchors[oe];
            SolverVariable* beginTarget = TargetVar(begin);
            SolverVariable* endTarget = TargetVar(end);
            if (last != lastVisibleWidget) {
                ConstraintAnchor* realEnd = last->mListAnchors[oe];
                endTarget = TargetVar(realEnd);
            }
            if (firstVisibleWidget == lastVisibleWidget) {
                begin = firstVisibleWidget->mListAnchors[ob];
                end = firstVisibleWidget->mListAnchors[oe];
            }
            if (beginTarget != nullptr && endTarget != nullptr) {
                const float bias = 0.5f;
                const int beginMargin = begin->getMargin();
                const int endMargin = lastVisibleWidget->mListAnchors[oe]->getMargin();
                system.addCentering(Var(begin), beginTarget, beginMargin, bias, endTarget, Var(end), endMargin, SV::STRENGTH_EQUALITY);
            }
        }
    }

}
