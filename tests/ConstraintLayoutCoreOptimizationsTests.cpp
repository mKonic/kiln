// Ported from constraintlayout-core OptimizationsTest.java (androidx), by tools in
// the scratchpad, then reviewed. Expected values are upstream's.
#include "ConstraintLayoutTestSupport.h"

namespace {
using namespace kiln::constraintlayout::core;
}

static void testVerticalGuideline(int directResolution) {
    using namespace kiln::constraintlayout::core;
    Widgets W;

        auto* root = W.make<ConstraintWidgetContainer>(0, 0, 600, 600);
        root->setOptimizationLevel(directResolution);
        auto* a = W.make<ConstraintWidget>(100, 20);
        auto* guideline = W.make<Guideline>();
        guideline->setOrientation(Guideline::VERTICAL);
        root->setDebugName("root");
        a->setDebugName("A");
        guideline->setDebugName("guideline");
        root->add(a);
        root->add(guideline);
        a->connect(ConstraintAnchor::Type::LEFT, guideline, ConstraintAnchor::Type::LEFT, 16);
        guideline->setGuideBegin(100);
        root->layout();
        (void)0;
        CHECK_EQ(guideline->getLeft(), 100);
        CHECK_EQ(a->getLeft(), 116);
        CHECK_EQ(a->getWidth(), 100);
        CHECK_EQ(a->getHeight(), 20);
        CHECK_EQ(a->getTop(), 0);
        guideline->setGuidePercent(0.5f);
        root->layout();
        (void)0;
        CHECK_EQ(guideline->getLeft(), root->getWidth() / 2);
        CHECK_EQ(a->getLeft(), 316);
        CHECK_EQ(a->getWidth(), 100);
        CHECK_EQ(a->getHeight(), 20);
        CHECK_EQ(a->getTop(), 0);
        guideline->setGuideEnd(100);
        root->layout();
        (void)0;
        CHECK_EQ(guideline->getLeft(), 500);
        CHECK_EQ(a->getLeft(), 516);
        CHECK_EQ(a->getWidth(), 100);
        CHECK_EQ(a->getHeight(), 20);
        CHECK_EQ(a->getTop(), 0);
    
}

static void testHorizontalGuideline(int directResolution) {
    using namespace kiln::constraintlayout::core;
    Widgets W;

        auto* root = W.make<ConstraintWidgetContainer>(0, 0, 600, 600);
        root->setOptimizationLevel(directResolution);
        auto* a = W.make<ConstraintWidget>(100, 20);
        auto* guideline = W.make<Guideline>();
        guideline->setOrientation(Guideline::HORIZONTAL);
        root->setDebugName("root");
        a->setDebugName("A");
        guideline->setDebugName("guideline");
        root->add(a);
        root->add(guideline);
        a->connect(ConstraintAnchor::Type::TOP, guideline, ConstraintAnchor::Type::TOP, 16);
        guideline->setGuideBegin(100);
        root->layout();
        (void)0;
        CHECK_EQ(guideline->getTop(), 100);
        CHECK_EQ(a->getTop(), 116);
        CHECK_EQ(a->getWidth(), 100);
        CHECK_EQ(a->getHeight(), 20);
        CHECK_EQ(a->getLeft(), 0);
        guideline->setGuidePercent(0.5f);
        root->layout();
        (void)0;
        CHECK_EQ(guideline->getTop(), root->getHeight() / 2);
        CHECK_EQ(a->getTop(), 316);
        CHECK_EQ(a->getWidth(), 100);
        CHECK_EQ(a->getHeight(), 20);
        CHECK_EQ(a->getLeft(), 0);
        guideline->setGuideEnd(100);
        root->layout();
        (void)0;
        CHECK_EQ(guideline->getTop(), 500);
        CHECK_EQ(a->getTop(), 516);
        CHECK_EQ(a->getWidth(), 100);
        CHECK_EQ(a->getHeight(), 20);
        CHECK_EQ(a->getLeft(), 0);
    
}

static void testBasicCentering_with(int directResolution) {
    using namespace kiln::constraintlayout::core;
    Widgets W;

        auto* root = W.make<ConstraintWidgetContainer>(0, 0, 600, 600);
        root->setOptimizationLevel(directResolution);
        auto* a = W.make<ConstraintWidget>(100, 20);
        root->setDebugName("root");
        a->setDebugName("A");
        root->add(a);
        a->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT, 10);
        a->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP, 10);
        a->connect(ConstraintAnchor::Type::RIGHT, root, ConstraintAnchor::Type::RIGHT, 10);
        a->connect(ConstraintAnchor::Type::BOTTOM, root, ConstraintAnchor::Type::BOTTOM, 10);
        root->layout();
        (void)0;
        CHECK_EQ(a->getLeft(), 250);
        CHECK_EQ(a->getTop(), 290);
    
}

static void testPercent_with(int directResolution) {
    using namespace kiln::constraintlayout::core;
    Widgets W;

        auto* root = W.make<ConstraintWidgetContainer>(0, 0, 600, 600);
        root->setOptimizationLevel(directResolution);
        auto* a = W.make<ConstraintWidget>(100, 20);
        root->setDebugName("root");
        a->setDebugName("A");
        root->add(a);
        a->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT, 10);
        a->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP, 10);
        a->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::MATCH_CONSTRAINT);
        a->setVerticalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::MATCH_CONSTRAINT);
        a->setHorizontalMatchStyle(ConstraintWidget::MATCH_CONSTRAINT_PERCENT, 0, 0, 0.5f);
        a->setVerticalMatchStyle(ConstraintWidget::MATCH_CONSTRAINT_PERCENT, 0, 0, 0.5f);
        root->layout();
        (void)0;
        CHECK_EQ(a->getLeft(), 10);
        CHECK_EQ(a->getTop(), 10);
        CHECK_EQ(a->getWidth(), 300);
        CHECK_EQ(a->getHeight(), 300);
    
}

static void testDependency_with(int directResolution) {
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
        a->setBaselineDistance(8);
        b->setBaselineDistance(8);
        c->setBaselineDistance(8);
        a->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT, 10);
        a->connect(ConstraintAnchor::Type::BASELINE, b, ConstraintAnchor::Type::BASELINE);
        b->connect(ConstraintAnchor::Type::LEFT, a, ConstraintAnchor::Type::RIGHT, 16);
        b->connect(ConstraintAnchor::Type::BASELINE, c, ConstraintAnchor::Type::BASELINE);
        c->connect(ConstraintAnchor::Type::LEFT, b, ConstraintAnchor::Type::RIGHT, 48);
        c->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP, 32);
        root->layout();
        (void)0;
        CHECK_EQ(a->getLeft(), 10);
        CHECK_EQ(a->getTop(), 32);
        CHECK_EQ(b->getLeft(), 126);
        CHECK_EQ(b->getTop(), 32);
        CHECK_EQ(c->getLeft(), 274);
        CHECK_EQ(c->getTop(), 32);
    
}

static void testDependency2_with(int directResolution) {
    using namespace kiln::constraintlayout::core;
    Widgets W;

        auto* root = W.make<ConstraintWidgetContainer>(0, 0, 600, 600);
        root->setOptimizationLevel(directResolution);
        auto* a = W.make<ConstraintWidget>(100, 20);
        auto* b = W.make<ConstraintWidget>(100, 20);
        auto* c = W.make<ConstraintWidget>(100, 20);
        a->setDebugName("A");
        b->setDebugName("B");
        c->setDebugName("C");
        root->add(a);
        root->add(b);
        root->add(c);
        a->setBaselineDistance(8);
        b->setBaselineDistance(8);
        c->setBaselineDistance(8);
        a->connect(ConstraintAnchor::Type::BOTTOM, root, ConstraintAnchor::Type::BOTTOM);
        a->connect(ConstraintAnchor::Type::LEFT, b, ConstraintAnchor::Type::LEFT);
        b->connect(ConstraintAnchor::Type::BOTTOM, a, ConstraintAnchor::Type::TOP);
        b->connect(ConstraintAnchor::Type::LEFT, c, ConstraintAnchor::Type::LEFT);
        c->connect(ConstraintAnchor::Type::BOTTOM, b, ConstraintAnchor::Type::TOP);
        c->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT, 12);
        root->layout();
        (void)0;
        CHECK_EQ(a->getLeft(), 12);
        CHECK_EQ(a->getTop(), 580);
        CHECK_EQ(b->getLeft(), 12);
        CHECK_EQ(b->getTop(), 560);
        CHECK_EQ(c->getLeft(), 12);
        CHECK_EQ(c->getTop(), 540);
    
}

static void testDependency3_with(int directResolution) {
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
        a->setBaselineDistance(8);
        b->setBaselineDistance(8);
        c->setBaselineDistance(8);
        a->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT, 10);
        a->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP, 20);
        b->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT, 30);
        b->connect(ConstraintAnchor::Type::BOTTOM, root, ConstraintAnchor::Type::BOTTOM, 60);
        b->connect(ConstraintAnchor::Type::RIGHT, root, ConstraintAnchor::Type::RIGHT, 10);
        c->connect(ConstraintAnchor::Type::BOTTOM, b, ConstraintAnchor::Type::TOP);
        c->connect(ConstraintAnchor::Type::LEFT, b, ConstraintAnchor::Type::RIGHT, 20);
        root->layout();
        (void)0;
        CHECK_EQ(a->getLeft(), 10);
        CHECK_EQ(a->getTop(), 20);
        CHECK_EQ(b->getLeft(), 260);
        CHECK_EQ(b->getTop(), 520);
        CHECK_EQ(c->getLeft(), 380);
        CHECK_EQ(c->getTop(), 500);
    
}

static void testDependency4_with(int directResolution) {
    using namespace kiln::constraintlayout::core;
    Widgets W;

        auto* root = W.make<ConstraintWidgetContainer>(0, 0, 600, 600);
        root->setOptimizationLevel(directResolution);
        auto* a = W.make<ConstraintWidget>(100, 20);
        auto* b = W.make<ConstraintWidget>(100, 20);
        root->setDebugName("root");
        a->setDebugName("A");
        b->setDebugName("B");
        root->add(a);
        root->add(b);
        a->setBaselineDistance(8);
        b->setBaselineDistance(8);
        a->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT, 10);
        a->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP, 20);
        a->connect(ConstraintAnchor::Type::RIGHT, root, ConstraintAnchor::Type::RIGHT, 10);
        a->connect(ConstraintAnchor::Type::BOTTOM, root, ConstraintAnchor::Type::BOTTOM, 20);
        b->connect(ConstraintAnchor::Type::RIGHT, a, ConstraintAnchor::Type::RIGHT, 30);
        b->connect(ConstraintAnchor::Type::BOTTOM, a, ConstraintAnchor::Type::BOTTOM, 60);
        root->layout();
        (void)0;
        CHECK_EQ(a->getLeft(), 250);
        CHECK_EQ(a->getTop(), 290);
        CHECK_EQ(b->getLeft(), 220);
        CHECK_EQ(b->getTop(), 230);
    
}

static void testDependency5_with(int directResolution) {
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
        a->setBaselineDistance(8);
        b->setBaselineDistance(8);
        c->setBaselineDistance(8);
        d->setBaselineDistance(8);
        a->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT, 10);
        a->connect(ConstraintAnchor::Type::RIGHT, root, ConstraintAnchor::Type::RIGHT, 10);
        a->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP, 20);
        a->connect(ConstraintAnchor::Type::BOTTOM, b, ConstraintAnchor::Type::TOP);
        b->connect(ConstraintAnchor::Type::TOP, a, ConstraintAnchor::Type::BOTTOM);
        b->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT);
        b->connect(ConstraintAnchor::Type::RIGHT, root, ConstraintAnchor::Type::RIGHT);
        b->connect(ConstraintAnchor::Type::BOTTOM, root, ConstraintAnchor::Type::BOTTOM, 10);
        c->connect(ConstraintAnchor::Type::TOP, b, ConstraintAnchor::Type::BOTTOM);
        c->connect(ConstraintAnchor::Type::RIGHT, b, ConstraintAnchor::Type::RIGHT, 20);
        d->connect(ConstraintAnchor::Type::TOP, c, ConstraintAnchor::Type::BOTTOM);
        d->connect(ConstraintAnchor::Type::RIGHT, c, ConstraintAnchor::Type::RIGHT, 20);
        root->layout();
        (void)0;
        CHECK_EQ(a->getLeft(), 250);
        CHECK_EQ(a->getTop(), 197);
        CHECK_EQ(b->getLeft(), 250);
        CHECK_EQ(b->getTop(), 393);
        CHECK_EQ(c->getLeft(), 230);
        CHECK_EQ(c->getTop(), 413);
        CHECK_EQ(d->getLeft(), 210);
        CHECK_EQ(d->getTop(), 433);
    
}

static void testUnconstrainedDependency_with(int directResolution) {
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
        a->setBaselineDistance(8);
        b->setBaselineDistance(8);
        c->setBaselineDistance(8);
        a->setFrame(142, 96, 242, 130);
        b->connect(ConstraintAnchor::Type::LEFT, a, ConstraintAnchor::Type::RIGHT, 10);
        b->connect(ConstraintAnchor::Type::TOP, a, ConstraintAnchor::Type::TOP, 100);
        c->connect(ConstraintAnchor::Type::RIGHT, a, ConstraintAnchor::Type::LEFT);
        c->connect(ConstraintAnchor::Type::BASELINE, a, ConstraintAnchor::Type::BASELINE);
        root->layout();
        (void)0;
        CHECK_EQ(a->getLeft(), 142);
        CHECK_EQ(a->getTop(), 96);
        CHECK_EQ(a->getWidth(), 100);
        CHECK_EQ(a->getHeight(), 34);
        CHECK_EQ(b->getLeft(), 252);
        CHECK_EQ(b->getTop(), 196);
        CHECK_EQ(c->getLeft(), 42);
        CHECK_EQ(c->getTop(), 96);
    
}

static void testFullLayout_with(int directResolution) {
    using namespace kiln::constraintlayout::core;
    Widgets W;

        
        
        
        
        
        
        
        
        
        auto* root = W.make<ConstraintWidgetContainer>(0, 0, 600, 600);
        root->setOptimizationLevel(directResolution);
        auto* a = W.make<ConstraintWidget>(100, 20);
        auto* b = W.make<ConstraintWidget>(100, 20);
        auto* c = W.make<ConstraintWidget>(100, 20);
        auto* d = W.make<ConstraintWidget>(100, 20);
        auto* e = W.make<ConstraintWidget>(100, 20);
        auto* f = W.make<ConstraintWidget>(100, 20);
        auto* g = W.make<ConstraintWidget>(100, 20);
        a->setDebugName("A");
        b->setDebugName("B");
        c->setDebugName("C");
        d->setDebugName("D");
        e->setDebugName("E");
        f->setDebugName("F");
        g->setDebugName("G");
        root->add(g);
        root->add(a);
        root->add(b);
        root->add(e);
        root->add(c);
        root->add(d);
        root->add(f);
        a->setBaselineDistance(8);
        b->setBaselineDistance(8);
        c->setBaselineDistance(8);
        d->setBaselineDistance(8);
        e->setBaselineDistance(8);
        f->setBaselineDistance(8);
        g->setBaselineDistance(8);
        a->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP, 20);
        a->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT);
        a->connect(ConstraintAnchor::Type::RIGHT, root, ConstraintAnchor::Type::RIGHT);
        b->connect(ConstraintAnchor::Type::TOP, a, ConstraintAnchor::Type::BOTTOM, 40);
        b->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT, 16);
        c->connect(ConstraintAnchor::Type::LEFT, b, ConstraintAnchor::Type::RIGHT, 16);
        c->connect(ConstraintAnchor::Type::BASELINE, b, ConstraintAnchor::Type::BASELINE);
        d->connect(ConstraintAnchor::Type::TOP, c, ConstraintAnchor::Type::BOTTOM);
        d->connect(ConstraintAnchor::Type::LEFT, c, ConstraintAnchor::Type::LEFT);
        e->connect(ConstraintAnchor::Type::RIGHT, b, ConstraintAnchor::Type::RIGHT);
        e->connect(ConstraintAnchor::Type::BASELINE, d, ConstraintAnchor::Type::BASELINE);
        f->connect(ConstraintAnchor::Type::RIGHT, root, ConstraintAnchor::Type::RIGHT);
        f->connect(ConstraintAnchor::Type::BOTTOM, root, ConstraintAnchor::Type::BOTTOM);
        g->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT, 16);
        g->connect(ConstraintAnchor::Type::BASELINE, f, ConstraintAnchor::Type::BASELINE);
        root->layout();

        (void)0;
        CHECK_EQ(a->getLeft(), 250);
        CHECK_EQ(a->getTop(), 20);
        CHECK_EQ(b->getLeft(), 16);
        CHECK_EQ(b->getTop(), 80);
        CHECK_EQ(c->getLeft(), 132);
        CHECK_EQ(c->getTop(), 80);
        CHECK_EQ(d->getLeft(), 132);
        CHECK_EQ(d->getTop(), 100);
        CHECK_EQ(e->getLeft(), 16);
        CHECK_EQ(e->getTop(), 100);
        CHECK_EQ(f->getLeft(), 500);
        CHECK_EQ(f->getTop(), 580);
        CHECK_EQ(g->getLeft(), 16);
        CHECK_EQ(g->getTop(), 580);
    
}

static void chainConnect(ConstraintAnchor::Type start, ConstraintWidget* startTarget, ConstraintAnchor::Type end, ConstraintWidget* endTarget,
                         std::initializer_list<ConstraintWidget*> widgets_list) {
    using namespace kiln::constraintlayout::core;
    std::vector<ConstraintWidget*> widgets(widgets_list);
    widgets[0]->connect(start, startTarget, start);
    ConstraintWidget* previousWidget = nullptr;
    for (std::size_t i = 0; i < widgets.size(); i++) {
        if (previousWidget != nullptr) widgets[i]->connect(start, previousWidget, end);
        if (i + 1 < widgets.size()) widgets[i]->connect(end, widgets[i + 1], start);
        previousWidget = widgets[i];
    }
    if (previousWidget != nullptr) previousWidget->connect(end, endTarget, end);
}

TEST(ClOptimizations_testGoneMatchConstraint) {
    using namespace kiln::constraintlayout::core;
    Widgets W;
        auto* root = W.make<ConstraintWidgetContainer>(0, 0, 600, 800);
        auto* a = W.make<ConstraintWidget>("A", 0, 10);
        auto* b = W.make<ConstraintWidget>("B", 10, 10);
        root->setDebugName("root");
        root->add(a);
        root->add(b);
        a->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP, 8);
        a->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT, 8);
        a->connect(ConstraintAnchor::Type::RIGHT, root, ConstraintAnchor::Type::RIGHT, 8);
        a->connect(ConstraintAnchor::Type::BOTTOM, root, ConstraintAnchor::Type::BOTTOM, 8);
        a->setVerticalBiasPercent(0.2f);
        a->setHorizontalBiasPercent(0.2f);
        a->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::MATCH_CONSTRAINT);
        b->connect(ConstraintAnchor::Type::TOP, a, ConstraintAnchor::Type::BOTTOM);
        root->setOptimizationLevel(Optimizer::OPTIMIZATION_STANDARD);
        root->layout();
        CHECK_EQ(a->getLeft(), 8);
        CHECK_EQ(a->getTop(), 163);
        CHECK_EQ(a->getRight(), 592);
        CHECK_EQ(a->getBottom(), 173);
        a->setVisibility(ConstraintWidget::GONE);
        root->layout();
        CHECK_EQ(a->getLeft(), 120);
        CHECK_EQ(a->getTop(), 160);
        CHECK_EQ(a->getRight(), 120);
        CHECK_EQ(a->getBottom(), 160);
}

TEST(ClOptimizations_test3EltsChain) {
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
        b->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP);
        c->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP);
        a->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT, 40);
        a->connect(ConstraintAnchor::Type::RIGHT, b, ConstraintAnchor::Type::LEFT);
        b->connect(ConstraintAnchor::Type::LEFT, a, ConstraintAnchor::Type::RIGHT);
        b->connect(ConstraintAnchor::Type::RIGHT, c, ConstraintAnchor::Type::LEFT);
        c->connect(ConstraintAnchor::Type::LEFT, b, ConstraintAnchor::Type::RIGHT);
        c->connect(ConstraintAnchor::Type::RIGHT, root, ConstraintAnchor::Type::RIGHT, 30);
        root->setOptimizationLevel(Optimizer::OPTIMIZATION_STANDARD);
        a->setHorizontalChainStyle(ConstraintWidget::CHAIN_SPREAD_INSIDE);
        root->layout();
        CHECK_EQ(a->getLeft(), 40);
        CHECK_EQ(b->getLeft(), 255);
        CHECK_EQ(c->getLeft(), 470);
        a->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::MATCH_CONSTRAINT);
        b->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::MATCH_CONSTRAINT);
        c->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::MATCH_CONSTRAINT);
        root->layout();
        CHECK_EQ(a->getLeft(), 40);
        CHECK_NEAR(b->getLeft(), 217, 1);
        CHECK_EQ(c->getLeft(), 393);
        CHECK_NEAR(a->getWidth(), 177, 1);
        CHECK_NEAR(b->getWidth(), 176, 1);
        CHECK_NEAR(c->getWidth(), 177, 1);
        a->setHorizontalChainStyle(ConstraintWidget::CHAIN_SPREAD);
        a->connect(ConstraintAnchor::Type::RIGHT, b, ConstraintAnchor::Type::LEFT, 7);
        b->connect(ConstraintAnchor::Type::LEFT, a, ConstraintAnchor::Type::RIGHT, 3);
        b->connect(ConstraintAnchor::Type::RIGHT, c, ConstraintAnchor::Type::LEFT, 7);
        c->connect(ConstraintAnchor::Type::LEFT, b, ConstraintAnchor::Type::RIGHT, 3);
        root->layout();
        CHECK_EQ(a->getLeft(), 40);
        CHECK_EQ(b->getLeft(), 220);
        CHECK_NEAR(c->getLeft(), 400, 1);
        CHECK_NEAR(a->getWidth(), 170, 1);
        CHECK_NEAR(b->getWidth(), 170, 1);
        CHECK_NEAR(c->getWidth(), 170, 1);
        a->setHorizontalChainStyle(ConstraintWidget::CHAIN_SPREAD_INSIDE);
        a->setVisibility(ConstraintWidget::GONE);
        root->layout();
        CHECK_EQ(a->getLeft(), 0);
        CHECK_EQ(b->getLeft(), 3);
        CHECK_NEAR(c->getLeft(), 292, 1);
        CHECK_EQ(a->getWidth(), 0);
        CHECK_NEAR(b->getWidth(), 279, 1);
        CHECK_NEAR(c->getWidth(), 278, 1);
}

TEST(ClOptimizations_testBasicChain) {
    using namespace kiln::constraintlayout::core;
    Widgets W;
        auto* root = W.make<ConstraintWidgetContainer>(0, 0, 600, 600);
        auto* a = W.make<ConstraintWidget>(100, 20);
        auto* b = W.make<ConstraintWidget>(100, 20);
        root->setDebugName("root");
        a->setDebugName("A");
        b->setDebugName("B");
        root->add(a);
        root->add(b);
        a->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP);
        b->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP);
        a->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT);
        a->connect(ConstraintAnchor::Type::RIGHT, b, ConstraintAnchor::Type::LEFT);
        b->connect(ConstraintAnchor::Type::LEFT, a, ConstraintAnchor::Type::RIGHT);
        b->connect(ConstraintAnchor::Type::RIGHT, root, ConstraintAnchor::Type::RIGHT);
        root->setOptimizationLevel(Optimizer::OPTIMIZATION_STANDARD);
        root->layout();
        CHECK_EQ(a->getLeft(), 133);
        CHECK_NEAR(b->getLeft(), 367, 1);
        auto* c = W.make<ConstraintWidget>(100, 20);
        c->setDebugName("C");
        root->add(c);
        c->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP);
        c->connect(ConstraintAnchor::Type::LEFT, b, ConstraintAnchor::Type::RIGHT);
        root->layout();
        CHECK_EQ(a->getLeft(), 133);
        CHECK_NEAR(b->getLeft(), 367, 1);
        CHECK_EQ(c->getLeft(), b->getRight());
        a->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT, 40);
        a->connect(ConstraintAnchor::Type::RIGHT, b, ConstraintAnchor::Type::LEFT, 100);
        a->setHorizontalChainStyle(ConstraintWidget::CHAIN_PACKED);
        root->layout();
        CHECK_EQ(a->getLeft(), 170);
        CHECK_EQ(b->getLeft(), 370);
        a->setHorizontalBiasPercent(0);
        root->layout();
        CHECK_EQ(a->getLeft(), 40);
        CHECK_EQ(b->getLeft(), 240);
        a->setHorizontalBiasPercent(0.5f);
        a->setVisibility(ConstraintWidget::GONE);
        root->layout();
        CHECK_EQ(a->getLeft(), 250);
        CHECK_EQ(b->getLeft(), 250);
}

TEST(ClOptimizations_testBasicChain2) {
    using namespace kiln::constraintlayout::core;
    Widgets W;
        auto* root = W.make<ConstraintWidgetContainer>(0, 0, 600, 600);
        auto* a = W.make<ConstraintWidget>(100, 20);
        auto* b = W.make<ConstraintWidget>(100, 20);
        root->setDebugName("root");
        a->setDebugName("A");
        b->setDebugName("B");
        root->add(a);
        root->add(b);
        a->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP);
        b->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP);
        a->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT);
        a->connect(ConstraintAnchor::Type::RIGHT, b, ConstraintAnchor::Type::LEFT);
        b->connect(ConstraintAnchor::Type::LEFT, a, ConstraintAnchor::Type::RIGHT);
        b->connect(ConstraintAnchor::Type::RIGHT, root, ConstraintAnchor::Type::RIGHT);
        auto* c = W.make<ConstraintWidget>(100, 20);
        c->setDebugName("C");
        root->add(c);
        c->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP);
        c->connect(ConstraintAnchor::Type::LEFT, b, ConstraintAnchor::Type::RIGHT);
        a->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT, 40);
        a->connect(ConstraintAnchor::Type::RIGHT, b, ConstraintAnchor::Type::LEFT, 100);
        a->setHorizontalChainStyle(ConstraintWidget::CHAIN_PACKED);
        a->setHorizontalBiasPercent(0.5f);
        a->setVisibility(ConstraintWidget::GONE);
        root->layout();
        CHECK_EQ(a->getLeft(), 250);
        CHECK_EQ(b->getLeft(), 250);
}

TEST(ClOptimizations_testBasicRatio) {
    using namespace kiln::constraintlayout::core;
    Widgets W;
        auto* root = W.make<ConstraintWidgetContainer>(0, 0, 600, 600);
        auto* a = W.make<ConstraintWidget>(100, 20);
        auto* b = W.make<ConstraintWidget>(100, 20);
        root->setDebugName("root");
        a->setDebugName("A");
        b->setDebugName("B");
        root->add(a);
        root->add(b);
        a->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP);
        a->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT);
        b->connect(ConstraintAnchor::Type::LEFT, a, ConstraintAnchor::Type::RIGHT);
        b->connect(ConstraintAnchor::Type::TOP, a, ConstraintAnchor::Type::TOP);
        b->connect(ConstraintAnchor::Type::BOTTOM, a, ConstraintAnchor::Type::BOTTOM);
        a->setVerticalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::MATCH_CONSTRAINT);
        a->setDimensionRatio("1:1");
        root->setOptimizationLevel(Optimizer::OPTIMIZATION_STANDARD);
        root->layout();
        CHECK_EQ(a->getHeight(), a->getWidth());
        CHECK_EQ(b->getTop(), (a->getHeight() - b->getHeight()) / 2);
}

TEST(ClOptimizations_testBasicBaseline) {
    using namespace kiln::constraintlayout::core;
    Widgets W;
        auto* root = W.make<ConstraintWidgetContainer>(0, 0, 600, 600);
        auto* a = W.make<ConstraintWidget>(100, 20);
        auto* b = W.make<ConstraintWidget>(100, 20);
        root->setDebugName("root");
        a->setDebugName("A");
        b->setDebugName("B");
        a->setBaselineDistance(8);
        b->setBaselineDistance(8);
        root->add(a);
        root->add(b);
        a->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP);
        a->connect(ConstraintAnchor::Type::BOTTOM, root, ConstraintAnchor::Type::BOTTOM);
        a->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT);
        b->connect(ConstraintAnchor::Type::LEFT, a, ConstraintAnchor::Type::RIGHT);
        b->connect(ConstraintAnchor::Type::BASELINE, a, ConstraintAnchor::Type::BASELINE);
        root->setOptimizationLevel(Optimizer::OPTIMIZATION_STANDARD);
        root->layout();
        CHECK_EQ(a->getTop(), 290);
        CHECK_EQ(b->getTop(), a->getTop());
}

TEST(ClOptimizations_testBasicMatchConstraints) {
    using namespace kiln::constraintlayout::core;
    Widgets W;
        auto* root = W.make<ConstraintWidgetContainer>(0, 0, 600, 600);
        auto* a = W.make<ConstraintWidget>(100, 20);
        root->setDebugName("root");
        a->setDebugName("A");
        root->add(a);
        a->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP);
        a->connect(ConstraintAnchor::Type::BOTTOM, root, ConstraintAnchor::Type::BOTTOM);
        a->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT);
        a->connect(ConstraintAnchor::Type::RIGHT, root, ConstraintAnchor::Type::RIGHT);
        a->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::MATCH_CONSTRAINT);
        a->setVerticalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::MATCH_CONSTRAINT);
        root->setOptimizationLevel(Optimizer::OPTIMIZATION_STANDARD);
        root->layout();
        CHECK_EQ(a->getLeft(), 0);
        CHECK_EQ(a->getTop(), 0);
        CHECK_EQ(a->getRight(), root->getWidth());
        CHECK_EQ(a->getBottom(), root->getHeight());
        a->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP, 10);
        a->connect(ConstraintAnchor::Type::BOTTOM, root, ConstraintAnchor::Type::BOTTOM, 20);
        a->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT, 30);
        a->connect(ConstraintAnchor::Type::RIGHT, root, ConstraintAnchor::Type::RIGHT, 40);
        root->layout();
        CHECK_EQ(a->getLeft(), 30);
        CHECK_EQ(a->getTop(), 10);
        CHECK_EQ(a->getRight(), root->getWidth() - 40);
        CHECK_EQ(a->getBottom(), root->getHeight() - 20);
}

TEST(ClOptimizations_testBasicCenteringPositioning) {
    using namespace kiln::constraintlayout::core;
    Widgets W;
        auto* root = W.make<ConstraintWidgetContainer>(0, 0, 600, 600);
        auto* a = W.make<ConstraintWidget>(100, 20);
        root->setDebugName("root");
        a->setDebugName("A");
        a->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP);
        a->connect(ConstraintAnchor::Type::BOTTOM, root, ConstraintAnchor::Type::BOTTOM);
        a->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT);
        a->connect(ConstraintAnchor::Type::RIGHT, root, ConstraintAnchor::Type::RIGHT);
        root->add(a);
        root->setOptimizationLevel(Optimizer::OPTIMIZATION_STANDARD);
        root->layout();
        CHECK_EQ(a->getLeft(), (root->getWidth() - a->getWidth()) / 2);
        CHECK_EQ(a->getTop(), (root->getHeight() - a->getHeight()) / 2);
        a->setHorizontalBiasPercent(0.3f);
        a->setVerticalBiasPercent(0.3f);
        root->layout();
        CHECK_EQ(a->getLeft(), (int) ((root->getWidth() - a->getWidth()) * 0.3f));
        CHECK_EQ(a->getTop(), (int) ((root->getHeight() - a->getHeight()) * 0.3f));
        a->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT, 10);
        a->connect(ConstraintAnchor::Type::RIGHT, root, ConstraintAnchor::Type::RIGHT, 30);
        a->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP, 50);
        a->connect(ConstraintAnchor::Type::BOTTOM, root, ConstraintAnchor::Type::BOTTOM, 20);
        root->layout();
        CHECK_EQ(a->getLeft(), (int) ((root->getWidth() - a->getWidth() - 40) * 0.3f) + 10);
        CHECK_EQ(a->getTop(), (int) ((root->getHeight() - a->getHeight() - 70) * 0.3f) + 50);
}

TEST(ClOptimizations_testBasicVerticalPositioning) {
    using namespace kiln::constraintlayout::core;
    Widgets W;
        auto* root = W.make<ConstraintWidgetContainer>(0, 0, 600, 600);
        auto* a = W.make<ConstraintWidget>(100, 20);
        auto* b = W.make<ConstraintWidget>(100, 20);
        root->setDebugName("root");
        a->setDebugName("A");
        b->setDebugName("B");
        int margin = 13;
        int marginR = 27;
        a->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP, 31);
        a->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT, 27);
        b->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT, 27);
        b->connect(ConstraintAnchor::Type::TOP, a, ConstraintAnchor::Type::BOTTOM, 104);
        root->add(a);
        root->add(b);
        root->setOptimizationLevel(Optimizer::OPTIMIZATION_STANDARD);
        a->setVisibility(ConstraintWidget::GONE);
        root->layout();
        CHECK_EQ(a->getLeft(), 0);
        CHECK_EQ(a->getTop(), 0);
        CHECK_EQ(b->getLeft(), 27);
        CHECK_EQ(b->getTop(), 104);
}

TEST(ClOptimizations_testBasicVerticalGuidelinePositioning) {
    using namespace kiln::constraintlayout::core;
    Widgets W;
        auto* root = W.make<ConstraintWidgetContainer>(0, 0, 600, 600);
        auto* a = W.make<ConstraintWidget>(100, 20);
        auto* guidelineA = W.make<Guideline>();
        guidelineA->setOrientation(Guideline::HORIZONTAL);
        guidelineA->setGuideEnd(67);
        root->setDebugName("root");
        a->setDebugName("A");
        guidelineA->setDebugName("guideline");
        a->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP, 31);
        a->connect(ConstraintAnchor::Type::BOTTOM, guidelineA, ConstraintAnchor::Type::TOP, 12);
        root->add(a);
        root->add(guidelineA);
        root->setOptimizationLevel(Optimizer::OPTIMIZATION_STANDARD);
        root->layout();
        CHECK_EQ(a->getTop(), 266);
        CHECK_EQ(guidelineA->getTop(), 533);
}

TEST(ClOptimizations_testSimpleCenterPositioning) {
    using namespace kiln::constraintlayout::core;
    Widgets W;
        auto* root = W.make<ConstraintWidgetContainer>(0, 0, 600, 600);
        auto* a = W.make<ConstraintWidget>(100, 20);
        root->setDebugName("root");
        a->setDebugName("A");
        int margin = 13;
        int marginR = 27;
        a->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP, margin);
        a->connect(ConstraintAnchor::Type::BOTTOM, root, ConstraintAnchor::Type::BOTTOM, -margin);
        a->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT, margin);
        a->connect(ConstraintAnchor::Type::RIGHT, root, ConstraintAnchor::Type::RIGHT, -marginR);
        root->add(a);
        root->setOptimizationLevel(Optimizer::OPTIMIZATION_STANDARD);
        root->layout();
        CHECK_NEAR(a->getLeft(), 270, 1);
        CHECK_NEAR(a->getTop(), 303, 1);
}

TEST(ClOptimizations_testSimpleGuideline) {
    using namespace kiln::constraintlayout::core;
    Widgets W;
        auto* root = W.make<ConstraintWidgetContainer>(0, 0, 600, 600);
        auto* guidelineA = W.make<Guideline>();
        auto* a = W.make<ConstraintWidget>(100, 20);
        guidelineA->setOrientation(Guideline::VERTICAL);
        guidelineA->setGuideBegin(100);
        root->setDebugName("root");
        a->setDebugName("A");
        guidelineA->setDebugName("guidelineA");
        a->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP, 32);
        a->connect(ConstraintAnchor::Type::LEFT, guidelineA, ConstraintAnchor::Type::LEFT, 2);
        a->connect(ConstraintAnchor::Type::RIGHT, root, ConstraintAnchor::Type::RIGHT, 7);
        a->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::MATCH_CONSTRAINT);
        root->add(guidelineA);
        root->add(a);
        root->setOptimizationLevel(Optimizer::OPTIMIZATION_STANDARD);
        root->layout();
        CHECK_EQ(a->getLeft(), 102);
        CHECK_EQ(a->getTop(), 32);
        CHECK_EQ(a->getWidth(), 491);
        CHECK_EQ(a->getHeight(), 20);
        CHECK_EQ(guidelineA->getLeft(), 100);
        root->setWidth(700);
        root->layout();
        CHECK_EQ(a->getLeft(), 102);
        CHECK_EQ(a->getTop(), 32);
        CHECK_EQ(a->getWidth(), 591);
        CHECK_EQ(a->getHeight(), 20);
        CHECK_EQ(guidelineA->getLeft(), 100);
}

TEST(ClOptimizations_testSimple) {
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
        a->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT, 10);
        a->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP, 20);
        b->connect(ConstraintAnchor::Type::LEFT, a, ConstraintAnchor::Type::RIGHT, 10);
        b->connect(ConstraintAnchor::Type::TOP, a, ConstraintAnchor::Type::BOTTOM, 20);
        c->connect(ConstraintAnchor::Type::LEFT, a, ConstraintAnchor::Type::RIGHT, 30);
        c->connect(ConstraintAnchor::Type::TOP, b, ConstraintAnchor::Type::BOTTOM, 20);
        root->add(a);
        root->add(b);
        root->add(c);
        root->setOptimizationLevel(Optimizer::OPTIMIZATION_STANDARD);
        root->layout();
        CHECK_EQ(a->getLeft(), 10);
        CHECK_EQ(a->getTop(), 20);
        CHECK_EQ(b->getLeft(), 120);
        CHECK_EQ(b->getTop(), 60);
        CHECK_EQ(c->getLeft(), 140);
        CHECK_EQ(c->getTop(), 100);
}

TEST(ClOptimizations_testGuideline) {
    using namespace kiln::constraintlayout::core;
    Widgets W;
        testVerticalGuideline(Optimizer::OPTIMIZATION_NONE);
        testVerticalGuideline(Optimizer::OPTIMIZATION_STANDARD);
        testHorizontalGuideline(Optimizer::OPTIMIZATION_NONE);
        testHorizontalGuideline(Optimizer::OPTIMIZATION_STANDARD);
}

TEST(ClOptimizations_testBasicCentering) {
    using namespace kiln::constraintlayout::core;
    Widgets W;
        testBasicCentering_with(Optimizer::OPTIMIZATION_NONE);
        testBasicCentering_with(Optimizer::OPTIMIZATION_STANDARD);
}

TEST(ClOptimizations_testPercent) {
    using namespace kiln::constraintlayout::core;
    Widgets W;
        testPercent_with(Optimizer::OPTIMIZATION_NONE);
        testPercent_with(Optimizer::OPTIMIZATION_STANDARD);
}

TEST(ClOptimizations_testDependency) {
    using namespace kiln::constraintlayout::core;
    Widgets W;
        testDependency_with(Optimizer::OPTIMIZATION_NONE);
        testDependency_with(Optimizer::OPTIMIZATION_STANDARD);
}

TEST(ClOptimizations_testDependency2) {
    using namespace kiln::constraintlayout::core;
    Widgets W;
        testDependency2_with(Optimizer::OPTIMIZATION_NONE);
        testDependency2_with(Optimizer::OPTIMIZATION_STANDARD);
}

TEST(ClOptimizations_testDependency3) {
    using namespace kiln::constraintlayout::core;
    Widgets W;
        testDependency3_with(Optimizer::OPTIMIZATION_NONE);
        testDependency3_with(Optimizer::OPTIMIZATION_STANDARD);
}

TEST(ClOptimizations_testDependency4) {
    using namespace kiln::constraintlayout::core;
    Widgets W;
        testDependency4_with(Optimizer::OPTIMIZATION_NONE);
        testDependency4_with(Optimizer::OPTIMIZATION_STANDARD);
}

TEST(ClOptimizations_testDependency5) {
    using namespace kiln::constraintlayout::core;
    Widgets W;
        testDependency5_with(Optimizer::OPTIMIZATION_NONE);
        testDependency5_with(Optimizer::OPTIMIZATION_STANDARD);
}

TEST(ClOptimizations_testUnconstrainedDependency) {
    using namespace kiln::constraintlayout::core;
    Widgets W;
        testUnconstrainedDependency_with(Optimizer::OPTIMIZATION_NONE);
        testUnconstrainedDependency_with(Optimizer::OPTIMIZATION_STANDARD);
}

TEST(ClOptimizations_testFullLayout) {
    using namespace kiln::constraintlayout::core;
    Widgets W;
        testFullLayout_with(Optimizer::OPTIMIZATION_NONE);
        testFullLayout_with(Optimizer::OPTIMIZATION_STANDARD);
}

TEST(ClOptimizations_testComplexLayout) {
    using namespace kiln::constraintlayout::core;
    Widgets W;
        auto* root = W.make<ConstraintWidgetContainer>(0, 0, 600, 600);
        root->setOptimizationLevel(Optimizer::OPTIMIZATION_GROUPING);
        auto* a = W.make<ConstraintWidget>(100, 100);
        auto* b = W.make<ConstraintWidget>(100, 20);
        auto* c = W.make<ConstraintWidget>(100, 20);
        auto* d = W.make<ConstraintWidget>(30, 30);
        auto* e = W.make<ConstraintWidget>(30, 30);
        auto* f = W.make<ConstraintWidget>(30, 30);
        auto* g = W.make<ConstraintWidget>(100, 20);
        auto* h = W.make<ConstraintWidget>(100, 20);
        root->setDebugName("root");
        a->setDebugName("A");
        b->setDebugName("B");
        c->setDebugName("C");
        d->setDebugName("D");
        e->setDebugName("E");
        f->setDebugName("F");
        g->setDebugName("G");
        h->setDebugName("H");
        root->add(g);
        root->add(a);
        root->add(b);
        root->add(e);
        root->add(c);
        root->add(d);
        root->add(f);
        root->add(h);
        b->setBaselineDistance(8);
        c->setBaselineDistance(8);
        d->setBaselineDistance(8);
        e->setBaselineDistance(8);
        f->setBaselineDistance(8);
        g->setBaselineDistance(8);
        h->setBaselineDistance(8);
        a->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP, 16);
        a->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT, 16);
        a->connect(ConstraintAnchor::Type::BOTTOM, root, ConstraintAnchor::Type::BOTTOM, 16);
        b->connect(ConstraintAnchor::Type::TOP, a, ConstraintAnchor::Type::TOP);
        b->connect(ConstraintAnchor::Type::LEFT, a, ConstraintAnchor::Type::RIGHT, 16);
        c->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP);
        c->connect(ConstraintAnchor::Type::LEFT, a, ConstraintAnchor::Type::RIGHT, 16);
        c->connect(ConstraintAnchor::Type::BOTTOM, root, ConstraintAnchor::Type::BOTTOM);
        d->connect(ConstraintAnchor::Type::BOTTOM, a, ConstraintAnchor::Type::BOTTOM);
        d->connect(ConstraintAnchor::Type::LEFT, a, ConstraintAnchor::Type::RIGHT, 16);
        e->connect(ConstraintAnchor::Type::BOTTOM, d, ConstraintAnchor::Type::BOTTOM);
        e->connect(ConstraintAnchor::Type::LEFT, d, ConstraintAnchor::Type::RIGHT, 16);
        f->connect(ConstraintAnchor::Type::BOTTOM, e, ConstraintAnchor::Type::BOTTOM);
        f->connect(ConstraintAnchor::Type::LEFT, e, ConstraintAnchor::Type::RIGHT, 16);
        g->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP);
        g->connect(ConstraintAnchor::Type::RIGHT, root, ConstraintAnchor::Type::RIGHT, 16);
        g->connect(ConstraintAnchor::Type::BOTTOM, root, ConstraintAnchor::Type::BOTTOM);
        h->connect(ConstraintAnchor::Type::BOTTOM, root, ConstraintAnchor::Type::BOTTOM, 16);
        h->connect(ConstraintAnchor::Type::RIGHT, root, ConstraintAnchor::Type::RIGHT, 16);
        root->setMeasurer(&sMeasurer);
        root->layout();
        CHECK_EQ(a->getLeft(), 16);
        CHECK_EQ(a->getTop(), 250);
        CHECK_EQ(b->getLeft(), 132);
        CHECK_EQ(b->getTop(), 250);
        CHECK_EQ(c->getLeft(), 132);
        CHECK_EQ(c->getTop(), 290);
        CHECK_EQ(d->getLeft(), 132);
        CHECK_EQ(d->getTop(), 320);
        CHECK_EQ(e->getLeft(), 178);
        CHECK_EQ(e->getTop(), 320);
        CHECK_EQ(f->getLeft(), 224);
        CHECK_EQ(f->getTop(), 320);
        CHECK_EQ(g->getLeft(), 484);
        CHECK_EQ(g->getTop(), 290);
        CHECK_EQ(h->getLeft(), 484);
        CHECK_EQ(h->getTop(), 564);
}

TEST(ClOptimizations_testComplexLayoutWrap) {
    using namespace kiln::constraintlayout::core;
    Widgets W;
        auto* root = W.make<ConstraintWidgetContainer>(0, 0, 600, 600);
        root->setOptimizationLevel(Optimizer::OPTIMIZATION_DIRECT);
        auto* a = W.make<ConstraintWidget>(100, 100);
        auto* b = W.make<ConstraintWidget>(100, 20);
        auto* c = W.make<ConstraintWidget>(100, 20);
        auto* d = W.make<ConstraintWidget>(30, 30);
        auto* e = W.make<ConstraintWidget>(30, 30);
        auto* f = W.make<ConstraintWidget>(30, 30);
        auto* g = W.make<ConstraintWidget>(100, 20);
        auto* h = W.make<ConstraintWidget>(100, 20);
        root->setDebugName("root");
        a->setDebugName("A");
        b->setDebugName("B");
        c->setDebugName("C");
        d->setDebugName("D");
        e->setDebugName("E");
        f->setDebugName("F");
        g->setDebugName("G");
        h->setDebugName("H");
        root->add(g);
        root->add(a);
        root->add(b);
        root->add(e);
        root->add(c);
        root->add(d);
        root->add(f);
        root->add(h);
        b->setBaselineDistance(8);
        c->setBaselineDistance(8);
        d->setBaselineDistance(8);
        e->setBaselineDistance(8);
        f->setBaselineDistance(8);
        g->setBaselineDistance(8);
        h->setBaselineDistance(8);
        a->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP, 16);
        a->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT, 16);
        a->connect(ConstraintAnchor::Type::BOTTOM, root, ConstraintAnchor::Type::BOTTOM, 16);
        b->connect(ConstraintAnchor::Type::TOP, a, ConstraintAnchor::Type::TOP);
        b->connect(ConstraintAnchor::Type::LEFT, a, ConstraintAnchor::Type::RIGHT, 16);
        c->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP);
        c->connect(ConstraintAnchor::Type::LEFT, a, ConstraintAnchor::Type::RIGHT, 16);
        c->connect(ConstraintAnchor::Type::BOTTOM, root, ConstraintAnchor::Type::BOTTOM);
        d->connect(ConstraintAnchor::Type::BOTTOM, a, ConstraintAnchor::Type::BOTTOM);
        d->connect(ConstraintAnchor::Type::LEFT, a, ConstraintAnchor::Type::RIGHT, 16);
        e->connect(ConstraintAnchor::Type::BOTTOM, d, ConstraintAnchor::Type::BOTTOM);
        e->connect(ConstraintAnchor::Type::LEFT, d, ConstraintAnchor::Type::RIGHT, 16);
        f->connect(ConstraintAnchor::Type::BOTTOM, e, ConstraintAnchor::Type::BOTTOM);
        f->connect(ConstraintAnchor::Type::LEFT, e, ConstraintAnchor::Type::RIGHT, 16);
        g->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP);
        g->connect(ConstraintAnchor::Type::RIGHT, root, ConstraintAnchor::Type::RIGHT, 16);
        g->connect(ConstraintAnchor::Type::BOTTOM, root, ConstraintAnchor::Type::BOTTOM);
        h->connect(ConstraintAnchor::Type::BOTTOM, root, ConstraintAnchor::Type::BOTTOM, 16);
        h->connect(ConstraintAnchor::Type::RIGHT, root, ConstraintAnchor::Type::RIGHT, 16);
        root->setMeasurer(&sMeasurer);
        root->setVerticalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::WRAP_CONTENT);
        root->layout();
        CHECK_EQ(a->getLeft(), 16);
        CHECK_EQ(a->getTop(), 16);
        CHECK_EQ(b->getLeft(), 132);
        CHECK_EQ(b->getTop(), 16);
        CHECK_EQ(c->getLeft(), 132);
        CHECK_EQ(c->getTop(), 56);
        CHECK_EQ(d->getLeft(), 132);
        CHECK_EQ(d->getTop(), 86);
        CHECK_EQ(e->getLeft(), 178);
        CHECK_EQ(e->getTop(), 86);
        CHECK_EQ(f->getLeft(), 224);
        CHECK_EQ(f->getTop(), 86);
        CHECK_EQ(g->getLeft(), 484);
        CHECK_EQ(g->getTop(), 56);
        CHECK_EQ(h->getLeft(), 484);
        CHECK_EQ(h->getTop(), 96);
}

TEST(ClOptimizations_testChainLayoutWrap) {
    using namespace kiln::constraintlayout::core;
    Widgets W;
        auto* root = W.make<ConstraintWidgetContainer>(0, 0, 600, 600);
        root->setOptimizationLevel(Optimizer::OPTIMIZATION_GROUPING);
        auto* a = W.make<ConstraintWidget>(100, 100);
        auto* b = W.make<ConstraintWidget>(100, 20);
        auto* c = W.make<ConstraintWidget>(100, 20);
        root->setDebugName("root");
        a->setDebugName("A");
        b->setDebugName("B");
        c->setDebugName("C");
        root->add(a);
        root->add(b);
        root->add(c);
        a->setBaselineDistance(28);
        b->setBaselineDistance(8);
        c->setBaselineDistance(8);
        a->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP, 16);
        a->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT, 16);
        a->connect(ConstraintAnchor::Type::RIGHT, b, ConstraintAnchor::Type::LEFT);
        a->connect(ConstraintAnchor::Type::BOTTOM, root, ConstraintAnchor::Type::BOTTOM, 16);
        b->connect(ConstraintAnchor::Type::BASELINE, a, ConstraintAnchor::Type::BASELINE);
        b->connect(ConstraintAnchor::Type::LEFT, a, ConstraintAnchor::Type::RIGHT);
        b->connect(ConstraintAnchor::Type::RIGHT, c, ConstraintAnchor::Type::LEFT);
        c->connect(ConstraintAnchor::Type::BASELINE, b, ConstraintAnchor::Type::BASELINE);
        c->connect(ConstraintAnchor::Type::LEFT, b, ConstraintAnchor::Type::RIGHT);
        c->connect(ConstraintAnchor::Type::RIGHT, root, ConstraintAnchor::Type::RIGHT, 16);
        root->setMeasurer(&sMeasurer);
        root->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::WRAP_CONTENT);
        root->layout();
        CHECK_EQ(a->getLeft(), 16);
        CHECK_EQ(a->getTop(), 250);
        CHECK_EQ(b->getLeft(), 116);
        CHECK_EQ(b->getTop(), 270);
        CHECK_EQ(c->getLeft(), 216);
        CHECK_EQ(c->getTop(), 270);
}

TEST(ClOptimizations_testChainLayoutWrap2) {
    using namespace kiln::constraintlayout::core;
    Widgets W;
        auto* root = W.make<ConstraintWidgetContainer>(0, 0, 600, 600);
        root->setOptimizationLevel(Optimizer::OPTIMIZATION_GROUPING);
        auto* a = W.make<ConstraintWidget>(100, 100);
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
        a->setBaselineDistance(28);
        b->setBaselineDistance(8);
        c->setBaselineDistance(8);
        d->setBaselineDistance(8);
        a->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP, 16);
        a->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT, 16);
        a->connect(ConstraintAnchor::Type::RIGHT, b, ConstraintAnchor::Type::LEFT);
        a->connect(ConstraintAnchor::Type::BOTTOM, root, ConstraintAnchor::Type::BOTTOM, 16);
        b->connect(ConstraintAnchor::Type::BASELINE, a, ConstraintAnchor::Type::BASELINE);
        b->connect(ConstraintAnchor::Type::LEFT, a, ConstraintAnchor::Type::RIGHT);
        b->connect(ConstraintAnchor::Type::RIGHT, c, ConstraintAnchor::Type::LEFT);
        c->connect(ConstraintAnchor::Type::BASELINE, b, ConstraintAnchor::Type::BASELINE);
        c->connect(ConstraintAnchor::Type::LEFT, b, ConstraintAnchor::Type::RIGHT);
        c->connect(ConstraintAnchor::Type::RIGHT, d, ConstraintAnchor::Type::LEFT, 16);
        d->connect(ConstraintAnchor::Type::RIGHT, root, ConstraintAnchor::Type::RIGHT);
        d->connect(ConstraintAnchor::Type::BOTTOM, root, ConstraintAnchor::Type::BOTTOM);
        root->setMeasurer(&sMeasurer);
        root->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::WRAP_CONTENT);
        root->layout();
        CHECK_EQ(a->getLeft(), 16);
        CHECK_EQ(a->getTop(), 250);
        CHECK_EQ(b->getLeft(), 116);
        CHECK_EQ(b->getTop(), 270);
        CHECK_EQ(c->getLeft(), 216);
        CHECK_EQ(c->getTop(), 270);
        CHECK_EQ(d->getLeft(), 332);
        CHECK_EQ(d->getTop(), 580);
}

TEST(ClOptimizations_testChainLayoutWrapGuideline) {
    using namespace kiln::constraintlayout::core;
    Widgets W;
        auto* root = W.make<ConstraintWidgetContainer>(0, 0, 600, 600);
        root->setOptimizationLevel(Optimizer::OPTIMIZATION_GROUPING);
        auto* a = W.make<ConstraintWidget>(100, 20);
        auto* guideline = W.make<Guideline>();
        guideline->setOrientation(Guideline::VERTICAL);
        guideline->setGuideEnd(100);
        root->setDebugName("root");
        a->setDebugName("A");
        guideline->setDebugName("guideline");
        root->add(a);
        root->add(guideline);
        a->setBaselineDistance(28);
        a->connect(ConstraintAnchor::Type::LEFT, guideline, ConstraintAnchor::Type::LEFT, 16);
        a->connect(ConstraintAnchor::Type::BOTTOM, root, ConstraintAnchor::Type::BOTTOM, 16);
        root->setMeasurer(&sMeasurer);
        root->setVerticalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::WRAP_CONTENT);
        root->layout();
        CHECK_EQ(a->getLeft(), 516);
        CHECK_EQ(a->getTop(), 0);
        CHECK_EQ(guideline->getLeft(), 500);
}

TEST(ClOptimizations_testChainLayoutWrapGuidelineChain) {
    using namespace kiln::constraintlayout::core;
    Widgets W;
        auto* root = W.make<ConstraintWidgetContainer>(0, 0, 600, 600);
        root->setOptimizationLevel(Optimizer::OPTIMIZATION_GROUPING);
        auto* a = W.make<ConstraintWidget>(20, 20);
        auto* b = W.make<ConstraintWidget>(20, 20);
        auto* c = W.make<ConstraintWidget>(20, 20);
        auto* d = W.make<ConstraintWidget>(20, 20);
        auto* a2 = W.make<ConstraintWidget>(20, 20);
        auto* b2 = W.make<ConstraintWidget>(20, 20);
        auto* c2 = W.make<ConstraintWidget>(20, 20);
        auto* d2 = W.make<ConstraintWidget>(20, 20);
        auto* guidelineStart = W.make<Guideline>();
        auto* guidelineEnd = W.make<Guideline>();
        guidelineStart->setOrientation(Guideline::VERTICAL);
        guidelineEnd->setOrientation(Guideline::VERTICAL);
        guidelineStart->setGuideBegin(30);
        guidelineEnd->setGuideEnd(30);
        root->setDebugName("root");
        a->setDebugName("A");
        b->setDebugName("B");
        c->setDebugName("C");
        d->setDebugName("D");
        a2->setDebugName("A2");
        b2->setDebugName("B2");
        c2->setDebugName("C2");
        d2->setDebugName("D2");
        guidelineStart->setDebugName("guidelineStart");
        guidelineEnd->setDebugName("guidelineEnd");
        root->add(a);
        root->add(b);
        root->add(c);
        root->add(d);
        root->add(a2);
        root->add(b2);
        root->add(c2);
        root->add(d2);
        root->add(guidelineStart);
        root->add(guidelineEnd);
        c->setVisibility(ConstraintWidget::GONE);
        chainConnect(ConstraintAnchor::Type::LEFT, guidelineStart, ConstraintAnchor::Type::RIGHT, guidelineEnd, {a, b, c, d});
        chainConnect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::RIGHT, root, {a2, b2, c2, d2});
        root->setMeasurer(&sMeasurer);
        root->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::WRAP_CONTENT);
        root->layout();
        CHECK_EQ(a->getLeft(), 30);
        CHECK_EQ(b->getLeft(), 50);
        CHECK_EQ(c->getLeft(), 70);
        CHECK_EQ(d->getLeft(), 70);
        CHECK_EQ(guidelineStart->getLeft(), 30);
        CHECK_EQ(guidelineEnd->getLeft(), 90);
        CHECK_EQ(a2->getLeft(), 8);
        CHECK_EQ(b2->getLeft(), 36);
        CHECK_EQ(c2->getLeft(), 64);
        CHECK_EQ(d2->getLeft(), 92);
}

TEST(ClOptimizations_testChainLayoutWrapGuidelineChainVertical) {
    using namespace kiln::constraintlayout::core;
    Widgets W;
        auto* root = W.make<ConstraintWidgetContainer>(0, 0, 600, 600);
        root->setOptimizationLevel(Optimizer::OPTIMIZATION_GROUPING);
        auto* a = W.make<ConstraintWidget>(20, 20);
        auto* b = W.make<ConstraintWidget>(20, 20);
        auto* c = W.make<ConstraintWidget>(20, 20);
        auto* d = W.make<ConstraintWidget>(20, 20);
        auto* a2 = W.make<ConstraintWidget>(20, 20);
        auto* b2 = W.make<ConstraintWidget>(20, 20);
        auto* c2 = W.make<ConstraintWidget>(20, 20);
        auto* d2 = W.make<ConstraintWidget>(20, 20);
        auto* guidelineStart = W.make<Guideline>();
        auto* guidelineEnd = W.make<Guideline>();
        guidelineStart->setOrientation(Guideline::HORIZONTAL);
        guidelineEnd->setOrientation(Guideline::HORIZONTAL);
        guidelineStart->setGuideBegin(30);
        guidelineEnd->setGuideEnd(30);
        root->setDebugName("root");
        a->setDebugName("A");
        b->setDebugName("B");
        c->setDebugName("C");
        d->setDebugName("D");
        a2->setDebugName("A2");
        b2->setDebugName("B2");
        c2->setDebugName("C2");
        d2->setDebugName("D2");
        guidelineStart->setDebugName("guidelineStart");
        guidelineEnd->setDebugName("guidelineEnd");
        root->add(a);
        root->add(b);
        root->add(c);
        root->add(d);
        root->add(a2);
        root->add(b2);
        root->add(c2);
        root->add(d2);
        root->add(guidelineStart);
        root->add(guidelineEnd);
        c->setVisibility(ConstraintWidget::GONE);
        chainConnect(ConstraintAnchor::Type::TOP, guidelineStart, ConstraintAnchor::Type::BOTTOM, guidelineEnd, {a, b, c, d});
        chainConnect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::BOTTOM, root, {a2, b2, c2, d2});
        root->setMeasurer(&sMeasurer);
        root->setVerticalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::WRAP_CONTENT);
        root->layout();
        CHECK_EQ(a->getTop(), 30);
        CHECK_EQ(b->getTop(), 50);
        CHECK_EQ(c->getTop(), 70);
        CHECK_EQ(d->getTop(), 70);
        CHECK_EQ(guidelineStart->getTop(), 30);
        CHECK_EQ(guidelineEnd->getTop(), 90);
        CHECK_EQ(a2->getTop(), 8);
        CHECK_EQ(b2->getTop(), 36);
        CHECK_EQ(c2->getTop(), 64);
        CHECK_EQ(d2->getTop(), 92);
        CHECK_EQ(a->getLeft(), 0);
        CHECK_EQ(b->getLeft(), 0);
        CHECK_EQ(c->getLeft(), 0);
        CHECK_EQ(d->getLeft(), 0);
        CHECK_EQ(a2->getLeft(), 0);
        CHECK_EQ(b2->getLeft(), 0);
        CHECK_EQ(c2->getLeft(), 0);
        CHECK_EQ(d2->getLeft(), 0);
}

TEST(ClOptimizations_testChainLayoutWrapRatioChain) {
    using namespace kiln::constraintlayout::core;
    Widgets W;
        auto* root = W.make<ConstraintWidgetContainer>(0, 0, 600, 600);
        root->setOptimizationLevel(Optimizer::OPTIMIZATION_GROUPING);
        auto* a = W.make<ConstraintWidget>(20, 20);
        auto* b = W.make<ConstraintWidget>(20, 20);
        auto* c = W.make<ConstraintWidget>(20, 20);
        root->setDebugName("root");
        a->setDebugName("A");
        b->setDebugName("B");
        c->setDebugName("C");
        root->add(a);
        root->add(b);
        root->add(c);
        chainConnect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::BOTTOM, root, {a, b, c});
        a->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT);
        b->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT);
        c->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT);
        a->connect(ConstraintAnchor::Type::RIGHT, root, ConstraintAnchor::Type::RIGHT);
        b->connect(ConstraintAnchor::Type::RIGHT, root, ConstraintAnchor::Type::RIGHT);
        c->connect(ConstraintAnchor::Type::RIGHT, root, ConstraintAnchor::Type::RIGHT);
        a->setVerticalChainStyle(ConstraintWidget::CHAIN_SPREAD_INSIDE);
        b->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::MATCH_CONSTRAINT);
        b->setVerticalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::MATCH_CONSTRAINT);
        b->setDimensionRatio("1:1");
        root->setMeasurer(&sMeasurer);
        root->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::WRAP_CONTENT);
        root->setVerticalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::FIXED);
        root->setHeight(600);
        root->layout();
        CHECK_EQ(a->getTop(), 0);
        CHECK_EQ(b->getTop(), 290);
        CHECK_EQ(c->getTop(), 580);
        CHECK_EQ(a->getLeft(), 0);
        CHECK_EQ(b->getLeft(), 0);
        CHECK_EQ(c->getLeft(), 0);
        CHECK_EQ(b->getWidth(), 20);
        CHECK_EQ(b->getHeight(), b->getWidth());
}

TEST(ClOptimizations_testLayoutWrapBarrier) {
    using namespace kiln::constraintlayout::core;
    Widgets W;
        auto* root = W.make<ConstraintWidgetContainer>("root", 600, 600);
        root->setOptimizationLevel(Optimizer::OPTIMIZATION_GROUPING);
        auto* a = W.make<ConstraintWidget>("A", 20, 20);
        auto* b = W.make<ConstraintWidget>("B", 20, 20);
        auto* c = W.make<ConstraintWidget>("C", 20, 20);
        auto* barrier = W.make<Barrier>("Barrier");
        barrier->setBarrierType(Barrier::BOTTOM);
        root->add(a);
        root->add(b);
        root->add(c);
        root->add(barrier);
        a->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP);
        b->connect(ConstraintAnchor::Type::TOP, a, ConstraintAnchor::Type::BOTTOM);
        b->setVisibility(ConstraintWidget::GONE);
        c->connect(ConstraintAnchor::Type::TOP, barrier, ConstraintAnchor::Type::TOP);
        barrier->add(a);
        barrier->add(b);
        root->setMeasurer(&sMeasurer);
        root->setVerticalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::WRAP_CONTENT);
        root->layout();
        CHECK_EQ(a->getLeft(), 0);
        CHECK_EQ(a->getTop(), 0);
        CHECK_EQ(b->getLeft(), 0);
        CHECK_EQ(b->getTop(), 20);
        CHECK_EQ(c->getLeft(), 0);
        CHECK_EQ(c->getTop(), 20);
        CHECK_EQ(barrier->getTop(), 20);
        CHECK_EQ(root->getHeight(), 40);
}

TEST(ClOptimizations_testLayoutWrapGuidelinesMatch) {
    using namespace kiln::constraintlayout::core;
    Widgets W;
        auto* root = W.make<ConstraintWidgetContainer>("root", 600, 600);
        root->setOptimizationLevel(Optimizer::OPTIMIZATION_GROUPING);
        auto* a = W.make<ConstraintWidget>("A", 20, 20);
        auto* left = W.make<Guideline>();
        left->setOrientation(Guideline::VERTICAL);
        left->setGuideBegin(30);
        left->setDebugName("L");
        auto* right = W.make<Guideline>();
        right->setOrientation(Guideline::VERTICAL);
        right->setGuideEnd(30);
        right->setDebugName("R");
        auto* top = W.make<Guideline>();
        top->setOrientation(Guideline::HORIZONTAL);
        top->setGuideBegin(30);
        top->setDebugName("T");
        auto* bottom = W.make<Guideline>();
        bottom->setOrientation(Guideline::HORIZONTAL);
        bottom->setGuideEnd(30);
        bottom->setDebugName("B");
        root->add(a);
        root->add(left);
        root->add(right);
        root->add(top);
        root->add(bottom);
        a->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::MATCH_CONSTRAINT);
        a->setVerticalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::MATCH_CONSTRAINT);
        a->connect(ConstraintAnchor::Type::LEFT, left, ConstraintAnchor::Type::LEFT);
        a->connect(ConstraintAnchor::Type::RIGHT, right, ConstraintAnchor::Type::RIGHT);
        a->connect(ConstraintAnchor::Type::TOP, top, ConstraintAnchor::Type::TOP);
        a->connect(ConstraintAnchor::Type::BOTTOM, bottom, ConstraintAnchor::Type::BOTTOM);
        root->setMeasurer(&sMeasurer);
        root->setVerticalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::WRAP_CONTENT);
        root->layout();
        CHECK_EQ(root->getHeight(), 60);
        CHECK_EQ(a->getLeft(), 30);
        CHECK_EQ(a->getTop(), 30);
        CHECK_EQ(a->getWidth(), 540);
        CHECK_EQ(a->getHeight(), 0);
}

TEST(ClOptimizations_testLayoutWrapMatch) {
    using namespace kiln::constraintlayout::core;
    Widgets W;
        auto* root = W.make<ConstraintWidgetContainer>("root", 600, 600);
        root->setOptimizationLevel(Optimizer::OPTIMIZATION_GROUPING);
        auto* a = W.make<ConstraintWidget>("A", 50, 20);
        auto* b = W.make<ConstraintWidget>("B", 50, 30);
        auto* c = W.make<ConstraintWidget>("C", 50, 20);
        root->add(a);
        root->add(b);
        root->add(c);
        a->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT);
        a->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP);
        b->connect(ConstraintAnchor::Type::LEFT, a, ConstraintAnchor::Type::RIGHT);
        b->connect(ConstraintAnchor::Type::RIGHT, c, ConstraintAnchor::Type::LEFT);
        b->connect(ConstraintAnchor::Type::TOP, a, ConstraintAnchor::Type::BOTTOM);
        b->connect(ConstraintAnchor::Type::BOTTOM, c, ConstraintAnchor::Type::TOP);
        c->connect(ConstraintAnchor::Type::RIGHT, root, ConstraintAnchor::Type::RIGHT);
        c->connect(ConstraintAnchor::Type::BOTTOM, root, ConstraintAnchor::Type::BOTTOM);
        b->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::MATCH_CONSTRAINT);
        b->setVerticalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::WRAP_CONTENT);
        root->setMeasurer(&sMeasurer);
        root->setVerticalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::WRAP_CONTENT);
        root->layout();
        CHECK_EQ(b->getTop(), 20);
        CHECK_EQ(b->getBottom(), 50);
        CHECK_EQ(b->getLeft(), 50);
        CHECK_EQ(b->getRight(), 550);
        CHECK_EQ(root->getHeight(), 70);
}

TEST(ClOptimizations_testLayoutWrapBarrier2) {
    using namespace kiln::constraintlayout::core;
    Widgets W;
        auto* root = W.make<ConstraintWidgetContainer>("root", 600, 600);
        root->setOptimizationLevel(Optimizer::OPTIMIZATION_GROUPING);
        auto* a = W.make<ConstraintWidget>("A", 50, 20);
        auto* b = W.make<ConstraintWidget>("B", 50, 30);
        auto* c = W.make<ConstraintWidget>("C", 50, 20);
        auto* guideline = W.make<Guideline>();
        guideline->setDebugName("end");
        guideline->setGuideEnd(40);
        guideline->setOrientation(ConstraintWidget::VERTICAL);
        auto* barrier = W.make<Barrier>();
        barrier->setBarrierType(Barrier::LEFT);
        barrier->setDebugName("barrier");
        barrier->add(b);
        barrier->add(c);
        root->add(a);
        root->add(b);
        root->add(c);
        root->add(barrier);
        root->add(guideline);
        a->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT);
        a->connect(ConstraintAnchor::Type::RIGHT, barrier, ConstraintAnchor::Type::LEFT);
        b->connect(ConstraintAnchor::Type::RIGHT, guideline, ConstraintAnchor::Type::RIGHT);
        c->connect(ConstraintAnchor::Type::RIGHT, root, ConstraintAnchor::Type::RIGHT);
        root->setMeasurer(&sMeasurer);
        root->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::WRAP_CONTENT);
        root->layout();
        CHECK_EQ(root->getWidth(), 140);
}

TEST(ClOptimizations_testLayoutWrapBarrier3) {
    using namespace kiln::constraintlayout::core;
    Widgets W;
        auto* root = W.make<ConstraintWidgetContainer>("root", 600, 600);
        root->setOptimizationLevel(Optimizer::OPTIMIZATION_GROUPING);
        root->setOptimizationLevel(Optimizer::OPTIMIZATION_NONE);
        auto* a = W.make<ConstraintWidget>("A", 50, 20);
        auto* b = W.make<ConstraintWidget>("B", 50, 30);
        auto* c = W.make<ConstraintWidget>("C", 50, 20);
        auto* guideline = W.make<Guideline>();
        guideline->setDebugName("end");
        guideline->setGuideEnd(40);
        guideline->setOrientation(ConstraintWidget::VERTICAL);
        auto* barrier = W.make<Barrier>();
        barrier->setBarrierType(Barrier::LEFT);
        barrier->setDebugName("barrier");
        barrier->add(b);
        barrier->add(c);
        root->add(a);
        root->add(b);
        root->add(c);
        root->add(barrier);
        root->add(guideline);
        a->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT);
        a->connect(ConstraintAnchor::Type::RIGHT, barrier, ConstraintAnchor::Type::LEFT);
        b->connect(ConstraintAnchor::Type::RIGHT, guideline, ConstraintAnchor::Type::RIGHT);
        c->connect(ConstraintAnchor::Type::RIGHT, root, ConstraintAnchor::Type::RIGHT);
        root->setMeasurer(&sMeasurer);
        root->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::WRAP_CONTENT);
        root->layout();
        CHECK_EQ(root->getWidth(), 140);
}

TEST(ClOptimizations_testSimpleGuideline2) {
    using namespace kiln::constraintlayout::core;
    Widgets W;
        auto* root = W.make<ConstraintWidgetContainer>("root", 600, 600);
        auto* guidelineStart = W.make<Guideline>();
        guidelineStart->setDebugName("start");
        guidelineStart->setGuidePercent(0.1f);
        guidelineStart->setOrientation(ConstraintWidget::VERTICAL);
        auto* guidelineEnd = W.make<Guideline>();
        guidelineEnd->setDebugName("end");
        guidelineEnd->setGuideEnd(40);
        guidelineEnd->setOrientation(ConstraintWidget::VERTICAL);
        auto* a = W.make<ConstraintWidget>("A", 50, 20);
        root->add(a);
        root->add(guidelineStart);
        root->add(guidelineEnd);
        a->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::MATCH_CONSTRAINT);
        a->connect(ConstraintAnchor::Type::LEFT, guidelineStart, ConstraintAnchor::Type::LEFT);
        a->connect(ConstraintAnchor::Type::RIGHT, guidelineEnd, ConstraintAnchor::Type::RIGHT);
        root->setMeasurer(&sMeasurer);
        root->layout();
}

