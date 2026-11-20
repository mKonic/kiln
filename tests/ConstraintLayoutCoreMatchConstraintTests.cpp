// Ported from constraintlayout-core MatchConstraintTest.java (androidx), by tools in
// the scratchpad, then reviewed. Expected values are upstream's.
#include "ConstraintLayoutTestSupport.h"

namespace {
using namespace kiln::constraintlayout::core;
}

TEST(ClMatchConstraint_testSimpleMinMatch) {
    using namespace kiln::constraintlayout::core;
    Widgets W;
        auto* root = W.make<ConstraintWidgetContainer>(0, 0, 800, 600);
        auto* a = W.make<ConstraintWidget>(100, 20);
        auto* b = W.make<ConstraintWidget>(100, 20);
        a->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT);
        a->connect(ConstraintAnchor::Type::RIGHT, root, ConstraintAnchor::Type::RIGHT);
        b->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT);
        b->connect(ConstraintAnchor::Type::RIGHT, root, ConstraintAnchor::Type::RIGHT);
        a->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::MATCH_CONSTRAINT);
        a->setHorizontalMatchStyle(ConstraintWidget::MATCH_CONSTRAINT_SPREAD, 150, 200, 1);
        root->add(a);
        root->add(b);
        root->setDebugName("root");
        a->setDebugName("A");
        b->setDebugName("B");
        root->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::WRAP_CONTENT);
        root->layout();
        CHECK_EQ(a->getWidth(), 150);
        CHECK_EQ(b->getWidth(), 100);
        CHECK_EQ(root->getWidth(), 150);
        b->setWidth(200);
        root->setWidth(0);
        root->layout();
        CHECK_EQ(a->getWidth(), 200);
        CHECK_EQ(b->getWidth(), 200);
        CHECK_EQ(root->getWidth(), 200);
        b->setWidth(300);
        root->setWidth(0);
        root->layout();
        CHECK_EQ(a->getWidth(), 200);
        CHECK_EQ(b->getWidth(), 300);
        CHECK_EQ(root->getWidth(), 300);
}

TEST(ClMatchConstraint_testMinMaxMatch) {
    using namespace kiln::constraintlayout::core;
    Widgets W;
        auto* root = W.make<ConstraintWidgetContainer>(0, 0, 800, 600);
        auto* guidelineA = W.make<Guideline>();
        guidelineA->setOrientation(Guideline::VERTICAL);
        guidelineA->setGuideBegin(100);
        auto* guidelineB = W.make<Guideline>();
        guidelineB->setOrientation(Guideline::VERTICAL);
        guidelineB->setGuideEnd(100);
        root->add(guidelineA);
        root->add(guidelineB);
        auto* a = W.make<ConstraintWidget>(100, 20);
        a->connect(ConstraintAnchor::Type::LEFT, guidelineA, ConstraintAnchor::Type::LEFT);
        a->connect(ConstraintAnchor::Type::RIGHT, guidelineB, ConstraintAnchor::Type::RIGHT);
        a->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::MATCH_CONSTRAINT);
        a->setHorizontalMatchStyle(ConstraintWidget::MATCH_CONSTRAINT_SPREAD, 150, 200, 1);
        root->add(a);
        root->setDebugName("root");
        guidelineA->setDebugName("guideline A");
        guidelineB->setDebugName("guideline B");
        a->setDebugName("A");
        root->layout();
        CHECK_EQ(root->getWidth(), 800);
        CHECK_EQ(a->getWidth(), 200);
        root->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::WRAP_CONTENT);
        a->setWidth(100);
        root->layout();
        CHECK_EQ(root->getWidth(), 350);
        CHECK_EQ(a->getWidth(), 150);
        a->setHorizontalMatchStyle(ConstraintWidget::MATCH_CONSTRAINT_WRAP, 150, 200, 1);
        root->layout();
        CHECK_EQ(root->getWidth(), 350);
        CHECK_EQ(a->getWidth(), 150);
        root->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::FIXED);
        root->setWidth(800);
        root->layout();
        CHECK_EQ(root->getWidth(), 800);
        CHECK_EQ(a->getWidth(), 150); 
        a->setWidth(250);
        root->layout();
        CHECK_EQ(root->getWidth(), 800);
        CHECK_EQ(a->getWidth(), 200);
        a->setWidth(700);
        a->setHorizontalMatchStyle(ConstraintWidget::MATCH_CONSTRAINT_SPREAD, 150, 0, 1);
        root->layout();
        CHECK_EQ(root->getWidth(), 800);
        CHECK_EQ(a->getWidth(), 600);
        a->setWidth(700);
        a->setHorizontalMatchStyle(ConstraintWidget::MATCH_CONSTRAINT_WRAP, 150, 0, 1);
        root->layout();
        CHECK_EQ(root->getWidth(), 800);
        CHECK_EQ(a->getWidth(), 600);
        a->setWidth(700);
        root->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::WRAP_CONTENT);
        root->setWidth(0);
        root->layout();
        CHECK_EQ(root->getWidth(), 900);
        CHECK_EQ(a->getWidth(), 700);
        a->setWidth(700);
        a->setHorizontalMatchStyle(ConstraintWidget::MATCH_CONSTRAINT_SPREAD, 150, 0, 1);
        root->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::WRAP_CONTENT);
        root->layout();
        CHECK_EQ(root->getWidth(), 350);
        CHECK_EQ(a->getWidth(), 150);
}

TEST(ClMatchConstraint_testSimpleHorizontalMatch) {
    using namespace kiln::constraintlayout::core;
    Widgets W;
        auto* root = W.make<ConstraintWidgetContainer>(0, 0, 800, 600);
        auto* a = W.make<ConstraintWidget>(100, 20);
        auto* b = W.make<ConstraintWidget>(100, 20);
        auto* c = W.make<ConstraintWidget>(100, 20);
        root->setDebugSolverName(root->getSystem(), "root");
        a->setDebugSolverName(root->getSystem(), "A");
        b->setDebugSolverName(root->getSystem(), "B");
        c->setDebugSolverName(root->getSystem(), "C");
        a->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT, 0);
        b->connect(ConstraintAnchor::Type::RIGHT, root, ConstraintAnchor::Type::RIGHT, 0);
        c->connect(ConstraintAnchor::Type::LEFT, a, ConstraintAnchor::Type::RIGHT, 0);
        c->connect(ConstraintAnchor::Type::RIGHT, b, ConstraintAnchor::Type::LEFT, 0);
        root->add(a);
        root->add(b);
        root->add(c);
        root->layout();
        CHECK_EQ(a->getWidth(), 100);
        CHECK_EQ(b->getWidth(), 100);
        CHECK_EQ(c->getWidth(), 100);
        CHECK(c->getLeft() >= a->getRight());
        CHECK(c->getRight() <= b->getLeft());
        CHECK_EQ(c->getLeft() - a->getRight(), b->getLeft() - c->getRight());
        c->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::MATCH_CONSTRAINT);
        root->layout();
        CHECK_EQ(a->getWidth(), 100);
        CHECK_EQ(b->getWidth(), 100);
        CHECK_EQ(c->getWidth(), 600);
        CHECK(c->getLeft() >= a->getRight());
        CHECK(c->getRight() <= b->getLeft());
        CHECK_EQ(c->getLeft() - a->getRight(), b->getLeft() - c->getRight());
        c->setWidth(144);
        c->setHorizontalMatchStyle(ConstraintWidget::MATCH_CONSTRAINT_WRAP, 0, 0, 0);
        root->layout();
        CHECK_EQ(a->getWidth(), 100);
        CHECK_EQ(b->getWidth(), 100);
        CHECK_EQ(c->getWidth(), 144);
        CHECK(c->getLeft() >= a->getRight());
        CHECK(c->getRight() <= b->getLeft());
        CHECK_EQ(c->getLeft() - a->getRight(), b->getLeft() - c->getRight());
        c->setWidth(1000);
        c->setHorizontalMatchStyle(ConstraintWidget::MATCH_CONSTRAINT_WRAP, 0, 0, 0);
        root->layout();
        CHECK_EQ(a->getWidth(), 100);
        CHECK_EQ(b->getWidth(), 100);
        CHECK_EQ(c->getWidth(), 600);
        CHECK(c->getLeft() >= a->getRight());
        CHECK(c->getRight() <= b->getLeft());
        CHECK_EQ(c->getLeft() - a->getRight(), b->getLeft() - c->getRight());
}

TEST(ClMatchConstraint_testDanglingRatio) {
    using namespace kiln::constraintlayout::core;
    Widgets W;
        auto* root = W.make<ConstraintWidgetContainer>(0, 0, 1000, 600);
        auto* a = W.make<ConstraintWidget>(100, 20);
        root->setDebugName("root");
        a->setDebugName("A");
        root->add(a);
        a->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT);
        a->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP);
        a->connect(ConstraintAnchor::Type::RIGHT, root, ConstraintAnchor::Type::RIGHT);
        a->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::MATCH_CONSTRAINT);
        a->setVerticalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::MATCH_CONSTRAINT);
        a->setHorizontalMatchStyle(ConstraintWidget::MATCH_CONSTRAINT_WRAP, 0, 0, 0);
        root->layout();
}

