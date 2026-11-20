// Ported from constraintlayout-core MotionTransitionTest.java (androidx). Expected values are
// upstream's.
#include "ConstraintLayoutMotionSupport.h"
#include "ConstraintLayoutTestSupport.h"

#include "kiln/constraintlayout/core/parser/CLParser.h"
#include "kiln/constraintlayout/core/state/Transition.h"
#include "kiln/constraintlayout/core/state/TransitionParser.h"

#include <cstdint>
#include <string>
#include <vector>

using namespace kiln::constraintlayout::core;
using namespace kiln::constraintlayout::core::state;

namespace {

    ConstraintWidgetContainer* makeLayout1(Widgets& W) {
        auto* root = W.make<ConstraintWidgetContainer>(0, 0, 1000, 1000);
        root->setDebugName("root");
        root->stringId = "root";
        auto* button0 = W.make<ConstraintWidget>(200, 20);
        button0->setDebugName("button0");
        button0->stringId = "button0";
        auto* button1 = W.make<ConstraintWidget>(200, 20);
        button1->setDebugName("button1");
        button1->stringId = "button1";
        button0->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT);
        button0->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP);
        button1->connect(ConstraintAnchor::Type::LEFT, button0, ConstraintAnchor::Type::LEFT);
        button1->connect(ConstraintAnchor::Type::TOP, button0, ConstraintAnchor::Type::BOTTOM);
        button1->connect(ConstraintAnchor::Type::RIGHT, button0, ConstraintAnchor::Type::RIGHT);
        root->add(button0);
        root->add(button1);
        root->layout();
        return root;
    }

    ConstraintWidgetContainer* makeLayout2(Widgets& W) {
        auto* root = W.make<ConstraintWidgetContainer>(0, 0, 1000, 1000);
        root->setDebugName("root");
        auto* button0 = W.make<ConstraintWidget>(200, 20);
        button0->setDebugName("button0");
        button0->stringId = "button0";
        auto* button1 = W.make<ConstraintWidget>(20, 200);
        button1->setDebugName("button1");
        button1->stringId = "button1";
        button0->connect(ConstraintAnchor::Type::RIGHT, root, ConstraintAnchor::Type::RIGHT);
        button0->connect(ConstraintAnchor::Type::BOTTOM, root, ConstraintAnchor::Type::BOTTOM);
        button1->connect(ConstraintAnchor::Type::LEFT, button0, ConstraintAnchor::Type::LEFT);
        button1->connect(ConstraintAnchor::Type::BOTTOM, button0, ConstraintAnchor::Type::TOP);
        button1->connect(ConstraintAnchor::Type::RIGHT, button0, ConstraintAnchor::Type::RIGHT);
        root->add(button0);
        root->add(button1);
        root->layout();
        return root;
    }

    ConstraintWidgetContainer* makeLayout(Widgets& W, int w1, int h1, int w2, int h2) {
        auto* root = W.make<ConstraintWidgetContainer>(0, 0, 1000, 1000);
        root->setDebugName("root");
        root->stringId = "root";
        auto* button0 = W.make<ConstraintWidget>(w1, h1);
        button0->setDebugName("button0");
        button0->stringId = "button0";
        auto* button1 = W.make<ConstraintWidget>(w2, h2);
        button1->setDebugName("button1");
        button1->stringId = "button1";
        button0->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT);
        button0->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP);
        button1->connect(ConstraintAnchor::Type::LEFT, button0, ConstraintAnchor::Type::RIGHT);
        button1->connect(ConstraintAnchor::Type::TOP, button0, ConstraintAnchor::Type::BOTTOM);
        root->add(button0);
        root->add(button1);
        root->layout();
        return root;
    }

    void setUpOnSwipe(Widgets& W, Transition& transition, const std::string& onSwipeString) {
        auto* cwc1 = makeLayout(W, 100, 100, 100, 100);
        auto* cwc2 = makeLayout(W, 500, 900, 100, 100);
        for (ConstraintWidget* child : cwc1->getChildren()) transition.getStart(child).widget = child;
        transition.updateFrom(*cwc1, Transition::START);
        for (ConstraintWidget* child : cwc2->getChildren()) transition.getEnd(child).widget = child;
        transition.updateFrom(*cwc2, Transition::END);
        const std::string jsonString = "                  default: {\n"
                                       "                    from: 'start',   to: 'end',\n"
                                       "                    pathMotionArc: 'startHorizontal',\n" +
                                       onSwipeString +
                                       "                    KeyFrames: {\n"
                                       "                     KeyPositions: [\n"
                                       "                     {\n"
                                       "                      target: ['button1'],\n"
                                       "                      type: 'pathRelative'\n"
                                       "                      frames: [25, 50, 75],\n"
                                       "                      percentX: [0.25, 0.5, 0.75],\n"
                                       "                      percentY: [0.0, 0.1, 0.0]\n"
                                       "                     }\n"
                                       "                     ]\n"
                                       "                  },\n"
                                       "                  }\n";
        auto json = CLParser::parse(jsonString);
        TransitionParser::parse(*json, transition);
    }

}

TEST(MotionTransition_testTransition) {
        Transition transition([](float dp) { return dp; });
        Widgets W;
        auto* cwc1 = makeLayout1(W);
        auto* cwc2 = makeLayout2(W);
        for (ConstraintWidget* child : cwc1->getChildren()) {
            WidgetFrame& wf = transition.getStart(child);
            wf.widget = child;
        }
        transition.updateFrom(*cwc1, Transition::START);
        for (ConstraintWidget* child : cwc2->getChildren()) {
            WidgetFrame& wf = transition.getEnd(child);
            wf.widget = child;
        }
        transition.updateFrom(*cwc2, Transition::END);
        transition.interpolate(cwc1->getWidth(), cwc1->getHeight(), 0.5f);
        WidgetFrame* s1 = transition.getStart("button1");
        WidgetFrame* e1 = transition.getEnd("button1");
        WidgetFrame* f1 = transition.getInterpolated("button1");
        CHECK(f1 != nullptr);
        CHECK_EQ(20, s1->top);
        CHECK_EQ(0, s1->left);
        CHECK_EQ(780, e1->top);
        CHECK_EQ(890, e1->left);

        CHECK_EQ(400, f1->top);
        CHECK_EQ(445, f1->left);
        CHECK_EQ(20, s1->bottom - s1->top);
        CHECK_EQ(200, s1->right - s1->left);
        CHECK_EQ(110, f1->bottom - f1->top);
        CHECK_EQ(110, f1->right - f1->left);
        CHECK_EQ(200, e1->bottom - e1->top);
        CHECK_EQ(20, e1->right - e1->left);
}

TEST(MotionTransition_testTransitionJson) {
        Transition transition([](float dp) { return dp; });
        Widgets W;
        auto* cwc1 = makeLayout1(W);
        auto* cwc2 = makeLayout2(W);
        for (ConstraintWidget* child : cwc1->getChildren()) {
            WidgetFrame& wf = transition.getStart(child);
            wf.widget = child;
        }
        transition.updateFrom(*cwc1, Transition::START);
        for (ConstraintWidget* child : cwc2->getChildren()) {
            WidgetFrame& wf = transition.getEnd(child);
            wf.widget = child;
        }
        transition.updateFrom(*cwc2, Transition::END);
        std::string jstr =
                "                  default: {\n"
                        "                    from: 'start',   to: 'end',\n"
                        "                    pathMotionArc: 'startVertical',\n"
                        "                    onSwipe: { \n"
                        "                        anchor : 'button1',\n"
                        "                    side: 'top',\n"
                        "                        direction: 'up',\n"
                        "                        scale: 1,\n"
                        "                        threshold: 10,\n"
                        "                        mode:  'velocity',\n"
                        "                        maxVelocity: 4.0,\n"
                        "                        maxAccel: 4.0,\n"
                        "                   },      "
                        "                    KeyFrames: {\n"
                        "                     KeyPositions: [\n"
                        "                     {\n"
                        "                      target: ['button1'],\n"
                        "                      frames: [25, 50, 75],\n"
                        "                      percentX: [0.2, 0.3, 0.7],\n"
                        "                      percentY: [0.4, 0.5, 0.7]\n"
                        "                      percentHeight: [0.4, 0.9, 0.7]\n"
                        "                     }\n"
                        "                     ]\n"
                        "                  },\n"
                        "                  }\n";
        try {
            auto json = CLParser::parse(jstr);
            TransitionParser::parse(*json, transition);
        } catch (const CLParsingException&) {
        }
        CHECK(transition.hasOnSwipe());
        float progress = transition.dragToProgress(0.5f,
                cwc1->getWidth(), cwc1->getHeight(), 10.0f, 47);
        CHECK_NEAR(0.1, progress, 0.001f);
        transition.interpolate(cwc1->getWidth(), cwc1->getHeight(), 0.5f);
        WidgetFrame* s1 = transition.getStart("button1");
        WidgetFrame* e1 = transition.getEnd("button1");
        WidgetFrame* f1 = transition.getInterpolated("button1");
        CHECK(f1 != nullptr);
        CHECK_EQ(20, s1->top);
        CHECK_EQ(0, s1->left);
        CHECK_EQ(780, e1->top);
        CHECK_EQ(890, e1->left);
        CHECK_EQ(20, s1->bottom - s1->top);
        CHECK_EQ(200, s1->right - s1->left);
        CHECK_EQ(182, f1->bottom - f1->top);   
        CHECK_EQ(110, f1->right - f1->left);
        CHECK_EQ(200, e1->bottom - e1->top);
        CHECK_EQ(20, e1->right - e1->left);

        CHECK_EQ(409, f1->top);
        CHECK_EQ(267, f1->left);
}

TEST(MotionTransition_testTransitionJson2) {
        Transition transition([](float dp) { return dp; });
        Widgets W;
        auto* cwc1 = makeLayout(W, 100, 100, 100, 100);
        auto* cwc2 = makeLayout(W, 500, 900, 100, 100);
        for (ConstraintWidget* child : cwc1->getChildren()) {
            WidgetFrame& wf = transition.getStart(child);
            wf.widget = child;
        }
        transition.updateFrom(*cwc1, Transition::START);
        for (ConstraintWidget* child : cwc2->getChildren()) {
            WidgetFrame& wf = transition.getEnd(child);
            wf.widget = child;
        }
        transition.updateFrom(*cwc2, Transition::END);
        std::string jsonString =
                "                  default: {\n"
                        "                    from: 'start',   to: 'end',\n"
                        "                    pathMotionArc: 'startHorizontal',\n"
                        "                    onSwipe: { \n"
                        "                        anchor :'button1',\n"
                        "                        side: 'top',\n"
                        "                        direction: 'up',\n"
                        "                        scale: 1,\n"
                        "                        threshold: 10,\n"
                        "                        mode:  'velocity',\n"
                        "                        maxVelocity: 4.0,\n"
                        "                        maxAccel: 4.0,\n"
                        "                   },      "
                        "                    KeyFrames: {\n"
                        "                     KeyPositions: [\n"
                        "                     {\n"
                        "                      target: ['button1'],\n"
                        "                      type: 'pathRelative'\n"
                        "                      frames: [25, 50, 75],\n"
                        "                      percentX: [0.25, 0.5, 0.75],\n"
                        "                      percentY: [0.0, 0.1, 0.0]\n"
                        "                     }\n"
                        "                     ]\n"
                        "                  },\n"
                        "                  }\n";
        auto json = CLParser::parse(jsonString);
        TransitionParser::parse(*json, transition);
        CHECK(transition.hasOnSwipe());
        float progress = transition.dragToProgress(0.5f,
                cwc1->getWidth(), cwc1->getHeight(), 10.0f, 80);
        std::vector<float> pos(100);
        std::vector<float> ratio(100);
        for (int i = 0; i < int(ratio.size()); i++) {
            pos[i] = i / (float) (int(ratio.size()) - 1);
            ratio[i] = transition.dragToProgress(pos[i],
                    cwc1->getWidth(), cwc1->getHeight(), 10.0f, 100);
        }
        transition.interpolate(cwc1->getWidth(), cwc1->getHeight(), 0.5f);

        int64_t nanoTime = 123123L;
        transition.setTouchUp(.5f, nanoTime, 0.5f, 0.5f);
        float deltaSec = 0.01f;
        int64_t deltaT = int64_t(deltaSec * 1E9);
        for (int i = 0; i < int(pos.size()); i++) {
            pos[i] = deltaSec * i;
            ratio[i] = transition.getTouchUpProgress(nanoTime + deltaT * i);
        }

        transition.interpolate(cwc1->getWidth(), cwc1->getHeight(), 0.5f);
        WidgetFrame* s1 = transition.getStart("button1");
        WidgetFrame* e1 = transition.getEnd("button1");
        WidgetFrame* f1 = transition.getInterpolated("button1");
        CHECK(f1 != nullptr);
        CHECK_EQ(100, s1->top);
        CHECK_EQ(100, s1->left);
        CHECK_EQ(900, e1->top);
        CHECK_EQ(500, e1->left);
        CHECK_EQ(100, s1->bottom - s1->top);
        CHECK_EQ(100, s1->right - s1->left);
        CHECK_EQ(100, e1->bottom - e1->top);
        CHECK_EQ(100, e1->right - e1->left);
        std::vector<float> xp(200);
        std::vector<float> yp(int(xp.size()));
        for (int i = 0; i < int(yp.size()); i++) {
            float v = yp[i];
            float p = i / (int(yp.size()) - 1.0f);
            transition.interpolate(cwc1->getWidth(), cwc1->getHeight(), p);
            WidgetFrame* dynamic = transition.getInterpolated("button1");
            xp[i] = (dynamic->left + dynamic->right) / 2;
            yp[i] = (dynamic->top + dynamic->bottom) / 2;
        }
        std::string str = cltest::TextDraw(80, 30, xp, yp, false);

        std::string expect =
                "|***                                                                            "
                        " | 150.0\n"
                        "|  ****                                                               "
                        "           |\n"
                        "|      ****                                                           "
                        "           |\n"
                        "|         ****                                                        "
                        "           |\n"
                        "|            ***                                                      "
                        "           |\n"
                        "|               ***                                                   "
                        "           | 287.931\n"
                        "|                 ***                                                 "
                        "           |\n"
                        "|                   **                                                "
                        "           |\n"
                        "|                    **                                               "
                        "           |\n"
                        "|                     *                                               "
                        "           |\n"
                        "|                    **                                               "
                        "           | 425.862\n"
                        "|                    *                                                "
                        "           |\n"
                        "|                    *                                                "
                        "           |\n"
                        "|                    *                                                "
                        "           |\n"
                        "|                    **                                               "
                        "           |\n"
                        "|                     ***                                             "
                        "           | 563.793\n"
                        "|                       *****                                         "
                        "           |\n"
                        "|                            *****                                    "
                        "           |\n"
                        "|                                 *********                           "
                        "           |\n"
                        "|                                         ********                    "
                        "           |\n"
                        "|                                                 *******             "
                        "           | 701.724\n"
                        "|                                                        *****        "
                        "           |\n"
                        "|                                                            ****     "
                        "           |\n"
                        "|                                                                ***  "
                        "           |\n"
                        "|                                                                   "
                        "***          |\n"
                        "|                                                                     "
                        "***        | 839.655\n"
                        "|                                                                     "
                        "  ***      |\n"
                        "|                                                                     "
                        "     ***   |\n"
                        "|                                                                     "
                        "       *** |\n"
                        "|                                                                     "
                        "          *| 950.0\n"
                        "150.0                                                                 "
                        "       550.0\n";
        CHECK_EQ(expect, str);
    }

TEST(MotionTransition_testTransitionOnSwipe1) {
        std::string onSwipeString =
                "                    onSwipe: { \n"
                        "                        anchor :'button1',\n"
                        "                        side: 'top',\n"
                        "                        direction: 'up',\n"
                        "                        scale: 1,\n"
                        "                        threshold: 1,\n"
                        "                        mode:  'spring',\n"
                        "                   },      ";

        Widgets W;
        Transition transition([](float dp) { return dp; });
        setUpOnSwipe(W, transition, onSwipeString);
        ConstraintWidget* w = transition.getStart("button1")->widget->mParent;

        std::string result;
        std::vector<float> pos(100);
        std::vector<float> ratio(100);
        for (int i = 0; i < int(ratio.size()); i++) {
            pos[i] = i / (float) (int(ratio.size()) - 1);
            ratio[i] = transition.dragToProgress(pos[i], w->getWidth(), w->getHeight(), 10.0f, 100);
        }
        transition.interpolate(w->getWidth(), w->getHeight(), 0.5f);
        result = cltest::TextDraw(80, 30, pos, ratio, true);

        std::string expect =
                "|                                                  *                            "
                        " | 0.173\n"
                        "|                                                ** *                 "
                        "           |\n"
                        "|                                               *    **               "
                        "           |\n"
                        "|                                               *      *              "
                        "           |\n"
                        "|                                              *                      "
                        "           |\n"
                        "|                                             *         *             "
                        "           | 0.16\n"
                        "|                                                       *             "
                        "           |\n"
                        "|                                            *                        "
                        "           |\n"
                        "|                                                        *            "
                        "           |\n"
                        "|                                           *             *           "
                        "           |\n"
                        "|                                           *                         "
                        "           | 0.147\n"
                        "|                                                          *          "
                        "           |\n"
                        "|                                          *                          "
                        "           |\n"
                        "|                                                           *         "
                        "           |\n"
                        "|                                         *                 *         "
                        "           |\n"
                        "|                                                            *        "
                        "           | 0.134\n"
                        "|                                        *                    *       "
                        "           |\n"
                        "|    ****                                                      **     "
                        "           |\n"
                        "| ***    ****                           *                       **    "
                        "           |\n"
                        "|*           **                                                   **  "
                        "         **|\n"
                        "|              **                       *                           "
                        "****   ****  | 0.121\n"
                        "|               **                                                    "
                        "  ***      |\n"
                        "|                 **                   *                              "
                        "           |\n"
                        "|                   *                 *                               "
                        "           |\n"
                        "|                   *                *                                "
                        "           |\n"
                        "|                    *              *                                 "
                        "           | 0.108\n"
                        "|                     *             *                                 "
                        "           |\n"
                        "|                      **          *                                  "
                        "           |\n"
                        "|                       **       **                                   "
                        "           |\n"
                        "|                         *******                                     "
                        "           | 0.098\n"
                        "0.0                                                                   "
                        "         1.0\n";
        CHECK_EQ(expect, result);
        int64_t start = 123123L;
        transition.setTouchUp(.5f, start, 0.5f, 0.5f);
        float deltaSec = 0.01f;
        int64_t deltaT = int64_t(deltaSec * 1E9);
        start += deltaT;
        for (int i = 0; i < int(pos.size()); i++) {
            pos[i] = deltaSec * i;
            ratio[i] = transition.getTouchUpProgress(start + deltaT * i);
        }
        result = cltest::TextDraw(80, 30, pos, ratio, true);

        expect =
                "|           *                                                                   "
                        " | 1.222\n"
                        "|          ****                                                       "
                        "           |\n"
                        "|              *                                                      "
                        "           |\n"
                        "|         *     *                                                     "
                        "           |\n"
                        "|        *      *                                                     "
                        "           |\n"
                        "|                *                                                    "
                        "           | 1.099\n"
                        "|       *         *                                                   "
                        "           |\n"
                        "|                                                                     "
                        "           |\n"
                        "|       *          *                *******                           "
                        "           |\n"
                        "|                   *            ***       ***              *         "
                        "           |\n"
                        "|                   *           *             ************** "
                        "********************| 0.976\n"
                        "|      *             *         **                                     "
                        "           |\n"
                        "|                     *      **                                       "
                        "           |\n"
                        "|     *                **  **                                         "
                        "           |\n"
                        "|                        **                                           "
                        "           |\n"
                        "|                                                                     "
                        "           | 0.854\n"
                        "|    *                                                                "
                        "           |\n"
                        "|                                                                     "
                        "           |\n"
                        "|                                                                     "
                        "           |\n"
                        "|   *                                                                 "
                        "           |\n"
                        "|                                                                     "
                        "           | 0.731\n"
                        "|                                                                     "
                        "           |\n"
                        "|   *                                                                 "
                        "           |\n"
                        "|                                                                     "
                        "           |\n"
                        "|  *                                                                  "
                        "           |\n"
                        "|                                                                     "
                        "           | 0.608\n"
                        "|                                                                     "
                        "           |\n"
                        "| *                                                                   "
                        "           |\n"
                        "|*                                                                    "
                        "           |\n"
                        "|*                                                                    "
                        "           | 0.51\n"
                        "0.0                                                                   "
                        "        0.99\n";
        CHECK_EQ(expect, result);
    }

TEST(MotionTransition_testTransitionOnSwipeRemoved) {
        Transition transition([](float dp) { return dp; });
        Widgets W;
        auto* cwc1 = makeLayout1(W);
        auto* cwc2 = makeLayout2(W);
        for (ConstraintWidget* child : cwc1->getChildren()) {
            WidgetFrame& wf = transition.getStart(child);
            wf.widget = child;
        }
        transition.updateFrom(*cwc1, Transition::START);
        for (ConstraintWidget* child : cwc2->getChildren()) {
            WidgetFrame& wf = transition.getEnd(child);
            wf.widget = child;
        }
        transition.updateFrom(*cwc2, Transition::END);
        std::string jsonString0 =
                "                  default: {\n"
                        "                    from: 'start',   to: 'end',\n"
                        "                    pathMotionArc: 'startHorizontal',\n"
                        "                    onSwipe: { \n"
                        "                        anchor :'button1',\n"
                        "                        side: 'top',\n"
                        "                        direction: 'up',\n"
                        "                        scale: 1,\n"
                        "                        threshold: 10,\n"
                        "                        mode:  'velocity',\n"
                        "                        maxVelocity: 4.0,\n"
                        "                        maxAccel: 4.0,\n"
                        "                   },      "
                        "                    KeyFrames: {\n"
                        "                     KeyPositions: [\n"
                        "                     {\n"
                        "                      target: ['button1'],\n"
                        "                      type: 'pathRelative'\n"
                        "                      frames: [25, 50, 75],\n"
                        "                      percentX: [0.25, 0.5, 0.75],\n"
                        "                      percentY: [0.0, 0.1, 0.0]\n"
                        "                     }\n"
                        "                     ]\n"
                        "                  },\n"
                        "                  }\n";
        try {
            auto json = CLParser::parse(jsonString0);
            TransitionParser::parse(*json, transition);
        } catch (const CLParsingException&) {
        }
        CHECK(transition.hasOnSwipe());
        std::string jsonString1 =
                "                  default: {\n"
                        "                    from: 'start',   to: 'end',\n"
                        "                    pathMotionArc: 'startHorizontal',\n"
                        "                    KeyFrames: {\n"
                        "                     KeyPositions: [\n"
                        "                     {\n"
                        "                      target: ['button1'],\n"
                        "                      type: 'pathRelative'\n"
                        "                      frames: [25, 50, 75],\n"
                        "                      percentX: [0.25, 0.5, 0.75],\n"
                        "                      percentY: [0.0, 0.1, 0.0]\n"
                        "                     }\n"
                        "                     ]\n"
                        "                  },\n"
                        "                  }\n";
        try {
            auto json = CLParser::parse(jsonString1);
            TransitionParser::parse(*json, transition);
        } catch (const CLParsingException&) {
        }
        CHECK(!(transition.hasOnSwipe()));
    }

