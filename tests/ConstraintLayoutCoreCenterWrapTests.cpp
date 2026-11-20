// Ported from constraintlayout-core CenterWrapTest.java (androidx), by tools in
// the scratchpad, then reviewed. Expected values are upstream's.
#include "ConstraintLayoutTestSupport.h"

namespace {
using namespace kiln::constraintlayout::core;
}

TEST(ClCenterWrap_testRatioCenter) {
    using namespace kiln::constraintlayout::core;
    Widgets W;
        auto* root = W.make<ConstraintWidgetContainer>(0, 0, 800, 600);
        auto* a = W.make<ConstraintWidget>(100, 20);
        auto* b = W.make<ConstraintWidget>(100, 20);
        root->setDebugName("Root");
        a->setDebugName("A");
        b->setDebugName("B");
        root->add(a);
        root->add(b);
        a->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT);
        a->connect(ConstraintAnchor::Type::RIGHT, root, ConstraintAnchor::Type::RIGHT);
        a->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP);
        a->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::MATCH_CONSTRAINT);
        a->setVerticalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::MATCH_CONSTRAINT);
        a->setDimensionRatio(0.3f, ConstraintWidget::VERTICAL);
        b->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT);
        b->connect(ConstraintAnchor::Type::RIGHT, root, ConstraintAnchor::Type::RIGHT);
        b->connect(ConstraintAnchor::Type::TOP, a, ConstraintAnchor::Type::BOTTOM);
        b->connect(ConstraintAnchor::Type::BOTTOM, root, ConstraintAnchor::Type::BOTTOM);
        b->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::MATCH_CONSTRAINT);
        b->setVerticalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::MATCH_CONSTRAINT);
        b->setDimensionRatio(1.0f, ConstraintWidget::VERTICAL);
        root->setOptimizationLevel(0);
        root->layout();
}

TEST(ClCenterWrap_testSimpleWrap) {
    using namespace kiln::constraintlayout::core;
    Widgets W;
        auto* root = W.make<ConstraintWidgetContainer>(0, 0, 800, 600);
        auto* a = W.make<ConstraintWidget>(100, 20);
        root->setDebugName("Root");
        a->setDebugName("A");
        root->add(a);
        a->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT);
        a->connect(ConstraintAnchor::Type::RIGHT, root, ConstraintAnchor::Type::RIGHT);
        a->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP);
        a->connect(ConstraintAnchor::Type::BOTTOM, root, ConstraintAnchor::Type::BOTTOM);
        root->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::WRAP_CONTENT);
        root->setVerticalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::WRAP_CONTENT);
        root->setOptimizationLevel(0);
        root->layout();
        CHECK_EQ(a->getWidth(), 100);
        CHECK_EQ(a->getHeight(), 20);
        CHECK_EQ(root->getWidth(), 100);
        CHECK_EQ(root->getHeight(), 20);
}

TEST(ClCenterWrap_testSimpleWrap2) {
    using namespace kiln::constraintlayout::core;
    Widgets W;
        auto* root = W.make<ConstraintWidgetContainer>(0, 0, 800, 600);
        auto* a = W.make<ConstraintWidget>(100, 20);
        root->setDebugName("Root");
        a->setDebugName("A");
        root->add(a);
        a->connect(ConstraintAnchor::Type::BOTTOM, root, ConstraintAnchor::Type::BOTTOM);
        a->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP);
        root->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::WRAP_CONTENT);
        root->setVerticalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::WRAP_CONTENT);
        root->setOptimizationLevel(0);
        root->layout();
        CHECK_EQ(a->getWidth(), 100);
        CHECK_EQ(a->getHeight(), 20);
        CHECK_EQ(root->getWidth(), 100);
        CHECK_EQ(root->getHeight(), 20);
}

TEST(ClCenterWrap_testWrap) {
    using namespace kiln::constraintlayout::core;
    Widgets W;
        auto* root = W.make<ConstraintWidgetContainer>(0, 0, 800, 600);
        auto* a = W.make<ConstraintWidget>(100, 20);
        auto* b = W.make<ConstraintWidget>(100, 20);
        auto* c = W.make<ConstraintWidget>(100, 20);
        root->setDebugName("Root");
        a->setDebugName("A");
        b->setDebugName("B");
        c->setDebugName("C");
        root->add(a);
        root->add(b);
        root->add(c);
        a->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT);
        a->connect(ConstraintAnchor::Type::RIGHT, root, ConstraintAnchor::Type::RIGHT);
        a->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP);
        a->connect(ConstraintAnchor::Type::BOTTOM, root, ConstraintAnchor::Type::BOTTOM);
        b->connect(ConstraintAnchor::Type::LEFT, a, ConstraintAnchor::Type::RIGHT);
        b->connect(ConstraintAnchor::Type::TOP, a, ConstraintAnchor::Type::TOP);
        c->connect(ConstraintAnchor::Type::LEFT, b, ConstraintAnchor::Type::RIGHT);
        c->connect(ConstraintAnchor::Type::TOP, b, ConstraintAnchor::Type::BOTTOM);
        root->setVerticalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::WRAP_CONTENT);
        root->setOptimizationLevel(0);
        root->layout();
        CHECK_EQ(a->getWidth(), 100);
        CHECK_EQ(b->getWidth(), 100);
        CHECK_EQ(c->getWidth(), 100);
        CHECK_EQ(a->getWidth(), 100);
        CHECK_EQ(a->getHeight(), 20);
        CHECK_EQ(b->getHeight(), 20);
        CHECK_EQ(c->getHeight(), 20);
}

TEST(ClCenterWrap_testWrapHeight) {
    using namespace kiln::constraintlayout::core;
    Widgets W;
        auto* root = W.make<ConstraintWidgetContainer>(0, 0, 800, 600);
        auto* tl = W.make<ConstraintWidget>(100, 20);
        auto* trl = W.make<ConstraintWidget>(100, 20);
        auto* tbl = W.make<ConstraintWidget>(100, 20);
        auto* img = W.make<ConstraintWidget>(100, 100);
        root->setDebugName("root");
        tl->setDebugName("TL");
        trl->setDebugName("TRL");
        tbl->setDebugName("TBL");
        img->setDebugName("IMG");
        tl->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP);
        tl->connect(ConstraintAnchor::Type::BOTTOM, tbl, ConstraintAnchor::Type::BOTTOM);
        trl->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP);
        tbl->connect(ConstraintAnchor::Type::TOP, trl, ConstraintAnchor::Type::BOTTOM);
        img->connect(ConstraintAnchor::Type::TOP, tbl, ConstraintAnchor::Type::BOTTOM);
        img->connect(ConstraintAnchor::Type::BOTTOM, root, ConstraintAnchor::Type::BOTTOM);
        root->add(tl);
        root->add(trl);
        root->add(tbl);
        root->add(img);
        root->setVerticalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::WRAP_CONTENT);
        root->layout();
        CHECK_EQ(root->getHeight(), 140);
}

TEST(ClCenterWrap_testComplexLayout) {
    using namespace kiln::constraintlayout::core;
    Widgets W;
        auto* root = W.make<ConstraintWidgetContainer>(0, 0, 800, 600);
        auto* img = W.make<ConstraintWidget>(100, 100);
        int margin = 16;
        auto* button = W.make<ConstraintWidget>(50, 50);
        auto* a = W.make<ConstraintWidget>(100, 20);
        auto* b = W.make<ConstraintWidget>(100, 20);
        auto* c = W.make<ConstraintWidget>(100, 20);
        img->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT);
        img->connect(ConstraintAnchor::Type::RIGHT, root, ConstraintAnchor::Type::RIGHT);
        img->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP);
        img->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::MATCH_CONSTRAINT);
        button->connect(ConstraintAnchor::Type::RIGHT, root, ConstraintAnchor::Type::RIGHT, margin);
        button->connect(ConstraintAnchor::Type::TOP, img, ConstraintAnchor::Type::BOTTOM);
        button->connect(ConstraintAnchor::Type::BOTTOM, img, ConstraintAnchor::Type::BOTTOM);
        a->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT, margin);
        a->connect(ConstraintAnchor::Type::TOP, button, ConstraintAnchor::Type::BOTTOM, margin);
        b->connect(ConstraintAnchor::Type::RIGHT, root, ConstraintAnchor::Type::RIGHT, margin);
        b->connect(ConstraintAnchor::Type::TOP, button, ConstraintAnchor::Type::BOTTOM, margin);
        c->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT, margin);
        c->connect(ConstraintAnchor::Type::RIGHT, root, ConstraintAnchor::Type::RIGHT, margin);
        c->connect(ConstraintAnchor::Type::TOP, a, ConstraintAnchor::Type::BOTTOM, margin);
        c->connect(ConstraintAnchor::Type::BOTTOM, root, ConstraintAnchor::Type::BOTTOM);
        root->add(img);
        root->add(button);
        root->add(a);
        root->add(b);
        root->add(c);
        root->setDebugName("root");
        img->setDebugName("IMG");
        button->setDebugName("BUTTON");
        a->setDebugName("A");
        b->setDebugName("B");
        c->setDebugName("C");
        root->layout();
        CHECK_EQ(root->getWidth(), 800);
        CHECK_EQ(root->getHeight(), 600);
        CHECK_EQ(img->getWidth(), root->getWidth());
        CHECK_EQ(button->getWidth(), 50);
        CHECK_EQ(a->getWidth(), 100);
        CHECK_EQ(b->getWidth(), 100);
        CHECK_EQ(c->getWidth(), 100);
        CHECK_EQ(img->getHeight(), 100);
        CHECK_EQ(button->getHeight(), 50);
        CHECK_EQ(a->getHeight(), 20);
        CHECK_EQ(b->getHeight(), 20);
        CHECK_EQ(c->getHeight(), 20);
        CHECK_EQ(img->getLeft(), 0);
        CHECK_EQ(img->getRight(), root->getRight());
        CHECK_EQ(button->getLeft(), 734);
        CHECK_EQ(button->getTop(), img->getBottom() - button->getHeight() / 2);
        CHECK_EQ(a->getLeft(), margin);
        CHECK_EQ(a->getTop(), button->getBottom() + margin);
        CHECK_EQ(b->getRight(), root->getRight() - margin);
        CHECK_EQ(b->getTop(), a->getTop());
        CHECK_EQ(c->getLeft(), 350);
        CHECK_EQ(c->getRight(), 450);
        CHECK_NEAR(c->getTop(), 379, 1);
        root->setVerticalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::WRAP_CONTENT);
        root->layout();
        root->setOptimizationLevel(0);
        CHECK_EQ(root->getWidth(), 800);
        CHECK_EQ(root->getHeight(), 197);
        CHECK_EQ(img->getWidth(), root->getWidth());
        CHECK_EQ(button->getWidth(), 50);
        CHECK_EQ(a->getWidth(), 100);
        CHECK_EQ(b->getWidth(), 100);
        CHECK_EQ(c->getWidth(), 100);
        CHECK_EQ(img->getHeight(), 100);
        CHECK_EQ(button->getHeight(), 50);
        CHECK_EQ(a->getHeight(), 20);
        CHECK_EQ(b->getHeight(), 20);
        CHECK_EQ(c->getHeight(), 20);
        CHECK_EQ(img->getLeft(), 0);
        CHECK_EQ(img->getRight(), root->getRight());
        CHECK_EQ(button->getLeft(), 734);
        CHECK_EQ(button->getTop(), img->getBottom() - button->getHeight() / 2);
        CHECK_EQ(a->getLeft(), margin);
        CHECK_EQ(a->getTop(), button->getBottom() + margin);
        CHECK_EQ(b->getRight(), root->getRight() - margin);
        CHECK_EQ(b->getTop(), a->getTop());
        CHECK_EQ(c->getLeft(), 350);
        CHECK_EQ(c->getRight(), 450);
        CHECK_EQ(c->getTop(), a->getBottom() + margin);
}

TEST(ClCenterWrap_testWrapCenter) {
    using namespace kiln::constraintlayout::core;
    Widgets W;
        auto* root = W.make<ConstraintWidgetContainer>(0, 0, 800, 600);
        auto* TextBox = W.make<ConstraintWidget>(100, 50);
        auto* TextBoxGone = W.make<ConstraintWidget>(100, 50);
        auto* ValueBox = W.make<ConstraintWidget>(20, 20);
        root->setDebugName("root");
        TextBox->setDebugName("TextBox");
        TextBoxGone->setDebugName("TextBoxGone");
        ValueBox->setDebugName("ValueBox");
        TextBox->setHorizontalDimensionBehaviour(
                ConstraintWidget::DimensionBehaviour::MATCH_CONSTRAINT);
        TextBox->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP, 10);
        TextBox->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT, 10);
        TextBox->connect(ConstraintAnchor::Type::RIGHT, ValueBox, ConstraintAnchor::Type::LEFT, 10);
        ValueBox->connect(ConstraintAnchor::Type::RIGHT, root, ConstraintAnchor::Type::RIGHT, 10);
        ValueBox->connect(ConstraintAnchor::Type::TOP, TextBox, ConstraintAnchor::Type::TOP);
        ValueBox->connect(ConstraintAnchor::Type::BOTTOM, TextBox, ConstraintAnchor::Type::BOTTOM);
        TextBoxGone->setHorizontalDimensionBehaviour(
                ConstraintWidget::DimensionBehaviour::MATCH_CONSTRAINT);
        TextBoxGone->connect(ConstraintAnchor::Type::TOP, TextBox, ConstraintAnchor::Type::BOTTOM, 10);
        TextBoxGone->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT, 10);
        TextBoxGone->connect(ConstraintAnchor::Type::RIGHT, TextBox, ConstraintAnchor::Type::RIGHT);
        TextBoxGone->setVisibility(ConstraintWidget::GONE);
        root->add(TextBox);
        root->add(ValueBox);
        root->add(TextBoxGone);
        root->setVerticalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::WRAP_CONTENT);
        root->layout();
        CHECK_EQ(ValueBox->getTop(), TextBox->getTop() + ((TextBox->getHeight() - ValueBox->getHeight()) / 2));
        CHECK_EQ(root->getHeight(), 60);
}

