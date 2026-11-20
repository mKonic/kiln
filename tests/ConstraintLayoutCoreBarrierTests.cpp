// Ported from constraintlayout-core BarrierTest.java (androidx), by tools in
// the scratchpad, then reviewed. Expected values are upstream's.
#include "ConstraintLayoutTestSupport.h"

namespace {
using namespace kiln::constraintlayout::core;
}

TEST(ClBarrier_barrierConstrainedWidth) {
    using namespace kiln::constraintlayout::core;
    Widgets W;
        auto* root = W.make<ConstraintWidgetContainer>(0, 0, 800, 600);
        auto* a = W.make<ConstraintWidget>(100, 20);
        auto* b = W.make<ConstraintWidget>(200, 20);
        auto* barrier = W.make<Barrier>();
        auto* guidelineStart = W.make<Guideline>();
        auto* guidelineEnd = W.make<Guideline>();
        guidelineStart->setOrientation(ConstraintWidget::VERTICAL);
        guidelineEnd->setOrientation(ConstraintWidget::VERTICAL);
        guidelineStart->setGuideBegin(30);
        guidelineEnd->setGuideEnd(20);
        root->setDebugSolverName(root->getSystem(), "root");
        guidelineStart->setDebugSolverName(root->getSystem(), "guidelineStart");
        guidelineEnd->setDebugSolverName(root->getSystem(), "guidelineEnd");
        a->setDebugSolverName(root->getSystem(), "A");
        b->setDebugSolverName(root->getSystem(), "B");
        barrier->setDebugSolverName(root->getSystem(), "Barrier");
        barrier->setBarrierType(Barrier::LEFT);
        barrier->add(a);
        barrier->add(b);
        root->add(a);
        root->add(b);
        root->add(guidelineStart);
        root->add(guidelineEnd);
        root->add(barrier);
        a->connect(ConstraintAnchor::Type::LEFT, guidelineStart, ConstraintAnchor::Type::LEFT);
        a->connect(ConstraintAnchor::Type::RIGHT, guidelineEnd, ConstraintAnchor::Type::RIGHT);
        b->connect(ConstraintAnchor::Type::LEFT, guidelineStart, ConstraintAnchor::Type::LEFT);
        b->connect(ConstraintAnchor::Type::RIGHT, guidelineEnd, ConstraintAnchor::Type::RIGHT);
        a->setHorizontalBiasPercent(1);
        b->setHorizontalBiasPercent(1);
        root->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::WRAP_CONTENT);
        root->layout();
        CHECK_EQ(root->getWidth(), 250);
        CHECK_EQ(guidelineStart->getLeft(), 30);
        CHECK_EQ(guidelineEnd->getLeft(), 230);
        CHECK_EQ(a->getLeft(), 130);
        CHECK_EQ(a->getWidth(), 100);
        CHECK_EQ(b->getLeft(), 30);
        CHECK_EQ(b->getWidth(), 200);
        CHECK_EQ(barrier->getLeft(), 30);
}

TEST(ClBarrier_barrierImage) {
    using namespace kiln::constraintlayout::core;
    Widgets W;
        auto* root = W.make<ConstraintWidgetContainer>(0, 0, 800, 600);
        auto* a = W.make<ConstraintWidget>(100, 20);
        auto* b = W.make<ConstraintWidget>(200, 20);
        auto* c = W.make<ConstraintWidget>(60, 60);
        auto* barrier = W.make<Barrier>();
        root->setDebugSolverName(root->getSystem(), "root");
        a->setDebugSolverName(root->getSystem(), "A");
        b->setDebugSolverName(root->getSystem(), "B");
        c->setDebugSolverName(root->getSystem(), "C");
        barrier->setDebugSolverName(root->getSystem(), "Barrier");
        barrier->setBarrierType(Barrier::RIGHT);
        barrier->add(a);
        barrier->add(b);
        root->add(a);
        root->add(b);
        root->add(c);
        root->add(barrier);
        a->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT);
        a->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP);
        a->connect(ConstraintAnchor::Type::BOTTOM, b, ConstraintAnchor::Type::TOP);
        b->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT);
        b->connect(ConstraintAnchor::Type::TOP, a, ConstraintAnchor::Type::BOTTOM);
        b->connect(ConstraintAnchor::Type::BOTTOM, root, ConstraintAnchor::Type::BOTTOM);
        a->setVerticalChainStyle(ConstraintWidget::CHAIN_SPREAD_INSIDE);
        c->setHorizontalBiasPercent(1);
        c->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP);
        c->connect(ConstraintAnchor::Type::BOTTOM, root, ConstraintAnchor::Type::BOTTOM);
        c->connect(ConstraintAnchor::Type::LEFT, barrier, ConstraintAnchor::Type::RIGHT);
        c->connect(ConstraintAnchor::Type::RIGHT, root, ConstraintAnchor::Type::RIGHT);
        root->setOptimizationLevel(Optimizer::OPTIMIZATION_STANDARD);
        root->layout();
        CHECK_EQ(a->getLeft(), 0);
        CHECK_EQ(a->getTop(), 0);
        CHECK_EQ(b->getLeft(), 0);
        CHECK_EQ(b->getTop(), 580);
        CHECK_EQ(c->getLeft(), 740);
        CHECK_EQ(c->getTop(), 270);
        CHECK_EQ(barrier->getLeft(), 200);
}

TEST(ClBarrier_barrierTooStrong) {
    using namespace kiln::constraintlayout::core;
    Widgets W;
        auto* root = W.make<ConstraintWidgetContainer>(0, 0, 800, 600);
        auto* a = W.make<ConstraintWidget>(60, 60);
        auto* b = W.make<ConstraintWidget>(100, 200);
        auto* c = W.make<ConstraintWidget>(100, 20);
        auto* barrier = W.make<Barrier>();
        root->setDebugSolverName(root->getSystem(), "root");
        a->setDebugSolverName(root->getSystem(), "A");
        b->setDebugSolverName(root->getSystem(), "B");
        c->setDebugSolverName(root->getSystem(), "C");
        barrier->setDebugSolverName(root->getSystem(), "Barrier");
        barrier->setBarrierType(Barrier::BOTTOM);
        barrier->add(b);
        root->add(a);
        root->add(b);
        root->add(c);
        root->add(barrier);
        a->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP);
        a->connect(ConstraintAnchor::Type::RIGHT, root, ConstraintAnchor::Type::RIGHT);
        b->connect(ConstraintAnchor::Type::TOP, c, ConstraintAnchor::Type::BOTTOM);
        b->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::MATCH_PARENT);
        c->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::MATCH_PARENT);
        c->setVerticalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::MATCH_CONSTRAINT);
        c->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP);
        c->connect(ConstraintAnchor::Type::BOTTOM, a, ConstraintAnchor::Type::BOTTOM);
        root->setOptimizationLevel(Optimizer::OPTIMIZATION_NONE);
        root->layout();
        CHECK_EQ(a->getLeft(), 740);
        CHECK_EQ(a->getTop(), 0);
        CHECK_EQ(b->getLeft(), 0);
        CHECK_EQ(b->getTop(), 60);
        CHECK_EQ(b->getWidth(), 800);
        CHECK_EQ(b->getHeight(), 200);
        CHECK_EQ(c->getLeft(), 0);
        CHECK_EQ(c->getTop(), 0);
        CHECK_EQ(c->getWidth(), 800);
        CHECK_EQ(c->getHeight(), 60);
        CHECK_EQ(barrier->getBottom(), 260);
}

TEST(ClBarrier_barrierMax) {
    using namespace kiln::constraintlayout::core;
    Widgets W;
        auto* root = W.make<ConstraintWidgetContainer>(0, 0, 800, 600);
        auto* a = W.make<ConstraintWidget>(100, 20);
        auto* b = W.make<ConstraintWidget>(150, 20);
        auto* barrier = W.make<Barrier>();
        root->setDebugSolverName(root->getSystem(), "root");
        a->setDebugSolverName(root->getSystem(), "A");
        b->setDebugSolverName(root->getSystem(), "B");
        barrier->setDebugSolverName(root->getSystem(), "Barrier");
        barrier->add(a);
        root->add(a);
        root->add(barrier);
        root->add(b);
        barrier->setBarrierType(Barrier::RIGHT);
        a->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT);
        a->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP);
        b->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP);
        b->connect(ConstraintAnchor::Type::LEFT, barrier, ConstraintAnchor::Type::LEFT);
        b->connect(ConstraintAnchor::Type::RIGHT, root, ConstraintAnchor::Type::RIGHT);
        b->setHorizontalBiasPercent(0);
        b->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::MATCH_CONSTRAINT);
        b->setHorizontalMatchStyle(ConstraintWidget::MATCH_CONSTRAINT_SPREAD, 0, 150, 1);
        root->setOptimizationLevel(Optimizer::OPTIMIZATION_NONE);
        root->layout();
        CHECK_EQ(a->getLeft(), 0);
        CHECK_EQ(barrier->getLeft(), 100);
        CHECK_EQ(b->getLeft(), 100);
        CHECK_EQ(b->getWidth(), 150);
}

TEST(ClBarrier_barrierCenter) {
    using namespace kiln::constraintlayout::core;
    Widgets W;
        auto* root = W.make<ConstraintWidgetContainer>(0, 0, 800, 600);
        auto* a = W.make<ConstraintWidget>(100, 20);
        auto* barrier = W.make<Barrier>();
        root->setDebugSolverName(root->getSystem(), "root");
        a->setDebugSolverName(root->getSystem(), "A");
        barrier->setDebugSolverName(root->getSystem(), "Barrier");
        barrier->add(a);
        root->add(a);
        root->add(barrier);
        barrier->setBarrierType(Barrier::RIGHT);
        a->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT, 10);
        a->connect(ConstraintAnchor::Type::RIGHT, barrier, ConstraintAnchor::Type::RIGHT, 30);
        a->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP);
        root->layout();
        CHECK_EQ(a->getLeft(), 10);
        CHECK_EQ(barrier->getLeft(), 140);
}

TEST(ClBarrier_barrierCenter2) {
    using namespace kiln::constraintlayout::core;
    Widgets W;
        auto* root = W.make<ConstraintWidgetContainer>(0, 0, 800, 600);
        auto* a = W.make<ConstraintWidget>(100, 20);
        auto* barrier = W.make<Barrier>();
        root->setDebugSolverName(root->getSystem(), "root");
        a->setDebugSolverName(root->getSystem(), "A");
        barrier->setDebugSolverName(root->getSystem(), "Barrier");
        barrier->add(a);
        root->add(a);
        root->add(barrier);
        barrier->setBarrierType(Barrier::LEFT);
        a->connect(ConstraintAnchor::Type::RIGHT, root, ConstraintAnchor::Type::RIGHT, 10);
        a->connect(ConstraintAnchor::Type::LEFT, barrier, ConstraintAnchor::Type::LEFT, 30);
        a->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP);
        root->layout();
        CHECK_EQ(a->getRight(), root->getWidth() - 10);
        CHECK_EQ(barrier->getLeft(), a->getLeft() - 30);
}

TEST(ClBarrier_barrierCenter3) {
    using namespace kiln::constraintlayout::core;
    Widgets W;
        auto* root = W.make<ConstraintWidgetContainer>(0, 0, 800, 600);
        auto* a = W.make<ConstraintWidget>(100, 20);
        auto* b = W.make<ConstraintWidget>(100, 20);
        auto* barrier = W.make<Barrier>();
        root->setDebugSolverName(root->getSystem(), "root");
        a->setDebugSolverName(root->getSystem(), "A");
        b->setDebugSolverName(root->getSystem(), "B");
        barrier->setDebugSolverName(root->getSystem(), "Barrier");
        barrier->add(a);
        barrier->add(b);
        root->add(a);
        root->add(b);
        root->add(barrier);
        barrier->setBarrierType(Barrier::LEFT);
        a->connect(ConstraintAnchor::Type::RIGHT, root, ConstraintAnchor::Type::RIGHT);
        a->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT);
        a->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP);
        b->connect(ConstraintAnchor::Type::RIGHT, root, ConstraintAnchor::Type::RIGHT);
        b->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT);
        b->connect(ConstraintAnchor::Type::TOP, a, ConstraintAnchor::Type::BOTTOM);
        a->setWidth(100);
        b->setWidth(200);
        a->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::MATCH_CONSTRAINT);
        a->setHorizontalMatchStyle(ConstraintWidget::MATCH_CONSTRAINT_WRAP, 0, 0, 0);
        a->setHorizontalBiasPercent(1);
        b->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::MATCH_CONSTRAINT);
        b->setHorizontalMatchStyle(ConstraintWidget::MATCH_CONSTRAINT_WRAP, 0, 0, 0);
        b->setHorizontalBiasPercent(1);
        root->layout();
        CHECK_EQ(a->getWidth(), 100);
        CHECK_EQ(b->getWidth(), 200);
        CHECK_EQ(barrier->getLeft(), b->getLeft());
}

TEST(ClBarrier_barrierCenter4) {
    using namespace kiln::constraintlayout::core;
    Widgets W;
        auto* root = W.make<ConstraintWidgetContainer>(0, 0, 800, 600);
        auto* a = W.make<ConstraintWidget>(150, 20);
        auto* b = W.make<ConstraintWidget>(100, 20);
        auto* barrier = W.make<Barrier>();
        root->setDebugSolverName(root->getSystem(), "root");
        a->setDebugSolverName(root->getSystem(), "A");
        b->setDebugSolverName(root->getSystem(), "B");
        barrier->setDebugSolverName(root->getSystem(), "Barrier");
        barrier->add(a);
        barrier->add(b);
        root->add(a);
        root->add(b);
        root->add(barrier);
        barrier->setBarrierType(Barrier::LEFT);
        a->connect(ConstraintAnchor::Type::RIGHT, root, ConstraintAnchor::Type::RIGHT);
        a->connect(ConstraintAnchor::Type::LEFT, barrier, ConstraintAnchor::Type::LEFT);
        a->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP);
        b->connect(ConstraintAnchor::Type::RIGHT, root, ConstraintAnchor::Type::RIGHT);
        b->connect(ConstraintAnchor::Type::LEFT, barrier, ConstraintAnchor::Type::LEFT);
        b->connect(ConstraintAnchor::Type::TOP, a, ConstraintAnchor::Type::BOTTOM);
        a->setHorizontalBiasPercent(0);
        b->setHorizontalBiasPercent(0);
        root->layout();
        CHECK_EQ(a->getRight(), root->getWidth());
        CHECK_EQ(barrier->getLeft(), std::min(a->getLeft(), b->getLeft()));
        CHECK_EQ(a->getLeft(), barrier->getLeft());
        CHECK_EQ(b->getLeft(), barrier->getLeft());
}

TEST(ClBarrier_barrierCenter5) {
    using namespace kiln::constraintlayout::core;
    Widgets W;
        auto* root = W.make<ConstraintWidgetContainer>(0, 0, 800, 600);
        auto* a = W.make<ConstraintWidget>(100, 20);
        auto* b = W.make<ConstraintWidget>(150, 20);
        auto* c = W.make<ConstraintWidget>(200, 20);
        auto* barrier = W.make<Barrier>();
        root->setDebugSolverName(root->getSystem(), "root");
        a->setDebugSolverName(root->getSystem(), "A");
        b->setDebugSolverName(root->getSystem(), "B");
        c->setDebugSolverName(root->getSystem(), "C");
        barrier->setDebugSolverName(root->getSystem(), "Barrier");
        barrier->add(a);
        barrier->add(b);
        barrier->add(c);
        root->add(a);
        root->add(b);
        root->add(c);
        root->add(barrier);
        barrier->setBarrierType(Barrier::RIGHT);
        a->connect(ConstraintAnchor::Type::RIGHT, barrier, ConstraintAnchor::Type::RIGHT);
        a->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT);
        a->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP);
        b->connect(ConstraintAnchor::Type::RIGHT, barrier, ConstraintAnchor::Type::RIGHT);
        b->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT);
        b->connect(ConstraintAnchor::Type::TOP, a, ConstraintAnchor::Type::BOTTOM);
        c->connect(ConstraintAnchor::Type::RIGHT, barrier, ConstraintAnchor::Type::RIGHT);
        c->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT);
        c->connect(ConstraintAnchor::Type::TOP, b, ConstraintAnchor::Type::BOTTOM);
        a->setHorizontalBiasPercent(0);
        b->setHorizontalBiasPercent(0);
        c->setHorizontalBiasPercent(0);
        a->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::MATCH_CONSTRAINT);
        b->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::MATCH_CONSTRAINT);
        c->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::MATCH_CONSTRAINT);
        a->setHorizontalMatchStyle(ConstraintWidget::MATCH_CONSTRAINT_WRAP, 0, 0, 0);
        b->setHorizontalMatchStyle(ConstraintWidget::MATCH_CONSTRAINT_WRAP, 0, 0, 0);
        c->setHorizontalMatchStyle(ConstraintWidget::MATCH_CONSTRAINT_WRAP, 0, 0, 0);
        root->layout();
        CHECK_EQ(barrier->getRight(), std::max(std::max(a->getRight(), b->getRight()), c->getRight()));
        CHECK_EQ(a->getWidth(), 100);
        CHECK_EQ(b->getWidth(), 150);
        CHECK_EQ(c->getWidth(), 200);
}

TEST(ClBarrier_basic) {
    using namespace kiln::constraintlayout::core;
    Widgets W;
        auto* root = W.make<ConstraintWidgetContainer>(0, 0, 800, 600);
        auto* a = W.make<ConstraintWidget>(100, 20);
        auto* b = W.make<ConstraintWidget>(150, 20);
        auto* barrier = W.make<Barrier>();
        root->setDebugSolverName(root->getSystem(), "root");
        a->setDebugSolverName(root->getSystem(), "A");
        b->setDebugSolverName(root->getSystem(), "B");
        barrier->setDebugSolverName(root->getSystem(), "Barrier");
        root->add(a);
        root->add(b);
        root->add(barrier);
        barrier->setBarrierType(Barrier::LEFT);
        a->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT);
        a->connect(ConstraintAnchor::Type::RIGHT, root, ConstraintAnchor::Type::RIGHT);
        a->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP, 50);
        b->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT);
        b->connect(ConstraintAnchor::Type::RIGHT, root, ConstraintAnchor::Type::RIGHT);
        b->connect(ConstraintAnchor::Type::TOP, a, ConstraintAnchor::Type::BOTTOM, 20);
        barrier->add(a);
        barrier->add(b);
        root->setOptimizationLevel(Optimizer::OPTIMIZATION_NONE);
        root->layout();
        CHECK_EQ(barrier->getLeft(), b->getLeft());
        barrier->setBarrierType(Barrier::RIGHT);
        root->layout();
        CHECK_EQ(barrier->getRight(), b->getRight());
        barrier->setBarrierType(Barrier::LEFT);
        b->setWidth(10);
        root->layout();
        CHECK_EQ(barrier->getLeft(), a->getLeft());
}

TEST(ClBarrier_basic2) {
    using namespace kiln::constraintlayout::core;
    Widgets W;
        auto* root = W.make<ConstraintWidgetContainer>(0, 0, 800, 600);
        auto* a = W.make<ConstraintWidget>(100, 20);
        auto* b = W.make<ConstraintWidget>(150, 20);
        auto* barrier = W.make<Barrier>();
        root->setDebugSolverName(root->getSystem(), "root");
        a->setDebugSolverName(root->getSystem(), "A");
        b->setDebugSolverName(root->getSystem(), "B");
        barrier->setDebugSolverName(root->getSystem(), "Barrier");
        root->add(a);
        root->add(b);
        root->add(barrier);
        barrier->setBarrierType(Barrier::BOTTOM);
        a->setVerticalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::MATCH_CONSTRAINT);
        a->setVerticalMatchStyle(ConstraintWidget::MATCH_CONSTRAINT_WRAP, 0, 0, 0);
        a->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT);
        a->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP);
        b->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT);
        b->connect(ConstraintAnchor::Type::TOP, barrier, ConstraintAnchor::Type::BOTTOM);
        b->connect(ConstraintAnchor::Type::BOTTOM, root, ConstraintAnchor::Type::BOTTOM);
        barrier->add(a);
        root->setOptimizationLevel(Optimizer::OPTIMIZATION_NONE);
        root->layout();
        CHECK_EQ(barrier->getTop(), a->getBottom());
        float actual = barrier->getBottom()
                + (root->getBottom() - barrier->getBottom() - b->getHeight()) / 2.0f;
        CHECK_NEAR((float) b->getTop(), actual, 1.0f);
}

TEST(ClBarrier_basic3) {
    using namespace kiln::constraintlayout::core;
    Widgets W;
        auto* root = W.make<ConstraintWidgetContainer>(0, 0, 800, 600);
        auto* a = W.make<ConstraintWidget>(100, 20);
        auto* b = W.make<ConstraintWidget>(150, 20);
        auto* barrier = W.make<Barrier>();
        root->setDebugSolverName(root->getSystem(), "root");
        a->setDebugSolverName(root->getSystem(), "A");
        b->setDebugSolverName(root->getSystem(), "B");
        barrier->setDebugSolverName(root->getSystem(), "Barrier");
        root->add(a);
        root->add(b);
        root->add(barrier);
        barrier->setBarrierType(Barrier::RIGHT);
        a->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT);
        a->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP);
        b->connect(ConstraintAnchor::Type::LEFT, barrier, ConstraintAnchor::Type::LEFT);
        b->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP);
        b->connect(ConstraintAnchor::Type::RIGHT, root, ConstraintAnchor::Type::RIGHT);
        barrier->add(a);
        root->setOptimizationLevel(Optimizer::OPTIMIZATION_NONE);
        root->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::WRAP_CONTENT);
        root->layout();
        CHECK_EQ(barrier->getRight(), a->getRight());
        CHECK_EQ(root->getWidth(), a->getWidth() + b->getWidth());
}

TEST(ClBarrier_basic4) {
    using namespace kiln::constraintlayout::core;
    Widgets W;
        auto* root = W.make<ConstraintWidgetContainer>(0, 0, 800, 600);
        auto* a = W.make<ConstraintWidget>(100, 20);
        auto* b = W.make<ConstraintWidget>(100, 20);
        auto* c = W.make<ConstraintWidget>(100, 20);
        auto* barrier = W.make<Barrier>();
        root->setDebugSolverName(root->getSystem(), "root");
        a->setDebugSolverName(root->getSystem(), "A");
        b->setDebugSolverName(root->getSystem(), "B");
        c->setDebugSolverName(root->getSystem(), "C");
        barrier->setDebugSolverName(root->getSystem(), "Barrier");
        root->add(a);
        root->add(b);
        root->add(c);
        root->add(barrier);
        a->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT);
        a->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP);
        b->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT);
        b->connect(ConstraintAnchor::Type::TOP, a, ConstraintAnchor::Type::BOTTOM);
        b->setVisibility(ConstraintWidget::GONE);
        c->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT);
        c->connect(ConstraintAnchor::Type::TOP, barrier, ConstraintAnchor::Type::TOP);
        barrier->add(a);
        barrier->add(b);
        barrier->setBarrierType(Barrier::BOTTOM);
        root->setOptimizationLevel(Optimizer::OPTIMIZATION_NONE);
        root->layout();
        CHECK_EQ(b->getTop(), a->getBottom());
        CHECK_EQ(barrier->getTop(), b->getBottom());
        CHECK_EQ(c->getTop(), barrier->getTop());
}

TEST(ClBarrier_growArray) {
    using namespace kiln::constraintlayout::core;
    Widgets W;
        auto* root = W.make<ConstraintWidgetContainer>(0, 0, 800, 600);
        auto* a = W.make<ConstraintWidget>(100, 20);
        auto* b = W.make<ConstraintWidget>(150, 20);
        auto* c = W.make<ConstraintWidget>(175, 20);
        auto* d = W.make<ConstraintWidget>(200, 20);
        auto* e = W.make<ConstraintWidget>(125, 20);
        auto* barrier = W.make<Barrier>();
        root->setDebugSolverName(root->getSystem(), "root");
        a->setDebugSolverName(root->getSystem(), "A");
        b->setDebugSolverName(root->getSystem(), "B");
        c->setDebugSolverName(root->getSystem(), "C");
        d->setDebugSolverName(root->getSystem(), "D");
        e->setDebugSolverName(root->getSystem(), "E");
        barrier->setDebugSolverName(root->getSystem(), "Barrier");
        root->add(a);
        root->add(b);
        root->add(c);
        root->add(d);
        root->add(e);
        root->add(barrier);
        barrier->setBarrierType(Barrier::LEFT);
        a->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT);
        a->connect(ConstraintAnchor::Type::RIGHT, root, ConstraintAnchor::Type::RIGHT);
        a->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP, 50);
        b->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT);
        b->connect(ConstraintAnchor::Type::RIGHT, root, ConstraintAnchor::Type::RIGHT);
        b->connect(ConstraintAnchor::Type::TOP, a, ConstraintAnchor::Type::BOTTOM, 20);
        c->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT);
        c->connect(ConstraintAnchor::Type::RIGHT, root, ConstraintAnchor::Type::RIGHT);
        c->connect(ConstraintAnchor::Type::TOP, b, ConstraintAnchor::Type::BOTTOM, 20);
        d->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT);
        d->connect(ConstraintAnchor::Type::RIGHT, root, ConstraintAnchor::Type::RIGHT);
        d->connect(ConstraintAnchor::Type::TOP, c, ConstraintAnchor::Type::BOTTOM, 20);
        e->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT);
        e->connect(ConstraintAnchor::Type::RIGHT, root, ConstraintAnchor::Type::RIGHT);
        e->connect(ConstraintAnchor::Type::TOP, d, ConstraintAnchor::Type::BOTTOM, 20);
        barrier->add(a);
        barrier->add(b);
        barrier->add(c);
        barrier->add(d);
        barrier->add(e);
        root->layout();
        CHECK_NEAR(a->getLeft(), (root->getWidth() - a->getWidth()) / 2, 1);
        CHECK_NEAR(b->getLeft(), (root->getWidth() - b->getWidth()) / 2, 1);
        CHECK_NEAR(c->getLeft(), (root->getWidth() - c->getWidth()) / 2, 1);
        CHECK_NEAR(d->getLeft(), (root->getWidth() - d->getWidth()) / 2, 1);
        CHECK_NEAR(e->getLeft(), (root->getWidth() - e->getWidth()) / 2, 1);
        CHECK_EQ(barrier->getLeft(), d->getLeft());
}

TEST(ClBarrier_connection) {
    using namespace kiln::constraintlayout::core;
    Widgets W;
        auto* root = W.make<ConstraintWidgetContainer>(0, 0, 800, 600);
        auto* a = W.make<ConstraintWidget>(100, 20);
        auto* b = W.make<ConstraintWidget>(150, 20);
        auto* c = W.make<ConstraintWidget>(100, 20);
        auto* barrier = W.make<Barrier>();
        root->setDebugSolverName(root->getSystem(), "root");
        a->setDebugSolverName(root->getSystem(), "A");
        b->setDebugSolverName(root->getSystem(), "B");
        c->setDebugSolverName(root->getSystem(), "C");
        barrier->setDebugSolverName(root->getSystem(), "Barrier");
        root->add(a);
        root->add(b);
        root->add(c);
        root->add(barrier);
        barrier->setBarrierType(Barrier::LEFT);
        a->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT);
        a->connect(ConstraintAnchor::Type::RIGHT, root, ConstraintAnchor::Type::RIGHT);
        a->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP, 50);
        b->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT);
        b->connect(ConstraintAnchor::Type::RIGHT, root, ConstraintAnchor::Type::RIGHT);
        b->connect(ConstraintAnchor::Type::TOP, a, ConstraintAnchor::Type::BOTTOM, 20);
        c->connect(ConstraintAnchor::Type::LEFT, barrier, ConstraintAnchor::Type::LEFT, 0);
        barrier->add(a);
        barrier->add(b);
        root->layout();
        CHECK_EQ(barrier->getLeft(), b->getLeft());
        CHECK_EQ(c->getLeft(), barrier->getLeft());
}

TEST(ClBarrier_withGuideline) {
    using namespace kiln::constraintlayout::core;
    Widgets W;
        auto* root = W.make<ConstraintWidgetContainer>(0, 0, 800, 600);
        auto* a = W.make<ConstraintWidget>(100, 20);
        auto* barrier = W.make<Barrier>();
        auto* guideline = W.make<Guideline>();
        root->setDebugSolverName(root->getSystem(), "root");
        a->setDebugSolverName(root->getSystem(), "A");
        barrier->setDebugSolverName(root->getSystem(), "Barrier");
        guideline->setDebugSolverName(root->getSystem(), "Guideline");
        guideline->setOrientation(ConstraintWidget::VERTICAL);
        guideline->setGuideBegin(200);
        barrier->setBarrierType(Barrier::RIGHT);
        root->add(a);
        root->add(barrier);
        root->add(guideline);
        a->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT, 10);
        a->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP, 50);
        barrier->add(a);
        barrier->add(guideline);
        root->layout();
        CHECK_EQ(barrier->getLeft(), guideline->getLeft());
}

TEST(ClBarrier_wrapIssue) {
    using namespace kiln::constraintlayout::core;
    Widgets W;
        auto* root = W.make<ConstraintWidgetContainer>(0, 0, 800, 600);
        auto* a = W.make<ConstraintWidget>(100, 20);
        auto* b = W.make<ConstraintWidget>(100, 20);
        auto* barrier = W.make<Barrier>();
        root->setDebugSolverName(root->getSystem(), "root");
        a->setDebugSolverName(root->getSystem(), "A");
        b->setDebugSolverName(root->getSystem(), "B");
        barrier->setDebugSolverName(root->getSystem(), "Barrier");
        barrier->setBarrierType(Barrier::BOTTOM);
        root->add(a);
        root->add(b);
        root->add(barrier);
        a->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT, 0);
        a->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP, 0);
        barrier->add(a);
        b->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT, 0);
        b->connect(ConstraintAnchor::Type::TOP, barrier, ConstraintAnchor::Type::BOTTOM, 0);
        b->connect(ConstraintAnchor::Type::BOTTOM, root, ConstraintAnchor::Type::BOTTOM, 0);
        root->setVerticalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::WRAP_CONTENT);
        root->layout();
        CHECK_EQ(barrier->getTop(), a->getBottom());
        CHECK_EQ(b->getTop(), barrier->getBottom());
        CHECK_EQ(root->getHeight(), a->getHeight() + b->getHeight());
        a->setVerticalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::MATCH_CONSTRAINT);
        a->setVerticalMatchStyle(ConstraintWidget::MATCH_CONSTRAINT_WRAP, 0, 0, 0);
        root->layout();
        CHECK_EQ(barrier->getTop(), a->getBottom());
        CHECK_EQ(b->getTop(), barrier->getBottom());
        CHECK_EQ(root->getHeight(), a->getHeight() + b->getHeight());
}

