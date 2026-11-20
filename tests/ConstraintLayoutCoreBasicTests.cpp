// Ported from constraintlayout-core BasicTest.java (androidx), by tools in
// the scratchpad, then reviewed. Expected values are upstream's.
#include "ConstraintLayoutTestSupport.h"

namespace {
using namespace kiln::constraintlayout::core;
}

TEST(ClBasic_testWrapPercent) {
    using namespace kiln::constraintlayout::core;
    Widgets W;
        auto* root = W.make<ConstraintWidgetContainer>(0, 0, 600, 800);
        auto* a = W.make<ConstraintWidget>(100, 30);
        root->setDebugName("root");
        a->setDebugName("A");
        a->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::MATCH_CONSTRAINT);
        a->setHorizontalMatchStyle(ConstraintWidget::MATCH_CONSTRAINT_PERCENT, BasicMeasure::WRAP_CONTENT, 0, 0.5f);
        a->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT);
        root->add(a);
        root->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::WRAP_CONTENT);
        root->layout();
        CHECK_EQ(a->getWidth(), 100);
        CHECK_EQ(root->getWidth(), a->getWidth() * 2);
}

TEST(ClBasic_testMiddleSplit) {
    using namespace kiln::constraintlayout::core;
    Widgets W;
        auto* root = W.make<ConstraintWidgetContainer>(0, 0, 600, 800);
        root->setMeasurer(&sMeasurer);
        auto* a = W.make<ConstraintWidget>(400, 30);
        auto* b = W.make<ConstraintWidget>(400, 60);
        auto* guideline = W.make<Guideline>();
        auto* divider = W.make<ConstraintWidget>(100, 30);
        root->setDebugName("root");
        a->setDebugName("A");
        b->setDebugName("B");
        guideline->setDebugName("guideline");
        divider->setDebugName("divider");
        root->add(a);
        root->add(b);
        root->add(guideline);
        root->add(divider);
        guideline->setOrientation(Guideline::VERTICAL);
        guideline->setGuidePercent(0.5f);
        a->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::MATCH_CONSTRAINT);
        b->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::MATCH_CONSTRAINT);
        a->setHorizontalMatchStyle(ConstraintWidget::MATCH_CONSTRAINT_WRAP, 0, 0, 0);
        b->setHorizontalMatchStyle(ConstraintWidget::MATCH_CONSTRAINT_WRAP, 0, 0, 0);
        a->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT);
        a->connect(ConstraintAnchor::Type::RIGHT, guideline, ConstraintAnchor::Type::LEFT);
        b->connect(ConstraintAnchor::Type::LEFT, guideline, ConstraintAnchor::Type::RIGHT);
        b->connect(ConstraintAnchor::Type::RIGHT, root, ConstraintAnchor::Type::RIGHT);
        divider->setWidth(1);
        divider->setVerticalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::MATCH_CONSTRAINT);
        divider->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP);
        divider->connect(ConstraintAnchor::Type::BOTTOM, root, ConstraintAnchor::Type::BOTTOM);
        root->setVerticalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::WRAP_CONTENT);
        root->updateHierarchy();
        root->measure(Optimizer::OPTIMIZATION_NONE, BasicMeasure::EXACTLY, 600, BasicMeasure::EXACTLY, 800, 0, 0, 0, 0);
        CHECK_EQ(a->getWidth(), 300);
        CHECK_EQ(b->getWidth(), 300);
        CHECK_EQ(a->getLeft(), 0);
        CHECK_EQ(b->getLeft(), 300);
        CHECK_EQ(divider->getHeight(), 60);
        CHECK_EQ(root->getWidth(), 600);
        CHECK_EQ(root->getHeight(), 60);
}

TEST(ClBasic_testSimpleConstraint) {
    using namespace kiln::constraintlayout::core;
    Widgets W;
        auto* root = W.make<ConstraintWidgetContainer>(0, 0, 600, 800);
        root->setMeasurer(&sMeasurer);
        auto* a = W.make<ConstraintWidget>(100, 30);
        root->setDebugName("root");
        a->setDebugName("A");
        root->add(a);
        a->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::FIXED);
        a->setVerticalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::FIXED);
        a->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP, 8);
        a->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT, 8);
        root->setOptimizationLevel(Optimizer::OPTIMIZATION_GRAPH);
        root->measure(Optimizer::OPTIMIZATION_GRAPH, 0, 0, 0, 0, 0, 0, 0, 0);
}

TEST(ClBasic_testSimpleWrapConstraint9) {
    using namespace kiln::constraintlayout::core;
    Widgets W;
        auto* root = W.make<ConstraintWidgetContainer>(0, 0, 600, 800);
        root->setMeasurer(&sMeasurer);
        auto* a = W.make<ConstraintWidget>(100, 30);
        root->setDebugName("root");
        a->setDebugName("A");
        root->add(a);
        a->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::FIXED);
        a->setVerticalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::FIXED);
        int margin = 8;
        a->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP, margin);
        a->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT, margin);
        a->connect(ConstraintAnchor::Type::BOTTOM, root, ConstraintAnchor::Type::BOTTOM, margin);
        a->connect(ConstraintAnchor::Type::RIGHT, root, ConstraintAnchor::Type::RIGHT, margin);
        root->setOptimizationLevel(Optimizer::OPTIMIZATION_GRAPH_WRAP);
        root->measure(Optimizer::OPTIMIZATION_GRAPH_WRAP, 0, 0, 0, 0, 0, 0, 0, 0);
        root->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::WRAP_CONTENT);
        root->setVerticalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::WRAP_CONTENT);
        root->measure(Optimizer::OPTIMIZATION_GRAPH_WRAP, 0, 0, 0, 0, 0, 0, 0, 0);
        CHECK_EQ(root->getWidth(), 116);
        CHECK_EQ(root->getHeight(), 46);
}

TEST(ClBasic_testSimpleWrapConstraint10) {
    using namespace kiln::constraintlayout::core;
    Widgets W;
        auto* root = W.make<ConstraintWidgetContainer>(0, 0, 600, 800);
        root->setMeasurer(&sMeasurer);
        auto* a = W.make<ConstraintWidget>(100, 30);
        root->setDebugName("root");
        a->setDebugName("A");
        root->add(a);
        a->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::FIXED);
        a->setVerticalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::FIXED);
        int margin = 8;
        a->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP, margin);
        a->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT, margin);
        a->connect(ConstraintAnchor::Type::BOTTOM, root, ConstraintAnchor::Type::BOTTOM, margin);
        a->connect(ConstraintAnchor::Type::RIGHT, root, ConstraintAnchor::Type::RIGHT, margin);
        root->measure(Optimizer::OPTIMIZATION_NONE, 0, 0, 0, 0, 0, 0, 0, 0);
        root->layout();
        root->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::WRAP_CONTENT);
        root->measure(Optimizer::OPTIMIZATION_GRAPH,
                BasicMeasure::WRAP_CONTENT, 0, BasicMeasure::EXACTLY, 800, 0, 0, 0, 0);
        CHECK_EQ(root->getWidth(), 116);
        CHECK_EQ(root->getHeight(), 800);
        CHECK_EQ(a->getLeft(), 8);
        CHECK_EQ(a->getTop(), 385);
        CHECK_EQ(a->getWidth(), 100);
        CHECK_EQ(a->getHeight(), 30);
}

TEST(ClBasic_testSimpleWrapConstraint11) {
    using namespace kiln::constraintlayout::core;
    Widgets W;
        auto* root = W.make<ConstraintWidgetContainer>(0, 0, 600, 800);
        root->setMeasurer(&sMeasurer);
        auto* a = W.make<ConstraintWidget>(10, 30);
        auto* b = W.make<ConstraintWidget>(800, 30);
        auto* c = W.make<ConstraintWidget>(10, 30);
        auto* d = W.make<ConstraintWidget>(800, 30);
        root->setDebugName("root");
        a->setDebugName("A");
        b->setDebugName("B");
        c->setDebugName("C");
        d->setDebugName("D");
        root->add(a);
        root->add(b);
        root->add(c);
        root->add(d);
        b->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::MATCH_CONSTRAINT);
        b->setVerticalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::FIXED);
        b->setHorizontalMatchStyle(ConstraintWidget::MATCH_CONSTRAINT_WRAP, 0, 0, 0);
        a->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP);
        b->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP);
        c->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP);
        d->connect(ConstraintAnchor::Type::TOP, b, ConstraintAnchor::Type::BOTTOM);
        a->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT);
        a->connect(ConstraintAnchor::Type::RIGHT, b, ConstraintAnchor::Type::LEFT);
        b->connect(ConstraintAnchor::Type::LEFT, a, ConstraintAnchor::Type::RIGHT);
        b->connect(ConstraintAnchor::Type::RIGHT, c, ConstraintAnchor::Type::LEFT);
        c->connect(ConstraintAnchor::Type::LEFT, b, ConstraintAnchor::Type::RIGHT);
        c->connect(ConstraintAnchor::Type::RIGHT, root, ConstraintAnchor::Type::RIGHT);
        d->connect(ConstraintAnchor::Type::LEFT, a, ConstraintAnchor::Type::RIGHT);
        d->connect(ConstraintAnchor::Type::RIGHT, c, ConstraintAnchor::Type::LEFT);
        root->layout();
        CHECK_EQ(a->getLeft(), 0);
        CHECK_EQ(a->getWidth(), 10);
        CHECK_EQ(c->getWidth(), 10);
        CHECK_EQ(b->getLeft(), a->getRight());
        CHECK_EQ(b->getWidth(), root->getWidth() - a->getWidth() - c->getWidth());
        CHECK_EQ(c->getLeft(), root->getWidth() - c->getWidth());
        CHECK_EQ(d->getWidth(), 800);
        CHECK_EQ(d->getLeft(), -99);
}

TEST(ClBasic_testSimpleWrapConstraint) {
    using namespace kiln::constraintlayout::core;
    Widgets W;
        auto* root = W.make<ConstraintWidgetContainer>(0, 0, 600, 800);
        root->setMeasurer(&sMeasurer);
        auto* a = W.make<ConstraintWidget>(100, 30);
        auto* b = W.make<ConstraintWidget>(100, 60);
        root->setDebugName("root");
        a->setDebugName("A");
        b->setDebugName("B");
        root->add(a);
        root->add(b);
        a->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::FIXED);
        a->setVerticalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::FIXED);
        b->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::FIXED);
        b->setVerticalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::FIXED);
        a->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP, 8);
        b->connect(ConstraintAnchor::Type::BOTTOM, root, ConstraintAnchor::Type::BOTTOM, 8);
        a->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT, 8);
        b->connect(ConstraintAnchor::Type::LEFT, a, ConstraintAnchor::Type::RIGHT, 8);
        root->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::WRAP_CONTENT);
        root->setVerticalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::WRAP_CONTENT);
        root->measure(Optimizer::OPTIMIZATION_STANDARD,
                BasicMeasure::WRAP_CONTENT, 0, BasicMeasure::WRAP_CONTENT, 0, 0, 0, 0, 0);
        CHECK_EQ(root->getWidth(), 216);
        CHECK_EQ(root->getHeight(), 68);
        CHECK_EQ(a->getLeft(), 8);
        CHECK_EQ(a->getTop(), 8);
        CHECK_EQ(a->getWidth(), 100);
        CHECK_EQ(a->getHeight(), 30);
        CHECK_EQ(b->getLeft(), 116);
        CHECK_EQ(b->getTop(), 0);
        CHECK_EQ(b->getWidth(), 100);
        CHECK_EQ(b->getHeight(), 60);
        root->measure(Optimizer::OPTIMIZATION_GRAPH,
                BasicMeasure::WRAP_CONTENT, 0, BasicMeasure::WRAP_CONTENT, 0, 0, 0, 0, 0);
        CHECK_EQ(root->getWidth(), 216);
        CHECK_EQ(root->getHeight(), 68);
        CHECK_EQ(a->getLeft(), 8);
        CHECK_EQ(a->getTop(), 8);
        CHECK_EQ(a->getWidth(), 100);
        CHECK_EQ(a->getHeight(), 30);
        CHECK_EQ(b->getLeft(), 116);
        CHECK_EQ(b->getTop(), 0);
        CHECK_EQ(b->getWidth(), 100);
        CHECK_EQ(b->getHeight(), 60);
}

TEST(ClBasic_testSimpleWrapConstraint2) {
    using namespace kiln::constraintlayout::core;
    Widgets W;
        auto* root = W.make<ConstraintWidgetContainer>(0, 0, 600, 800);
        root->setMeasurer(&sMeasurer);
        auto* a = W.make<ConstraintWidget>(100, 30);
        auto* b = W.make<ConstraintWidget>(120, 60);
        root->setDebugName("root");
        a->setDebugName("A");
        b->setDebugName("B");
        root->add(a);
        root->add(b);
        a->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::FIXED);
        a->setVerticalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::FIXED);
        b->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::FIXED);
        b->setVerticalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::FIXED);
        a->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP, 8);
        b->connect(ConstraintAnchor::Type::TOP, a, ConstraintAnchor::Type::BOTTOM, 8);
        b->connect(ConstraintAnchor::Type::BOTTOM, root, ConstraintAnchor::Type::BOTTOM, 8);
        a->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT, 8);
        b->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT, 8);
        root->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::WRAP_CONTENT);
        root->setVerticalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::WRAP_CONTENT);
        root->measure(Optimizer::OPTIMIZATION_STANDARD,
                BasicMeasure::WRAP_CONTENT, 0, BasicMeasure::WRAP_CONTENT, 0, 0, 0, 0, 0);
        CHECK_EQ(root->getWidth(), 128);
        CHECK_EQ(root->getHeight(), 114);
        CHECK_EQ(a->getLeft(), 8);
        CHECK_EQ(a->getTop(), 8);
        CHECK_EQ(a->getWidth(), 100);
        CHECK_EQ(a->getHeight(), 30);
        CHECK_EQ(b->getLeft(), 8);
        CHECK_EQ(b->getTop(), 46);
        CHECK_EQ(b->getWidth(), 120);
        CHECK_EQ(b->getHeight(), 60);
        root->measure(Optimizer::OPTIMIZATION_GRAPH,
                BasicMeasure::WRAP_CONTENT, 0, BasicMeasure::WRAP_CONTENT, 0, 0, 0, 0, 0);
        CHECK_EQ(root->getWidth(), 128);
        CHECK_EQ(root->getHeight(), 114);
        CHECK_EQ(a->getLeft(), 8);
        CHECK_EQ(a->getTop(), 8);
        CHECK_EQ(a->getWidth(), 100);
        CHECK_EQ(a->getHeight(), 30);
        CHECK_EQ(b->getLeft(), 8);
        CHECK_EQ(b->getTop(), 46);
        CHECK_EQ(b->getWidth(), 120);
        CHECK_EQ(b->getHeight(), 60);
}

TEST(ClBasic_testSimpleWrapConstraint3) {
    using namespace kiln::constraintlayout::core;
    Widgets W;
        auto* root = W.make<ConstraintWidgetContainer>(0, 0, 600, 800);
        root->setMeasurer(&sMeasurer);
        auto* a = W.make<ConstraintWidget>(100, 30);
        root->setDebugName("root");
        a->setDebugName("A");
        root->add(a);
        a->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::FIXED);
        a->setVerticalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::FIXED);
        a->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP, 8);
        a->connect(ConstraintAnchor::Type::BOTTOM, root, ConstraintAnchor::Type::BOTTOM, 8);
        a->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT, 8);
        a->connect(ConstraintAnchor::Type::RIGHT, root, ConstraintAnchor::Type::RIGHT, 8);
        root->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::WRAP_CONTENT);
        root->setVerticalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::WRAP_CONTENT);
        root->measure(Optimizer::OPTIMIZATION_STANDARD,
                BasicMeasure::WRAP_CONTENT, 0, BasicMeasure::WRAP_CONTENT, 0, 0, 0, 0, 0);
        CHECK_EQ(root->getWidth(), 116);
        CHECK_EQ(root->getHeight(), 46);
        CHECK_EQ(a->getLeft(), 8);
        CHECK_EQ(a->getTop(), 8);
        CHECK_EQ(a->getWidth(), 100);
        CHECK_EQ(a->getHeight(), 30);
        root->measure(Optimizer::OPTIMIZATION_GRAPH,
                BasicMeasure::WRAP_CONTENT, 0, BasicMeasure::WRAP_CONTENT, 0, 0, 0, 0, 0);
        CHECK_EQ(root->getWidth(), 116);
        CHECK_EQ(root->getHeight(), 46);
        CHECK_EQ(a->getLeft(), 8);
        CHECK_EQ(a->getTop(), 8);
        CHECK_EQ(a->getWidth(), 100);
        CHECK_EQ(a->getHeight(), 30);
}

TEST(ClBasic_testSimpleWrapConstraint4) {
    using namespace kiln::constraintlayout::core;
    Widgets W;
        auto* root = W.make<ConstraintWidgetContainer>(0, 0, 600, 800);
        root->setMeasurer(&sMeasurer);
        auto* a = W.make<ConstraintWidget>(100, 30);
        auto* b = W.make<ConstraintWidget>(100, 30);
        auto* c = W.make<ConstraintWidget>(100, 30);
        auto* d = W.make<ConstraintWidget>(100, 30);
        root->setDebugName("root");
        a->setDebugName("A");
        b->setDebugName("B");
        c->setDebugName("C");
        d->setDebugName("D");
        root->add(a);
        root->add(b);
        root->add(c);
        root->add(d);
        a->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::FIXED);
        a->setVerticalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::FIXED);
        b->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::FIXED);
        b->setVerticalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::FIXED);
        c->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::FIXED);
        c->setVerticalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::FIXED);
        d->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::FIXED);
        d->setVerticalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::FIXED);
        a->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP, 8);
        a->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT, 8);
        b->connect(ConstraintAnchor::Type::TOP, a, ConstraintAnchor::Type::BOTTOM, 8);
        b->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT, 8);
        b->connect(ConstraintAnchor::Type::RIGHT, root, ConstraintAnchor::Type::RIGHT, 8);
        c->connect(ConstraintAnchor::Type::BOTTOM, b, ConstraintAnchor::Type::TOP, 8);
        c->connect(ConstraintAnchor::Type::LEFT, b, ConstraintAnchor::Type::RIGHT, 8);
        d->connect(ConstraintAnchor::Type::BOTTOM, c, ConstraintAnchor::Type::TOP, 8);
        d->connect(ConstraintAnchor::Type::LEFT, c, ConstraintAnchor::Type::RIGHT, 8);
        root->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::WRAP_CONTENT);
        root->setVerticalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::WRAP_CONTENT);
        root->measure(Optimizer::OPTIMIZATION_STANDARD,
                BasicMeasure::WRAP_CONTENT, 0, BasicMeasure::WRAP_CONTENT, 0, 0, 0, 0, 0);
        CHECK_EQ(root->getWidth(), 532);
        CHECK_EQ(root->getHeight(), 76);
        CHECK_EQ(a->getLeft(), 8);
        CHECK_EQ(a->getTop(), 8);
        CHECK_EQ(a->getWidth(), 100);
        CHECK_EQ(a->getHeight(), 30);
        CHECK_EQ(b->getLeft(), 216);
        CHECK_EQ(b->getTop(), 46);
        CHECK_EQ(b->getWidth(), 100);
        CHECK_EQ(b->getHeight(), 30);
        CHECK_EQ(c->getLeft(), 324);
        CHECK_EQ(c->getTop(), 8);
        CHECK_EQ(c->getWidth(), 100);
        CHECK_EQ(c->getHeight(), 30);
        CHECK_EQ(d->getLeft(), 432);
        CHECK_NEAR(d->getTop(), -28, 2);
        CHECK_EQ(d->getWidth(), 100);
        CHECK_EQ(d->getHeight(), 30);
        root->measure(Optimizer::OPTIMIZATION_GRAPH,
                BasicMeasure::WRAP_CONTENT, 0, BasicMeasure::WRAP_CONTENT, 0, 0, 0, 0, 0);
        CHECK_EQ(root->getWidth(), 532);
        CHECK_EQ(root->getHeight(), 76);
        CHECK_EQ(a->getLeft(), 8);
        CHECK_EQ(a->getTop(), 8);
        CHECK_EQ(a->getWidth(), 100);
        CHECK_EQ(a->getHeight(), 30);
        CHECK_EQ(b->getLeft(), 216);
        CHECK_EQ(b->getTop(), 46);
        CHECK_EQ(b->getWidth(), 100);
        CHECK_EQ(b->getHeight(), 30);
        CHECK_EQ(c->getLeft(), 324);
        CHECK_EQ(c->getTop(), 8);
        CHECK_EQ(c->getWidth(), 100);
        CHECK_EQ(c->getHeight(), 30);
        CHECK_EQ(d->getLeft(), 432);
        CHECK_NEAR(d->getTop(), -28, 2);
        CHECK_EQ(d->getWidth(), 100);
        CHECK_EQ(d->getHeight(), 30);
}

TEST(ClBasic_testSimpleWrapConstraint5) {
    using namespace kiln::constraintlayout::core;
    Widgets W;
        auto* root = W.make<ConstraintWidgetContainer>(0, 0, 600, 800);
        root->setMeasurer(&sMeasurer);
        auto* a = W.make<ConstraintWidget>(100, 30);
        auto* b = W.make<ConstraintWidget>(100, 30);
        auto* c = W.make<ConstraintWidget>(100, 30);
        auto* d = W.make<ConstraintWidget>(100, 30);
        root->setDebugName("root");
        a->setDebugName("A");
        b->setDebugName("B");
        c->setDebugName("C");
        d->setDebugName("D");
        root->add(a);
        root->add(b);
        root->add(c);
        root->add(d);
        a->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::FIXED);
        a->setVerticalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::FIXED);
        b->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::FIXED);
        b->setVerticalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::FIXED);
        c->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::FIXED);
        c->setVerticalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::FIXED);
        d->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::FIXED);
        d->setVerticalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::FIXED);
        a->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP, 8);
        a->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT, 8);
        b->connect(ConstraintAnchor::Type::TOP, a, ConstraintAnchor::Type::BOTTOM, 8);
        b->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT, 8);
        b->connect(ConstraintAnchor::Type::RIGHT, root, ConstraintAnchor::Type::RIGHT, 8);
        b->setHorizontalBiasPercent(0.2f);
        c->connect(ConstraintAnchor::Type::BOTTOM, b, ConstraintAnchor::Type::TOP, 8);
        c->connect(ConstraintAnchor::Type::LEFT, b, ConstraintAnchor::Type::RIGHT, 8);
        d->connect(ConstraintAnchor::Type::BOTTOM, c, ConstraintAnchor::Type::TOP, 8);
        d->connect(ConstraintAnchor::Type::LEFT, c, ConstraintAnchor::Type::RIGHT, 8);
        root->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::WRAP_CONTENT);
        root->setVerticalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::WRAP_CONTENT);
        root->measure(Optimizer::OPTIMIZATION_STANDARD,
                BasicMeasure::WRAP_CONTENT, 0, BasicMeasure::WRAP_CONTENT, 0, 0, 0, 0, 0);
        CHECK_EQ(root->getWidth(), 376);
        CHECK_EQ(root->getHeight(), 76);
        CHECK_EQ(a->getLeft(), 8);
        CHECK_EQ(a->getTop(), 8);
        CHECK_EQ(a->getWidth(), 100);
        CHECK_EQ(a->getHeight(), 30);
        CHECK_EQ(b->getLeft(), 60);
        CHECK_EQ(b->getTop(), 46);
        CHECK_EQ(b->getWidth(), 100);
        CHECK_EQ(b->getHeight(), 30);
        CHECK_EQ(c->getLeft(), 168);
        CHECK_EQ(c->getTop(), 8);
        CHECK_EQ(c->getWidth(), 100);
        CHECK_EQ(c->getHeight(), 30);
        CHECK_EQ(d->getLeft(), 276);
        CHECK_NEAR(d->getTop(), -28, 2);
        CHECK_EQ(d->getWidth(), 100);
        CHECK_EQ(d->getHeight(), 30);
        root->measure(Optimizer::OPTIMIZATION_GRAPH,
                BasicMeasure::WRAP_CONTENT, 0, BasicMeasure::WRAP_CONTENT, 0, 0, 0, 0, 0);
        CHECK_EQ(root->getWidth(), 376);
        CHECK_EQ(root->getHeight(), 76);
        CHECK_EQ(a->getLeft(), 8);
        CHECK_EQ(a->getTop(), 8);
        CHECK_EQ(a->getWidth(), 100);
        CHECK_EQ(a->getHeight(), 30);
        CHECK_EQ(b->getLeft(), 60);
        CHECK_EQ(b->getTop(), 46);
        CHECK_EQ(b->getWidth(), 100);
        CHECK_EQ(b->getHeight(), 30);
        CHECK_EQ(c->getLeft(), 168);
        CHECK_EQ(c->getTop(), 8);
        CHECK_EQ(c->getWidth(), 100);
        CHECK_EQ(c->getHeight(), 30);
        CHECK_EQ(d->getLeft(), 276);
        CHECK_NEAR(d->getTop(), -28, 2);
        CHECK_EQ(d->getWidth(), 100);
        CHECK_EQ(d->getHeight(), 30);
}

TEST(ClBasic_testSimpleWrapConstraint6) {
    using namespace kiln::constraintlayout::core;
    Widgets W;
        auto* root = W.make<ConstraintWidgetContainer>(0, 0, 600, 800);
        root->setMeasurer(&sMeasurer);
        auto* a = W.make<ConstraintWidget>(100, 30);
        auto* b = W.make<ConstraintWidget>(100, 30);
        auto* c = W.make<ConstraintWidget>(100, 30);
        auto* d = W.make<ConstraintWidget>(100, 30);
        root->setDebugName("root");
        a->setDebugName("A");
        b->setDebugName("B");
        c->setDebugName("C");
        d->setDebugName("D");
        root->add(a);
        root->add(b);
        root->add(c);
        root->add(d);
        a->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::FIXED);
        a->setVerticalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::FIXED);
        b->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::FIXED);
        b->setVerticalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::FIXED);
        c->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::FIXED);
        c->setVerticalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::FIXED);
        d->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::FIXED);
        d->setVerticalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::FIXED);
        a->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP, 8);
        a->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT, 8);
        b->connect(ConstraintAnchor::Type::TOP, a, ConstraintAnchor::Type::BOTTOM, 8);
        b->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT, 33);
        b->connect(ConstraintAnchor::Type::RIGHT, root, ConstraintAnchor::Type::RIGHT, 16);
        b->setHorizontalBiasPercent(0.15f);
        c->connect(ConstraintAnchor::Type::BOTTOM, b, ConstraintAnchor::Type::TOP, 8);
        c->connect(ConstraintAnchor::Type::LEFT, b, ConstraintAnchor::Type::RIGHT, 12);
        d->connect(ConstraintAnchor::Type::BOTTOM, c, ConstraintAnchor::Type::TOP, 8);
        d->connect(ConstraintAnchor::Type::LEFT, c, ConstraintAnchor::Type::RIGHT, 8);
        root->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::WRAP_CONTENT);
        root->setVerticalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::WRAP_CONTENT);
        root->measure(Optimizer::OPTIMIZATION_STANDARD,
                BasicMeasure::WRAP_CONTENT, 0, BasicMeasure::WRAP_CONTENT, 0, 0, 0, 0, 0);
        CHECK_EQ(root->getWidth(), 389);
        CHECK_EQ(root->getHeight(), 76);
        CHECK_EQ(a->getLeft(), 8);
        CHECK_EQ(a->getTop(), 8);
        CHECK_EQ(a->getWidth(), 100);
        CHECK_EQ(a->getHeight(), 30);
        CHECK_EQ(b->getLeft(), 69);
        CHECK_EQ(b->getTop(), 46);
        CHECK_EQ(b->getWidth(), 100);
        CHECK_EQ(b->getHeight(), 30);
        CHECK_EQ(c->getLeft(), 181);
        CHECK_EQ(c->getTop(), 8);
        CHECK_EQ(c->getWidth(), 100);
        CHECK_EQ(c->getHeight(), 30);
        CHECK_EQ(d->getLeft(), 289);
        CHECK_NEAR(d->getTop(), -28, 2);
        CHECK_EQ(d->getWidth(), 100);
        CHECK_EQ(d->getHeight(), 30);
        root->measure(Optimizer::OPTIMIZATION_GRAPH,
                BasicMeasure::WRAP_CONTENT, 0, BasicMeasure::WRAP_CONTENT, 0, 0, 0, 0, 0);
        CHECK_EQ(root->getWidth(), 389);
        CHECK_EQ(root->getHeight(), 76);
        CHECK_EQ(a->getLeft(), 8);
        CHECK_EQ(a->getTop(), 8);
        CHECK_EQ(a->getWidth(), 100);
        CHECK_EQ(a->getHeight(), 30);
        CHECK_EQ(b->getLeft(), 69);
        CHECK_EQ(b->getTop(), 46);
        CHECK_EQ(b->getWidth(), 100);
        CHECK_EQ(b->getHeight(), 30);
        CHECK_EQ(c->getLeft(), 181);
        CHECK_EQ(c->getTop(), 8);
        CHECK_EQ(c->getWidth(), 100);
        CHECK_EQ(c->getHeight(), 30);
        CHECK_EQ(d->getLeft(), 289);
        CHECK_NEAR(d->getTop(), -28, 2);
        CHECK_EQ(d->getWidth(), 100);
        CHECK_EQ(d->getHeight(), 30);
}

TEST(ClBasic_testSimpleWrapConstraint7) {
    using namespace kiln::constraintlayout::core;
    Widgets W;
        auto* root = W.make<ConstraintWidgetContainer>(0, 0, 600, 800);
        root->setMeasurer(&sMeasurer);
        auto* a = W.make<ConstraintWidget>(100, 30);
        root->setDebugName("root");
        a->setDebugName("A");
        root->add(a);
        a->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::MATCH_CONSTRAINT);
        a->setVerticalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::FIXED);
        a->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP, 8);
        a->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT, 8);
        a->connect(ConstraintAnchor::Type::RIGHT, root, ConstraintAnchor::Type::RIGHT, 8);
        root->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::WRAP_CONTENT);
        root->setVerticalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::WRAP_CONTENT);
        root->measure(Optimizer::OPTIMIZATION_STANDARD,
                BasicMeasure::WRAP_CONTENT, 0, BasicMeasure::WRAP_CONTENT, 0, 0, 0, 0, 0);
        CHECK_EQ(root->getWidth(), 16);
        CHECK_EQ(root->getHeight(), 38);
        CHECK_EQ(a->getLeft(), 8);
        CHECK_EQ(a->getTop(), 8);
        CHECK_EQ(a->getWidth(), 0);
        CHECK_EQ(a->getHeight(), 30);
        root->measure(Optimizer::OPTIMIZATION_GRAPH,
                BasicMeasure::WRAP_CONTENT, 0, BasicMeasure::WRAP_CONTENT, 0, 0, 0, 0, 0);
        CHECK_EQ(root->getWidth(), 16);
        CHECK_EQ(root->getHeight(), 38);
        CHECK_EQ(a->getLeft(), 8);
        CHECK_EQ(a->getTop(), 8);
        CHECK_EQ(a->getWidth(), 0);
        CHECK_EQ(a->getHeight(), 30);
}

TEST(ClBasic_testSimpleWrapConstraint8) {
    using namespace kiln::constraintlayout::core;
    Widgets W;
        auto* root = W.make<ConstraintWidgetContainer>(0, 0, 600, 800);
        root->setMeasurer(&sMeasurer);
        auto* a = W.make<ConstraintWidget>(100, 30);
        auto* b = W.make<ConstraintWidget>(10, 30);
        auto* c = W.make<ConstraintWidget>(10, 30);
        auto* d = W.make<ConstraintWidget>(100, 30);
        root->setDebugName("root");
        a->setDebugName("A");
        b->setDebugName("B");
        c->setDebugName("C");
        d->setDebugName("D");
        root->add(a);
        root->add(b);
        root->add(c);
        root->add(d);
        a->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::FIXED);
        b->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::MATCH_CONSTRAINT);
        c->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::FIXED);
        d->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::MATCH_CONSTRAINT);
        a->setVerticalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::FIXED);
        b->setVerticalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::FIXED);
        c->setVerticalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::FIXED);
        d->setVerticalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::FIXED);
        applyChain(ConstraintWidget::HORIZONTAL, {a, b, c, d});
        a->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT);
        d->connect(ConstraintAnchor::Type::RIGHT, root, ConstraintAnchor::Type::RIGHT);
        root->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::WRAP_CONTENT);
        root->setVerticalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::WRAP_CONTENT);
        root->measure(Optimizer::OPTIMIZATION_STANDARD,
                BasicMeasure::WRAP_CONTENT, 0, BasicMeasure::WRAP_CONTENT, 0, 0, 0, 0, 0);
        CHECK_EQ(root->getWidth(), 110);
        CHECK_EQ(root->getHeight(), 30);
        root->measure(Optimizer::OPTIMIZATION_GRAPH,
                BasicMeasure::WRAP_CONTENT, 0, BasicMeasure::WRAP_CONTENT, 0, 0, 0, 0, 0);
        CHECK_EQ(root->getWidth(), 110);
        CHECK_EQ(root->getHeight(), 30);
}

TEST(ClBasic_testSimpleCircleConstraint) {
    using namespace kiln::constraintlayout::core;
    Widgets W;
        auto* root = W.make<ConstraintWidgetContainer>(0, 0, 600, 800);
        root->setMeasurer(&sMeasurer);
        auto* a = W.make<ConstraintWidget>(100, 30);
        auto* b = W.make<ConstraintWidget>(100, 30);
        root->setDebugName("root");
        a->setDebugName("A");
        b->setDebugName("B");
        root->add(a);
        root->add(b);
        a->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::FIXED);
        a->setVerticalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::FIXED);
        b->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::FIXED);
        b->setVerticalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::FIXED);
        a->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP, 8);
        a->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT, 8);
        b->connectCircularConstraint(a, 30, 50);
        root->setOptimizationLevel(Optimizer::OPTIMIZATION_GRAPH);
        root->measure(Optimizer::OPTIMIZATION_GRAPH, BasicMeasure::EXACTLY, 600, BasicMeasure::EXACTLY, 800, 0, 0, 0, 0);
}

TEST(ClBasic_testRatioChainConstraint) {
    using namespace kiln::constraintlayout::core;
    Widgets W;
        auto* root = W.make<ConstraintWidgetContainer>(0, 0, 600, 800);
        root->setMeasurer(&sMeasurer);
        auto* a = W.make<ConstraintWidget>(100, 30);
        auto* b = W.make<ConstraintWidget>(0, 30);
        auto* c = W.make<ConstraintWidget>(0, 30);
        auto* d = W.make<ConstraintWidget>(100, 30);
        root->setDebugName("root");
        a->setDebugName("A");
        b->setDebugName("B");
        root->add(a);
        root->add(b);
        root->add(c);
        root->add(d);
        a->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::FIXED);
        b->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::MATCH_CONSTRAINT);
        c->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::MATCH_CONSTRAINT);
        d->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::FIXED);
        b->setDimensionRatio("w,1:1");
        a->setVerticalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::FIXED);
        b->setVerticalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::FIXED);
        c->setVerticalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::FIXED);
        d->setVerticalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::FIXED);
        applyChain(ConstraintWidget::HORIZONTAL, {a, b, c, d});
        a->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT);
        d->connect(ConstraintAnchor::Type::RIGHT, root, ConstraintAnchor::Type::RIGHT);
        root->setOptimizationLevel(Optimizer::OPTIMIZATION_GRAPH);
        root->measure(Optimizer::OPTIMIZATION_GRAPH, BasicMeasure::EXACTLY, 600, BasicMeasure::EXACTLY, 800, 0, 0, 0, 0);
}

TEST(ClBasic_testCycleConstraints) {
    using namespace kiln::constraintlayout::core;
    Widgets W;
        auto* root = W.make<ConstraintWidgetContainer>(0, 0, 600, 800);
        root->setMeasurer(&sMeasurer);
        auto* a = W.make<ConstraintWidget>(100, 30);
        auto* b = W.make<ConstraintWidget>(40, 20);
        auto* c = W.make<ConstraintWidget>(40, 20);
        auto* d = W.make<ConstraintWidget>(30, 30);
        root->setDebugName("root");
        a->setDebugName("A");
        b->setDebugName("B");
        c->setDebugName("C");
        d->setDebugName("D");
        root->add(a);
        root->add(b);
        root->add(c);
        root->add(d);
        a->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT);
        a->connect(ConstraintAnchor::Type::RIGHT, root, ConstraintAnchor::Type::RIGHT);
        a->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP);
        b->connect(ConstraintAnchor::Type::TOP, a, ConstraintAnchor::Type::BOTTOM);
        b->connect(ConstraintAnchor::Type::LEFT, c, ConstraintAnchor::Type::LEFT);
        c->connect(ConstraintAnchor::Type::TOP, b, ConstraintAnchor::Type::BOTTOM);
        c->connect(ConstraintAnchor::Type::BOTTOM, root, ConstraintAnchor::Type::BOTTOM);
        c->connect(ConstraintAnchor::Type::LEFT, d, ConstraintAnchor::Type::RIGHT);
        d->connect(ConstraintAnchor::Type::TOP, b, ConstraintAnchor::Type::TOP);
        d->connect(ConstraintAnchor::Type::BOTTOM, c, ConstraintAnchor::Type::BOTTOM);
        d->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT);
        root->setVerticalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::WRAP_CONTENT);
        root->measure(Optimizer::OPTIMIZATION_NONE, BasicMeasure::EXACTLY, 600, BasicMeasure::EXACTLY, 800, 0, 0, 0, 0);
        CHECK_EQ(a->getTop(), 0);
        CHECK_EQ(b->getTop(), 30);
        CHECK_EQ(c->getTop(), 50);
        CHECK_EQ(d->getTop(), 35);
}

TEST(ClBasic_testGoneChain) {
    using namespace kiln::constraintlayout::core;
    Widgets W;
        auto* root = W.make<ConstraintWidgetContainer>(0, 0, 600, 800);
        root->setMeasurer(&sMeasurer);
        auto* a = W.make<ConstraintWidget>(100, 30);
        auto* b = W.make<ConstraintWidget>(100, 30);
        auto* c = W.make<ConstraintWidget>(100, 30);
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
        a->setVisibility(ConstraintWidget::GONE);
        c->setVisibility(ConstraintWidget::GONE);
        root->measure(Optimizer::OPTIMIZATION_NONE, BasicMeasure::EXACTLY, 600, BasicMeasure::EXACTLY, 800, 0, 0, 0, 0);
        CHECK_EQ(b->getWidth(), root->getWidth());
}

TEST(ClBasic_testGoneChainWithCenterWidget) {
    using namespace kiln::constraintlayout::core;
    Widgets W;
        auto* root = W.make<ConstraintWidgetContainer>(0, 0, 600, 800);
        root->setMeasurer(&sMeasurer);
        auto* a = W.make<ConstraintWidget>(100, 30);
        auto* b = W.make<ConstraintWidget>(100, 30);
        auto* c = W.make<ConstraintWidget>(100, 30);
        auto* d = W.make<ConstraintWidget>(100, 30);
        root->setDebugName("root");
        a->setDebugName("A");
        b->setDebugName("B");
        c->setDebugName("C");
        d->setDebugName("D");
        root->add(a);
        root->add(b);
        root->add(c);
        root->add(d);
        a->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT);
        a->connect(ConstraintAnchor::Type::RIGHT, b, ConstraintAnchor::Type::LEFT);
        b->connect(ConstraintAnchor::Type::LEFT, a, ConstraintAnchor::Type::RIGHT);
        b->connect(ConstraintAnchor::Type::RIGHT, c, ConstraintAnchor::Type::LEFT);
        c->connect(ConstraintAnchor::Type::LEFT, b, ConstraintAnchor::Type::RIGHT);
        c->connect(ConstraintAnchor::Type::RIGHT, root, ConstraintAnchor::Type::RIGHT);
        b->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::MATCH_CONSTRAINT);
        a->setVisibility(ConstraintWidget::GONE);
        c->setVisibility(ConstraintWidget::GONE);
        d->connect(ConstraintAnchor::Type::LEFT, b, ConstraintAnchor::Type::LEFT);
        d->connect(ConstraintAnchor::Type::RIGHT, b, ConstraintAnchor::Type::RIGHT);
        d->setVisibility(ConstraintWidget::GONE);
        root->measure(Optimizer::OPTIMIZATION_NONE, BasicMeasure::EXACTLY, 600, BasicMeasure::EXACTLY, 800, 0, 0, 0, 0);
        CHECK_EQ(b->getWidth(), root->getWidth());
}

TEST(ClBasic_testBarrier) {
    using namespace kiln::constraintlayout::core;
    Widgets W;
        auto* root = W.make<ConstraintWidgetContainer>(0, 0, 600, 800);
        root->setMeasurer(&sMeasurer);
        auto* a = W.make<ConstraintWidget>(100, 30);
        auto* b = W.make<ConstraintWidget>(100, 30);
        auto* c = W.make<ConstraintWidget>(100, 30);
        auto* d = W.make<ConstraintWidget>(100, 30);
        auto* barrier1 = W.make<Barrier>();
        root->setDebugName("root");
        a->setDebugName("A");
        b->setDebugName("B");
        c->setDebugName("C");
        d->setDebugName("D");
        barrier1->setDebugName("barrier1");
        root->add(a);
        root->add(b);
        root->add(c);
        root->add(d);
        root->add(barrier1);
        a->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP);
        barrier1->add(a);
        barrier1->setBarrierType(Barrier::BOTTOM);
        b->connect(ConstraintAnchor::Type::TOP, barrier1, ConstraintAnchor::Type::BOTTOM);
        c->connect(ConstraintAnchor::Type::TOP, b, ConstraintAnchor::Type::BOTTOM);
        d->connect(ConstraintAnchor::Type::TOP, c, ConstraintAnchor::Type::BOTTOM);
        d->connect(ConstraintAnchor::Type::BOTTOM, root, ConstraintAnchor::Type::BOTTOM);
        root->measure(Optimizer::OPTIMIZATION_NONE, BasicMeasure::EXACTLY, 600, BasicMeasure::EXACTLY, 800, 0, 0, 0, 0);
        CHECK_EQ(a->getTop(), 0);
        CHECK_EQ(b->getTop(), a->getBottom());
        CHECK_EQ(barrier1->getTop(), a->getBottom());
        CHECK_EQ(c->getTop(), b->getBottom());
        CHECK_EQ(d->getTop(), 430);
}

TEST(ClBasic_testDirectCentering) {
    using namespace kiln::constraintlayout::core;
    Widgets W;
        auto* root = W.make<ConstraintWidgetContainer>(0, 0, 192, 168);
        root->setMeasurer(&sMeasurer);
        auto* a = W.make<ConstraintWidget>(43, 43);
        auto* b = W.make<ConstraintWidget>(59, 59);
        root->setDebugName("root");
        a->setDebugName("A");
        b->setDebugName("B");
        root->add(b);
        root->add(a);
        b->connect(ConstraintAnchor::Type::TOP, a, ConstraintAnchor::Type::TOP);
        b->connect(ConstraintAnchor::Type::LEFT, a, ConstraintAnchor::Type::LEFT);
        b->connect(ConstraintAnchor::Type::RIGHT, a, ConstraintAnchor::Type::RIGHT);
        b->connect(ConstraintAnchor::Type::BOTTOM, a, ConstraintAnchor::Type::BOTTOM);
        a->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP);
        a->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT);
        a->connect(ConstraintAnchor::Type::RIGHT, root, ConstraintAnchor::Type::RIGHT);
        a->connect(ConstraintAnchor::Type::BOTTOM, root, ConstraintAnchor::Type::BOTTOM);
        root->setOptimizationLevel(Optimizer::OPTIMIZATION_NONE);
        root->measure(Optimizer::OPTIMIZATION_NONE, BasicMeasure::EXACTLY, 100, BasicMeasure::EXACTLY, 100, 0, 0, 0, 0);
        CHECK_EQ(a->getTop(), 63);
        CHECK_EQ(a->getLeft(), 75);
        CHECK_EQ(b->getTop(), 55);
        CHECK_EQ(b->getLeft(), 67);
        root->setOptimizationLevel(Optimizer::OPTIMIZATION_STANDARD);
        root->measure(Optimizer::OPTIMIZATION_STANDARD, BasicMeasure::EXACTLY, 100, BasicMeasure::EXACTLY, 100, 0, 0, 0, 0);
        CHECK_EQ(63, a->getTop());
        CHECK_EQ(75, a->getLeft());
        CHECK_EQ(55, b->getTop());
        CHECK_EQ(67, b->getLeft());
}

