#include "kiln/constraintlayout/core/widgets/analyzer/Grouping.h"

#include "kiln/constraintlayout/core/widgets/Barrier.h"
#include "kiln/constraintlayout/core/widgets/Chain.h"
#include "kiln/constraintlayout/core/widgets/ConstraintWidgetContainer.h"
#include "kiln/constraintlayout/core/widgets/Guideline.h"
#include "kiln/constraintlayout/core/widgets/HelperWidget.h"

#include <algorithm>

namespace kiln::constraintlayout::core {

    using DB = ConstraintWidget::DimensionBehaviour;
    using CW = ConstraintWidget;
    using T = ConstraintAnchor::Type;

    namespace {
        thread_local std::vector<std::shared_ptr<WidgetGroup>> tRetired;
    }

    // --- WidgetGroup ------------------------------------------------------------------------------

    bool WidgetGroup::add(ConstraintWidget* widget) {
        if (contains(widget)) return false;
        mWidgets.push_back(widget);
        return true;
    }

    bool WidgetGroup::contains(const ConstraintWidget* widget) const { return std::find(mWidgets.begin(), mWidgets.end(), widget) != mWidgets.end(); }

    void WidgetGroup::moveTo(int orientation, WidgetGroup* widgetGroup) {
        for (ConstraintWidget* widget : mWidgets) {
            widgetGroup->add(widget);
            if (orientation == CW::HORIZONTAL) widget->horizontalGroup = widgetGroup->getId();
            else widget->verticalGroup = widgetGroup->getId();
        }
        mMoveTo = widgetGroup->mId;
    }

    int WidgetGroup::measureWrap(LinearSystem& system, int orientation) {
        if (mWidgets.empty()) return 0;
        return solverMeasure(system, mWidgets, orientation);
    }

    int WidgetGroup::solverMeasure(LinearSystem& system, std::vector<ConstraintWidget*>& widgets, int orientation) {
        auto* container = static_cast<ConstraintWidgetContainer*>(widgets[0]->getParent());
        system.reset();
        container->addToSolver(system, false);
        for (ConstraintWidget* widget : widgets) widget->addToSolver(system, false);
        if (orientation == CW::HORIZONTAL && container->mHorizontalChainsSize > 0) Chain::applyChainConstraints(container, system, &widgets, CW::HORIZONTAL);
        if (orientation == CW::VERTICAL && container->mVerticalChainsSize > 0) Chain::applyChainConstraints(container, system, &widgets, CW::VERTICAL);
        system.minimize();
        mResults.clear();
        mHasResults = true;
        for (ConstraintWidget* widget : widgets) mResults.emplace_back(widget, system, orientation);
        if (orientation == CW::HORIZONTAL) {
            const int left = system.getObjectVariableValue(&container->mLeft);
            const int right = system.getObjectVariableValue(&container->mRight);
            system.reset();
            return right - left;
        }
        const int top = system.getObjectVariableValue(&container->mTop);
        const int bottom = system.getObjectVariableValue(&container->mBottom);
        system.reset();
        return bottom - top;
    }

    void WidgetGroup::apply() {
        if (!mHasResults) return;
        if (!mAuthoritative) return;
        for (const MeasureResult& result : mResults) result.apply();
    }

    bool WidgetGroup::intersectWith(const WidgetGroup& group) const {
        for (ConstraintWidget* widget : mWidgets)
            if (group.contains(widget)) return true;
        return false;
    }

    void WidgetGroup::cleanup(std::vector<std::shared_ptr<WidgetGroup>>& dependencyLists) {
        const std::size_t count = mWidgets.size();
        if (mMoveTo != -1 && count > 0) {
            for (std::size_t i = 0; i < dependencyLists.size(); i++) {
                WidgetGroup* group = dependencyLists[i].get();
                if (mMoveTo == group->mId) moveTo(mOrientation, group);
            }
        }
        if (count == 0) Grouping::remove(dependencyLists, this);
    }

    WidgetGroup::MeasureResult::MeasureResult(ConstraintWidget* widget, LinearSystem& system, int orientation)
        : mWidget(widget), mLeft(system.getObjectVariableValue(&widget->mLeft)), mTop(system.getObjectVariableValue(&widget->mTop)),
          mRight(system.getObjectVariableValue(&widget->mRight)), mBottom(system.getObjectVariableValue(&widget->mBottom)),
          mBaseline(system.getObjectVariableValue(&widget->mBaseline)), mOrientation(orientation) {}

    void WidgetGroup::MeasureResult::apply() const {
        if (mWidget != nullptr) mWidget->setFinalFrame(mLeft, mTop, mRight, mBottom, mBaseline, mOrientation);
    }

    // --- Grouping ---------------------------------------------------------------------------------

    void Grouping::remove(std::vector<std::shared_ptr<WidgetGroup>>& list, WidgetGroup* group) {
        auto it = std::find_if(list.begin(), list.end(), [&](const std::shared_ptr<WidgetGroup>& g) { return g.get() == group; });
        if (it == list.end()) return;
        tRetired.push_back(*it);
        list.erase(it);
    }

    bool Grouping::validInGroup(DB layoutHorizontal, DB layoutVertical, DB widgetHorizontal, DB widgetVertical) {
        const bool fixedHorizontal = widgetHorizontal == DB::FIXED || widgetHorizontal == DB::WRAP_CONTENT ||
                                     (widgetHorizontal == DB::MATCH_PARENT && layoutHorizontal != DB::WRAP_CONTENT);
        const bool fixedVertical =
            widgetVertical == DB::FIXED || widgetVertical == DB::WRAP_CONTENT || (widgetVertical == DB::MATCH_PARENT && layoutVertical != DB::WRAP_CONTENT);
        return fixedHorizontal || fixedVertical;
    }

    bool Grouping::simpleSolvingPass(ConstraintWidgetContainer* layout, BasicMeasure::Measurer* measurer) {
        struct RetiredScope {
            RetiredScope() { tRetired.clear(); }
            ~RetiredScope() { tRetired.clear(); }
        } retiredScope;

        std::vector<ConstraintWidget*>& children = layout->getChildren();
        const std::size_t count = children.size();

        std::vector<Guideline*> verticalGuidelines, horizontalGuidelines;
        std::vector<HelperWidget*> horizontalBarriers, verticalBarriers;
        std::vector<ConstraintWidget*> isolatedHorizontalChildren, isolatedVerticalChildren;

        for (std::size_t i = 0; i < count; i++) {
            ConstraintWidget* child = children[i];
            if (!validInGroup(layout->getHorizontalDimensionBehaviour(), layout->getVerticalDimensionBehaviour(), child->getHorizontalDimensionBehaviour(),
                              child->getVerticalDimensionBehaviour()))
                return false;
            if (child->is(CW::KIND_FLOW)) return false;
        }

        for (std::size_t i = 0; i < count; i++) {
            ConstraintWidget* child = children[i];
            if (!validInGroup(layout->getHorizontalDimensionBehaviour(), layout->getVerticalDimensionBehaviour(), child->getHorizontalDimensionBehaviour(),
                              child->getVerticalDimensionBehaviour()))
                ConstraintWidgetContainer::measure(0, child, measurer, layout->mMeasure, BasicMeasure::Measure::SELF_DIMENSIONS);
            if (child->is(CW::KIND_GUIDELINE)) {
                auto* guideline = static_cast<Guideline*>(child);
                if (guideline->getOrientation() == CW::HORIZONTAL) horizontalGuidelines.push_back(guideline);
                if (guideline->getOrientation() == CW::VERTICAL) verticalGuidelines.push_back(guideline);
            }
            if (child->is(CW::KIND_HELPER_WIDGET)) {
                if (child->is(CW::KIND_BARRIER)) {
                    auto* barrier = static_cast<Barrier*>(child);
                    if (barrier->getOrientation() == CW::HORIZONTAL) horizontalBarriers.push_back(barrier);
                    if (barrier->getOrientation() == CW::VERTICAL) verticalBarriers.push_back(barrier);
                } else {
                    auto* helper = static_cast<HelperWidget*>(child);
                    horizontalBarriers.push_back(helper);
                    verticalBarriers.push_back(helper);
                }
            }
            if (child->mLeft.mTarget == nullptr && child->mRight.mTarget == nullptr && !child->is(CW::KIND_GUIDELINE) && !child->is(CW::KIND_BARRIER))
                isolatedHorizontalChildren.push_back(child);
            if (child->mTop.mTarget == nullptr && child->mBottom.mTarget == nullptr && child->mBaseline.mTarget == nullptr && !child->is(CW::KIND_GUIDELINE) &&
                !child->is(CW::KIND_BARRIER))
                isolatedVerticalChildren.push_back(child);
        }

        std::vector<std::shared_ptr<WidgetGroup>> allDependencyLists;
        auto dependentsOf = [&](ConstraintAnchor* anchor, int orientation) {
            for (ConstraintAnchor* first : std::vector<ConstraintAnchor*>(anchor->getDependents()))
                findDependents(first->mOwner, orientation, allDependencyLists, nullptr);
        };

        {   // FORCE_USE: horizontal groups always.
            for (Guideline* guideline : verticalGuidelines) findDependents(guideline, CW::HORIZONTAL, allDependencyLists, nullptr);
            for (HelperWidget* barrier : horizontalBarriers) {
                WidgetGroup* group = findDependents(barrier, CW::HORIZONTAL, allDependencyLists, nullptr);
                barrier->addDependents(allDependencyLists, CW::HORIZONTAL, group);
                group->cleanup(allDependencyLists);
            }
            dependentsOf(layout->getAnchor(T::LEFT), CW::HORIZONTAL);
            dependentsOf(layout->getAnchor(T::RIGHT), CW::HORIZONTAL);
            dependentsOf(layout->getAnchor(T::CENTER), CW::HORIZONTAL);
            for (ConstraintWidget* widget : isolatedHorizontalChildren) findDependents(widget, CW::HORIZONTAL, allDependencyLists, nullptr);
        }
        {   // And vertical.
            for (Guideline* guideline : horizontalGuidelines) findDependents(guideline, CW::VERTICAL, allDependencyLists, nullptr);
            for (HelperWidget* barrier : verticalBarriers) {
                WidgetGroup* group = findDependents(barrier, CW::VERTICAL, allDependencyLists, nullptr);
                barrier->addDependents(allDependencyLists, CW::VERTICAL, group);
                group->cleanup(allDependencyLists);
            }
            dependentsOf(layout->getAnchor(T::TOP), CW::VERTICAL);
            dependentsOf(layout->getAnchor(T::BASELINE), CW::VERTICAL);
            dependentsOf(layout->getAnchor(T::BOTTOM), CW::VERTICAL);
            dependentsOf(layout->getAnchor(T::CENTER), CW::VERTICAL);
            for (ConstraintWidget* widget : isolatedVerticalChildren) findDependents(widget, CW::VERTICAL, allDependencyLists, nullptr);
        }

        // Groups tied by a widget with both dimensions match constraint become one.
        for (std::size_t i = 0; i < count; i++) {
            ConstraintWidget* child = children[i];
            if (child->oppositeDimensionsTied()) {
                WidgetGroup* horizontalGroup = findGroup(allDependencyLists, child->horizontalGroup);
                WidgetGroup* verticalGroup = findGroup(allDependencyLists, child->verticalGroup);
                if (horizontalGroup != nullptr && verticalGroup != nullptr) {
                    horizontalGroup->moveTo(CW::HORIZONTAL, verticalGroup);
                    verticalGroup->setOrientation(CW::BOTH);
                    remove(allDependencyLists, horizontalGroup);
                }
            }
        }

        if (allDependencyLists.size() <= 1) return false;

        WidgetGroup* horizontalPick = nullptr;
        WidgetGroup* verticalPick = nullptr;
        if (layout->getHorizontalDimensionBehaviour() == DB::WRAP_CONTENT) {
            int maxWrap = 0;
            WidgetGroup* picked = nullptr;
            for (auto& list : allDependencyLists) {
                if (list->getOrientation() == CW::VERTICAL) continue;
                list->setAuthoritative(false);
                const int wrap = list->measureWrap(layout->getSystem(), CW::HORIZONTAL);
                if (wrap > maxWrap) {
                    picked = list.get();
                    maxWrap = wrap;
                }
            }
            if (picked != nullptr) {
                layout->setHorizontalDimensionBehaviour(DB::FIXED);
                layout->setWidth(maxWrap);
                picked->setAuthoritative(true);
                horizontalPick = picked;
            }
        }
        if (layout->getVerticalDimensionBehaviour() == DB::WRAP_CONTENT) {
            int maxWrap = 0;
            WidgetGroup* picked = nullptr;
            for (auto& list : allDependencyLists) {
                if (list->getOrientation() == CW::HORIZONTAL) continue;
                list->setAuthoritative(false);
                const int wrap = list->measureWrap(layout->getSystem(), CW::VERTICAL);
                if (wrap > maxWrap) {
                    picked = list.get();
                    maxWrap = wrap;
                }
            }
            if (picked != nullptr) {
                layout->setVerticalDimensionBehaviour(DB::FIXED);
                layout->setHeight(maxWrap);
                picked->setAuthoritative(true);
                verticalPick = picked;
            }
        }
        return horizontalPick != nullptr || verticalPick != nullptr;
    }

    WidgetGroup* Grouping::findGroup(std::vector<std::shared_ptr<WidgetGroup>>& list, int groupId) {
        for (auto& group : list)
            if (groupId == group->getId()) return group.get();
        return nullptr;
    }

    WidgetGroup* Grouping::findDependents(ConstraintWidget* constraintWidget, int orientation, std::vector<std::shared_ptr<WidgetGroup>>& list,
                                          WidgetGroup* group) {
        int groupId = orientation == CW::HORIZONTAL ? constraintWidget->horizontalGroup : constraintWidget->verticalGroup;

        if (groupId != -1 && (group == nullptr || groupId != group->getId())) {
            for (std::size_t i = 0; i < list.size(); i++) {
                WidgetGroup* widgetGroup = list[i].get();
                if (widgetGroup->getId() == groupId) {
                    if (group != nullptr) {
                        group->moveTo(orientation, widgetGroup);
                        remove(list, group);
                    }
                    group = widgetGroup;
                    break;
                }
            }
        } else if (groupId != -1) {
            return group;
        }

        if (group == nullptr) {
            if (constraintWidget->is(CW::KIND_HELPER_WIDGET)) {
                groupId = static_cast<HelperWidget*>(constraintWidget)->findGroupInDependents(orientation);
                if (groupId != -1) {
                    for (auto& widgetGroup : list) {
                        if (widgetGroup->getId() == groupId) {
                            group = widgetGroup.get();
                            break;
                        }
                    }
                }
            }
            if (group == nullptr) {
                list.push_back(std::make_shared<WidgetGroup>(orientation));
                group = list.back().get();
            } else {
                // Upstream adds the found group to the list again (a second reference).
                for (auto& g : list) {
                    if (g.get() == group) {
                        list.push_back(g);
                        break;
                    }
                }
            }
        }

        if (group->add(constraintWidget)) {
            if (constraintWidget->is(CW::KIND_GUIDELINE)) {
                auto* guideline = static_cast<Guideline*>(constraintWidget);
                guideline->getAnchor()->findDependents(guideline->getOrientation() == Guideline::HORIZONTAL ? CW::VERTICAL : CW::HORIZONTAL, list, group);
            }
            if (orientation == CW::HORIZONTAL) {
                constraintWidget->horizontalGroup = group->getId();
                constraintWidget->mLeft.findDependents(orientation, list, group);
                constraintWidget->mRight.findDependents(orientation, list, group);
            } else {
                constraintWidget->verticalGroup = group->getId();
                constraintWidget->mTop.findDependents(orientation, list, group);
                constraintWidget->mBaseline.findDependents(orientation, list, group);
                constraintWidget->mBottom.findDependents(orientation, list, group);
            }
            constraintWidget->mCenter.findDependents(orientation, list, group);
        }
        return group;
    }

}
