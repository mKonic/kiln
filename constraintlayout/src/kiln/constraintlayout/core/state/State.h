#pragma once

// constraintlayout-core's state layer (androidx.constraintlayout.core.state): State keeps a
// reference per id, each describing one widget's constraints and dimensions, and builds a
// ConstraintWidgetContainer's children from them. ConstraintSetParser and the Compose DSL fill it.
//
// Ids are strings; State::PARENT, Java's Integer 0, is a key no id can spell. A constraint target
// (Java's Object: an id, or the reference itself once dereferenced) is a RefTarget.
// State owns every widget its references create; widgets it discards in reset() stay alive
// until the next apply() has taken them out of the container.

#include "kiln/constraintlayout/core/state/Dimension.h"
#include "kiln/constraintlayout/core/motion/utils/TypedBundle.h"
#include "kiln/constraintlayout/core/state/JavaHashMap.h"
#include "kiln/constraintlayout/core/widgets/ConstraintWidget.h"

#include <cmath>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace kiln::constraintlayout::core {
    class ConstraintWidgetContainer;
    class HelperWidget;
}

namespace kiln::constraintlayout::core::state {

    class State;
    class HelperReference;
    class GuidelineReference;
    class BarrierReference;
    class FlowReference;
    class GridReference;
    class HorizontalChainReference;
    class VerticalChainReference;
    class AlignHorizontallyReference;
    class AlignVerticallyReference;

    class Facade {
    public:
        virtual ~Facade() = default;
        virtual ConstraintWidget* getConstraintWidget() = 0;
        virtual void apply() = 0;
        // instanceof checks.
        virtual HelperReference* asHelperReference() { return nullptr; }
        virtual GuidelineReference* asGuidelineReference() { return nullptr; }
        virtual BarrierReference* asBarrierReference() { return nullptr; }
        virtual FlowReference* asFlowReference() { return nullptr; }
        virtual GridReference* asGridReference() { return nullptr; }
    };

    class Reference {
    public:
        virtual ~Reference() = default;
        virtual ConstraintWidget* getConstraintWidget() = 0;
        virtual void setConstraintWidget(ConstraintWidget* widget) = 0;
        virtual void setKey(const std::string& key) = 0;
        virtual const std::string& getKey() const = 0;
        virtual void apply() = 0;
        virtual Facade* getFacade() = 0;
        virtual class ConstraintReference* asConstraintReference() { return nullptr; }
    };

    // A constraint's target: none (Java's null), an id, or a reference.
    using RefTarget = std::variant<std::monostate, std::string, Reference*>;
    inline bool isSet(const RefTarget& t) { return !std::holds_alternative<std::monostate>(t); }

    class ConstraintReference : public Reference {
    public:
        explicit ConstraintReference(State& state) : mState(state) {}

        void setKey(const std::string& key) override { mKey = key; }
        const std::string& getKey() const override { return mKey; }
        void setTag(const std::string& tag) { mTag = tag; }
        const std::string& getTag() const { return mTag; }
        ConstraintReference* asConstraintReference() override { return this; }
        void addCustomColor(const std::string& name, int color) { mCustomColors.put(name, color); }
        void addCustomFloat(const std::string& name, float value) { mCustomFloats.put(name, value); }
        // MotionLayout's per-widget motion attributes (pathArc, relativeTo, easing, ...).
        std::shared_ptr<motion::TypedBundle> mMotionProperties;

        void setView(void* view);
        void* getView() const { return mView; }
        void setFacade(std::shared_ptr<Facade> facade);
        Facade* getFacade() override { return mFacade.get(); }
        const std::shared_ptr<Facade>& getFacadePtr() const { return mFacade; }
        void setConstraintWidget(ConstraintWidget* widget) override;
        ConstraintWidget* getConstraintWidget() override;
        virtual std::shared_ptr<ConstraintWidget> createConstraintWidget();

        // Throws std::logic_error listing the conflicting constraints.
        void validate() const;

        void setHorizontalChainStyle(int chainStyle) { mHorizontalChainStyle = chainStyle; }
        int getHorizontalChainStyle() const { return mHorizontalChainStyle; }
        void setVerticalChainStyle(int chainStyle) { mVerticalChainStyle = chainStyle; }
        int getVerticalChainStyle() const { return mVerticalChainStyle; }
        float getHorizontalChainWeight() const { return mHorizontalChainWeight; }
        void setHorizontalChainWeight(float weight) { mHorizontalChainWeight = weight; }
        float getVerticalChainWeight() const { return mVerticalChainWeight; }
        void setVerticalChainWeight(float weight) { mVerticalChainWeight = weight; }

        ConstraintReference& clearVertical();
        ConstraintReference& clearHorizontal();

        float getTranslationX() const { return mTranslationX; }
        float getTranslationY() const { return mTranslationY; }
        float getTranslationZ() const { return mTranslationZ; }
        float getScaleX() const { return mScaleX; }
        float getScaleY() const { return mScaleY; }
        float getAlpha() const { return mAlpha; }
        float getPivotX() const { return mPivotX; }
        float getPivotY() const { return mPivotY; }
        float getRotationX() const { return mRotationX; }
        float getRotationY() const { return mRotationY; }
        float getRotationZ() const { return mRotationZ; }
        ConstraintReference& pivotX(float x) { mPivotX = x; return *this; }
        ConstraintReference& pivotY(float y) { mPivotY = y; return *this; }
        ConstraintReference& rotationX(float x) { mRotationX = x; return *this; }
        ConstraintReference& rotationY(float y) { mRotationY = y; return *this; }
        ConstraintReference& rotationZ(float z) { mRotationZ = z; return *this; }
        ConstraintReference& translationX(float x) { mTranslationX = x; return *this; }
        ConstraintReference& translationY(float y) { mTranslationY = y; return *this; }
        ConstraintReference& translationZ(float z) { mTranslationZ = z; return *this; }
        ConstraintReference& scaleX(float x) { mScaleX = x; return *this; }
        ConstraintReference& scaleY(float y) { mScaleY = y; return *this; }
        ConstraintReference& alpha(float alpha) { mAlpha = alpha; return *this; }
        ConstraintReference& visibility(int visibility) { mVisibility = visibility; return *this; }

        ConstraintReference& left();
        ConstraintReference& right();
        ConstraintReference& start();
        ConstraintReference& end();
        ConstraintReference& top();
        ConstraintReference& bottom();
        ConstraintReference& baseline();

        ConstraintReference& leftToLeft(RefTarget reference);
        ConstraintReference& leftToRight(RefTarget reference);
        ConstraintReference& rightToLeft(RefTarget reference);
        ConstraintReference& rightToRight(RefTarget reference);
        ConstraintReference& startToStart(RefTarget reference);
        ConstraintReference& startToEnd(RefTarget reference);
        ConstraintReference& endToStart(RefTarget reference);
        ConstraintReference& endToEnd(RefTarget reference);
        ConstraintReference& topToTop(RefTarget reference);
        ConstraintReference& topToBottom(RefTarget reference);
        ConstraintReference& topToBaseline(RefTarget reference);
        ConstraintReference& bottomToTop(RefTarget reference);
        ConstraintReference& bottomToBottom(RefTarget reference);
        ConstraintReference& bottomToBaseline(RefTarget reference);
        ConstraintReference& baselineToBaseline(RefTarget reference);
        ConstraintReference& baselineToTop(RefTarget reference);
        ConstraintReference& baselineToBottom(RefTarget reference);
        ConstraintReference& centerHorizontally(RefTarget reference);
        ConstraintReference& centerVertically(RefTarget reference);
        ConstraintReference& circularConstraint(RefTarget reference, float angle, float distance);

        ConstraintReference& width(const Dimension& dimension) { return setWidth(dimension); }
        ConstraintReference& height(const Dimension& dimension) { return setHeight(dimension); }
        Dimension& getWidth() { return mHorizontalDimension; }
        ConstraintReference& setWidth(const Dimension& dimension) {
            mHorizontalDimension = dimension;
            return *this;
        }
        Dimension& getHeight() { return mVerticalDimension; }
        ConstraintReference& setHeight(const Dimension& dimension) {
            mVerticalDimension = dimension;
            return *this;
        }

        // Java's margin(Object): a float rounds, as State.convertDimension does.
        virtual ConstraintReference& margin(float marginValue);
        ConstraintReference& marginGone(float marginGoneValue);
        virtual ConstraintReference& margin(int value);
        ConstraintReference& marginGone(int value);
        ConstraintReference& horizontalBias(float value) {
            mHorizontalBias = value;
            return *this;
        }
        ConstraintReference& verticalBias(float value) {
            mVerticalBias = value;
            return *this;
        }
        virtual ConstraintReference& bias(float value);
        ConstraintReference& clearAll();
        ConstraintReference& clear();

        void applyWidgetConstraints();
        void apply() override;

    protected:
        enum class Constraint {
            LEFT_TO_LEFT, LEFT_TO_RIGHT, RIGHT_TO_LEFT, RIGHT_TO_RIGHT, START_TO_START, START_TO_END, END_TO_START, END_TO_END,
            TOP_TO_TOP, TOP_TO_BOTTOM, TOP_TO_BASELINE, BOTTOM_TO_TOP, BOTTOM_TO_BOTTOM, BOTTOM_TO_BASELINE, BASELINE_TO_BASELINE,
            BASELINE_TO_TOP, BASELINE_TO_BOTTOM, CENTER_HORIZONTALLY, CENTER_VERTICALLY, CIRCULAR_CONSTRAINT
        };

        State& mState;
        std::string mKey;
        std::string mTag;
        std::shared_ptr<Facade> mFacade;
        int mHorizontalChainStyle = ConstraintWidget::CHAIN_SPREAD;
        int mVerticalChainStyle = ConstraintWidget::CHAIN_SPREAD;
        float mHorizontalChainWeight = float(ConstraintWidget::UNKNOWN);
        float mVerticalChainWeight = float(ConstraintWidget::UNKNOWN);
        float mHorizontalBias = 0.5f;
        float mVerticalBias = 0.5f;
        int mMarginLeft = 0;
        int mMarginRight = 0;
        int mMarginStart = 0;
        int mMarginEnd = 0;
        int mMarginTop = 0;
        int mMarginBottom = 0;
        int mMarginLeftGone = 0;
        int mMarginRightGone = 0;
        int mMarginStartGone = 0;
        int mMarginEndGone = 0;
        int mMarginTopGone = 0;
        int mMarginBottomGone = 0;
        int mMarginBaseline = 0;
        int mMarginBaselineGone = 0;
        float mPivotX = WidgetFrame::kNaN;
        float mPivotY = WidgetFrame::kNaN;
        float mRotationX = WidgetFrame::kNaN;
        float mRotationY = WidgetFrame::kNaN;
        float mRotationZ = WidgetFrame::kNaN;
        float mTranslationX = WidgetFrame::kNaN;
        float mTranslationY = WidgetFrame::kNaN;
        float mTranslationZ = WidgetFrame::kNaN;
        float mAlpha = WidgetFrame::kNaN;
        float mScaleX = WidgetFrame::kNaN;
        float mScaleY = WidgetFrame::kNaN;
        int mVisibility = ConstraintWidget::VISIBLE;
        JavaHashMap<int> mCustomColors;
        JavaHashMap<float> mCustomFloats;
        RefTarget mLeftToLeft;
        RefTarget mLeftToRight;
        RefTarget mRightToLeft;
        RefTarget mRightToRight;
        RefTarget mStartToStart;
        RefTarget mStartToEnd;
        RefTarget mEndToStart;
        RefTarget mEndToEnd;
        RefTarget mTopToTop;
        RefTarget mTopToBottom;
        RefTarget mTopToBaseline;
        RefTarget mBottomToTop;
        RefTarget mBottomToBottom;
        RefTarget mBottomToBaseline;
        RefTarget mBaselineToBaseline;
        RefTarget mBaselineToTop;
        RefTarget mBaselineToBottom;
        RefTarget mCircularConstraint;
        float mCircularAngle = 0;
        float mCircularDistance = 0;
        std::optional<Constraint> mLast;
        Dimension mHorizontalDimension = Dimension::createFixed(Dimension::Value::WRAP_DIMENSION);
        Dimension mVerticalDimension = Dimension::createFixed(Dimension::Value::WRAP_DIMENSION);

    private:
        RefTarget get(const RefTarget& reference) const;
        void dereference();
        static ConstraintWidget* getTarget(const RefTarget& target);
        void applyConnection(ConstraintWidget* widget, const RefTarget& opaqueTarget, Constraint type);

        void* mView = nullptr;
        ConstraintWidget* mConstraintWidget = nullptr;
    };

    class HelperReference : public ConstraintReference, public Facade {
    public:
        HelperReference(State& state, int type);
        int getType() const { return mType; }
        HelperReference& add(const std::string& object) {
            mReferences.push_back(object);
            return *this;
        }
        HelperReference& add(std::initializer_list<std::string> objects) {
            for (const auto& o : objects) mReferences.push_back(o);
            return *this;
        }
        virtual void setHelperWidget(HelperWidget* helperWidget) { mHelperWidget = helperWidget; }
        virtual HelperWidget* getHelperWidget() { return mHelperWidget; }
        ConstraintWidget* getConstraintWidget() override;
        void apply() override {}
        void applyBase() { ConstraintReference::apply(); }
        HelperReference* asHelperReference() override { return this; }

        std::vector<std::string> mReferences;

    protected:
        State& mHelperState;
        const int mType;   // State::Helper

    private:
        HelperWidget* mHelperWidget = nullptr;
    };

    class State {
    public:
        static constexpr int UNKNOWN = -1;
        static constexpr int CONSTRAINT_SPREAD = 0;
        static constexpr int CONSTRAINT_WRAP = 1;
        static constexpr int CONSTRAINT_RATIO = 2;
        // Java's Integer 0: no id spells it. Its toString is "0".
        static const std::string PARENT;
        static std::string keyToString(const std::string& key) { return key == PARENT ? "0" : key; }

        enum class Direction { LEFT, RIGHT, START, END, TOP, BOTTOM };
        // Ints, as HelperReference keeps its type.
        enum Helper {
            HORIZONTAL_CHAIN, VERTICAL_CHAIN, ALIGN_HORIZONTALLY, ALIGN_VERTICALLY, BARRIER, LAYER, HORIZONTAL_FLOW, VERTICAL_FLOW,
            GRID, ROW, COLUMN, FLOW
        };
        enum class Chain { SPREAD, SPREAD_INSIDE, PACKED };
        static int chainValueByString(const std::string& str);
        static std::optional<Chain> chainByString(const std::string& str);
        enum class Wrap { NONE, CHAIN, ALIGNED };
        static int wrapValueByString(const std::string& str);

        State();
        virtual ~State();
        State(const State&) = delete;
        State& operator=(const State&) = delete;

        std::function<float(float)> getDpToPixel() const { return mDpToPixel; }
        void setDpToPixel(std::function<float(float)> dpToPixel) { mDpToPixel = std::move(dpToPixel); }
        void setRtl(bool isRtl) { mIsLtr = !isRtl; }
        bool isRtl() const { return !mIsLtr; }

        void reset();
        // Java's convertDimension(Object): a Float rounds (Math.round), an Integer stays.
        virtual int convertDimension(float value) const { return JavaInt(std::floor(value + 0.5f)); }
        virtual int convertDimension(int value) const { return value; }
        virtual std::shared_ptr<ConstraintReference> createConstraintReference(const std::string& key);
        bool sameFixedWidth(int width) { return mParent->getWidth().equalsFixedValue(width); }
        bool sameFixedHeight(int height) { return mParent->getHeight().equalsFixedValue(height); }
        State& width(const Dimension& dimension) { return setWidth(dimension); }
        State& height(const Dimension& dimension) { return setHeight(dimension); }
        State& setWidth(const Dimension& dimension) {
            mParent->setWidth(dimension);
            return *this;
        }
        State& setHeight(const Dimension& dimension) {
            mParent->setHeight(dimension);
            return *this;
        }
        Reference* reference(const std::string& key) const;
        ConstraintReference* constraints(const std::string& key);
        // An empty key creates one (Java's null).
        HelperReference* helper(std::string key, int type);
        GuidelineReference* horizontalGuideline(const std::string& key);
        GuidelineReference* verticalGuideline(const std::string& key);
        GuidelineReference* guideline(const std::string& key, int orientation);
        BarrierReference* barrier(const std::string& key, Direction direction);
        GridReference* getGrid(const std::string& key, const std::string& gridType);
        FlowReference* getFlow(const std::string& key, bool vertical);
        VerticalChainReference* verticalChain();
        VerticalChainReference* verticalChain(std::initializer_list<std::string> references);
        HorizontalChainReference* horizontalChain();
        HorizontalChainReference* horizontalChain(std::initializer_list<std::string> references);
        FlowReference* getVerticalFlow();
        FlowReference* getHorizontalFlow();
        AlignHorizontallyReference* centerHorizontally(std::initializer_list<std::string> references);
        AlignVerticallyReference* centerVertically(std::initializer_list<std::string> references);
        void directMapping();
        void map(const std::string& key, void* view);
        void setTag(const std::string& key, const std::string& tag);
        const std::vector<std::string>* getIdsForTag(const std::string& tag) const { return mTags.find(tag); }
        void apply(ConstraintWidgetContainer& container);
        void baselineNeededFor(const std::string& id) {
            mBaselineNeeded.push_back(id);
            mDirtyBaselineNeededWidgets = true;
        }
        bool isBaselineNeeded(ConstraintWidget* constraintWidget);
        // The helper reference whose widget this is (compose State.getKeyId).
        std::optional<std::string> getKeyId(const HelperWidget* helperWidget) const;

        // Takes ownership of a widget a reference created.
        template<typename T>
        T* adopt(std::shared_ptr<T> widget) {
            T* raw = widget.get();
            mWidgets.push_back(std::move(widget));
            return raw;
        }

        const std::shared_ptr<ConstraintReference> mParent;

    protected:
        JavaHashMap<std::shared_ptr<Reference>> mReferences;
        JavaHashMap<std::shared_ptr<HelperReference>> mHelperReferences;
        JavaHashMap<std::vector<std::string>> mTags;

    private:
        std::string createHelperKey() { return "__HELPER_KEY_" + std::to_string(mNumHelpers++) + "__"; }

        std::function<float(float)> mDpToPixel;
        bool mIsLtr = true;
        int mNumHelpers = 0;
        std::vector<std::string> mBaselineNeeded;
        std::vector<ConstraintWidget*> mBaselineNeededWidgets;
        bool mDirtyBaselineNeededWidgets = true;
        std::vector<std::shared_ptr<ConstraintWidget>> mWidgets;
        std::vector<std::shared_ptr<ConstraintWidget>> mRetiredWidgets;
        // References dropped by reset(), kept until apply() so nothing dangles meanwhile.
        std::vector<std::shared_ptr<Reference>> mRetiredReferences;
    };

}
