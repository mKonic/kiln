#include "kiln/constraintlayout/core/widgets/analyzer/DependencyGraph.h"

#include "kiln/constraintlayout/core/widgets/ConstraintWidgetContainer.h"
#include "kiln/constraintlayout/core/widgets/analyzer/WidgetRuns.h"

#include <algorithm>

namespace kiln::constraintlayout::core {

    using DB = ConstraintWidget::DimensionBehaviour;
    using CW = ConstraintWidget;

    DependencyGraph::DependencyGraph(ConstraintWidgetContainer* container) : mWidgetcontainer(container), mContainer(container) {}
    DependencyGraph::~DependencyGraph() = default;

    int DependencyGraph::computeWrap(ConstraintWidgetContainer* container, int orientation) {
        long long wrapSize = 0;
        for (auto& run : mGroups) wrapSize = std::max(wrapSize, run->computeWrapSize(container, orientation));
        return int(wrapSize);   // Java (int) of a long: the low 32 bits
    }

    void DependencyGraph::defineTerminalWidgets(DB horizontalBehavior, DB verticalBehavior) {
        if (mNeedBuildGraph) {
            buildGraph();
            bool hasBarrier = false;
            for (ConstraintWidget* widget : mWidgetcontainer->mChildren) {
                widget->isTerminalWidget[CW::HORIZONTAL] = true;
                widget->isTerminalWidget[CW::VERTICAL] = true;
                if (widget->is(CW::KIND_BARRIER)) hasBarrier = true;
            }
            if (!hasBarrier)
                for (auto& group : mGroups) group->defineTerminalWidgets(horizontalBehavior == DB::WRAP_CONTENT, verticalBehavior == DB::WRAP_CONTENT);
        }
    }

    bool DependencyGraph::directMeasure(bool optimizeWrap) {
        optimizeWrap &= USE_GROUPS;
        if (mNeedBuildGraph || mNeedRedoMeasures) {
            for (ConstraintWidget* widget : mWidgetcontainer->mChildren) {
                widget->ensureWidgetRuns();
                widget->measured = false;
                widget->mHorizontalRun->reset();
                widget->mVerticalRun->reset();
            }
            mWidgetcontainer->ensureWidgetRuns();
            mWidgetcontainer->measured = false;
            mWidgetcontainer->mHorizontalRun->reset();
            mWidgetcontainer->mVerticalRun->reset();
            mNeedRedoMeasures = false;
        }
        const bool avoid = basicMeasureWidgets(mContainer);
        if (avoid) return false;

        mWidgetcontainer->setX(0);
        mWidgetcontainer->setY(0);
        const DB originalHorizontalDimension = mWidgetcontainer->getDimensionBehaviour(CW::HORIZONTAL);
        const DB originalVerticalDimension = mWidgetcontainer->getDimensionBehaviour(CW::VERTICAL);

        if (mNeedBuildGraph) buildGraph();

        const int x1 = mWidgetcontainer->getX();
        const int y1 = mWidgetcontainer->getY();
        mWidgetcontainer->mHorizontalRun->start.resolve(x1);
        mWidgetcontainer->mVerticalRun->start.resolve(y1);

        measureWidgets();

        if (originalHorizontalDimension == DB::WRAP_CONTENT || originalVerticalDimension == DB::WRAP_CONTENT) {
            if (optimizeWrap) {
                for (auto& run : mRuns) {
                    if (!run->supportsWrapComputation()) {
                        optimizeWrap = false;
                        break;
                    }
                }
            }
            if (optimizeWrap && originalHorizontalDimension == DB::WRAP_CONTENT) {
                mWidgetcontainer->setHorizontalDimensionBehaviour(DB::FIXED);
                mWidgetcontainer->setWidth(computeWrap(mWidgetcontainer, CW::HORIZONTAL));
                mWidgetcontainer->mHorizontalRun->mDimension.resolve(mWidgetcontainer->getWidth());
            }
            if (optimizeWrap && originalVerticalDimension == DB::WRAP_CONTENT) {
                mWidgetcontainer->setVerticalDimensionBehaviour(DB::FIXED);
                mWidgetcontainer->setHeight(computeWrap(mWidgetcontainer, CW::VERTICAL));
                mWidgetcontainer->mVerticalRun->mDimension.resolve(mWidgetcontainer->getHeight());
            }
        }

        bool checkRoot = false;
        if (mWidgetcontainer->mListDimensionBehaviors[CW::HORIZONTAL] == DB::FIXED ||
            mWidgetcontainer->mListDimensionBehaviors[CW::HORIZONTAL] == DB::MATCH_PARENT) {
            const int x2 = x1 + mWidgetcontainer->getWidth();
            mWidgetcontainer->mHorizontalRun->end.resolve(x2);
            mWidgetcontainer->mHorizontalRun->mDimension.resolve(x2 - x1);
            measureWidgets();
            if (mWidgetcontainer->mListDimensionBehaviors[CW::VERTICAL] == DB::FIXED ||
                mWidgetcontainer->mListDimensionBehaviors[CW::VERTICAL] == DB::MATCH_PARENT) {
                const int y2 = y1 + mWidgetcontainer->getHeight();
                mWidgetcontainer->mVerticalRun->end.resolve(y2);
                mWidgetcontainer->mVerticalRun->mDimension.resolve(y2 - y1);
            }
            measureWidgets();
            checkRoot = true;
        }

        for (auto& run : mRuns) {
            if (run->mWidget == mWidgetcontainer && !run->mResolved) continue;
            run->applyToWidget();
        }

        bool allResolved = true;
        for (auto& run : mRuns) {
            if (!checkRoot && run->mWidget == mWidgetcontainer) continue;
            if (!run->start.resolved) {
                allResolved = false;
                break;
            }
            if (!run->end.resolved && run->mKind != WidgetRun::Kind::GUIDELINE) {
                allResolved = false;
                break;
            }
            if (!run->mDimension.resolved && run->mKind != WidgetRun::Kind::CHAIN && run->mKind != WidgetRun::Kind::GUIDELINE) {
                allResolved = false;
                break;
            }
        }

        mWidgetcontainer->setHorizontalDimensionBehaviour(originalHorizontalDimension);
        mWidgetcontainer->setVerticalDimensionBehaviour(originalVerticalDimension);
        return allResolved;
    }

    bool DependencyGraph::directMeasureSetup(bool /*optimizeWrap*/) {
        if (mNeedBuildGraph) {
            auto resetRuns = [](ConstraintWidget* widget) {
                widget->ensureWidgetRuns();
                widget->measured = false;
                widget->mHorizontalRun->mDimension.resolved = false;
                widget->mHorizontalRun->mResolved = false;
                widget->mHorizontalRun->reset();
                widget->mVerticalRun->mDimension.resolved = false;
                widget->mVerticalRun->mResolved = false;
                widget->mVerticalRun->reset();
            };
            for (ConstraintWidget* widget : mWidgetcontainer->mChildren) resetRuns(widget);
            resetRuns(mWidgetcontainer);
            buildGraph();
        }
        const bool avoid = basicMeasureWidgets(mContainer);
        if (avoid) return false;
        mWidgetcontainer->setX(0);
        mWidgetcontainer->setY(0);
        mWidgetcontainer->mHorizontalRun->start.resolve(0);
        mWidgetcontainer->mVerticalRun->start.resolve(0);
        return true;
    }

    bool DependencyGraph::directMeasureWithOrientation(bool optimizeWrap, int orientation) {
        optimizeWrap &= USE_GROUPS;
        const DB originalHorizontalDimension = mWidgetcontainer->getDimensionBehaviour(CW::HORIZONTAL);
        const DB originalVerticalDimension = mWidgetcontainer->getDimensionBehaviour(CW::VERTICAL);
        const int x1 = mWidgetcontainer->getX();
        const int y1 = mWidgetcontainer->getY();

        if (optimizeWrap && (originalHorizontalDimension == DB::WRAP_CONTENT || originalVerticalDimension == DB::WRAP_CONTENT)) {
            for (auto& run : mRuns) {
                if (run->orientation == orientation && !run->supportsWrapComputation()) {
                    optimizeWrap = false;
                    break;
                }
            }
            if (orientation == CW::HORIZONTAL) {
                if (optimizeWrap && originalHorizontalDimension == DB::WRAP_CONTENT) {
                    mWidgetcontainer->setHorizontalDimensionBehaviour(DB::FIXED);
                    mWidgetcontainer->setWidth(computeWrap(mWidgetcontainer, CW::HORIZONTAL));
                    mWidgetcontainer->mHorizontalRun->mDimension.resolve(mWidgetcontainer->getWidth());
                }
            } else {
                if (optimizeWrap && originalVerticalDimension == DB::WRAP_CONTENT) {
                    mWidgetcontainer->setVerticalDimensionBehaviour(DB::FIXED);
                    mWidgetcontainer->setHeight(computeWrap(mWidgetcontainer, CW::VERTICAL));
                    mWidgetcontainer->mVerticalRun->mDimension.resolve(mWidgetcontainer->getHeight());
                }
            }
        }

        bool checkRoot = false;
        if (orientation == CW::HORIZONTAL) {
            if (mWidgetcontainer->mListDimensionBehaviors[CW::HORIZONTAL] == DB::FIXED ||
                mWidgetcontainer->mListDimensionBehaviors[CW::HORIZONTAL] == DB::MATCH_PARENT) {
                const int x2 = x1 + mWidgetcontainer->getWidth();
                mWidgetcontainer->mHorizontalRun->end.resolve(x2);
                mWidgetcontainer->mHorizontalRun->mDimension.resolve(x2 - x1);
                checkRoot = true;
            }
        } else {
            if (mWidgetcontainer->mListDimensionBehaviors[CW::VERTICAL] == DB::FIXED ||
                mWidgetcontainer->mListDimensionBehaviors[CW::VERTICAL] == DB::MATCH_PARENT) {
                const int y2 = y1 + mWidgetcontainer->getHeight();
                mWidgetcontainer->mVerticalRun->end.resolve(y2);
                mWidgetcontainer->mVerticalRun->mDimension.resolve(y2 - y1);
                checkRoot = true;
            }
        }
        measureWidgets();

        for (auto& run : mRuns) {
            if (run->orientation != orientation) continue;
            if (run->mWidget == mWidgetcontainer && !run->mResolved) continue;
            run->applyToWidget();
        }

        bool allResolved = true;
        for (auto& run : mRuns) {
            if (run->orientation != orientation) continue;
            if (!checkRoot && run->mWidget == mWidgetcontainer) continue;
            if (!run->start.resolved) {
                allResolved = false;
                break;
            }
            if (!run->end.resolved) {
                allResolved = false;
                break;
            }
            if (run->mKind != WidgetRun::Kind::CHAIN && !run->mDimension.resolved) {
                allResolved = false;
                break;
            }
        }

        mWidgetcontainer->setHorizontalDimensionBehaviour(originalHorizontalDimension);
        mWidgetcontainer->setVerticalDimensionBehaviour(originalVerticalDimension);
        return allResolved;
    }

    void DependencyGraph::measure(ConstraintWidget* widget, DB horizontalBehavior, int horizontalDimension, DB verticalBehavior, int verticalDimension) {
        mMeasure.horizontalBehavior = horizontalBehavior;
        mMeasure.verticalBehavior = verticalBehavior;
        mMeasure.horizontalDimension = horizontalDimension;
        mMeasure.verticalDimension = verticalDimension;
        mMeasurer->measure(widget, mMeasure);
        widget->setWidth(mMeasure.measuredWidth);
        widget->setHeight(mMeasure.measuredHeight);
        widget->setHasBaseline(mMeasure.measuredHasBaseline);
        widget->setBaselineDistance(mMeasure.measuredBaseline);
    }

    bool DependencyGraph::basicMeasureWidgets(ConstraintWidgetContainer* container) {
        // By index over the count at the start: measuring a virtual layout (a grid) can add
        // children (Java's iterator would throw).
        const std::size_t childCount = container->mChildren.size();
        for (std::size_t i = 0; i < childCount; i++) {
            ConstraintWidget* widget = container->mChildren[i];
            DB horizontal = widget->mListDimensionBehaviors[CW::HORIZONTAL];
            DB vertical = widget->mListDimensionBehaviors[CW::VERTICAL];

            if (widget->getVisibility() == CW::GONE) {
                widget->measured = true;
                continue;
            }

            if (widget->mMatchConstraintPercentWidth < 1 && horizontal == DB::MATCH_CONSTRAINT) widget->mMatchConstraintDefaultWidth = CW::MATCH_CONSTRAINT_PERCENT;
            if (widget->mMatchConstraintPercentHeight < 1 && vertical == DB::MATCH_CONSTRAINT) widget->mMatchConstraintDefaultHeight = CW::MATCH_CONSTRAINT_PERCENT;
            if (widget->getDimensionRatio() > 0) {
                if (horizontal == DB::MATCH_CONSTRAINT && (vertical == DB::WRAP_CONTENT || vertical == DB::FIXED)) {
                    widget->mMatchConstraintDefaultWidth = CW::MATCH_CONSTRAINT_RATIO;
                } else if (vertical == DB::MATCH_CONSTRAINT && (horizontal == DB::WRAP_CONTENT || horizontal == DB::FIXED)) {
                    widget->mMatchConstraintDefaultHeight = CW::MATCH_CONSTRAINT_RATIO;
                } else if (horizontal == DB::MATCH_CONSTRAINT && vertical == DB::MATCH_CONSTRAINT) {
                    if (widget->mMatchConstraintDefaultWidth == CW::MATCH_CONSTRAINT_SPREAD) widget->mMatchConstraintDefaultWidth = CW::MATCH_CONSTRAINT_RATIO;
                    if (widget->mMatchConstraintDefaultHeight == CW::MATCH_CONSTRAINT_SPREAD) widget->mMatchConstraintDefaultHeight = CW::MATCH_CONSTRAINT_RATIO;
                }
            }

            if (horizontal == DB::MATCH_CONSTRAINT && widget->mMatchConstraintDefaultWidth == CW::MATCH_CONSTRAINT_WRAP) {
                if (widget->mLeft.mTarget == nullptr || widget->mRight.mTarget == nullptr) horizontal = DB::WRAP_CONTENT;
            }
            if (vertical == DB::MATCH_CONSTRAINT && widget->mMatchConstraintDefaultHeight == CW::MATCH_CONSTRAINT_WRAP) {
                if (widget->mTop.mTarget == nullptr || widget->mBottom.mTarget == nullptr) vertical = DB::WRAP_CONTENT;
            }

            widget->mHorizontalRun->mDimensionBehavior = horizontal;
            widget->mHorizontalRun->matchConstraintsType = widget->mMatchConstraintDefaultWidth;
            widget->mVerticalRun->mDimensionBehavior = vertical;
            widget->mVerticalRun->matchConstraintsType = widget->mMatchConstraintDefaultHeight;

            if ((horizontal == DB::MATCH_PARENT || horizontal == DB::FIXED || horizontal == DB::WRAP_CONTENT) &&
                (vertical == DB::MATCH_PARENT || vertical == DB::FIXED || vertical == DB::WRAP_CONTENT)) {
                int width = widget->getWidth();
                if (horizontal == DB::MATCH_PARENT) {
                    width = container->getWidth() - widget->mLeft.mMargin - widget->mRight.mMargin;
                    horizontal = DB::FIXED;
                }
                int height = widget->getHeight();
                if (vertical == DB::MATCH_PARENT) {
                    height = container->getHeight() - widget->mTop.mMargin - widget->mBottom.mMargin;
                    vertical = DB::FIXED;
                }
                measure(widget, horizontal, width, vertical, height);
                widget->mHorizontalRun->mDimension.resolve(widget->getWidth());
                widget->mVerticalRun->mDimension.resolve(widget->getHeight());
                widget->measured = true;
                continue;
            }

            if (horizontal == DB::MATCH_CONSTRAINT && (vertical == DB::WRAP_CONTENT || vertical == DB::FIXED)) {
                if (widget->mMatchConstraintDefaultWidth == CW::MATCH_CONSTRAINT_RATIO) {
                    if (vertical == DB::WRAP_CONTENT) measure(widget, DB::WRAP_CONTENT, 0, DB::WRAP_CONTENT, 0);
                    const int height = widget->getHeight();
                    const int width = JavaInt(float(height) * widget->mDimensionRatio + 0.5f);
                    measure(widget, DB::FIXED, width, DB::FIXED, height);
                    widget->mHorizontalRun->mDimension.resolve(widget->getWidth());
                    widget->mVerticalRun->mDimension.resolve(widget->getHeight());
                    widget->measured = true;
                    continue;
                } else if (widget->mMatchConstraintDefaultWidth == CW::MATCH_CONSTRAINT_WRAP) {
                    measure(widget, DB::WRAP_CONTENT, 0, vertical, 0);
                    widget->mHorizontalRun->mDimension.wrapValue = widget->getWidth();
                    continue;
                } else if (widget->mMatchConstraintDefaultWidth == CW::MATCH_CONSTRAINT_PERCENT) {
                    if (container->mListDimensionBehaviors[CW::HORIZONTAL] == DB::FIXED || container->mListDimensionBehaviors[CW::HORIZONTAL] == DB::MATCH_PARENT) {
                        const float percent = widget->mMatchConstraintPercentWidth;
                        const int width = JavaInt(0.5f + percent * float(container->getWidth()));
                        const int height = widget->getHeight();
                        measure(widget, DB::FIXED, width, vertical, height);
                        widget->mHorizontalRun->mDimension.resolve(widget->getWidth());
                        widget->mVerticalRun->mDimension.resolve(widget->getHeight());
                        widget->measured = true;
                        continue;
                    }
                } else {
                    if (widget->mListAnchors[CW::ANCHOR_LEFT]->mTarget == nullptr || widget->mListAnchors[CW::ANCHOR_RIGHT]->mTarget == nullptr) {
                        measure(widget, DB::WRAP_CONTENT, 0, vertical, 0);
                        widget->mHorizontalRun->mDimension.resolve(widget->getWidth());
                        widget->mVerticalRun->mDimension.resolve(widget->getHeight());
                        widget->measured = true;
                        continue;
                    }
                }
            }
            if (vertical == DB::MATCH_CONSTRAINT && (horizontal == DB::WRAP_CONTENT || horizontal == DB::FIXED)) {
                if (widget->mMatchConstraintDefaultHeight == CW::MATCH_CONSTRAINT_RATIO) {
                    if (horizontal == DB::WRAP_CONTENT) measure(widget, DB::WRAP_CONTENT, 0, DB::WRAP_CONTENT, 0);
                    const int width = widget->getWidth();
                    float ratio = widget->mDimensionRatio;
                    if (widget->getDimensionRatioSide() == CW::UNKNOWN) ratio = 1.0f / ratio;
                    const int height = JavaInt(float(width) * ratio + 0.5f);
                    measure(widget, DB::FIXED, width, DB::FIXED, height);
                    widget->mHorizontalRun->mDimension.resolve(widget->getWidth());
                    widget->mVerticalRun->mDimension.resolve(widget->getHeight());
                    widget->measured = true;
                    continue;
                } else if (widget->mMatchConstraintDefaultHeight == CW::MATCH_CONSTRAINT_WRAP) {
                    measure(widget, horizontal, 0, DB::WRAP_CONTENT, 0);
                    widget->mVerticalRun->mDimension.wrapValue = widget->getHeight();
                    continue;
                } else if (widget->mMatchConstraintDefaultHeight == CW::MATCH_CONSTRAINT_PERCENT) {
                    if (container->mListDimensionBehaviors[CW::VERTICAL] == DB::FIXED || container->mListDimensionBehaviors[CW::VERTICAL] == DB::MATCH_PARENT) {
                        const float percent = widget->mMatchConstraintPercentHeight;
                        const int width = widget->getWidth();
                        const int height = JavaInt(0.5f + percent * float(container->getHeight()));
                        measure(widget, horizontal, width, DB::FIXED, height);
                        widget->mHorizontalRun->mDimension.resolve(widget->getWidth());
                        widget->mVerticalRun->mDimension.resolve(widget->getHeight());
                        widget->measured = true;
                        continue;
                    }
                } else {
                    if (widget->mListAnchors[CW::ANCHOR_TOP]->mTarget == nullptr || widget->mListAnchors[CW::ANCHOR_BOTTOM]->mTarget == nullptr) {
                        measure(widget, DB::WRAP_CONTENT, 0, vertical, 0);
                        widget->mHorizontalRun->mDimension.resolve(widget->getWidth());
                        widget->mVerticalRun->mDimension.resolve(widget->getHeight());
                        widget->measured = true;
                        continue;
                    }
                }
            }
            if (horizontal == DB::MATCH_CONSTRAINT && vertical == DB::MATCH_CONSTRAINT) {
                if (widget->mMatchConstraintDefaultWidth == CW::MATCH_CONSTRAINT_WRAP || widget->mMatchConstraintDefaultHeight == CW::MATCH_CONSTRAINT_WRAP) {
                    measure(widget, DB::WRAP_CONTENT, 0, DB::WRAP_CONTENT, 0);
                    widget->mHorizontalRun->mDimension.wrapValue = widget->getWidth();
                    widget->mVerticalRun->mDimension.wrapValue = widget->getHeight();
                } else if (widget->mMatchConstraintDefaultHeight == CW::MATCH_CONSTRAINT_PERCENT &&
                           widget->mMatchConstraintDefaultWidth == CW::MATCH_CONSTRAINT_PERCENT &&
                           container->mListDimensionBehaviors[CW::HORIZONTAL] == DB::FIXED && container->mListDimensionBehaviors[CW::VERTICAL] == DB::FIXED) {
                    const float horizPercent = widget->mMatchConstraintPercentWidth;
                    const float vertPercent = widget->mMatchConstraintPercentHeight;
                    const int width = JavaInt(0.5f + horizPercent * float(container->getWidth()));
                    const int height = JavaInt(0.5f + vertPercent * float(container->getHeight()));
                    measure(widget, DB::FIXED, width, DB::FIXED, height);
                    widget->mHorizontalRun->mDimension.resolve(widget->getWidth());
                    widget->mVerticalRun->mDimension.resolve(widget->getHeight());
                    widget->measured = true;
                }
            }
        }
        return false;
    }

    void DependencyGraph::measureWidgets() {
        for (ConstraintWidget* widget : mWidgetcontainer->mChildren) {
            if (widget->measured) continue;
            const DB horiz = widget->mListDimensionBehaviors[CW::HORIZONTAL];
            const DB vert = widget->mListDimensionBehaviors[CW::VERTICAL];
            const int horizMatchConstraintsType = widget->mMatchConstraintDefaultWidth;
            const int vertMatchConstraintsType = widget->mMatchConstraintDefaultHeight;
            const bool horizWrap = horiz == DB::WRAP_CONTENT || (horiz == DB::MATCH_CONSTRAINT && horizMatchConstraintsType == CW::MATCH_CONSTRAINT_WRAP);
            const bool vertWrap = vert == DB::WRAP_CONTENT || (vert == DB::MATCH_CONSTRAINT && vertMatchConstraintsType == CW::MATCH_CONSTRAINT_WRAP);
            const bool horizResolved = widget->mHorizontalRun->mDimension.resolved;
            const bool vertResolved = widget->mVerticalRun->mDimension.resolved;
            if (horizResolved && vertResolved) {
                measure(widget, DB::FIXED, widget->mHorizontalRun->mDimension.value, DB::FIXED, widget->mVerticalRun->mDimension.value);
                widget->measured = true;
            } else if (horizResolved && vertWrap) {
                measure(widget, DB::FIXED, widget->mHorizontalRun->mDimension.value, DB::WRAP_CONTENT, widget->mVerticalRun->mDimension.value);
                if (vert == DB::MATCH_CONSTRAINT) {
                    widget->mVerticalRun->mDimension.wrapValue = widget->getHeight();
                } else {
                    widget->mVerticalRun->mDimension.resolve(widget->getHeight());
                    widget->measured = true;
                }
            } else if (vertResolved && horizWrap) {
                measure(widget, DB::WRAP_CONTENT, widget->mHorizontalRun->mDimension.value, DB::FIXED, widget->mVerticalRun->mDimension.value);
                if (horiz == DB::MATCH_CONSTRAINT) {
                    widget->mHorizontalRun->mDimension.wrapValue = widget->getWidth();
                } else {
                    widget->mHorizontalRun->mDimension.resolve(widget->getWidth());
                    widget->measured = true;
                }
            }
            if (widget->measured && widget->mVerticalRun->mBaselineDimension != nullptr)
                widget->mVerticalRun->mBaselineDimension->resolve(widget->getBaselineDistance());
        }
    }

    void DependencyGraph::buildGraph() {
        buildGraph(mRuns);
        mGroups.clear();
        RunGroup::index = 0;
        findGroup(mWidgetcontainer->mHorizontalRun.get(), CW::HORIZONTAL, mGroups);
        findGroup(mWidgetcontainer->mVerticalRun.get(), CW::VERTICAL, mGroups);
        mNeedBuildGraph = false;
    }

    void DependencyGraph::buildGraph(std::vector<std::shared_ptr<WidgetRun>>& runs) {
        // The previous references stay alive until the new graph is built: nodes may still list them.
        std::vector<std::shared_ptr<WidgetRun>> previous;
        previous.swap(runs);
        mContainer->mHorizontalRun->clear();
        mContainer->mVerticalRun->clear();
        runs.push_back(mContainer->mHorizontalRun);
        runs.push_back(mContainer->mVerticalRun);
        // Upstream's HashSet of chain runs; insertion order here.
        std::vector<std::shared_ptr<ChainRun>> chainRuns;
        auto addChainRun = [&](const std::shared_ptr<ChainRun>& r) {
            if (std::find(chainRuns.begin(), chainRuns.end(), r) == chainRuns.end()) chainRuns.push_back(r);
        };
        for (ConstraintWidget* widget : mContainer->mChildren) {
            if (widget->is(CW::KIND_GUIDELINE)) {
                runs.push_back(std::make_shared<GuidelineReference>(widget));
                continue;
            }
            if (widget->isInHorizontalChain()) {
                if (widget->horizontalChainRun == nullptr) {
                    auto run = std::make_shared<ChainRun>(widget, CW::HORIZONTAL);
                    ChainRun::attach(run);
                    widget->horizontalChainRun = run;
                }
                addChainRun(widget->horizontalChainRun);
            } else {
                runs.push_back(widget->mHorizontalRun);
            }
            if (widget->isInVerticalChain()) {
                if (widget->verticalChainRun == nullptr) {
                    auto run = std::make_shared<ChainRun>(widget, CW::VERTICAL);
                    ChainRun::attach(run);
                    widget->verticalChainRun = run;
                }
                addChainRun(widget->verticalChainRun);
            } else {
                runs.push_back(widget->mVerticalRun);
            }
            if (widget->is(CW::KIND_HELPER_WIDGET)) runs.push_back(std::make_shared<HelperReferences>(widget));
        }
        for (auto& r : chainRuns) runs.push_back(r);
        for (auto& run : runs) run->clear();
        for (auto& run : runs) {
            if (run->mWidget == mContainer) continue;
            run->apply();
        }
    }

    void DependencyGraph::applyGroup(DependencyNode* node, int orientation, int direction, DependencyNode* end,
                                     std::vector<std::unique_ptr<RunGroup>>& groups, RunGroup* group) {
        WidgetRun* run = node->mRun;
        if (run->mRunGroup != nullptr || run == mWidgetcontainer->mHorizontalRun.get() || run == mWidgetcontainer->mVerticalRun.get()) return;
        if (group == nullptr) {
            groups.push_back(std::make_unique<RunGroup>(run, direction));
            group = groups.back().get();
        }
        run->mRunGroup = group;
        group->add(run);
        auto dependents = [&](DependencyNode& side, int dir) {
            for (std::size_t i = 0; i < side.mDependencies.size(); i++)
                if (side.mDependencies[i]->isNode()) applyGroup(static_cast<DependencyNode*>(side.mDependencies[i]), orientation, dir, end, groups, group);
        };
        dependents(run->start, RunGroup::START);
        dependents(run->end, RunGroup::END);
        if (orientation == CW::VERTICAL && run->mKind == WidgetRun::Kind::VERTICAL)
            dependents(static_cast<VerticalWidgetRun*>(run)->baseline, RunGroup::BASELINE);
        for (std::size_t i = 0; i < run->start.mTargets.size(); i++) {
            DependencyNode* target = run->start.mTargets[i];
            if (target == end) group->dual = true;
            applyGroup(target, orientation, RunGroup::START, end, groups, group);
        }
        for (std::size_t i = 0; i < run->end.mTargets.size(); i++) {
            DependencyNode* target = run->end.mTargets[i];
            if (target == end) group->dual = true;
            applyGroup(target, orientation, RunGroup::END, end, groups, group);
        }
        if (orientation == CW::VERTICAL && run->mKind == WidgetRun::Kind::VERTICAL) {
            auto& baseline = static_cast<VerticalWidgetRun*>(run)->baseline;
            for (std::size_t i = 0; i < baseline.mTargets.size(); i++) applyGroup(baseline.mTargets[i], orientation, RunGroup::BASELINE, end, groups, group);
        }
    }

    void DependencyGraph::findGroup(WidgetRun* run, int orientation, std::vector<std::unique_ptr<RunGroup>>& groups) {
        for (std::size_t i = 0; i < run->start.mDependencies.size(); i++) {
            Dependency* dependent = run->start.mDependencies[i];
            if (dependent->isNode()) applyGroup(static_cast<DependencyNode*>(dependent), orientation, RunGroup::START, &run->end, groups, nullptr);
            else applyGroup(&static_cast<WidgetRun*>(dependent)->start, orientation, RunGroup::START, &run->end, groups, nullptr);
        }
        for (std::size_t i = 0; i < run->end.mDependencies.size(); i++) {
            Dependency* dependent = run->end.mDependencies[i];
            if (dependent->isNode()) applyGroup(static_cast<DependencyNode*>(dependent), orientation, RunGroup::END, &run->start, groups, nullptr);
            else applyGroup(&static_cast<WidgetRun*>(dependent)->end, orientation, RunGroup::END, &run->start, groups, nullptr);
        }
        if (orientation == CW::VERTICAL) {
            auto& baseline = static_cast<VerticalWidgetRun*>(run)->baseline;
            for (std::size_t i = 0; i < baseline.mDependencies.size(); i++)
                if (baseline.mDependencies[i]->isNode())
                    applyGroup(static_cast<DependencyNode*>(baseline.mDependencies[i]), orientation, RunGroup::BASELINE, nullptr, groups, nullptr);
        }
    }

}
