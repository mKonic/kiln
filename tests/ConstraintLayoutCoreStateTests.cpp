// Ported from constraintlayout-core ComposeLayoutsTest.java and cl/ConstraintSetParserTest.java
// (androidx). Expected values are upstream's.
#include "ConstraintLayoutTestSupport.h"

#include "kiln/constraintlayout/core/state/ConstraintSetParser.h"
#include "kiln/constraintlayout/core/state/StateHelpers.h"

#include <cstring>

namespace {
    using namespace kiln::constraintlayout::core;
    using namespace kiln::constraintlayout::core::state;

    // Views are names here; widgets carry them as companions.
    const char* const kBox = "box";
    const char* const kDivider = "divider";

    bool isBox(ConstraintWidget* widget) { return widget->getCompanionWidget() == kBox; }

    struct ComposeLayoutsMeasurer : BasicMeasure::Measurer {
        void measure(ConstraintWidget* widget, BasicMeasure::Measure& measure) override {
            using DB = ConstraintWidget::DimensionBehaviour;
            DB horizontalBehavior = measure.horizontalBehavior;
            DB verticalBehavior = measure.verticalBehavior;
            int horizontalDimension = measure.horizontalDimension;
            int verticalDimension = measure.verticalDimension;
            if (horizontalBehavior == DB::FIXED) {
                measure.measuredWidth = horizontalDimension;
            } else if (horizontalBehavior == DB::MATCH_CONSTRAINT) {
                measure.measuredWidth = horizontalDimension;
                if (isBox(widget) && measure.measureStrategy == BasicMeasure::Measure::SELF_DIMENSIONS) measure.measuredWidth = 1080;
            } else if (horizontalBehavior == DB::WRAP_CONTENT) {
                if (isBox(widget)) measure.measuredWidth = 1080;
            }
            if (verticalBehavior == DB::FIXED) {
                measure.measuredHeight = verticalDimension;
            } else if (verticalBehavior == DB::WRAP_CONTENT) {
                if (isBox(widget)) measure.measuredHeight = measure.measuredWidth / 2;
            }
        }
        void didMeasures() override {}
    };
}

TEST(ClComposeLayouts_dividerMatchTextHeight_inWrapConstraintLayout_longText) {
    using namespace kiln::constraintlayout::core;
    using namespace kiln::constraintlayout::core::state;
    ComposeLayoutsMeasurer measurer;
    ConstraintWidgetContainer root(0, 0, 1080, 1977);
    State state;
    ConstraintReference* parent = state.constraints(State::PARENT);
    state.verticalGuideline("guideline")->percent(0.5f);
    state.constraints("box")
        ->centerHorizontally(parent)
        .centerVertically(parent)
        .startToEnd(std::string("guideline"))
        .width(Dimension::createSuggested(Dimension::Value::WRAP_DIMENSION))
        .height(Dimension::createWrap())
        .setView(const_cast<char*>(kBox));
    state.constraints("divider")
        ->centerHorizontally(parent)
        .centerVertically(parent)
        .width(Dimension::createFixed(1))
        .height(Dimension::createPercent(0.8f).suggested(0))
        .setView(const_cast<char*>(kDivider));
    state.apply(root);
    root.setWidth(1080);
    root.setHorizontalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::FIXED);
    ConstraintWidget* box = state.constraints("box")->getConstraintWidget();
    ConstraintWidget* guideline = state.guideline("guideline", ConstraintWidget::VERTICAL)->getConstraintWidget();
    ConstraintWidget* divider = state.constraints("divider")->getConstraintWidget();
    root.setDebugName("root");
    box->setDebugName("box");
    guideline->setDebugName("guideline");
    divider->setDebugName("divider");
    root.setMeasurer(&measurer);
    root.setVerticalDimensionBehaviour(ConstraintWidget::DimensionBehaviour::WRAP_CONTENT);
    root.setOptimizationLevel(Optimizer::OPTIMIZATION_STANDARD);
    root.measure(root.getOptimizationLevel(), 0, 0, 0, 0, 0, 0, 0, 0);
    CHECK_EQ(root.getWidth() / 2, box->getWidth());
    CHECK_EQ(root.getWidth() / 2 / 2, box->getHeight());
    CHECK_EQ(1, divider->getWidth());
    CHECK_EQ(int(box->getHeight() * 0.8), divider->getHeight());
}

TEST(ClConstraintSetParser_testSimpleConstraintSet1) {
    using namespace kiln::constraintlayout::core;
    using namespace kiln::constraintlayout::core::state;
    std::string jsonString = "     n"
                             "                  start: {\n"
                             "                    id1: {\n"
                             "                      width: 40, height: 40,\n"
                             "                      start:  ['parent', 'start' , 16],\n"
                             "                      bottom: ['parent', 'bottom', 16]\n"
                             "                    }\n"
                             "                  },\n";
    ConstraintWidgetContainer root(1000, 1000);
    ConstraintWidget idWidget;
    State state;
    state.setDpToPixel([](float dp) { return dp; });
    ConstraintSetParser::LayoutVariables vars;
    idWidget.stringId = "id1";
    ConstraintSetParser::parseJSON(jsonString, state, vars);
    root.add(&idWidget);
    ConstraintReference* id1 = state.constraints("id1");
    CHECK(id1 != nullptr);
    state.reset();
    state.apply(root);
}
