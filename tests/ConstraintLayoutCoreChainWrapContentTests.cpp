// Ported from constraintlayout-core ChainWrapContentTest.java (androidx), by tools in
// the scratchpad, then reviewed. Expected values are upstream's.
#include "ConstraintLayoutTestSupport.h"

namespace {
using namespace kiln::constraintlayout::core;
}

static void testVerticalWrapContentChain_with(int directResolution) {
    using namespace kiln::constraintlayout::core;
    Widgets W;

        auto* root = W.make<ConstraintWidgetContainer>(0, 0, 600, 600);
        root->setOptimizationLevel(directResolution);
        auto* a = W.make<ConstraintWidget>(100, 20);
        auto* b = W.make<ConstraintWidget>(100, 20);
        auto* c = W.make<ConstraintWidget>(100, 20);
        root->setDebugName("root");
        a->setDebugName("A");
        b->setDebugName("B");
        c->setDebugName("C");
        root->add(a);
        root->add(b);
        root->add(c);
        root->setVerticalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::WRAP_CONTENT);
        b->setVerticalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::MATCH_CONSTRAINT);
        a->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP, 10);
        a->connect(ConstraintAnchor::Type::BOTTOM, b, ConstraintAnchor::Type::TOP);
        b->connect(ConstraintAnchor::Type::TOP, a, ConstraintAnchor::Type::BOTTOM);
        b->connect(ConstraintAnchor::Type::BOTTOM, c, ConstraintAnchor::Type::TOP);
        c->connect(ConstraintAnchor::Type::TOP, b, ConstraintAnchor::Type::BOTTOM);
        c->connect(ConstraintAnchor::Type::BOTTOM, root, ConstraintAnchor::Type::BOTTOM, 32);
        root->layout();
        (void)0;
        CHECK_EQ(a->getTop(), 10);
        CHECK_EQ(b->getTop(), 30);
        CHECK_EQ(c->getTop(), 30);
        CHECK_EQ(root->getHeight(), 82);
    
}

static void testHorizontalWrapContentChain_with(int directResolution) {
    using namespace kiln::constraintlayout::core;
    Widgets W;

        auto* root = W.make<ConstraintWidgetContainer>(0, 0, 600, 600);
        root->setOptimizationLevel(directResolution);
        auto* a = W.make<ConstraintWidget>(100, 20);
        auto* b = W.make<ConstraintWidget>(100, 20);
        auto* c = W.make<ConstraintWidget>(100, 20);
        root->setDebugName("root");
        a->setDebugName("A");
        b->setDebugName("B");
        c->setDebugName("C");
        root->add(a);
        root->add(b);
        root->add(c);
        b->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::MATCH_CONSTRAINT);
        a->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT, 10);
        a->connect(ConstraintAnchor::Type::RIGHT, b, ConstraintAnchor::Type::LEFT);
        b->connect(ConstraintAnchor::Type::LEFT, a, ConstraintAnchor::Type::RIGHT);
        b->connect(ConstraintAnchor::Type::RIGHT, c, ConstraintAnchor::Type::LEFT);
        c->connect(ConstraintAnchor::Type::LEFT, b, ConstraintAnchor::Type::RIGHT);
        c->connect(ConstraintAnchor::Type::RIGHT, root, ConstraintAnchor::Type::RIGHT, 32);
        root->layout();
        (void)0;
        root->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::WRAP_CONTENT);
        root->layout();
        (void)0;
        CHECK_EQ(a->getLeft(), 10);
        CHECK_EQ(b->getLeft(), 110);
        CHECK_EQ(c->getLeft(), 110);
        CHECK_EQ(root->getWidth(), 242);
        root->setMinWidth(400);
        root->layout();
        (void)0;
        CHECK_EQ(a->getLeft(), 10);
        CHECK_EQ(b->getLeft(), 110);
        CHECK_EQ(c->getLeft(), 268);
        CHECK_EQ(root->getWidth(), 400);
    
}

static void testVerticalWrapContentChain3Elts_with(int directResolution) {
    using namespace kiln::constraintlayout::core;
    Widgets W;

        auto* root = W.make<ConstraintWidgetContainer>(0, 0, 600, 600);
        root->setOptimizationLevel(directResolution);
        auto* a = W.make<ConstraintWidget>(100, 20);
        auto* b = W.make<ConstraintWidget>(100, 20);
        auto* c = W.make<ConstraintWidget>(100, 20);
        auto* d = W.make<ConstraintWidget>(100, 20);
        root->setDebugName("root");
        a->setDebugName("A");
        b->setDebugName("B");
        c->setDebugName("C");
        d->setDebugName("D");
        root->add(a);
        root->add(b);
        root->add(c);
        root->add(d);
        root->setVerticalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::WRAP_CONTENT);
        b->setVerticalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::MATCH_CONSTRAINT);
        c->setVerticalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::MATCH_CONSTRAINT);
        a->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP, 10);
        a->connect(ConstraintAnchor::Type::BOTTOM, b, ConstraintAnchor::Type::TOP);
        b->connect(ConstraintAnchor::Type::TOP, a, ConstraintAnchor::Type::BOTTOM);
        b->connect(ConstraintAnchor::Type::BOTTOM, c, ConstraintAnchor::Type::TOP);
        c->connect(ConstraintAnchor::Type::TOP, b, ConstraintAnchor::Type::BOTTOM);
        c->connect(ConstraintAnchor::Type::BOTTOM, d, ConstraintAnchor::Type::TOP);
        d->connect(ConstraintAnchor::Type::TOP, c, ConstraintAnchor::Type::BOTTOM);
        d->connect(ConstraintAnchor::Type::BOTTOM, root, ConstraintAnchor::Type::BOTTOM, 32);
        root->layout();
        (void)0;
        CHECK_EQ(a->getTop(), 10);
        CHECK_EQ(b->getTop(), 30);
        CHECK_EQ(c->getTop(), 30);
        CHECK_EQ(d->getTop(), 30);
        CHECK_EQ(root->getHeight(), 82);
        root->setMinHeight(300);
        root->layout();
        (void)0;
        CHECK_EQ(a->getTop(), 10);
        CHECK_EQ(b->getTop(), 30);
        CHECK_EQ(c->getTop(), 139);
        CHECK_EQ(d->getTop(), 248);
        CHECK_EQ(root->getHeight(), 300);
        root->setHeight(600);
        root->setVerticalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::FIXED);
        root->layout();
        (void)0;
        CHECK_EQ(a->getTop(), 10);
        CHECK_EQ(b->getTop(), 30);
        CHECK_EQ(c->getTop(), 289);
        CHECK_EQ(d->getTop(), 548);
        CHECK_EQ(root->getHeight(), 600);
    
}

static void testHorizontalWrapContentChain3Elts_with(int directResolution) {
    using namespace kiln::constraintlayout::core;
    Widgets W;

        auto* root = W.make<ConstraintWidgetContainer>(0, 0, 600, 600);
        root->setOptimizationLevel(directResolution);
        auto* a = W.make<ConstraintWidget>(100, 20);
        auto* b = W.make<ConstraintWidget>(100, 20);
        auto* c = W.make<ConstraintWidget>(100, 20);
        auto* d = W.make<ConstraintWidget>(100, 20);
        root->setDebugName("root");
        a->setDebugName("A");
        b->setDebugName("B");
        c->setDebugName("C");
        d->setDebugName("D");
        root->add(a);
        root->add(b);
        root->add(c);
        root->add(d);
        root->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::WRAP_CONTENT);
        b->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::MATCH_CONSTRAINT);
        c->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::MATCH_CONSTRAINT);
        a->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT, 10);
        a->connect(ConstraintAnchor::Type::RIGHT, b, ConstraintAnchor::Type::LEFT);
        b->connect(ConstraintAnchor::Type::LEFT, a, ConstraintAnchor::Type::RIGHT);
        b->connect(ConstraintAnchor::Type::RIGHT, c, ConstraintAnchor::Type::LEFT);
        c->connect(ConstraintAnchor::Type::LEFT, b, ConstraintAnchor::Type::RIGHT);
        c->connect(ConstraintAnchor::Type::RIGHT, d, ConstraintAnchor::Type::LEFT);
        d->connect(ConstraintAnchor::Type::LEFT, c, ConstraintAnchor::Type::RIGHT);
        d->connect(ConstraintAnchor::Type::RIGHT, root, ConstraintAnchor::Type::RIGHT, 32);
        root->layout();
        (void)0;
        CHECK_EQ(a->getLeft(), 10);
        CHECK_EQ(b->getLeft(), 110);
        CHECK_EQ(c->getLeft(), 110);
        CHECK_EQ(d->getLeft(), 110);
        CHECK_EQ(root->getWidth(), 242);
        root->setMinWidth(300);
        root->layout();
        (void)0;
        CHECK_EQ(a->getLeft(), 10);
        CHECK_EQ(b->getLeft(), 110);
        CHECK_EQ(c->getLeft(), 139);
        CHECK_EQ(d->getLeft(), 168);
        CHECK_EQ(root->getWidth(), 300);
        root->setWidth(600);
        root->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::FIXED);
        root->layout();
        (void)0;
        CHECK_EQ(a->getLeft(), 10);
        CHECK_EQ(b->getLeft(), 110);
        CHECK_EQ(c->getLeft(), 289);
        CHECK_EQ(d->getLeft(), 468);
        CHECK_EQ(root->getWidth(), 600);
    
}

TEST(ClChainWrapContent_testVerticalWrapContentChain) {
    using namespace kiln::constraintlayout::core;
    Widgets W;
        testVerticalWrapContentChain_with(Optimizer::OPTIMIZATION_NONE);
        testVerticalWrapContentChain_with(Optimizer::OPTIMIZATION_STANDARD);
}

TEST(ClChainWrapContent_testHorizontalWrapContentChain) {
    using namespace kiln::constraintlayout::core;
    Widgets W;
        testHorizontalWrapContentChain_with(Optimizer::OPTIMIZATION_NONE);
        testHorizontalWrapContentChain_with(Optimizer::OPTIMIZATION_STANDARD);
}

TEST(ClChainWrapContent_testVerticalWrapContentChain3Elts) {
    using namespace kiln::constraintlayout::core;
    Widgets W;
        testVerticalWrapContentChain3Elts_with(Optimizer::OPTIMIZATION_NONE);
        testVerticalWrapContentChain3Elts_with(Optimizer::OPTIMIZATION_STANDARD);
}

TEST(ClChainWrapContent_testHorizontalWrapContentChain3Elts) {
    using namespace kiln::constraintlayout::core;
    Widgets W;
        testHorizontalWrapContentChain3Elts_with(Optimizer::OPTIMIZATION_NONE);
        testHorizontalWrapContentChain3Elts_with(Optimizer::OPTIMIZATION_STANDARD);
}

TEST(ClChainWrapContent_testHorizontalWrapChain) {
    using namespace kiln::constraintlayout::core;
    Widgets W;
        auto* root = W.make<ConstraintWidgetContainer>(0, 0, 600, 1000);
        auto* a = W.make<ConstraintWidget>(20, 20);
        auto* b = W.make<ConstraintWidget>(100, 20);
        auto* c = W.make<ConstraintWidget>(20, 20);
        root->setDebugName("root");
        a->setDebugName("A");
        b->setDebugName("B");
        c->setDebugName("C");
        root->add(a);
        root->add(b);
        root->add(c);
        a->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT);
        a->connect(ConstraintAnchor::Type::RIGHT, b, ConstraintAnchor::Type::LEFT);
        b->connect(ConstraintAnchor::Type::LEFT, a, ConstraintAnchor::Type::RIGHT);
        b->connect(ConstraintAnchor::Type::RIGHT, c, ConstraintAnchor::Type::LEFT);
        c->connect(ConstraintAnchor::Type::LEFT, b, ConstraintAnchor::Type::RIGHT);
        c->connect(ConstraintAnchor::Type::RIGHT, root, ConstraintAnchor::Type::RIGHT);
        b->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::MATCH_CONSTRAINT);
        b->setHorizontalMatchStyle(ConstraintWidget::MATCH_CONSTRAINT_WRAP, 0, 0, 0);
        b->setWidth(600);
        root->layout();
        CHECK_EQ(a->getLeft(), 0);
        CHECK_EQ(b->getLeft(), 20);
        CHECK_EQ(c->getLeft(), 580);
        a->setHorizontalChainStyle(ConstraintWidget::CHAIN_PACKED);
        b->setWidth(600);
        root->layout();
        CHECK_EQ(a->getLeft(), 0);
        CHECK_EQ(b->getLeft(), 20);
        CHECK_EQ(c->getLeft(), 580); 
        b->setWidth(100);
        root->layout();
        CHECK_EQ(a->getLeft(), 230);
        CHECK_EQ(b->getLeft(), 250);
        CHECK_EQ(c->getLeft(), 350);
        b->setWidth(600);
        root->setVerticalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::WRAP_CONTENT);
        a->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP);
        b->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP);
        c->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP);
        root->layout();
        CHECK_EQ(root->getHeight(), 20);
        CHECK_EQ(a->getLeft(), 0);
        CHECK_EQ(b->getLeft(), 20);
        CHECK_EQ(c->getLeft(), 580);
        b->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::FIXED);
        b->setWidth(600);
        root->setWidth(0);
        root->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::WRAP_CONTENT);
        root->layout();
        CHECK_EQ(root->getHeight(), 20);
        CHECK_EQ(a->getLeft(), 0);
        CHECK_EQ(b->getLeft(), 20);
        CHECK_EQ(c->getLeft(), 620);
}

TEST(ClChainWrapContent_testWrapChain) {
    using namespace kiln::constraintlayout::core;
    Widgets W;
        auto* root = W.make<ConstraintWidgetContainer>(0, 0, 1440, 1944);
        auto* a = W.make<ConstraintWidget>(308, 168);
        auto* b = W.make<ConstraintWidget>(308, 168);
        auto* c = W.make<ConstraintWidget>(308, 168);
        auto* d = W.make<ConstraintWidget>(308, 168);
        auto* e = W.make<ConstraintWidget>(308, 168);
        root->setDebugName("root");
        a->setDebugName("A");
        b->setDebugName("B");
        c->setDebugName("C");
        d->setDebugName("D");
        e->setDebugName("E");
        root->add(e);
        root->add(a);
        root->add(b);
        root->add(c);
        root->add(d);
        a->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT);
        a->connect(ConstraintAnchor::Type::RIGHT, b, ConstraintAnchor::Type::LEFT);
        b->connect(ConstraintAnchor::Type::LEFT, a, ConstraintAnchor::Type::RIGHT);
        b->connect(ConstraintAnchor::Type::RIGHT, c, ConstraintAnchor::Type::LEFT);
        c->connect(ConstraintAnchor::Type::LEFT, b, ConstraintAnchor::Type::RIGHT);
        c->connect(ConstraintAnchor::Type::RIGHT, d, ConstraintAnchor::Type::LEFT);
        d->connect(ConstraintAnchor::Type::LEFT, c, ConstraintAnchor::Type::RIGHT);
        d->connect(ConstraintAnchor::Type::RIGHT, root, ConstraintAnchor::Type::RIGHT);
        e->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT);
        e->connect(ConstraintAnchor::Type::RIGHT, root, ConstraintAnchor::Type::RIGHT);
        e->connect(ConstraintAnchor::Type::TOP, c, ConstraintAnchor::Type::BOTTOM);
        root->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::FIXED);
        root->setVerticalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::WRAP_CONTENT);
        root->layout();
        CHECK_EQ(root->getWidth(), 1440);
        CHECK_EQ(root->getHeight(), 336);
}

TEST(ClChainWrapContent_testWrapDanglingChain) {
    using namespace kiln::constraintlayout::core;
    Widgets W;
        auto* root = W.make<ConstraintWidgetContainer>(0, 0, 1440, 1944);
        auto* a = W.make<ConstraintWidget>(308, 168);
        auto* b = W.make<ConstraintWidget>(308, 168);
        auto* c = W.make<ConstraintWidget>(308, 168);
        auto* d = W.make<ConstraintWidget>(308, 168);
        root->setDebugName("root");
        a->setDebugName("A");
        b->setDebugName("B");
        root->add(a);
        root->add(b);
        a->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT);
        a->connect(ConstraintAnchor::Type::RIGHT, b, ConstraintAnchor::Type::LEFT);
        b->connect(ConstraintAnchor::Type::LEFT, a, ConstraintAnchor::Type::RIGHT);
        root->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::WRAP_CONTENT);
        root->setVerticalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::WRAP_CONTENT);
        root->layout();
        CHECK_EQ(root->getWidth(), 616);
        CHECK_EQ(root->getHeight(), 168);
        CHECK_EQ(a->getLeft(), 0);
        CHECK_EQ(b->getLeft(), 308);
        CHECK_EQ(a->getWidth(), 308);
        CHECK_EQ(b->getWidth(), 308);
}

