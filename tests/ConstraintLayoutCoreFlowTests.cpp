// Ported from constraintlayout-core FlowTest.java (androidx), by tools in
// the scratchpad, then reviewed. Expected values are upstream's.
#include "ConstraintLayoutTestSupport.h"

namespace {
using namespace kiln::constraintlayout::core;
}

// FlowTest.sMeasurer: measures virtual layouts through their own measure().
namespace {
struct FlowTestMeasurer : kiln::constraintlayout::core::BasicMeasure::Measurer {
    void measure(kiln::constraintlayout::core::ConstraintWidget* widget, kiln::constraintlayout::core::BasicMeasure::Measure& measure) override {
        using namespace kiln::constraintlayout::core;
        using DB = ConstraintWidget::DimensionBehaviour;
        DB horizontalBehavior = measure.horizontalBehavior;
        DB verticalBehavior = measure.verticalBehavior;
        int horizontalDimension = measure.horizontalDimension;
        int verticalDimension = measure.verticalDimension;
        if (widget->is(ConstraintWidget::KIND_VIRTUAL_LAYOUT)) {
            auto* layout = static_cast<VirtualLayout*>(widget);
            int widthMode = BasicMeasure::UNSPECIFIED;
            int heightMode = BasicMeasure::UNSPECIFIED;
            int widthSize = 0;
            int heightSize = 0;
            if (layout->getHorizontalDimensionBehaviour() == DB::MATCH_PARENT) {
                widthSize = layout->getParent() != nullptr ? layout->getParent()->getWidth() : 0;
                widthMode = BasicMeasure::EXACTLY;
            } else if (horizontalBehavior == DB::FIXED) {
                widthSize = horizontalDimension;
                widthMode = BasicMeasure::EXACTLY;
            }
            if (layout->getVerticalDimensionBehaviour() == DB::MATCH_PARENT) {
                heightSize = layout->getParent() != nullptr ? layout->getParent()->getHeight() : 0;
                heightMode = BasicMeasure::EXACTLY;
            } else if (verticalBehavior == DB::FIXED) {
                heightSize = verticalDimension;
                heightMode = BasicMeasure::EXACTLY;
            }
            layout->measure(widthMode, widthSize, heightMode, heightSize);
            measure.measuredWidth = layout->getMeasuredWidth();
            measure.measuredHeight = layout->getMeasuredHeight();
        } else {
            if (horizontalBehavior == DB::FIXED) measure.measuredWidth = horizontalDimension;
            if (verticalBehavior == DB::FIXED) measure.measuredHeight = verticalDimension;
        }
    }
    void didMeasures() override {}
} sFlowMeasurer;
}

TEST(ClFlow_testFlowBaseline) {
    using namespace kiln::constraintlayout::core;
    Widgets W;
        auto* root = W.make<ConstraintWidgetContainer>(0, 0, 1080, 1536);
        auto* a = W.make<ConstraintWidget>(100, 20);
        auto* b = W.make<ConstraintWidget>(20, 15);
        auto* flow = W.make<Flow>();
        root->setMeasurer(&sFlowMeasurer);
        root->setDebugName("root");
        a->setDebugName("A");
        b->setDebugName("B");
        flow->setDebugName("Flow");
        flow->setVerticalAlign(Flow::VERTICAL_ALIGN_BASELINE);
        flow->add(a);
        flow->add(b);
        a->setBaselineDistance(15);
        flow->setHeight(30);
        flow->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::MATCH_CONSTRAINT);
        flow->setVerticalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::FIXED);
        flow->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT);
        flow->connect(ConstraintAnchor::Type::RIGHT, root, ConstraintAnchor::Type::RIGHT);
        flow->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP);
        flow->connect(ConstraintAnchor::Type::BOTTOM, root, ConstraintAnchor::Type::BOTTOM);
        root->add(flow);
        root->add(a);
        root->add(b);
        root->measure(Optimizer::OPTIMIZATION_NONE,
                0, 0, 0, 0, 0, 0, 0, 0);
        root->layout();
        CHECK_EQ(flow->getWidth(), 1080);
        CHECK_EQ(flow->getHeight(), 30);
        CHECK_EQ(flow->getTop(), 753);
        CHECK_EQ(a->getLeft(), 320);
        CHECK_EQ(a->getTop(), 758);
        CHECK_EQ(b->getLeft(), 740);
        CHECK_EQ(b->getTop(), 761);
}

TEST(ClFlow_testComplexChain) {
    using namespace kiln::constraintlayout::core;
    Widgets W;
        auto* root = W.make<ConstraintWidgetContainer>(0, 0, 1080, 1536);
        auto* a = W.make<ConstraintWidget>(100, 20);
        auto* b = W.make<ConstraintWidget>(100, 20);
        auto* c = W.make<ConstraintWidget>(100, 20);
        auto* flow = W.make<Flow>();
        root->setMeasurer(&sFlowMeasurer);
        root->setDebugName("root");
        a->setDebugName("A");
        b->setDebugName("B");
        c->setDebugName("C");
        flow->setDebugName("Flow");
        flow->setWrapMode(Flow::WRAP_CHAIN);
        flow->setMaxElementsWrap(2);
        flow->add(a);
        flow->add(b);
        flow->add(c);
        root->add(flow);
        root->add(a);
        root->add(b);
        root->add(c);
        a->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::MATCH_CONSTRAINT);
        b->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::MATCH_CONSTRAINT);
        c->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::MATCH_CONSTRAINT);
        flow->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT);
        flow->connect(ConstraintAnchor::Type::RIGHT, root, ConstraintAnchor::Type::RIGHT);
        flow->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP);
        flow->connect(ConstraintAnchor::Type::BOTTOM, root, ConstraintAnchor::Type::BOTTOM);
        flow->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::MATCH_PARENT);
        flow->setVerticalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::WRAP_CONTENT);
        root->measure(Optimizer::OPTIMIZATION_NONE,
                0, 0, 0, 0, 0, 0, 0, 0);
        root->layout();
        CHECK_EQ(a->getWidth(), 540);
        CHECK_EQ(b->getWidth(), 540);
        CHECK_EQ(c->getWidth(), 1080);
        CHECK_EQ(flow->getWidth(), root->getWidth());
        CHECK_EQ(flow->getHeight(), std::max(a->getHeight(), b->getHeight()) + c->getHeight());
        CHECK_EQ(flow->getTop(), 748);
}

TEST(ClFlow_testFlowText) {
    using namespace kiln::constraintlayout::core;
    Widgets W;
        auto* root = W.make<ConstraintWidgetContainer>(20, 5);
        auto* a = W.make<ConstraintWidget>(7, 1);
        auto* b = W.make<ConstraintWidget>(6, 1);
        a->setDebugName("A");
        b->setDebugName("B");
        auto* flow = W.make<Flow>();
        flow->setDebugName("flow");
        flow->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::FIXED);
        flow->setVerticalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::WRAP_CONTENT);
        flow->setWidth(20);
        flow->setHeight(2);
        flow->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT);
        flow->connect(ConstraintAnchor::Type::RIGHT, root, ConstraintAnchor::Type::RIGHT);
        flow->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP);
        flow->connect(ConstraintAnchor::Type::BOTTOM, root, ConstraintAnchor::Type::BOTTOM);
        flow->add(a);
        flow->add(b);
        root->add(flow);
        root->add(a);
        root->add(b);
        struct SizeMeasurer : BasicMeasure::Measurer {
            void measure(ConstraintWidget* widget, BasicMeasure::Measure& measure) override {
                measure.measuredWidth = widget->getWidth();
                measure.measuredHeight = widget->getHeight();
            }
            void didMeasures() override {}
        } sizeMeasurer;
        root->setMeasurer(&sizeMeasurer);
        root->setMeasurer(&sFlowMeasurer);
        root->measure(Optimizer::OPTIMIZATION_NONE,
                0, 0, 0, 0, 0, 0, 0, 0);
}

