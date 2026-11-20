// Ported from constraintlayout-core PriorityTest.java (androidx), by tools in
// the scratchpad, then reviewed. Expected values are upstream's.
#include "ConstraintLayoutTestSupport.h"

namespace {
using namespace kiln::constraintlayout::core;
}

TEST(ClPriority_testPriorityChainHorizontal) {
    using namespace kiln::constraintlayout::core;
    Widgets W;
        auto* root = W.make<ConstraintWidgetContainer>(0, 0, 1000, 600);
        auto* a = W.make<ConstraintWidget>(400, 20);
        auto* b = W.make<ConstraintWidget>(100, 20);
        auto* c = W.make<ConstraintWidget>(100, 20);
        root->setDebugName("root");
        a->setDebugName("A");
        b->setDebugName("B");
        c->setDebugName("C");
        root->add(a);
        root->add(b);
        root->add(c);
        a->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT);
        a->connect(ConstraintAnchor::Type::RIGHT, root, ConstraintAnchor::Type::RIGHT);
        b->connect(ConstraintAnchor::Type::LEFT, a, ConstraintAnchor::Type::LEFT);
        b->connect(ConstraintAnchor::Type::RIGHT, c, ConstraintAnchor::Type::LEFT);
        c->connect(ConstraintAnchor::Type::LEFT, b, ConstraintAnchor::Type::RIGHT);
        c->connect(ConstraintAnchor::Type::RIGHT, a, ConstraintAnchor::Type::RIGHT);
        b->setHorizontalChainStyle(ConstraintWidget::CHAIN_PACKED);
        root->layout();
        CHECK_EQ(a->getWidth(), 400);
        CHECK_EQ(b->getWidth(), 100);
        CHECK_EQ(c->getWidth(), 100);
        CHECK_EQ(a->getLeft(), 300);
        CHECK_EQ(b->getLeft(), 400);
        CHECK_EQ(c->getLeft(), 500);
        b->setHorizontalChainStyle(ConstraintWidget::CHAIN_SPREAD);
        root->layout();
        CHECK_EQ(a->getWidth(), 400);
        CHECK_EQ(b->getWidth(), 100);
        CHECK_EQ(c->getWidth(), 100);
        CHECK_EQ(a->getLeft(), 300);
        CHECK_EQ(b->getLeft(), 367);
        CHECK_EQ(c->getLeft(), 533);
        b->setHorizontalChainStyle(ConstraintWidget::CHAIN_SPREAD_INSIDE);
        root->layout();
        CHECK_EQ(a->getWidth(), 400);
        CHECK_EQ(b->getWidth(), 100);
        CHECK_EQ(c->getWidth(), 100);
        CHECK_EQ(a->getLeft(), 300);
        CHECK_EQ(b->getLeft(), 300);
        CHECK_EQ(c->getLeft(), 600);
}

TEST(ClPriority_testPriorityChainVertical) {
    using namespace kiln::constraintlayout::core;
    Widgets W;
        auto* root = W.make<ConstraintWidgetContainer>(0, 0, 1000, 1000);
        auto* a = W.make<ConstraintWidget>(400, 400);
        auto* b = W.make<ConstraintWidget>(100, 100);
        auto* c = W.make<ConstraintWidget>(100, 100);
        root->setDebugName("root");
        a->setDebugName("A");
        b->setDebugName("B");
        c->setDebugName("C");
        root->add(a);
        root->add(b);
        root->add(c);
        a->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP);
        a->connect(ConstraintAnchor::Type::BOTTOM, root, ConstraintAnchor::Type::BOTTOM);
        b->connect(ConstraintAnchor::Type::TOP, a, ConstraintAnchor::Type::TOP);
        b->connect(ConstraintAnchor::Type::BOTTOM, c, ConstraintAnchor::Type::TOP);
        c->connect(ConstraintAnchor::Type::TOP, b, ConstraintAnchor::Type::BOTTOM);
        c->connect(ConstraintAnchor::Type::BOTTOM, a, ConstraintAnchor::Type::BOTTOM);
        b->setVerticalChainStyle(ConstraintWidget::CHAIN_PACKED);
        root->layout();
        CHECK_EQ(a->getHeight(), 400);
        CHECK_EQ(b->getHeight(), 100);
        CHECK_EQ(c->getHeight(), 100);
        CHECK_EQ(a->getTop(), 300);
        CHECK_EQ(b->getTop(), 400);
        CHECK_EQ(c->getTop(), 500);
        b->setVerticalChainStyle(ConstraintWidget::CHAIN_SPREAD);
        root->layout();
        CHECK_EQ(a->getHeight(), 400);
        CHECK_EQ(b->getHeight(), 100);
        CHECK_EQ(c->getHeight(), 100);
        CHECK_EQ(a->getTop(), 300);
        CHECK_EQ(b->getTop(), 367);
        CHECK_EQ(c->getTop(), 533);
        b->setVerticalChainStyle(ConstraintWidget::CHAIN_SPREAD_INSIDE);
        root->layout();
        CHECK_EQ(a->getHeight(), 400);
        CHECK_EQ(b->getHeight(), 100);
        CHECK_EQ(c->getHeight(), 100);
        CHECK_EQ(a->getTop(), 300);
        CHECK_EQ(b->getTop(), 300);
        CHECK_EQ(c->getTop(), 600);
}

