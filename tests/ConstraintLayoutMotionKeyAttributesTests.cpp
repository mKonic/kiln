// Ported from constraintlayout-core MotionKeyAttributesTest.java (androidx). Expected values
// are upstream's.
#include "Check.h"

#include "kiln/constraintlayout/core/motion/Motion.h"
#include "kiln/constraintlayout/core/motion/MotionWidget.h"
#include "kiln/constraintlayout/core/motion/key/MotionKeyAttributes.h"
#include "kiln/constraintlayout/core/motion/utils/CurveFit.h"
#include "kiln/constraintlayout/core/motion/utils/KeyCache.h"

#include <memory>

using namespace kiln::constraintlayout::core::motion;

namespace {

    struct Scene {
        MotionWidget mMW1;
        MotionWidget mMW2;
        MotionWidget mRes;
        KeyCache mCache;
        Motion mMotion{ &mMW1 };
        Scene() {
            mMW1.setBounds(0, 0, 30, 40);
            mMW2.setBounds(400, 400, 430, 440);
            mMotion.setPathMotionArc(ArcCurveFit::ARC_START_VERTICAL);
        }
        void setup() {
            mMotion.setStart(mMW1);
            mMotion.setEnd(mMW2);
            mMotion.setup(1000, 1000, 1, 1000000);
        }
    };

    void basicRange(Scene& s, int type, float start, float end) {
        s.mMW1.setValue(type, start);
        s.mMW2.setValue(type, end);
        s.setup();
        s.mMotion.interpolate(s.mRes, 0.5f, 1000000 + 1000, s.mCache);
    }

}

TEST(MotionKeyAttributes_attributes) {
        MotionWidget mw1;
        MotionWidget mw2;
        MotionWidget res;
        KeyCache cache;
        mw1.setBounds(0, 0, 30, 40);
        mw2.setBounds(400, 400, 430, 440);
        mw1.setRotationZ(0);
        mw2.setRotationZ(360);
        Motion motion(&mw1);
        motion.setPathMotionArc(ArcCurveFit::ARC_START_VERTICAL);
        motion.setStart(mw1);
        motion.setEnd(mw2);
        motion.setup(1000, 1000, 1, 1000000);
        
        motion.interpolate(res, 0.5f, 1000000 + 1000, cache);
        CHECK_NEAR(180, res.getRotationZ(), 0.001);
    }

TEST(MotionKeyAttributes_checkRotationZ) {
        Scene s; basicRange(s, TypedValues::AttributesType::TYPE_ROTATION_Z, 0, 360);
        CHECK_NEAR(180, s.mRes.getRotationZ(), 0.001);
    }

TEST(MotionKeyAttributes_checkRotationX) {
        Scene s; basicRange(s, TypedValues::AttributesType::TYPE_ROTATION_X, 0, 100);
        CHECK_NEAR(50, s.mRes.getRotationX(), 0.001);
    }

TEST(MotionKeyAttributes_checkRotationY) {
        Scene s; basicRange(s, TypedValues::AttributesType::TYPE_ROTATION_Y, 0, 50);
        CHECK_NEAR(25, s.mRes.getRotationY(), 0.001);
    }

TEST(MotionKeyAttributes_checkTranslateX) {
        Scene s; basicRange(s, TypedValues::AttributesType::TYPE_TRANSLATION_X, 0, 30);
        CHECK_NEAR(15, s.mRes.getTranslationX(), 0.001);
    }

TEST(MotionKeyAttributes_checkTranslateY) {
        Scene s; basicRange(s, TypedValues::AttributesType::TYPE_TRANSLATION_Y, 0, 40);
        CHECK_NEAR(20, s.mRes.getTranslationY(), 0.001);
    }

TEST(MotionKeyAttributes_checkTranslateZ) {
        Scene s; basicRange(s, TypedValues::AttributesType::TYPE_TRANSLATION_Z, 0, 18);
        CHECK_NEAR(9, s.mRes.getTranslationZ(), 0.001);
    }

TEST(MotionKeyAttributes_checkScaleX) {
        Scene s; basicRange(s, TypedValues::AttributesType::TYPE_SCALE_X, 1, 19);
        CHECK_NEAR(10, s.mRes.getScaleX(), 0.001);
    }

TEST(MotionKeyAttributes_checkScaleY) {
        Scene s; basicRange(s, TypedValues::AttributesType::TYPE_SCALE_Y, 1, 3);
        CHECK_NEAR(2, s.mRes.getScaleY(), 0.001);
    }

TEST(MotionKeyAttributes_attributesRotateX) {
        Scene s;
        s.mMW1.setRotationX(-10);
        s.mMW2.setRotationX(10);
        s.setup();
        
        s.mMotion.interpolate(s.mRes, 0.5f, 1000000 + 1000, s.mCache);
        CHECK_NEAR(0, s.mRes.getRotationX(), 0.001);
    }

TEST(MotionKeyAttributes_attributesRotateY) {
        Scene s;
        s.mMW1.setRotationY(-10);
        s.mMW2.setRotationY(10);
        s.setup();
        
        s.mMotion.interpolate(s.mRes, 0.5f, 1000000 + 1000, s.mCache);
        CHECK_NEAR(0, s.mRes.getRotationY(), 0.001);
    }

TEST(MotionKeyAttributes_attributesRotateZ) {
        Scene s;
        s.mMW1.setRotationZ(-10);
        s.mMW2.setRotationZ(10);
        s.setup();
        
        s.mMotion.interpolate(s.mRes, 0.5f, 1000000 + 1000, s.mCache);
        CHECK_NEAR(0, s.mRes.getRotationZ(), 0.001);
    }

TEST(MotionKeyAttributes_attributesTranslateX) {
        Scene s;
        s.mMW1.setTranslationX(-10);
        s.mMW2.setTranslationX(40);
        s.setup();
        
        s.mMotion.interpolate(s.mRes, 0.5f, 1000000 + 1000, s.mCache);
        CHECK_NEAR(15.0, s.mRes.getTranslationX(), 0.001);
    }

TEST(MotionKeyAttributes_attributesTranslateY) {
        Scene s;
        s.mMW1.setTranslationY(-10);
        s.mMW2.setTranslationY(40);
        s.setup();
        
        s.mMotion.interpolate(s.mRes, 0.5f, 1000000 + 1000, s.mCache);
        CHECK_NEAR(15.0, s.mRes.getTranslationY(), 0.001);
    }

TEST(MotionKeyAttributes_attributesTranslateZ) {
        Scene s;
        s.mMW1.setTranslationZ(-10);
        s.mMW2.setTranslationZ(40);
        s.setup();
        
        s.mMotion.interpolate(s.mRes, 0.5f, 1000000 + 1000, s.mCache);
        CHECK_NEAR(15.0, s.mRes.getTranslationZ(), 0.001);
    }

TEST(MotionKeyAttributes_attributesScaleX) {
        Scene s;
        s.mMW1.setScaleX(-10);
        s.mMW2.setScaleX(40);
        s.setup();
        
        s.mMotion.interpolate(s.mRes, 0.5f, 1000000 + 1000, s.mCache);
        CHECK_NEAR(15.0, s.mRes.getScaleX(), 0.001);
    }

TEST(MotionKeyAttributes_attributesScaleY) {
        Scene s;
        s.mMW1.setScaleY(-10);
        s.mMW2.setScaleY(40);
        s.setup();
        
        s.mMotion.interpolate(s.mRes, 0.5f, 1000000 + 1000, s.mCache);
        CHECK_NEAR(15.0, s.mRes.getScaleY(), 0.001);
    }

TEST(MotionKeyAttributes_attributesPivotX) {
        Scene s;
        s.mMW1.setPivotX(-10);
        s.mMW2.setPivotX(40);
        s.setup();
        
        s.mMotion.interpolate(s.mRes, 0.5f, 1000000 + 1000, s.mCache);
        CHECK_NEAR(15.0, s.mRes.getPivotX(), 0.001);
    }

TEST(MotionKeyAttributes_attributesPivotY) {
        Scene s;
        s.mMW1.setPivotY(-10);
        s.mMW2.setPivotY(40);
        s.setup();
        
        s.mMotion.interpolate(s.mRes, 0.5f, 1000000 + 1000, s.mCache);
        CHECK_NEAR(15.0, s.mRes.getPivotY(), 0.001);
    }

TEST(MotionKeyAttributes_keyFrameRotateX) {
        Scene s;
        s.mMW1.setRotationX(-10);
        s.mMW2.setRotationX(10);
        auto attribute = std::make_shared<MotionKeyAttributes>();
        attribute->setValue(TypedValues::AttributesType::TYPE_ROTATION_X, 23.0f);
        attribute->setFramePosition(50);
        s.mMotion.addKey(attribute);
        s.setup();
        
        s.mMotion.interpolate(s.mRes, 0.5f, 1000000 + 1000, s.mCache);
        CHECK_NEAR(23, s.mRes.getRotationX(), 0.001);
    }

TEST(MotionKeyAttributes_keyFrameRotateY) {
        Scene s;
        s.mMW1.setRotationY(-10);
        s.mMW2.setRotationY(10);
        auto attribute = std::make_shared<MotionKeyAttributes>();
        attribute->setValue(TypedValues::AttributesType::TYPE_ROTATION_Y, 23.0f);
        attribute->setFramePosition(50);
        s.mMotion.addKey(attribute);
        s.setup();
        
        s.mMotion.interpolate(s.mRes, 0.5f, 1000000 + 1000, s.mCache);
        CHECK_NEAR(23, s.mRes.getRotationY(), 0.001);
    }

TEST(MotionKeyAttributes_keyFrameRotateZ) {
        Scene s;
        s.mMW1.setRotationZ(-10);
        s.mMW2.setRotationZ(10);
        auto attribute = std::make_shared<MotionKeyAttributes>();
        attribute->setValue(TypedValues::AttributesType::TYPE_ROTATION_Z, 23.0f);
        attribute->setFramePosition(50);
        s.mMotion.addKey(attribute);
        s.setup();
        
        s.mMotion.interpolate(s.mRes, 0.5f, 1000000 + 1000, s.mCache);
        CHECK_NEAR(23, s.mRes.getRotationZ(), 0.001);
    }

TEST(MotionKeyAttributes_keyFrameTranslationX) {
        Scene s;
        s.mMW1.setTranslationX(-10);
        s.mMW2.setTranslationX(10);
        auto attribute = std::make_shared<MotionKeyAttributes>();
        attribute->setValue(TypedValues::AttributesType::TYPE_TRANSLATION_X, 23.0f);
        attribute->setFramePosition(50);
        s.mMotion.addKey(attribute);
        s.setup();
        
        s.mMotion.interpolate(s.mRes, 0.5f, 1000000 + 1000, s.mCache);
        CHECK_NEAR(23, s.mRes.getTranslationX(), 0.001);
    }

TEST(MotionKeyAttributes_keyFrameTranslationY) {
        Scene s;
        s.mMW1.setTranslationY(-10);
        s.mMW2.setTranslationY(10);
        auto attribute = std::make_shared<MotionKeyAttributes>();
        attribute->setValue(TypedValues::AttributesType::TYPE_TRANSLATION_Y, 23.0f);
        attribute->setFramePosition(50);
        s.mMotion.addKey(attribute);
        s.setup();
        
        s.mMotion.interpolate(s.mRes, 0.5f, 1000000 + 1000, s.mCache);
        CHECK_NEAR(23, s.mRes.getTranslationY(), 0.001);
    }

TEST(MotionKeyAttributes_keyFrameTranslationZ) {
        Scene s;
        s.mMW1.setTranslationZ(-10);
        s.mMW2.setTranslationZ(10);
        auto attribute = std::make_shared<MotionKeyAttributes>();
        attribute->setValue(TypedValues::AttributesType::TYPE_TRANSLATION_Z, 23.0f);
        attribute->setFramePosition(50);
        s.mMotion.addKey(attribute);
        s.setup();
        
        s.mMotion.interpolate(s.mRes, 0.5f, 1000000 + 1000, s.mCache);
        CHECK_NEAR(23, s.mRes.getTranslationZ(), 0.001);
    }

TEST(MotionKeyAttributes_keyFrameScaleX) {
        Scene s;
        s.mMW1.setScaleX(-10);
        s.mMW2.setScaleX(10);
        auto attribute = std::make_shared<MotionKeyAttributes>();
        attribute->setValue(TypedValues::AttributesType::TYPE_SCALE_X, 23.0f);
        attribute->setFramePosition(50);
        s.mMotion.addKey(attribute);
        s.setup();
        
        s.mMotion.interpolate(s.mRes, 0.5f, 1000000 + 1000, s.mCache);
        CHECK_NEAR(23, s.mRes.getScaleX(), 0.001);
    }

TEST(MotionKeyAttributes_keyFrameScaleY) {
        Scene s;
        s.mMW1.setScaleY(-10);
        s.mMW2.setScaleY(10);
        auto attribute = std::make_shared<MotionKeyAttributes>();
        attribute->setValue(TypedValues::AttributesType::TYPE_SCALE_Y, 23.0f);
        attribute->setFramePosition(50);
        s.mMotion.addKey(attribute);
        s.setup();
        
        s.mMotion.interpolate(s.mRes, 0.5f, 1000000 + 1000, s.mCache);
        CHECK_NEAR(23, s.mRes.getScaleY(), 0.001);
    }

TEST(MotionKeyAttributes_keyFrameNoAttrRotateX) {
        Scene s;
        auto attribute = std::make_shared<MotionKeyAttributes>();
        attribute->setValue(TypedValues::AttributesType::TYPE_ROTATION_X, 23.0f);
        attribute->setFramePosition(50);
        s.mMotion.addKey(attribute);
        s.setup();
        
        s.mMotion.interpolate(s.mRes, 0.5f, 1000000 + 1000, s.mCache);
        CHECK_NEAR(23, s.mRes.getRotationX(), 0.001);
    }

TEST(MotionKeyAttributes_keyFrameNoAttrRotateY) {
        Scene s;
        auto attribute = std::make_shared<MotionKeyAttributes>();
        attribute->setValue(TypedValues::AttributesType::TYPE_ROTATION_Y, 23.0f);
        attribute->setFramePosition(50);
        s.mMotion.addKey(attribute);
        s.setup();
        
        s.mMotion.interpolate(s.mRes, 0.5f, 1000000 + 1000, s.mCache);
        CHECK_NEAR(23, s.mRes.getRotationY(), 0.001);
    }

TEST(MotionKeyAttributes_keyFrameNoAttrRotateZ) {
        Scene s;
        auto attribute = std::make_shared<MotionKeyAttributes>();
        attribute->setValue(TypedValues::AttributesType::TYPE_ROTATION_Z, 23.0f);
        attribute->setFramePosition(50);
        s.mMotion.addKey(attribute);
        s.setup();
        
        s.mMotion.interpolate(s.mRes, 0.5f, 1000000 + 1000, s.mCache);
        CHECK_NEAR(23, s.mRes.getRotationZ(), 0.001);
    }

TEST(MotionKeyAttributes_keyFrameNoAttrTranslationX) {
        Scene s;
        auto attribute = std::make_shared<MotionKeyAttributes>();
        attribute->setValue(TypedValues::AttributesType::TYPE_TRANSLATION_X, 23.0f);
        attribute->setFramePosition(50);
        s.mMotion.addKey(attribute);
        s.setup();
        
        s.mMotion.interpolate(s.mRes, 0.5f, 1000000 + 1000, s.mCache);
        CHECK_NEAR(23, s.mRes.getTranslationX(), 0.001);
    }

TEST(MotionKeyAttributes_keyFrameNoAttrTranslationY) {
        Scene s;
        auto attribute = std::make_shared<MotionKeyAttributes>();
        attribute->setValue(TypedValues::AttributesType::TYPE_TRANSLATION_Y, 23.0f);
        attribute->setFramePosition(50);
        s.mMotion.addKey(attribute);
        s.setup();
        
        s.mMotion.interpolate(s.mRes, 0.5f, 1000000 + 1000, s.mCache);
        CHECK_NEAR(23, s.mRes.getTranslationY(), 0.001);
    }

TEST(MotionKeyAttributes_keyFrameNoAttrTranslationZ) {
        Scene s;
        auto attribute = std::make_shared<MotionKeyAttributes>();
        attribute->setValue(TypedValues::AttributesType::TYPE_TRANSLATION_Z, 23.0f);
        attribute->setFramePosition(50);
        s.mMotion.addKey(attribute);
        s.setup();
        
        s.mMotion.interpolate(s.mRes, 0.5f, 1000000 + 1000, s.mCache);
        CHECK_NEAR(23, s.mRes.getTranslationZ(), 0.001);
    }

TEST(MotionKeyAttributes_keyFrameNoAttrScaleX) {
        Scene s;
        auto attribute = std::make_shared<MotionKeyAttributes>();
        attribute->setValue(TypedValues::AttributesType::TYPE_SCALE_X, 23.0f);
        attribute->setFramePosition(50);
        s.mMotion.addKey(attribute);
        s.setup();
        
        s.mMotion.interpolate(s.mRes, 0.5f, 1000000 + 1000, s.mCache);
        CHECK_NEAR(23, s.mRes.getScaleX(), 0.001);
    }

TEST(MotionKeyAttributes_keyFrameNoAttrScaleY) {
        Scene s;
        auto attribute = std::make_shared<MotionKeyAttributes>();
        attribute->setValue(TypedValues::AttributesType::TYPE_SCALE_Y, 23.0f);
        attribute->setFramePosition(50);
        s.mMotion.addKey(attribute);
        s.setup();
        
        s.mMotion.interpolate(s.mRes, 0.5f, 1000000 + 1000, s.mCache);
        CHECK_NEAR(23, s.mRes.getScaleY(), 0.001);
    }

