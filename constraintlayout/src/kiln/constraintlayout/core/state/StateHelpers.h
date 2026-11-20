#pragma once

// constraintlayout-core's state helpers (androidx.constraintlayout.core.state.helpers): guideline,
// barrier, alignment, chain and flow references.

#include "kiln/constraintlayout/core/state/State.h"

#include <cmath>

namespace kiln::constraintlayout::core {
    class Barrier;
    class Flow;
    class Guideline;
}

namespace kiln::constraintlayout::core::state {


    class GuidelineReference : public Facade, public Reference {
    public:
        explicit GuidelineReference(State& state) : mState(state) {}
        void setKey(const std::string& key) override { mKey = key; }
        const std::string& getKey() const override { return mKey; }
        GuidelineReference& start(float margin);
        GuidelineReference& start(int margin);
        GuidelineReference& end(float margin);
        GuidelineReference& end(int margin);
        GuidelineReference& percent(float percent) {
            mStart = -1;
            mEnd = -1;
            mPercent = percent;
            return *this;
        }
        void setOrientation(int orientation) { mOrientation = orientation; }
        int getOrientation() const { return mOrientation; }
        void apply() override;
        Facade* getFacade() override { return nullptr; }
        ConstraintWidget* getConstraintWidget() override;
        void setConstraintWidget(ConstraintWidget* widget) override;
        GuidelineReference* asGuidelineReference() override { return this; }

    private:
        State& mState;
        int mOrientation = 0;
        Guideline* mGuidelineWidget = nullptr;
        int mStart = -1;
        int mEnd = -1;
        float mPercent = 0;
        std::string mKey;
    };

    class BarrierReference : public HelperReference {
    public:
        explicit BarrierReference(State& state) : HelperReference(state, State::BARRIER) {}
        void setBarrierDirection(State::Direction barrierDirection) { mDirection = barrierDirection; }
        ConstraintReference& margin(float marginValue) override {
            margin(mHelperState.convertDimension(marginValue));
            return *this;
        }
        ConstraintReference& margin(int value) override {
            mMargin = value;
            return *this;
        }
        HelperWidget* getHelperWidget() override;
        void apply() override;
        BarrierReference* asBarrierReference() override { return this; }

    private:
        State::Direction mDirection = State::Direction::LEFT;
        int mMargin = 0;
        Barrier* mBarrierWidget = nullptr;
    };

    class AlignHorizontallyReference : public HelperReference {
    public:
        // Upstream passes ALIGN_VERTICALLY here too.
        explicit AlignHorizontallyReference(State& state) : HelperReference(state, State::ALIGN_VERTICALLY) {}
        void apply() override;

    private:
        float mBias = 0.5f;
    };

    class AlignVerticallyReference : public HelperReference {
    public:
        explicit AlignVerticallyReference(State& state) : HelperReference(state, State::ALIGN_VERTICALLY) {}
        void apply() override;

    private:
        float mBias = 0.5f;
    };

    class ChainReference : public HelperReference {
    public:
        ChainReference(State& state, int type) : HelperReference(state, type) {}
        State::Chain getStyle() const { return State::Chain::SPREAD; }
        ChainReference& style(State::Chain style) {
            mStyle = style;
            return *this;
        }
        void addChainElement(const std::string& id, float weight, float preMargin, float postMargin) {
            addChainElement(id, weight, preMargin, postMargin, 0, 0);
        }
        void addChainElement(const std::string& id, float weight, float preMargin, float postMargin, float preGoneMargin, float postGoneMargin);
        float getBias() const { return mBias; }
        ConstraintReference& bias(float bias) override {
            mBias = bias;
            return *this;
        }

    protected:
        float getWeight(const std::string& id) const;
        float getPostMargin(const std::string& id) const;
        float getPreMargin(const std::string& id) const;
        float getPostGoneMargin(const std::string& id) const;
        float getPreGoneMargin(const std::string& id) const;

        float mBias = 0.5f;
        JavaHashMap<float> mMapWeights;
        JavaHashMap<float> mMapPreMargin;
        JavaHashMap<float> mMapPostMargin;
        JavaHashMap<float> mMapPreGoneMargin;
        JavaHashMap<float> mMapPostGoneMargin;
        State::Chain mStyle = State::Chain::SPREAD;
    };

    class HorizontalChainReference : public ChainReference {
    public:
        explicit HorizontalChainReference(State& state) : ChainReference(state, State::HORIZONTAL_CHAIN) {}
        void apply() override;
    };

    class VerticalChainReference : public ChainReference {
    public:
        explicit VerticalChainReference(State& state) : ChainReference(state, State::VERTICAL_CHAIN) {}
        void apply() override;
    };

    class FlowReference : public HelperReference {
    public:
        FlowReference(State& state, int type);
        void addFlowElement(const std::string& id, float weight, float preMargin, float postMargin);
        int getWrapMode() const { return mWrapMode; }
        void setWrapMode(int wrap) { mWrapMode = wrap; }
        int getPaddingLeft() const { return mPaddingLeft; }
        void setPaddingLeft(int padding) { mPaddingLeft = padding; }
        int getPaddingTop() const { return mPaddingTop; }
        void setPaddingTop(int padding) { mPaddingTop = padding; }
        int getPaddingRight() const { return mPaddingRight; }
        void setPaddingRight(int padding) { mPaddingRight = padding; }
        int getPaddingBottom() const { return mPaddingBottom; }
        void setPaddingBottom(int padding) { mPaddingBottom = padding; }
        int getVerticalStyle() const { return mVerticalStyle; }
        void setVerticalStyle(int verticalStyle) { mVerticalStyle = verticalStyle; }
        int getFirstVerticalStyle() const { return mFirstVerticalStyle; }
        void setFirstVerticalStyle(int firstVerticalStyle) { mFirstVerticalStyle = firstVerticalStyle; }
        int getLastVerticalStyle() const { return mLastVerticalStyle; }
        void setLastVerticalStyle(int lastVerticalStyle) { mLastVerticalStyle = lastVerticalStyle; }
        int getHorizontalStyle() const { return mHorizontalStyle; }
        void setHorizontalStyle(int horizontalStyle) { mHorizontalStyle = horizontalStyle; }
        int getFirstHorizontalStyle() const { return mFirstHorizontalStyle; }
        void setFirstHorizontalStyle(int firstHorizontalStyle) { mFirstHorizontalStyle = firstHorizontalStyle; }
        int getLastHorizontalStyle() const { return mLastHorizontalStyle; }
        void setLastHorizontalStyle(int lastHorizontalStyle) { mLastHorizontalStyle = lastHorizontalStyle; }
        int getVerticalAlign() const { return mVerticalAlign; }
        void setVerticalAlign(int verticalAlign) { mVerticalAlign = verticalAlign; }
        int getHorizontalAlign() const { return mHorizontalAlign; }
        void setHorizontalAlign(int horizontalAlign) { mHorizontalAlign = horizontalAlign; }
        int getVerticalGap() const { return mVerticalGap; }
        void setVerticalGap(int verticalGap) { mVerticalGap = verticalGap; }
        int getHorizontalGap() const { return mHorizontalGap; }
        void setHorizontalGap(int horizontalGap) { mHorizontalGap = horizontalGap; }
        int getMaxElementsWrap() const { return mMaxElementsWrap; }
        void setMaxElementsWrap(int maxElementsWrap) { mMaxElementsWrap = maxElementsWrap; }
        int getOrientation() const { return mOrientation; }
        void setOrientation(int orientation) { mOrientation = orientation; }
        float getVerticalBias() const { return mVerticalBias; }
        float getFirstVerticalBias() const { return mFirstVerticalBias; }
        void setFirstVerticalBias(float firstVerticalBias) { mFirstVerticalBias = firstVerticalBias; }
        float getLastVerticalBias() const { return mLastVerticalBias; }
        void setLastVerticalBias(float lastVerticalBias) { mLastVerticalBias = lastVerticalBias; }
        float getHorizontalBias() const { return mHorizontalBias; }
        float getFirstHorizontalBias() const { return mFirstHorizontalBias; }
        void setFirstHorizontalBias(float firstHorizontalBias) { mFirstHorizontalBias = firstHorizontalBias; }
        float getLastHorizontalBias() const { return mLastHorizontalBias; }
        void setLastHorizontalBias(float lastHorizontalBias) { mLastHorizontalBias = lastHorizontalBias; }
        HelperWidget* getHelperWidget() override;
        void setHelperWidget(HelperWidget* widget) override;
        void apply() override;
        FlowReference* asFlowReference() override { return this; }

    protected:
        float getWeight(const std::string& id) const;
        float getPostMargin(const std::string& id) const;
        float getPreMargin(const std::string& id) const;

        Flow* mFlow = nullptr;
        JavaHashMap<float> mMapWeights;
        JavaHashMap<float> mMapPreMargin;
        JavaHashMap<float> mMapPostMargin;
        int mWrapMode = 0;   // Flow::WRAP_NONE
        int mVerticalStyle = ConstraintWidget::UNKNOWN;
        int mFirstVerticalStyle = ConstraintWidget::UNKNOWN;
        int mLastVerticalStyle = ConstraintWidget::UNKNOWN;
        int mHorizontalStyle = ConstraintWidget::UNKNOWN;
        int mFirstHorizontalStyle = ConstraintWidget::UNKNOWN;
        int mLastHorizontalStyle = ConstraintWidget::UNKNOWN;
        // Upstream crosses these defaults (both constants are 2, CENTER).
        int mVerticalAlign = 2;     // Flow::HORIZONTAL_ALIGN_CENTER
        int mHorizontalAlign = 2;   // Flow::VERTICAL_ALIGN_CENTER
        int mVerticalGap = 0;
        int mHorizontalGap = 0;
        int mPaddingLeft = 0;
        int mPaddingRight = 0;
        int mPaddingTop = 0;
        int mPaddingBottom = 0;
        int mMaxElementsWrap = ConstraintWidget::UNKNOWN;
        int mOrientation = ConstraintWidget::HORIZONTAL;
        float mFirstVerticalBias = 0.5f;
        float mLastVerticalBias = 0.5f;
        float mFirstHorizontalBias = 0.5f;
        float mLastHorizontalBias = 0.5f;
    };

}
