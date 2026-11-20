// Ported from constraintlayout-core ChainHeadTest.java (androidx), by tools in
// the scratchpad, then reviewed. Expected values are upstream's.
#include "ConstraintLayoutTestSupport.h"
#include "kiln/constraintlayout/core/widgets/ChainHead.h"

namespace {
using namespace kiln::constraintlayout::core;
}

TEST(ClChainHead_basicHorizontalChainHeadTest) {
    using namespace kiln::constraintlayout::core;
    Widgets W;
        auto* root = W.make<ConstraintWidgetContainer>(0, 0, 600, 600);
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
        a->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT);
        a->connect(ConstraintAnchor::Type::RIGHT, b, ConstraintAnchor::Type::LEFT);
        b->connect(ConstraintAnchor::Type::LEFT, a, ConstraintAnchor::Type::RIGHT);
        b->connect(ConstraintAnchor::Type::RIGHT, c, ConstraintAnchor::Type::LEFT);
        c->connect(ConstraintAnchor::Type::LEFT, b, ConstraintAnchor::Type::RIGHT);
        c->connect(ConstraintAnchor::Type::RIGHT, root, ConstraintAnchor::Type::RIGHT);
        a->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP);
        b->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP);
        c->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP);
        auto chainHead = std::make_unique<ChainHead>(a, ConstraintWidget::HORIZONTAL, false);
        chainHead->define();
        CHECK_EQ(chainHead->getHead(), a);
        CHECK_EQ(chainHead->getFirst(), a);
        CHECK_EQ(chainHead->getFirstVisibleWidget(), a);
        CHECK_EQ(chainHead->getLast(), c);
        CHECK_EQ(chainHead->getLastVisibleWidget(), c);
        a->setVisibility(ConstraintWidget::GONE);
        chainHead = std::make_unique<ChainHead>(a, ConstraintWidget::HORIZONTAL, false);
        chainHead->define();
        CHECK_EQ(chainHead->getHead(), a);
        CHECK_EQ(chainHead->getFirst(), a);
        CHECK_EQ(chainHead->getFirstVisibleWidget(), b);
        chainHead = std::make_unique<ChainHead>(a, ConstraintWidget::HORIZONTAL, true);
        chainHead->define();
        CHECK_EQ(chainHead->getHead(), c);
        CHECK_EQ(chainHead->getFirst(), a);
        CHECK_EQ(chainHead->getFirstVisibleWidget(), b);
}

TEST(ClChainHead_basicVerticalChainHeadTest) {
    using namespace kiln::constraintlayout::core;
    Widgets W;
        auto* root = W.make<ConstraintWidgetContainer>(0, 0, 600, 600);
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
        a->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP);
        a->connect(ConstraintAnchor::Type::BOTTOM, b, ConstraintAnchor::Type::TOP);
        b->connect(ConstraintAnchor::Type::TOP, a, ConstraintAnchor::Type::BOTTOM);
        b->connect(ConstraintAnchor::Type::BOTTOM, c, ConstraintAnchor::Type::TOP);
        c->connect(ConstraintAnchor::Type::TOP, b, ConstraintAnchor::Type::BOTTOM);
        c->connect(ConstraintAnchor::Type::BOTTOM, root, ConstraintAnchor::Type::BOTTOM);
        a->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT);
        b->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT);
        c->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT);
        auto chainHead = std::make_unique<ChainHead>(a, ConstraintWidget::VERTICAL, false);
        chainHead->define();
        CHECK_EQ(chainHead->getHead(), a);
        CHECK_EQ(chainHead->getFirst(), a);
        CHECK_EQ(chainHead->getFirstVisibleWidget(), a);
        CHECK_EQ(chainHead->getLast(), c);
        CHECK_EQ(chainHead->getLastVisibleWidget(), c);
        a->setVisibility(ConstraintWidget::GONE);
        chainHead = std::make_unique<ChainHead>(a, ConstraintWidget::VERTICAL, false);
        chainHead->define();
        CHECK_EQ(chainHead->getHead(), a);
        CHECK_EQ(chainHead->getFirst(), a);
        CHECK_EQ(chainHead->getFirstVisibleWidget(), b);
        chainHead = std::make_unique<ChainHead>(a, ConstraintWidget::VERTICAL, true);
        chainHead->define();
        CHECK_EQ(chainHead->getHead(), a);
        CHECK_EQ(chainHead->getFirst(), a);
        CHECK_EQ(chainHead->getFirstVisibleWidget(), b);
}

TEST(ClChainHead_basicMatchConstraintTest) {
    using namespace kiln::constraintlayout::core;
    Widgets W;
        auto* root = W.make<ConstraintWidgetContainer>(0, 0, 600, 600);
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
        a->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT);
        a->connect(ConstraintAnchor::Type::RIGHT, b, ConstraintAnchor::Type::LEFT);
        b->connect(ConstraintAnchor::Type::LEFT, a, ConstraintAnchor::Type::RIGHT);
        b->connect(ConstraintAnchor::Type::RIGHT, c, ConstraintAnchor::Type::LEFT);
        c->connect(ConstraintAnchor::Type::LEFT, b, ConstraintAnchor::Type::RIGHT);
        c->connect(ConstraintAnchor::Type::RIGHT, root, ConstraintAnchor::Type::RIGHT);
        a->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP);
        b->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP);
        c->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP);
        a->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::MATCH_CONSTRAINT);
        b->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::MATCH_CONSTRAINT);
        c->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::MATCH_CONSTRAINT);
        a->setHorizontalWeight(1.0f);
        b->setHorizontalWeight(2.0f);
        c->setHorizontalWeight(3.0f);
        auto chainHead = std::make_unique<ChainHead>(a, ConstraintWidget::HORIZONTAL, false);
        chainHead->define();
        CHECK_EQ(chainHead->getFirstMatchConstraintWidget(), a);
        CHECK_EQ(chainHead->getLastMatchConstraintWidget(), c);
        CHECK_NEAR(chainHead->getTotalWeight(), 6.0f, 0.0f);
        c->setVisibility(ConstraintWidget::GONE);
        chainHead = std::make_unique<ChainHead>(a, ConstraintWidget::HORIZONTAL, false);
        chainHead->define();
        CHECK_EQ(chainHead->getFirstMatchConstraintWidget(), a);
        CHECK_EQ(chainHead->getLastMatchConstraintWidget(), b);
        CHECK_NEAR(chainHead->getTotalWeight(), 3.0f, 0.0f);
}

TEST(ClChainHead_chainOptimizerValuesTest) {
    using namespace kiln::constraintlayout::core;
    Widgets W;
        auto* root = W.make<ConstraintWidgetContainer>(0, 0, 600, 600);
        auto* a = W.make<ConstraintWidget>(50, 20);
        auto* b = W.make<ConstraintWidget>(100, 20);
        auto* c = W.make<ConstraintWidget>(200, 20);
        root->setDebugName("root");
        a->setDebugName("A");
        b->setDebugName("B");
        c->setDebugName("C");
        root->add({a, b, c});
        a->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT, 5);
        a->connect(ConstraintAnchor::Type::RIGHT, b, ConstraintAnchor::Type::LEFT, 5);
        b->connect(ConstraintAnchor::Type::LEFT, a, ConstraintAnchor::Type::RIGHT, 1);
        b->connect(ConstraintAnchor::Type::RIGHT, c, ConstraintAnchor::Type::LEFT, 1);
        c->connect(ConstraintAnchor::Type::LEFT, b, ConstraintAnchor::Type::RIGHT, 10);
        c->connect(ConstraintAnchor::Type::RIGHT, root, ConstraintAnchor::Type::RIGHT, 10);
        a->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP);
        b->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP);
        c->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP);
        auto chainHead = std::make_unique<ChainHead>(a, ConstraintWidget::HORIZONTAL, false);
        chainHead->define();
        CHECK_EQ(chainHead->mVisibleWidgets, 3);
        CHECK_EQ(chainHead->mTotalSize, 367); 
        CHECK_EQ(chainHead->mTotalMargins, 32);
        CHECK_EQ(chainHead->mWidgetsMatchCount, 0);
        CHECK(chainHead->mOptimizable);
        b->setVisibility(ConstraintWidget::GONE);
        chainHead = std::make_unique<ChainHead>(a, ConstraintWidget::HORIZONTAL, false);
        chainHead->define();
        CHECK_EQ(chainHead->mVisibleWidgets, 2);
        CHECK_EQ(chainHead->mTotalSize, 265);
        CHECK_EQ(chainHead->mTotalMargins, 30);
        CHECK_EQ(chainHead->mWidgetsMatchCount, 0);
        CHECK(chainHead->mOptimizable);
        a->setVisibility(ConstraintWidget::GONE);
        b->setVisibility(ConstraintWidget::VISIBLE);
        c->setVisibility(ConstraintWidget::GONE);
        chainHead = std::make_unique<ChainHead>(a, ConstraintWidget::HORIZONTAL, false);
        chainHead->define();
        CHECK_EQ(chainHead->mVisibleWidgets, 1);
        CHECK_EQ(chainHead->mTotalSize, 100);
        CHECK_EQ(chainHead->mTotalMargins, 2);
        CHECK_EQ(chainHead->mWidgetsMatchCount, 0);
        CHECK(chainHead->mOptimizable);
        a->setVisibility(ConstraintWidget::VISIBLE);
        b->setVisibility(ConstraintWidget::VISIBLE);
        c->setVisibility(ConstraintWidget::VISIBLE);
        a->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::MATCH_CONSTRAINT);
        a->mMatchConstraintDefaultWidth = ConstraintWidget::MATCH_CONSTRAINT_PERCENT;
        chainHead = std::make_unique<ChainHead>(a, ConstraintWidget::HORIZONTAL, false);
        chainHead->define();
        CHECK_EQ(chainHead->mVisibleWidgets, 3);
        CHECK_EQ(chainHead->mTotalSize, 317);
        CHECK_EQ(chainHead->mTotalMargins, 32);
        CHECK_EQ(chainHead->mWidgetsMatchCount, 1);
        CHECK(!(chainHead->mOptimizable));
}

