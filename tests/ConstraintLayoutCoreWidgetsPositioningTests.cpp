// Ported from constraintlayout-core WidgetsPositioningTest.java (androidx), by tools in
// the scratchpad, then reviewed. Expected values are upstream's.
#include "ConstraintLayoutTestSupport.h"

#include <functional>

namespace {
using namespace kiln::constraintlayout::core;
}


// The test's own harness: every permutation of adding the widgets to a fresh LinearSystem
// must give the same result (upstream's runTestOnWidgets).
static std::unique_ptr<LinearSystem> sLS;
static constexpr bool mOptimize = false;

static void setUp() { sLS = std::make_unique<LinearSystem>(); }

static void runTest(ConstraintWidgetContainer* root, const std::function<void()>& check) {
    root->layout();
    check();
}

static void addToSolverWithPermutation(const std::vector<ConstraintWidget*>& widgets, std::vector<int>& list,
                                       const std::vector<int>& tail, const std::function<void()>& check) {
    if (!tail.empty()) {
        for (std::size_t i = 0; i < tail.size(); i++) {
            list.push_back(tail[i]);
            std::vector<int> permuted(tail);
            permuted.erase(permuted.begin() + static_cast<std::ptrdiff_t>(i));
            addToSolverWithPermutation(widgets, list, permuted, check);
            list.pop_back();
        }
        return;
    }
    sLS->reset();
    for (int index : list) widgets[std::size_t(index)]->resetSolverVariables(&sLS->getCache());
    for (int index : list) {
        ConstraintWidget* widget = widgets[std::size_t(index)];
        if (!widget->getDebugName().empty()) widget->setDebugSolverName(*sLS, widget->getDebugName());
        widget->addToSolver(*sLS, mOptimize);
    }
    sLS->minimize();
    for (ConstraintWidget* w : widgets) w->updateFromSolver(*sLS, mOptimize);
    check();
}

static void runTestOnWidgets(const std::vector<ConstraintWidget*>& widgets, const std::function<void()>& check) {
    std::vector<int> tail;
    for (std::size_t i = 0; i < widgets.size(); i++) tail.push_back(int(i));
    std::vector<int> list;
    addToSolverWithPermutation(widgets, list, tail, check);
}

TEST(ClWidgetsPositioning_testCentering) {
    using namespace kiln::constraintlayout::core;
    setUp();
    Widgets W;
        auto* root = W.make<ConstraintWidgetContainer>(0, 0, 600, 600);
        auto* a = W.make<ConstraintWidget>(100, 20);
        auto* b = W.make<ConstraintWidget>(20, 100);
        auto* c = W.make<ConstraintWidget>(100, 20);
        a->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP, 200);
        b->connect(ConstraintAnchor::Type::TOP, a, ConstraintAnchor::Type::TOP, 0);
        b->connect(ConstraintAnchor::Type::BOTTOM, a, ConstraintAnchor::Type::BOTTOM, 0);
        c->connect(ConstraintAnchor::Type::TOP, b, ConstraintAnchor::Type::TOP, 0);
        c->connect(ConstraintAnchor::Type::BOTTOM, b, ConstraintAnchor::Type::BOTTOM, 0);
        root->add(a);
        root->add(b);
        root->add(c);
        root->layout();
}

TEST(ClWidgetsPositioning_testDimensionRatio) {
    using namespace kiln::constraintlayout::core;
    setUp();
    Widgets W;
        auto* a = W.make<ConstraintWidget>(0, 0, 600, 600);
        auto* b = W.make<ConstraintWidget>(100, 100);
        std::vector<ConstraintWidget*> widgets;
        widgets.push_back(a);
        widgets.push_back(b);
        int margin = 10;
        b->connect(ConstraintAnchor::Type::LEFT, a, ConstraintAnchor::Type::LEFT, margin);
        b->connect(ConstraintAnchor::Type::RIGHT, a, ConstraintAnchor::Type::RIGHT, margin);
        b->connect(ConstraintAnchor::Type::TOP, a, ConstraintAnchor::Type::TOP, margin);
        b->connect(ConstraintAnchor::Type::BOTTOM, a, ConstraintAnchor::Type::BOTTOM, margin);
        b->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::MATCH_CONSTRAINT);
        b->setVerticalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::MATCH_CONSTRAINT);
        a->setDebugName("A");
        b->setDebugName("B");
        float ratio = 0.3f;
        b->setDimensionRatio(ratio, ConstraintWidget::VERTICAL);
        runTestOnWidgets(widgets, [&] {
                CHECK_EQ(b->getWidth(), a->getWidth() - 2 * margin);
                CHECK_EQ(b->getHeight(), (int) (ratio * b->getWidth()));
                CHECK_EQ(b->getTop() - a->getTop(), (int) ((a->getHeight() - b->getHeight()) / 2));
                CHECK_EQ(a->getBottom() - b->getBottom(), (int) ((a->getHeight() - b->getHeight()) / 2));
                CHECK_EQ(b->getTop() - a->getTop(), a->getBottom() - b->getBottom());
            });
        b->setVerticalBiasPercent(1);
        runTestOnWidgets(widgets, [&] {
                CHECK_EQ(b->getWidth(), a->getWidth() - 2 * margin);
                CHECK_EQ(b->getHeight(), (int) (ratio * b->getWidth()));
                CHECK_EQ(b->getTop(), a->getHeight() - b->getHeight() - margin);
                CHECK_EQ(a->getBottom(), b->getBottom() + margin);
            });
        b->setVerticalBiasPercent(0);
        runTestOnWidgets(widgets, [&] {
                CHECK_EQ(b->getWidth(), a->getWidth() - 2 * margin);
                CHECK_EQ(b->getHeight(), (int) (ratio * b->getWidth()));
                CHECK_EQ(b->getTop(), a->getTop() + margin);
                CHECK_EQ(b->getBottom(), a->getTop() + b->getHeight() + margin);
            });
        b->setDimensionRatio(ratio, ConstraintWidget::HORIZONTAL);
        runTestOnWidgets(widgets, [&] {
                CHECK_EQ(b->getHeight(), a->getHeight() - 2 * margin);
                CHECK_EQ(b->getWidth(), (int) (ratio * b->getHeight()));
                CHECK_EQ(b->getLeft() - a->getLeft(), (int) ((a->getWidth() - b->getWidth()) / 2));
                CHECK_EQ(a->getRight() - b->getRight(), (int) ((a->getWidth() - b->getWidth()) / 2));
            });
        b->setHorizontalBiasPercent(1);
        runTestOnWidgets(widgets, [&] {
                CHECK_EQ(b->getHeight(), a->getHeight() - 2 * margin);
                CHECK_EQ(b->getWidth(), (int) (ratio * b->getHeight()));
                CHECK_EQ(b->getRight(), a->getRight() - margin);
                CHECK_EQ(b->getLeft(), a->getRight() - b->getWidth() - margin);
            });
        b->setHorizontalBiasPercent(0);
        runTestOnWidgets(widgets, [&] {
                CHECK_EQ(b->getHeight(), a->getHeight() - 2 * margin);
                CHECK_EQ(b->getWidth(), (int) (ratio * b->getHeight()));
                CHECK_EQ(b->getRight(), a->getLeft() + margin + b->getWidth());
                CHECK_EQ(b->getLeft(), a->getLeft() + margin);
            });
}

TEST(ClWidgetsPositioning_testCreateManyVariables) {
    using namespace kiln::constraintlayout::core;
    setUp();
    Widgets W;
        auto* rootWidget = W.make<ConstraintWidgetContainer>(0, 0, 600, 400);
        auto* previous = W.make<ConstraintWidget>(0, 0, 100, 20);
        rootWidget->add(previous);
        for (int i = 0; i < 100; i++) {
            auto* w = W.make<ConstraintWidget>(0, 0, 100, 20);
            w->connect(ConstraintAnchor::Type::LEFT, previous, ConstraintAnchor::Type::RIGHT, 20);
            w->connect(ConstraintAnchor::Type::RIGHT, rootWidget, ConstraintAnchor::Type::RIGHT, 20);
            rootWidget->add(w);
        }
        rootWidget->layout();
}

TEST(ClWidgetsPositioning_testWidgetCenterPositioning) {
    using namespace kiln::constraintlayout::core;
    setUp();
    Widgets W;
        int x = 20;
        int y = 30;
        auto* rootWidget = W.make<ConstraintWidget>(x, y, 600, 400);
        auto* centeredWidget = W.make<ConstraintWidget>(100, 20);
        std::vector<ConstraintWidget*> widgets;
        centeredWidget->resetSolverVariables(&sLS->getCache());
        rootWidget->resetSolverVariables(&sLS->getCache());
        widgets.push_back(centeredWidget);
        widgets.push_back(rootWidget);
        centeredWidget->setDebugName("A");
        rootWidget->setDebugName("Root");
        centeredWidget->connect(ConstraintAnchor::Type::CENTER_X,
                rootWidget, ConstraintAnchor::Type::CENTER_X);
        centeredWidget->connect(ConstraintAnchor::Type::CENTER_Y,
                rootWidget, ConstraintAnchor::Type::CENTER_Y);
        runTestOnWidgets(widgets, [&] {
                int left = centeredWidget->getLeft();
                int top = centeredWidget->getTop();
                int right = centeredWidget->getRight();
                int bottom = centeredWidget->getBottom();
                CHECK_EQ(left, x + 250);
                CHECK_EQ(right, x + 350);
                CHECK_EQ(top, y + 190);
                CHECK_EQ(bottom, y + 210);
            });
}

TEST(ClWidgetsPositioning_testBaselinePositioning) {
    using namespace kiln::constraintlayout::core;
    setUp();
    Widgets W;
        auto* a = W.make<ConstraintWidget>(20, 230, 200, 70);
        auto* b = W.make<ConstraintWidget>(200, 60, 200, 100);
        std::vector<ConstraintWidget*> widgets;
        widgets.push_back(a);
        widgets.push_back(b);
        a->setDebugName("A");
        b->setDebugName("B");
        a->setBaselineDistance(40);
        b->setBaselineDistance(60);
        b->connect(ConstraintAnchor::Type::BASELINE, a, ConstraintAnchor::Type::BASELINE);
        auto* root = W.make<ConstraintWidgetContainer>(0, 0, 1000, 1000);
        root->setDebugName("root");
        root->add(a);
        root->add(b);
        root->layout();
        CHECK_EQ(b->getTop() + b->getBaselineDistance(), a->getTop() + a->getBaselineDistance());
        runTestOnWidgets(widgets, [&] {
                CHECK_EQ(b->getTop() + b->getBaselineDistance(), a->getTop() + a->getBaselineDistance());
            });
}

TEST(ClWidgetsPositioning_testWidgetTopRightPositioning) {
    using namespace kiln::constraintlayout::core;
    setUp();
    Widgets W;
        int numLoops = 10;
        int numWidgets = 100;
        for (int j = 0; j < numLoops; j++) {
            sLS->reset();
            std::vector<ConstraintWidget*> widgets;
            int w = 100 + j;
            int h = 20 + j;
            auto* first = W.make<ConstraintWidget>(w, h);
            widgets.push_back(first);
            ConstraintWidget* previous = first;
            int margin = 20;
            for (int i = 0; i < numWidgets; i++) {
                auto* widget = W.make<ConstraintWidget>(w, h);
                widget->connect(ConstraintAnchor::Type::LEFT,
                        previous, ConstraintAnchor::Type::RIGHT, margin);
                widget->connect(ConstraintAnchor::Type::TOP,
                        previous, ConstraintAnchor::Type::BOTTOM, margin);
                widgets.push_back(widget);
                previous = widget;
            }
            for (auto* widget : widgets) {
                widget->addToSolver(*sLS, mOptimize);
            }
            sLS->minimize();
            for (int i = 0; i < int(widgets.size()); i++) {
                ConstraintWidget* widget = widgets[std::size_t(i)];
                widget->updateFromSolver(*sLS, mOptimize);
                int left = widget->getLeft();
                int top = widget->getTop();
                int right = widget->getRight();
                int bottom = widget->getBottom();
                CHECK_EQ(left, i * (w + margin));
                CHECK_EQ(right, i * (w + margin) + w);
                CHECK_EQ(top, i * (h + margin));
                CHECK_EQ(bottom, i * (h + margin) + h);
            }
        }
}

TEST(ClWidgetsPositioning_testWrapSimpleWrapContent) {
    using namespace kiln::constraintlayout::core;
    setUp();
    Widgets W;
        auto* root = W.make<ConstraintWidgetContainer>(0, 0, 1000, 1000);
        auto* a = W.make<ConstraintWidget>(0, 0, 200, 20);
        std::vector<ConstraintWidget*> widgets;
        widgets.push_back(root);
        widgets.push_back(a);
        root->setDebugSolverName(*sLS, "root");
        a->setDebugSolverName(*sLS, "A");
        root->add(a);
        a->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT);
        a->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP);
        a->connect(ConstraintAnchor::Type::RIGHT, root, ConstraintAnchor::Type::RIGHT);
        a->connect(ConstraintAnchor::Type::BOTTOM, root, ConstraintAnchor::Type::BOTTOM);
        root->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::WRAP_CONTENT);
        root->setVerticalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::WRAP_CONTENT);
        runTestOnWidgets(widgets, [&] {
                CHECK_EQ(root->getWidth(), a->getWidth());
                CHECK_EQ(root->getHeight(), a->getHeight());
                CHECK_EQ(a->getWidth(), 200);
                CHECK_EQ(a->getHeight(), 20);
            });
}

TEST(ClWidgetsPositioning_testMatchConstraint) {
    using namespace kiln::constraintlayout::core;
    setUp();
    Widgets W;
        auto* root = W.make<ConstraintWidgetContainer>(50, 50, 500, 500);
        auto* a = W.make<ConstraintWidget>(10, 20, 100, 30);
        auto* b = W.make<ConstraintWidget>(150, 200, 100, 30);
        auto* c = W.make<ConstraintWidget>(50, 50);
        std::vector<ConstraintWidget*> widgets;
        a->setDebugName("A");
        b->setDebugName("B");
        c->setDebugName("C");
        root->setDebugName("root");
        root->add(a);
        root->add(b);
        root->add(c);
        widgets.push_back(root);
        widgets.push_back(a);
        widgets.push_back(b);
        widgets.push_back(c);
        c->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::MATCH_CONSTRAINT);
        c->setVerticalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::MATCH_CONSTRAINT);
        c->connect(ConstraintAnchor::Type::LEFT, a, ConstraintAnchor::Type::RIGHT);
        c->connect(ConstraintAnchor::Type::RIGHT, b, ConstraintAnchor::Type::LEFT);
        c->connect(ConstraintAnchor::Type::TOP, a, ConstraintAnchor::Type::BOTTOM);
        c->connect(ConstraintAnchor::Type::BOTTOM, b, ConstraintAnchor::Type::TOP);
        runTestOnWidgets(widgets, [&] {
                CHECK_EQ(c->getX(), a->getRight());
                CHECK_EQ(c->getRight(), b->getX());
                CHECK_EQ(c->getY(), a->getBottom());
                CHECK_EQ(c->getBottom(), b->getY());
            });
}

TEST(ClWidgetsPositioning_testWidgetPositionMove) {
    using namespace kiln::constraintlayout::core;
    setUp();
    Widgets W;
        auto* a = W.make<ConstraintWidget>(0, 0, 100, 20);
        auto* b = W.make<ConstraintWidget>(0, 30, 200, 20);
        auto* c = W.make<ConstraintWidget>(0, 60, 100, 20);
        std::vector<ConstraintWidget*> widgets;
        widgets.push_back(a);
        widgets.push_back(b);
        widgets.push_back(c);
        a->setDebugSolverName(*sLS, "A");
        b->setDebugSolverName(*sLS, "B");
        c->setDebugSolverName(*sLS, "C");
        b->connect(ConstraintAnchor::Type::LEFT, a, ConstraintAnchor::Type::RIGHT);
        c->setOrigin(200, 0);
        b->connect(ConstraintAnchor::Type::RIGHT, c, ConstraintAnchor::Type::RIGHT);
        std::function<void()> check = [&] {
                CHECK_EQ(a->getWidth(), 100);
                CHECK_EQ(b->getWidth(), 200);
                CHECK_EQ(c->getWidth(), 100);
            };
        runTestOnWidgets(widgets, check);
        c->setOrigin(100, 0);
        runTestOnWidgets(widgets, check);
        c->setOrigin(50, 0);
        runTestOnWidgets(widgets, check);
}

TEST(ClWidgetsPositioning_testWrapProblem) {
    using namespace kiln::constraintlayout::core;
    setUp();
    Widgets W;
        auto* root = W.make<ConstraintWidgetContainer>(400, 400);
        auto* a = W.make<ConstraintWidget>(80, 300);
        auto* b = W.make<ConstraintWidget>(250, 80);
        std::vector<ConstraintWidget*> widgets;
        widgets.push_back(root);
        widgets.push_back(b);
        widgets.push_back(a);
        a->setParent(root);
        b->setParent(root);
        root->setDebugSolverName(*sLS, "root");
        a->setDebugSolverName(*sLS, "A");
        b->setDebugSolverName(*sLS, "B");
        a->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP);
        a->connect(ConstraintAnchor::Type::BOTTOM, root, ConstraintAnchor::Type::BOTTOM);
        b->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP);
        b->connect(ConstraintAnchor::Type::BOTTOM, root, ConstraintAnchor::Type::BOTTOM);
        root->setVerticalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::WRAP_CONTENT);
        runTestOnWidgets(widgets, [&] {
                CHECK_EQ(a->getWidth(), 80);
                CHECK_EQ(a->getHeight(), 300);
                CHECK_EQ(b->getWidth(), 250);
                CHECK_EQ(b->getHeight(), 80);
                CHECK_EQ(a->getY(), 0);
                CHECK_EQ(b->getY(), 110);
            });
}

TEST(ClWidgetsPositioning_testGuideline) {
    using namespace kiln::constraintlayout::core;
    setUp();
    Widgets W;
        auto* root = W.make<ConstraintWidgetContainer>(400, 400);
        auto* a = W.make<ConstraintWidget>(100, 20);
        auto* guideline = W.make<Guideline>();
        root->add(guideline);
        root->add(a);
        guideline->setGuidePercent(0.50f);
        guideline->setOrientation(Guideline::VERTICAL);
        root->setDebugName("root");
        a->setDebugName("A");
        guideline->setDebugName("guideline");
        std::vector<ConstraintWidget*> widgets;
        widgets.push_back(root);
        widgets.push_back(a);
        widgets.push_back(guideline);
        a->connect(ConstraintAnchor::Type::LEFT, guideline, ConstraintAnchor::Type::LEFT);
        std::function<void()> check = [&] {
                CHECK_EQ(a->getWidth(), 100);
                CHECK_EQ(a->getHeight(), 20);
                CHECK_EQ(a->getX(), 200);
            };
        runTest(root, check);
        guideline->setGuidePercent(0);
        runTest(root, [&] {
                CHECK_EQ(a->getWidth(), 100);
                CHECK_EQ(a->getHeight(), 20);
                CHECK_EQ(a->getX(), 0);
            });
        guideline->setGuideBegin(150);
        runTest(root, [&] {
                CHECK_EQ(a->getWidth(), 100);
                CHECK_EQ(a->getHeight(), 20);
                CHECK_EQ(a->getX(), 150);
            });
        guideline->setGuideEnd(150);
        runTest(root, [&] {
                CHECK_EQ(a->getWidth(), 100);
                CHECK_EQ(a->getHeight(), 20);
                CHECK_EQ(a->getX(), 250);
            });
        guideline->setOrientation(Guideline::HORIZONTAL);
        a->resetAnchors();
        a->connect(ConstraintAnchor::Type::TOP, guideline, ConstraintAnchor::Type::TOP);
        guideline->setGuideBegin(150);
        runTest(root, [&] {
                CHECK_EQ(a->getWidth(), 100);
                CHECK_EQ(a->getHeight(), 20);
                CHECK_EQ(a->getY(), 150);
            });
        a->resetAnchors();
        a->connect(ConstraintAnchor::Type::TOP, guideline, ConstraintAnchor::Type::BOTTOM);
        runTest(root, [&] {
                CHECK_EQ(a->getWidth(), 100);
                CHECK_EQ(a->getHeight(), 20);
                CHECK_EQ(a->getY(), 150);
            });
}

TEST(ClWidgetsPositioning_testGuidelinePosition) {
    using namespace kiln::constraintlayout::core;
    setUp();
    Widgets W;
        auto* root = W.make<ConstraintWidgetContainer>(800, 400);
        auto* a = W.make<ConstraintWidget>(100, 20);
        auto* b = W.make<ConstraintWidget>(100, 20);
        auto* guideline = W.make<Guideline>();
        root->add(guideline);
        root->add(a);
        root->add(b);
        guideline->setGuidePercent(0.651f);
        guideline->setOrientation(Guideline::VERTICAL);
        root->setDebugSolverName(*sLS, "root");
        a->setDebugSolverName(*sLS, "A");
        b->setDebugSolverName(*sLS, "B");
        guideline->setDebugSolverName(*sLS, "guideline");
        std::vector<ConstraintWidget*> widgets;
        widgets.push_back(root);
        widgets.push_back(a);
        widgets.push_back(b);
        widgets.push_back(guideline);
        a->connect(ConstraintAnchor::Type::LEFT, guideline, ConstraintAnchor::Type::RIGHT);
        b->connect(ConstraintAnchor::Type::RIGHT, guideline, ConstraintAnchor::Type::RIGHT);
        std::function<void()> check = [&] {
                CHECK_EQ(a->getWidth(), 100);
                CHECK_EQ(a->getHeight(), 20);
                CHECK_EQ(a->getX(), 521);
                CHECK_EQ(b->getRight(), 521);
            };
        runTestOnWidgets(widgets, check);
}

TEST(ClWidgetsPositioning_testWidgetInfeasiblePosition) {
    using namespace kiln::constraintlayout::core;
    setUp();
    Widgets W;
        auto* a = W.make<ConstraintWidget>(100, 20);
        auto* b = W.make<ConstraintWidget>(100, 20);
        std::vector<ConstraintWidget*> widgets;
        widgets.push_back(b);
        widgets.push_back(a);
        a->resetSolverVariables(&sLS->getCache());
        b->resetSolverVariables(&sLS->getCache());
        a->connect(ConstraintAnchor::Type::RIGHT, b, ConstraintAnchor::Type::LEFT);
        b->connect(ConstraintAnchor::Type::RIGHT, a, ConstraintAnchor::Type::LEFT);
        runTestOnWidgets(widgets, [&] {
            });
}

TEST(ClWidgetsPositioning_testWidgetMultipleDependentPositioning) {
    using namespace kiln::constraintlayout::core;
    setUp();
    Widgets W;
        auto* root = W.make<ConstraintWidget>(400, 400);
        auto* a = W.make<ConstraintWidget>(100, 20);
        auto* b = W.make<ConstraintWidget>(100, 20);
        root->setDebugSolverName(*sLS, "root");
        a->setDebugSolverName(*sLS, "A");
        b->setDebugSolverName(*sLS, "B");
        std::vector<ConstraintWidget*> widgets;
        widgets.push_back(root);
        widgets.push_back(b);
        widgets.push_back(a);
        a->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP, 10);
        a->connect(ConstraintAnchor::Type::BOTTOM, root, ConstraintAnchor::Type::BOTTOM, 10);
        b->connect(ConstraintAnchor::Type::TOP, a, ConstraintAnchor::Type::BOTTOM);
        b->connect(ConstraintAnchor::Type::BOTTOM, root, ConstraintAnchor::Type::BOTTOM);
        root->resetSolverVariables(&sLS->getCache());
        a->resetSolverVariables(&sLS->getCache());
        b->resetSolverVariables(&sLS->getCache());
        runTestOnWidgets(widgets, [&] {
                CHECK_EQ(root->getHeight(), 400);
                CHECK_EQ(root->getHeight(), 400);
                CHECK_EQ(a->getHeight(), 20);
                CHECK_EQ(b->getHeight(), 20);
                CHECK_EQ(a->getTop() - root->getTop(), root->getBottom() - a->getBottom());
                CHECK_EQ(b->getTop() - a->getBottom(), root->getBottom() - b->getBottom());
            });
}

TEST(ClWidgetsPositioning_testMinSize) {
    using namespace kiln::constraintlayout::core;
    setUp();
    Widgets W;
        auto* root = W.make<ConstraintWidgetContainer>(600, 400);
        auto* a = W.make<ConstraintWidget>(100, 20);
        a->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT);
        a->connect(ConstraintAnchor::Type::RIGHT, root, ConstraintAnchor::Type::RIGHT);
        a->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP);
        a->connect(ConstraintAnchor::Type::BOTTOM, root, ConstraintAnchor::Type::BOTTOM);
        root->setDebugName("root");
        a->setDebugName("A");
        root->add(a);
        root->setOptimizationLevel(0);
        root->layout();
        CHECK_EQ(root->getWidth(), 600);
        CHECK_EQ(root->getHeight(), 400);
        CHECK_EQ(a->getWidth(), 100);
        CHECK_EQ(a->getHeight(), 20);
        CHECK_EQ(a->getLeft() - root->getLeft(), root->getRight() - a->getRight());
        CHECK_EQ(a->getTop() - root->getTop(), root->getBottom() - a->getBottom());
        root->setVerticalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::WRAP_CONTENT);
        root->layout();
        CHECK_EQ(root->getHeight(), a->getHeight());
        CHECK_EQ(a->getWidth(), 100);
        CHECK_EQ(a->getHeight(), 20);
        CHECK_EQ(a->getLeft() - root->getLeft(), root->getRight() - a->getRight());
        CHECK_EQ(a->getTop() - root->getTop(), root->getBottom() - a->getBottom());
        root->setMinHeight(200);
        root->layout();
        CHECK_EQ(root->getHeight(), 200);
        CHECK_EQ(a->getWidth(), 100);
        CHECK_EQ(a->getHeight(), 20);
        CHECK_EQ(a->getLeft() - root->getLeft(), root->getRight() - a->getRight());
        CHECK_EQ(a->getTop() - root->getTop(), root->getBottom() - a->getBottom());
        root->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::WRAP_CONTENT);
        root->layout();
        CHECK_EQ(root->getWidth(), a->getWidth());
        CHECK_EQ(root->getHeight(), 200);
        CHECK_EQ(a->getWidth(), 100);
        CHECK_EQ(a->getHeight(), 20);
        CHECK_EQ(a->getLeft() - root->getLeft(), root->getRight() - a->getRight());
        CHECK_EQ(a->getTop() - root->getTop(), root->getBottom() - a->getBottom());
        root->setMinWidth(300);
        root->layout();
        CHECK_EQ(root->getWidth(), 300);
        CHECK_EQ(root->getHeight(), 200);
        CHECK_EQ(a->getWidth(), 100);
        CHECK_EQ(a->getHeight(), 20);
        CHECK_EQ(a->getLeft() - root->getLeft(), root->getRight() - a->getRight());
        CHECK_EQ(a->getTop() - root->getTop(), root->getBottom() - a->getBottom());
}

