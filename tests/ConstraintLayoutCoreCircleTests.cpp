// Ported from constraintlayout-core CircleTest.java (androidx), by tools in
// the scratchpad, then reviewed. Expected values are upstream's.
#include "ConstraintLayoutTestSupport.h"

namespace {
using namespace kiln::constraintlayout::core;
}

TEST(ClCircle_basic) {
    using namespace kiln::constraintlayout::core;
    Widgets W;
        auto* root = W.make<ConstraintWidgetContainer>(0, 0, 1000, 600);
        auto* a = W.make<ConstraintWidget>(100, 20);
        auto* w1 = W.make<ConstraintWidget>(10, 10);
        auto* w2 = W.make<ConstraintWidget>(10, 10);
        auto* w3 = W.make<ConstraintWidget>(10, 10);
        auto* w4 = W.make<ConstraintWidget>(10, 10);
        auto* w5 = W.make<ConstraintWidget>(10, 10);
        auto* w6 = W.make<ConstraintWidget>(10, 10);
        auto* w7 = W.make<ConstraintWidget>(10, 10);
        auto* w8 = W.make<ConstraintWidget>(10, 10);
        auto* w9 = W.make<ConstraintWidget>(10, 10);
        auto* w10 = W.make<ConstraintWidget>(10, 10);
        auto* w11 = W.make<ConstraintWidget>(10, 10);
        auto* w12 = W.make<ConstraintWidget>(10, 10);
        root->setDebugName("root");
        a->setDebugName("A");
        w1->setDebugName("w1");
        w2->setDebugName("w2");
        w3->setDebugName("w3");
        w4->setDebugName("w4");
        w5->setDebugName("w5");
        w6->setDebugName("w6");
        w7->setDebugName("w7");
        w8->setDebugName("w8");
        w9->setDebugName("w9");
        w10->setDebugName("w10");
        w11->setDebugName("w11");
        w12->setDebugName("w12");
        root->add(a);
        root->add(w1);
        root->add(w2);
        root->add(w3);
        root->add(w4);
        root->add(w5);
        root->add(w6);
        root->add(w7);
        root->add(w8);
        root->add(w9);
        root->add(w10);
        root->add(w11);
        root->add(w12);
        a->connect(ConstraintAnchor::Type::LEFT, root, ConstraintAnchor::Type::LEFT);
        a->connect(ConstraintAnchor::Type::RIGHT, root, ConstraintAnchor::Type::RIGHT);
        a->connect(ConstraintAnchor::Type::TOP, root, ConstraintAnchor::Type::TOP);
        a->connect(ConstraintAnchor::Type::BOTTOM, root, ConstraintAnchor::Type::BOTTOM);
        w1->connectCircularConstraint(a, 30, 50);
        w2->connectCircularConstraint(a, 60, 50);
        w3->connectCircularConstraint(a, 90, 50);
        w4->connectCircularConstraint(a, 120, 50);
        w5->connectCircularConstraint(a, 150, 50);
        w6->connectCircularConstraint(a, 180, 50);
        w7->connectCircularConstraint(a, 210, 50);
        w8->connectCircularConstraint(a, 240, 50);
        w9->connectCircularConstraint(a, 270, 50);
        w10->connectCircularConstraint(a, 300, 50);
        w11->connectCircularConstraint(a, 330, 50);
        w12->connectCircularConstraint(a, 360, 50);
        root->layout();
        CHECK_EQ(w1->getLeft(), 520);
        CHECK_EQ(w1->getTop(), 252);
        CHECK_EQ(w2->getLeft(), 538);
        CHECK_EQ(w2->getTop(), 270);
        CHECK_EQ(w3->getLeft(), 545);
        CHECK_EQ(w3->getTop(), 295);
        CHECK_EQ(w4->getLeft(), 538);
        CHECK_EQ(w4->getTop(), 320);
        CHECK_EQ(w5->getLeft(), 520);
        CHECK_EQ(w5->getTop(), 338);
        CHECK_EQ(w6->getLeft(), 495);
        CHECK_EQ(w6->getTop(), 345);
        CHECK_EQ(w7->getLeft(), 470);
        CHECK_EQ(w7->getTop(), 338);
        CHECK_EQ(w8->getLeft(), 452);
        CHECK_EQ(w8->getTop(), 320);
        CHECK_EQ(w9->getLeft(), 445);
        CHECK_EQ(w9->getTop(), 295);
        CHECK_EQ(w10->getLeft(), 452);
        CHECK_EQ(w10->getTop(), 270);
        CHECK_EQ(w11->getLeft(), 470);
        CHECK_EQ(w11->getTop(), 252);
        CHECK_EQ(w12->getLeft(), 495);
        CHECK_EQ(w12->getTop(), 245);
}

