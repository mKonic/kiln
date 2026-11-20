// Ported from constraintlayout-core GuidelineTest.java (androidx), by tools in
// the scratchpad, then reviewed. Expected values are upstream's.
#include "ConstraintLayoutTestSupport.h"

namespace {
using namespace kiln::constraintlayout::core;
}

TEST(ClGuideline_testWrapGuideline) {
    using namespace kiln::constraintlayout::core;
    Widgets W;
        auto* root = W.make<ConstraintWidgetContainer>(0, 0, 800, 600);
        auto* a = W.make<ConstraintWidget>(100, 20);
        auto* guidelineRight = W.make<Guideline>();
        guidelineRight->setOrientation(Guideline::VERTICAL);
        auto* guidelineBottom = W.make<Guideline>();
        guidelineBottom->setOrientation(Guideline::HORIZONTAL);
        guidelineRight->setGuidePercent(0.64f);
        guidelineBottom->setGuideEnd(60);
        root->setDebugName("Root");
        a->setDebugName("A");
        guidelineRight->setDebugName("GuidelineRight");
        guidelineBottom->setDebugName("GuidelineBottom");
        a->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT);
        a->connect(ConstraintAnchor::Type::RIGHT, guidelineRight, ConstraintAnchor::Type::RIGHT);
        a->connect(ConstraintAnchor::Type::BOTTOM, guidelineBottom, ConstraintAnchor::Type::TOP);
        root->add(a);
        root->add(guidelineRight);
        root->add(guidelineBottom);
        root->layout();
        root->setVerticalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::WRAP_CONTENT);
        root->layout();
        CHECK_EQ(root->getHeight(), 80);
}

TEST(ClGuideline_testWrapGuideline2) {
    using namespace kiln::constraintlayout::core;
    Widgets W;
        auto* root = W.make<ConstraintWidgetContainer>(0, 0, 800, 600);
        auto* a = W.make<ConstraintWidget>(100, 20);
        auto* guideline = W.make<Guideline>();
        guideline->setOrientation(Guideline::VERTICAL);
        guideline->setGuideBegin(60);
        root->setDebugName("Root");
        a->setDebugName("A");
        guideline->setDebugName("Guideline");
        a->connect(ConstraintAnchor::Type::LEFT, guideline, ConstraintAnchor::Type::LEFT, 5);
        a->connect(ConstraintAnchor::Type::RIGHT, root, ConstraintAnchor::Type::RIGHT, 5);
        a->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::MATCH_CONSTRAINT);
        root->add(a);
        root->add(guideline);
        root->setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::WRAP_CONTENT);
        root->layout();
        CHECK_EQ(root->getWidth(), 70);
}

