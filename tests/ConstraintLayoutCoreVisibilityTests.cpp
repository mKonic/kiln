// Ported from constraintlayout-core VisibilityTest.java (androidx), by tools in
// the scratchpad, then reviewed. Expected values are upstream's.
#include "ConstraintLayoutTestSupport.h"

namespace {
using namespace kiln::constraintlayout::core;
}

TEST(ClVisibility_testGoneSingleConnection) {
    using namespace kiln::constraintlayout::core;
    Widgets W;
        auto* root = W.make<ConstraintWidgetContainer>(0, 0, 800, 600);
        auto* a = W.make<ConstraintWidget>(100, 20);
        auto* b = W.make<ConstraintWidget>(100, 20);
        root->setDebugSolverName(root->getSystem(), "root");
        a->setDebugSolverName(root->getSystem(), "A");
        b->setDebugSolverName(root->getSystem(), "B");
        int margin = 175;
        int goneMargin = 42;
        root->add(a);
        root->add(b);
        a->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT, margin);
        a->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP, margin);
        b->connect(ConstraintAnchor::Type::LEFT, a, ConstraintAnchor::Type::RIGHT, margin);
        b->connect(ConstraintAnchor::Type::TOP, a, ConstraintAnchor::Type::BOTTOM, margin);
        root->layout();
        CHECK_EQ(root->getWidth(), 800);
        CHECK_EQ(root->getHeight(), 600);
        CHECK_EQ(a->getWidth(), 100);
        CHECK_EQ(a->getHeight(), 20);
        CHECK_EQ(b->getWidth(), 100);
        CHECK_EQ(b->getHeight(), 20);
        CHECK_EQ(a->getLeft(), root->getLeft() + margin);
        CHECK_EQ(a->getTop(), root->getTop() + margin);
        CHECK_EQ(b->getLeft(), a->getRight() + margin);
        CHECK_EQ(b->getTop(), a->getBottom() + margin);
        a->setVisibility(ConstraintWidget::GONE);
        root->layout();
        CHECK_EQ(root->getWidth(), 800);
        CHECK_EQ(root->getHeight(), 600);
        CHECK_EQ(a->getWidth(), 0);
        CHECK_EQ(a->getHeight(), 0);
        CHECK_EQ(b->getWidth(), 100);
        CHECK_EQ(b->getHeight(), 20);
        CHECK_EQ(a->getLeft(), root->getLeft());
        CHECK_EQ(a->getTop(), root->getTop());
        CHECK_EQ(b->getLeft(), a->getRight() + margin);
        CHECK_EQ(b->getTop(), a->getBottom() + margin);
        b->setGoneMargin(ConstraintAnchor::Type::LEFT, goneMargin);
        b->setGoneMargin(ConstraintAnchor::Type::TOP, goneMargin);
        root->layout();
        CHECK_EQ(root->getWidth(), 800);
        CHECK_EQ(root->getHeight(), 600);
        CHECK_EQ(a->getWidth(), 0);
        CHECK_EQ(a->getHeight(), 0);
        CHECK_EQ(b->getWidth(), 100);
        CHECK_EQ(b->getHeight(), 20);
        CHECK_EQ(a->getLeft(), root->getLeft());
        CHECK_EQ(a->getTop(), root->getTop());
        CHECK_EQ(b->getLeft(), a->getRight() + goneMargin);
        CHECK_EQ(b->getTop(), a->getBottom() + goneMargin);
}

TEST(ClVisibility_testGoneDualConnection) {
    using namespace kiln::constraintlayout::core;
    Widgets W;
        auto* root = W.make<ConstraintWidgetContainer>(0, 0, 800, 600);
        auto* a = W.make<ConstraintWidget>(100, 20);
        auto* b = W.make<ConstraintWidget>(100, 20);
        auto* guideline = W.make<Guideline>();
        guideline->setGuidePercent(0.5f);
        guideline->setOrientation(ConstraintWidget::HORIZONTAL);
        root->setDebugSolverName(root->getSystem(), "root");
        a->setDebugSolverName(root->getSystem(), "A");
        b->setDebugSolverName(root->getSystem(), "B");
        root->add(a);
        root->add(b);
        root->add(guideline);
        a->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::MATCH_CONSTRAINT);
        a->setVerticalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::MATCH_CONSTRAINT);
        b->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::MATCH_CONSTRAINT);
        b->setVerticalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::MATCH_CONSTRAINT);
        a->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT);
        a->connect(ConstraintAnchor::Type::RIGHT, root, ConstraintAnchor::Type::RIGHT);
        a->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP);
        a->connect(ConstraintAnchor::Type::BOTTOM, guideline, ConstraintAnchor::Type::TOP);
        b->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT);
        b->connect(ConstraintAnchor::Type::RIGHT, root, ConstraintAnchor::Type::RIGHT);
        b->connect(ConstraintAnchor::Type::TOP, a, ConstraintAnchor::Type::BOTTOM);
        b->connect(ConstraintAnchor::Type::BOTTOM, root, ConstraintAnchor::Type::BOTTOM);
        root->layout();
        CHECK_EQ(root->getWidth(), 800);
        CHECK_EQ(root->getHeight(), 600);
        CHECK_EQ(a->getLeft(), root->getLeft());
        CHECK_EQ(a->getRight(), root->getRight());
        CHECK_EQ(b->getLeft(), root->getLeft());
        CHECK_EQ(b->getRight(), root->getRight());
        CHECK_EQ(guideline->getTop(), root->getHeight() / 2);
        CHECK_EQ(a->getTop(), root->getTop());
        CHECK_EQ(a->getBottom(), guideline->getTop());
        CHECK_EQ(b->getTop(), a->getBottom());
        CHECK_EQ(b->getBottom(), root->getBottom());
        a->setVisibility(ConstraintWidget::GONE);
        root->layout();
        CHECK_EQ(root->getWidth(), 800);
        CHECK_EQ(root->getHeight(), 600);
        CHECK_EQ(a->getWidth(), 0);
        CHECK_EQ(a->getHeight(), 0);
        CHECK_EQ(a->getLeft(), 400);
        CHECK_EQ(a->getRight(), 400);
        CHECK_EQ(b->getLeft(), root->getLeft());
        CHECK_EQ(b->getRight(), root->getRight());
        CHECK_EQ(guideline->getTop(), root->getHeight() / 2);
        CHECK_EQ(a->getTop(), 150);
        CHECK_EQ(a->getBottom(), 150);
        CHECK_EQ(b->getTop(), 150);
        CHECK_EQ(b->getBottom(), root->getBottom());
}

