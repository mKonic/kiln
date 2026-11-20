// constraintlayout-compose's measurement and composables: Measurer2 (Measurer.kt), the
// ConstraintSet kinds (ConstraintSet.kt, DslConstraintSet.kt, JSONConstraintSet.kt), the
// inline DSL's constraint set (ConstraintSetForInlineDsl), placeWithFrameTransform and the
// ConstraintLayout composables (ConstraintLayout.kt), Modifier.layoutId with a tag
// (ConstraintLayoutTag.kt).

#include "kiln/constraintlayout/ConstraintLayout.h"

#include "kiln/constraintlayout/core/state/ConstraintSetParser.h"
#include "kiln/constraintlayout/core/state/State.h"
#include "kiln/constraintlayout/core/widgets/ConstraintWidgetContainer.h"
#include "kiln/constraintlayout/core/widgets/VirtualLayout.h"
#include "kiln/foundation/Layouts.h"
#include "kiln/runtime/Composition.h"

#include <algorithm>
#include <any>
#include <array>
#include <cmath>
#include <cstdio>
#include <unordered_map>
#include <unordered_set>

namespace kiln::constraintlayout {

    using core::BasicMeasure;
    using core::ConstraintWidget;
    using core::ConstraintWidgetContainer;
    using core::WidgetFrame;
    using DB = ConstraintWidget::DimensionBehaviour;

    namespace {

        // What a child tells its ConstraintLayout (ConstraintLayoutParentData and
        // ConstraintLayoutTagParentData); kept in ParentData::extra.
        struct ConstrainAsData {
            ConstrainedLayoutReference ref;
            std::shared_ptr<const kiln::Function<void(ConstrainScope&)>> constrain;
        };
        struct ChildData {
            std::shared_ptr<const ConstrainAsData> constrainAs;
            std::optional<std::string> tag;
        };
        const ChildData* ChildDataOf(const ui::Measurable& m) { return std::any_cast<ChildData>(&m.GetParentData().extra); }
        ChildData& ChildDataIn(ui::ParentData& d) {
            if (ChildData* existing = std::any_cast<ChildData>(&d.extra)) return *existing;
            d.extra = ChildData{};
            return *std::any_cast<ChildData>(&d.extra);
        }

        // The id a child goes by (Measurable.anyOrNullId).
        std::string AnyOrNullId(const ui::Measurable& m) {
            if (const std::string* id = foundation::LayoutIdOf(m)) return *id;
            return "null";
        }

        // compose's State: the solver state with the incoming constraints.
        class ComposeState final : public core::state::State {
        public:
            ui::Constraints rootIncomingConstraints;
        };

        int CoerceIn(int v, int lo, int hi) {
            // Kotlin's coerceIn with null bounds (given as 0 or less here).
            if (lo > 0 && hi > 0 && lo > hi) return v;   // upstream would throw; keep the size
            if (lo > 0 && v < lo) v = lo;
            if (hi > 0 && v > hi) v = hi;
            return v;
        }

    }

    // Measurer2.
    class Measurer final : public BasicMeasure::Measurer {
    public:
        Measurer() { m_Root.setMeasurer(this); }

        void measure(ConstraintWidget* constraintWidget, BasicMeasure::Measure& measure) override {
            const std::string& widgetId = constraintWidget->stringId;
            const auto last = m_LastMeasures.find(widgetId);
            int widthConstraints[2], heightConstraints[2];
            ObtainConstraints(measure.horizontalBehavior, measure.horizontalDimension, constraintWidget->mMatchConstraintDefaultWidth,
                              measure.measureStrategy, (last != m_LastMeasures.end() ? last->second[1] : 0) == constraintWidget->getHeight(),
                              constraintWidget->isResolvedHorizontally(), m_State.rootIncomingConstraints.maxWidth, widthConstraints);
            ObtainConstraints(measure.verticalBehavior, measure.verticalDimension, constraintWidget->mMatchConstraintDefaultHeight,
                              measure.measureStrategy, (last != m_LastMeasures.end() ? last->second[0] : 0) == constraintWidget->getWidth(),
                              constraintWidget->isResolvedVertically(), m_State.rootIncomingConstraints.maxHeight, heightConstraints);
            ui::Constraints constraints{ widthConstraints[0], widthConstraints[1], heightConstraints[0], heightConstraints[1] };

            if ((measure.measureStrategy == BasicMeasure::Measure::TRY_GIVEN_DIMENSIONS ||
                 measure.measureStrategy == BasicMeasure::Measure::USE_GIVEN_DIMENSIONS) ||
                !(measure.horizontalBehavior == DB::MATCH_CONSTRAINT &&
                  constraintWidget->mMatchConstraintDefaultWidth == ConstraintWidget::MATCH_CONSTRAINT_SPREAD &&
                  measure.verticalBehavior == DB::MATCH_CONSTRAINT &&
                  constraintWidget->mMatchConstraintDefaultHeight == ConstraintWidget::MATCH_CONSTRAINT_SPREAD)) {
                const ui::IntSize result = MeasureWidget(constraintWidget, constraints);
                constraintWidget->setMeasureRequested(false);
                const int coercedWidth = CoerceIn(result.width, constraintWidget->mMatchConstraintMinWidth, constraintWidget->mMatchConstraintMaxWidth);
                const int coercedHeight =
                    CoerceIn(result.height, constraintWidget->mMatchConstraintMinHeight, constraintWidget->mMatchConstraintMaxHeight);
                bool remeasure = false;
                if (coercedWidth != result.width) {
                    constraints = { coercedWidth, coercedWidth, constraints.minHeight, constraints.maxHeight };
                    remeasure = true;
                }
                if (coercedHeight != result.height) {
                    constraints = { constraints.minWidth, constraints.maxWidth, coercedHeight, coercedHeight };
                    remeasure = true;
                }
                if (remeasure) {
                    MeasureWidget(constraintWidget, constraints);
                    constraintWidget->setMeasureRequested(false);
                }
            }

            auto* measurable = static_cast<ui::Measurable*>(constraintWidget->getCompanionWidget());
            const auto placeable = measurable ? m_Placeables.find(measurable) : m_Placeables.end();
            ui::Placeable* currentPlaceable = placeable != m_Placeables.end() ? placeable->second : nullptr;
            measure.measuredWidth = currentPlaceable ? currentPlaceable->Width() : constraintWidget->getWidth();
            measure.measuredHeight = currentPlaceable ? currentPlaceable->Height() : constraintWidget->getHeight();
            const int baseline = currentPlaceable != nullptr && m_State.isBaselineNeeded(constraintWidget) ? currentPlaceable->Get(ui::FirstBaseline)
                                                                                                          : ui::AlignmentLine::Unspecified;
            measure.measuredHasBaseline = baseline != ui::AlignmentLine::Unspecified;
            measure.measuredBaseline = baseline;
            auto& saved = m_LastMeasures.try_emplace(widgetId, std::array<int, 3>{ 0, 0, ui::AlignmentLine::Unspecified }).first->second;
            saved = { measure.measuredWidth, measure.measuredHeight, measure.measuredBaseline };
            measure.measuredNeedsSolverPass = measure.measuredWidth != measure.horizontalDimension || measure.measuredHeight != measure.verticalDimension;
        }

        void didMeasures() override {}

        ui::IntSize PerformMeasure(const ui::MeasureScope& scope, const ui::Constraints& constraints, ConstraintSet& constraintSet,
                                   std::span<ui::Measurable* const> measurables, int optimizationLevel) {
            if (measurables.empty()) return { constraints.minWidth, constraints.minHeight };
            const float density = scope.density;
            m_State.setDpToPixel([density](float dp) { return density * dp; });

            m_State.width(constraints.HasFixedWidth() ? core::state::Dimension::createFixed(constraints.maxWidth)
                                                      : core::state::Dimension::createWrap().min(constraints.minWidth));
            m_State.height(constraints.HasFixedHeight() ? core::state::Dimension::createFixed(constraints.maxHeight)
                                                        : core::state::Dimension::createWrap().min(constraints.minHeight));
            m_State.mParent->getWidth().apply(m_State, &m_Root, ConstraintWidget::HORIZONTAL);
            m_State.mParent->getHeight().apply(m_State, &m_Root, ConstraintWidget::VERTICAL);
            m_State.rootIncomingConstraints = constraints;
            m_State.setRtl(scope.Direction() == ui::LayoutDirection::Rtl);

            ResetMeasureState();
            if (constraintSet.IsDirty(measurables)) {
                m_State.reset();
                constraintSet.ApplyTo(m_State, measurables);
                BuildMapping(measurables);
                m_State.apply(m_Root);
            } else {
                BuildMapping(measurables);
            }

            m_Root.setWidth(constraints.maxWidth);
            m_Root.setHeight(constraints.maxHeight);
            m_Root.updateHierarchy();
            m_Root.setOptimizationLevel(optimizationLevel);
            m_Root.measure(m_Root.getOptimizationLevel(), 0, 0, 0, 0, 0, 0, 0, 0);
            return { m_Root.getWidth(), m_Root.getHeight() };
        }

        void PerformLayout(ui::PlacementScope& place, std::span<ui::Measurable* const> measurables) {
            if (m_FrameCache.empty()) {
                for (ConstraintWidget* child : m_Root.getChildren()) {
                    auto* measurable = static_cast<ui::Measurable*>(child->getCompanionWidget());
                    if (measurable == nullptr || !m_Known.count(measurable)) continue;
                    WidgetFrame frame(child->frame);
                    frame.update();
                    m_FrameCache[AnyOrNullId(*measurable)] = frame;
                }
            }
            for (ui::Measurable* measurable : measurables) {
                const auto frame = m_FrameCache.find(AnyOrNullId(*measurable));
                if (frame == m_FrameCache.end()) continue;
                const auto placeable = m_Placeables.find(measurable);
                if (placeable == m_Placeables.end()) continue;
                PlaceWithFrameTransform(place, *placeable->second, frame->second);
            }
        }

    private:
        static bool ObtainConstraints(DB dimensionBehaviour, int dimension, int matchConstraintDefaultDimension, int measureStrategy,
                                      bool otherDimensionResolved, bool currentDimensionResolved, int rootMaxConstraint, int* outConstraints) {
            switch (dimensionBehaviour) {
                case DB::FIXED:
                    outConstraints[0] = dimension;
                    outConstraints[1] = dimension;
                    return false;
                case DB::WRAP_CONTENT:
                    outConstraints[0] = 0;
                    outConstraints[1] = rootMaxConstraint;
                    return true;
                case DB::MATCH_CONSTRAINT: {
                    const bool useDimension =
                        currentDimensionResolved ||
                        ((measureStrategy == BasicMeasure::Measure::TRY_GIVEN_DIMENSIONS || measureStrategy == BasicMeasure::Measure::USE_GIVEN_DIMENSIONS) &&
                         (measureStrategy == BasicMeasure::Measure::USE_GIVEN_DIMENSIONS ||
                          matchConstraintDefaultDimension != ConstraintWidget::MATCH_CONSTRAINT_WRAP || otherDimensionResolved));
                    outConstraints[0] = useDimension ? dimension : 0;
                    outConstraints[1] = useDimension ? dimension : rootMaxConstraint;
                    return !useDimension;
                }
                case DB::MATCH_PARENT:
                    outConstraints[0] = rootMaxConstraint;
                    outConstraints[1] = rootMaxConstraint;
                    return false;
            }
            return false;
        }

        ui::IntSize MeasureWidget(ConstraintWidget* constraintWidget, const ui::Constraints& constraints) {
            if (constraintWidget->is(ConstraintWidget::KIND_VIRTUAL_LAYOUT)) {
                const int widthMode = constraints.HasFixedWidth() ? BasicMeasure::EXACTLY
                                      : constraints.HasBoundedWidth() ? BasicMeasure::AT_MOST
                                                                       : BasicMeasure::UNSPECIFIED;
                const int heightMode = constraints.HasFixedHeight() ? BasicMeasure::EXACTLY
                                       : constraints.HasBoundedHeight() ? BasicMeasure::AT_MOST
                                                                         : BasicMeasure::UNSPECIFIED;
                auto* layout = static_cast<core::VirtualLayout*>(constraintWidget);
                layout->measure(widthMode, constraints.maxWidth, heightMode, constraints.maxHeight);
                return { layout->getMeasuredWidth(), layout->getMeasuredHeight() };
            }
            auto* measurable = static_cast<ui::Measurable*>(constraintWidget->getCompanionWidget());
            if (measurable != nullptr && m_Known.count(measurable)) {
                ui::Placeable& result = measurable->Measure(constraints);
                m_Placeables[measurable] = &result;
                return { result.Width(), result.Height() };
            }
            return { 0, 0 };   // upstream logs "Nothing to measure"
        }

        // buildMapping: each child's id and the child itself, and its tag.
        void BuildMapping(std::span<ui::Measurable* const> measurables) {
            m_Known.clear();
            for (ui::Measurable* measurable : measurables) {
                m_Known.insert(measurable);
                const std::string* layoutId = foundation::LayoutIdOf(*measurable);
                // createId(): a fresh object, so a fresh id, for a child without one.
                const std::string id = layoutId ? *layoutId : "\x02" + std::to_string(++m_AnonymousIds);
                m_State.map(id, measurable);
                const ChildData* data = ChildDataOf(*measurable);
                if (data && data->tag && layoutId) m_State.setTag(id, *data->tag);
            }
        }

        void ResetMeasureState() {
            m_Placeables.clear();
            m_LastMeasures.clear();
            m_FrameCache.clear();
        }

        // placeWithFrameTransform: gone children are not placed; a transform makes a layer.
        static void PlaceWithFrameTransform(ui::PlacementScope& place, ui::Placeable& placeable, const WidgetFrame& frame) {
            if (frame.visibility == ConstraintWidget::GONE) return;
            if (frame.isDefaultTransform()) {
                place.Place(placeable, frame.left, frame.top);
                return;
            }
            foundation::GraphicsLayerParams layer;
            if (!std::isnan(frame.pivotX) || !std::isnan(frame.pivotY)) {
                layer.originX = std::isnan(frame.pivotX) ? 0.5f : frame.pivotX;
                layer.originY = std::isnan(frame.pivotY) ? 0.5f : frame.pivotY;
            }
            if (!std::isnan(frame.rotationX)) layer.rotationX = frame.rotationX;
            if (!std::isnan(frame.rotationY)) layer.rotationY = frame.rotationY;
            if (!std::isnan(frame.rotationZ)) layer.rotationZ = frame.rotationZ;
            if (!std::isnan(frame.translationX)) layer.translationX = frame.translationX;
            if (!std::isnan(frame.translationY)) layer.translationY = frame.translationY;
            if (!std::isnan(frame.translationZ)) layer.shadowElevation = frame.translationZ;
            if (!std::isnan(frame.scaleX) || !std::isnan(frame.scaleY)) {
                layer.scaleX = std::isnan(frame.scaleX) ? 1.0f : frame.scaleX;
                layer.scaleY = std::isnan(frame.scaleY) ? 1.0f : frame.scaleY;
            }
            if (!std::isnan(frame.alpha)) layer.alpha = frame.alpha;
            const float zIndex = std::isnan(frame.translationZ) ? 0.0f : frame.translationZ;
            place.PlaceWithLayer(placeable, frame.left, frame.top, foundation::PlacementLayer(layer), zIndex);
        }

        ConstraintWidgetContainer m_Root{ 0, 0 };
        ComposeState m_State;
        std::unordered_map<ui::Measurable*, ui::Placeable*> m_Placeables;
        std::unordered_set<ui::Measurable*> m_Known;   // this pass's children (companions may be stale)
        std::unordered_map<std::string, std::array<int, 3>> m_LastMeasures;
        std::unordered_map<std::string, WidgetFrame> m_FrameCache;
        int m_AnonymousIds = 0;
    };

    // ConstraintSet kinds.

    namespace {

        // DerivedConstraintSet: applies what it extends first.
        class DerivedConstraintSet : public ConstraintSet {
        public:
            explicit DerivedConstraintSet(std::shared_ptr<ConstraintSet> extendFrom) : m_ExtendFrom(std::move(extendFrom)) {}
            void ApplyTo(core::state::State& state, std::span<ui::Measurable* const> measurables) override {
                if (m_ExtendFrom) m_ExtendFrom->ApplyTo(state, measurables);
                ApplyToState(state);
            }
            virtual void ApplyToState(core::state::State& state) = 0;

        protected:
            std::shared_ptr<ConstraintSet> m_ExtendFrom;
        };

        class DslConstraintSet final : public DerivedConstraintSet {
        public:
            DslConstraintSet(const kiln::Function<void(ConstraintSetScope&)>& description, std::shared_ptr<ConstraintSet> extendFrom)
                : DerivedConstraintSet(nullptr), m_Scope(ExtendedContainer(extendFrom)) {
                // Upstream extends through the scope's JSON (and only from another DSL set).
                description(m_Scope);
            }
            void ApplyToState(core::state::State& state) override { m_Scope.ApplyTo(state); }
            bool Equals(const ConstraintSet& other) const override {
                const auto* o = dynamic_cast<const DslConstraintSet*>(&other);
                return o != nullptr && m_Scope == o->m_Scope;
            }
            const ConstraintSetScope& Scope() const { return m_Scope; }

        private:
            static std::shared_ptr<core::CLObject> ExtendedContainer(const std::shared_ptr<ConstraintSet>& extendFrom) {
                const auto* dsl = dynamic_cast<const DslConstraintSet*>(extendFrom.get());
                return dsl ? dsl->m_Scope.ContainerObject() : nullptr;
            }
            ConstraintSetScope m_Scope;
        };

        class JsonConstraintSet final : public DerivedConstraintSet {
        public:
            JsonConstraintSet(std::string content, std::shared_ptr<ConstraintSet> extendFrom, std::string overrideVariables)
                : DerivedConstraintSet(std::move(extendFrom)), m_Content(std::move(content)), m_OverrideVariables(std::move(overrideVariables)) {}
            bool IsDirty(std::span<ui::Measurable* const>) override { return m_Dirty; }
            void ApplyToState(core::state::State& state) override {
                core::state::ConstraintSetParser::LayoutVariables layoutVariables;
                ApplyLayoutVariables(layoutVariables);
                try {
                    core::state::ConstraintSetParser::parseJSON(m_Content, state, layoutVariables);
                    m_Dirty = false;
                } catch (const std::exception&) {
                    m_Dirty = true;
                }
            }
            void Override(const std::string& name, float value) override {
                m_Overridden.put(name, value);
                m_Dirty = true;
            }
            bool Equals(const ConstraintSet& other) const override {
                const auto* o = dynamic_cast<const JsonConstraintSet*>(&other);
                return o != nullptr && m_Content == o->m_Content;
            }

        private:
            void ApplyLayoutVariables(core::state::ConstraintSetParser::LayoutVariables& layoutVariables) {
                if (!m_OverrideVariables.empty()) {
                    try {
                        auto variables = core::CLParser::parse(m_OverrideVariables);
                        for (int i = 0; i < variables->size(); i++) {
                            auto* key = static_cast<core::CLKey*>(variables->get(i));
                            layoutVariables.putOverride(key->content(), key->getValue()->getFloat());
                        }
                    } catch (const core::CLParsingException& e) {
                        std::fprintf(stderr, "exception: %s\n", e.reason().c_str());
                    }
                }
                m_Overridden.forEach([&](const std::string& name, float value) { layoutVariables.putOverride(name, value); });
            }
            std::string m_Content;
            std::string m_OverrideVariables;
            core::JavaHashMap<float> m_Overridden;
            bool m_Dirty = true;
        };

        // ConstraintSetForInlineDsl: runs the children's constrainAs blocks into the scope at
        // measure time; a state read in a block marks it dirty and remeasures.
        class InlineConstraintSet final : public ConstraintSet {
        public:
            explicit InlineConstraintSet(ConstraintLayoutScope& scope) : m_Scope(scope) { m_Watch.owner = this; }
            ~InlineConstraintSet() override { m_Watch.Unsubscribe(); }

            void ApplyTo(core::state::State& state, std::span<ui::Measurable* const> measurables) override {
                m_PreviousDatas.clear();
                rt::Observation::Track(m_Watch, [&] {
                    for (ui::Measurable* measurable : measurables) {
                        const ChildData* data = ChildDataOf(*measurable);
                        std::shared_ptr<const ConstrainAsData> parentData = data ? data->constrainAs : nullptr;
                        if (parentData) {
                            ConstrainScope constrainScope(parentData->ref.id, m_Scope.AsCLContainer(parentData->ref));
                            (*parentData->constrain)(constrainScope);
                        }
                        m_PreviousDatas.push_back(parentData);
                    }
                    m_Scope.ApplyTo(state);
                });
                knownDirty = false;
            }

            bool IsDirty(std::span<ui::Measurable* const> measurables) override {
                if (knownDirty || measurables.size() != m_PreviousDatas.size()) return true;
                for (std::size_t i = 0; i < measurables.size(); i++) {
                    const ChildData* data = ChildDataOf(*measurables[i]);
                    const ConstrainAsData* now = data && data->constrainAs ? data->constrainAs.get() : nullptr;
                    const ConstrainAsData* before = m_PreviousDatas[i].get();
                    if (now == before) continue;
                    if (!now || !before || now->ref.id != before->ref.id || now->constrain != before->constrain) return true;
                }
                return false;
            }

            bool knownDirty = true;
            ui::LayoutNode* node = nullptr;

        private:
            struct Watch final : rt::Subscriber {
                InlineConstraintSet* owner = nullptr;
                void OnStateChanged() override {
                    owner->knownDirty = true;
                    if (owner->node) owner->node->RequestRemeasure();
                }
            };
            ConstraintLayoutScope& m_Scope;
            std::vector<std::shared_ptr<const ConstrainAsData>> m_PreviousDatas;
            Watch m_Watch;
        };

        // The measure policy: a measurer, the set it measures with, and the optimization level.
        class ConstraintLayoutPolicy final : public ui::MeasurePolicy {
        public:
            ui::MeasureResult Measure(ui::MeasureScope& scope, std::span<ui::Measurable* const> children, const ui::Constraints& c) override {
                if (contentTracker) (void)contentTracker->Get();
                const ui::IntSize size = measurer.PerformMeasure(scope, c, *constraintSet, children, optimizationLevel);
                std::vector<ui::Measurable*> measured(children.begin(), children.end());
                return scope.Layout(size.width, size.height, [this, measured = std::move(measured)](ui::PlacementScope& place) {
                    measurer.PerformLayout(place, measured);
                });
            }

            Measurer measurer;
            std::shared_ptr<ConstraintSet> constraintSet;
            int optimizationLevel = core::Optimizer::OPTIMIZATION_STANDARD;
            // Written by the content each time it composes, read here: content changes remeasure.
            std::shared_ptr<rt::State<int>> contentTracker;
        };

        // ConstrainAsModifier and ConstraintLayoutTag.
        struct ConstrainAsParams {
            std::shared_ptr<const ConstrainAsData> data;
            // ConstraintLayoutParentData.equals: the same id and the same block.
            bool operator==(const ConstrainAsParams& o) const {
                return data == o.data || (data && o.data && data->ref.id == o.data->ref.id && data->constrain == o.data->constrain);
            }
        };
        struct ConstrainAsNode final : ui::ModifierNode, ui::ParentDataModifier {
            ConstrainAsParams params;
            explicit ConstrainAsNode(ConstrainAsParams p) : params(std::move(p)) {}
            void OnParamsChanged() {
                if (Owner() && Owner()->Parent()) Owner()->Parent()->RequestRemeasure();
            }
            ui::ParentDataModifier* AsParentData() override { return this; }
            void ModifyParentData(ui::ParentData& d) const override {
                d.layoutId = params.data->ref.id;   // LayoutIdParentData
                ChildDataIn(d).constrainAs = params.data;
            }
        };

        struct TagParams {
            std::string tag;
            bool operator==(const TagParams&) const = default;
        };
        struct TagNode final : ui::ModifierNode, ui::ParentDataModifier {
            TagParams params;
            explicit TagNode(TagParams p) : params(std::move(p)) {}
            void OnParamsChanged() {
                if (Owner() && Owner()->Parent()) Owner()->Parent()->RequestRemeasure();
            }
            ui::ParentDataModifier* AsParentData() override { return this; }
            void ModifyParentData(ui::ParentData& d) const override { ChildDataIn(d).tag = params.tag; }
        };

    }

    Modifier ConstraintLayoutScope::ConstrainAs(Modifier modifier, const ConstrainedLayoutReference& ref, kiln::Function<void(ConstrainScope&)> block) {
        return ConstrainAs(std::move(modifier), ref, std::make_shared<const kiln::Function<void(ConstrainScope&)>>(std::move(block)));
    }

    Modifier ConstraintLayoutScope::ConstrainAs(Modifier modifier, const ConstrainedLayoutReference& ref, ConstrainBlock block) {
        auto data = std::make_shared<const ConstrainAsData>(ConstrainAsData{ ref, std::move(block) });
        return modifier.With<ConstrainAsNode>(ConstrainAsParams{ std::move(data) });
    }

    Modifier LayoutId(Modifier modifier, std::string layoutId, std::string tag) {
        Modifier m = foundation::LayoutId(std::move(modifier), std::move(layoutId));
        if (tag.empty()) return m;
        return m.With<TagNode>(TagParams{ std::move(tag) });
    }

    std::shared_ptr<ConstraintSet> MakeConstraintSet(const kiln::Function<void(ConstraintSetScope&)>& description,
                                                     std::shared_ptr<ConstraintSet> extendConstraintSet) {
        return std::make_shared<DslConstraintSet>(description, std::move(extendConstraintSet));
    }

    std::shared_ptr<ConstraintSet> MakeConstraintSet(const std::string& jsonContent, std::shared_ptr<ConstraintSet> extendConstraintSet,
                                                     const std::string& overrideVariables) {
        return std::make_shared<JsonConstraintSet>(jsonContent, std::move(extendConstraintSet), overrideVariables);
    }

    std::shared_ptr<ConstraintSet> RememberConstraintSet(const std::string& content, const std::string& overrideVariables) {
        auto* c = rt::Composition::Current();
        return c->Remember<std::shared_ptr<ConstraintSet>>([&] { return MakeConstraintSet(content, nullptr, overrideVariables); },
                                                           rt::Keys(content, overrideVariables));
    }

    void ConstraintLayout(const Modifier& modifier, kiln::Function<void(ConstraintLayoutScope&)> content, int optimizationLevel, rt::Key key) {
        struct Holder {
            ConstraintLayoutScope scope;
            std::shared_ptr<InlineConstraintSet> constraintSet = std::make_shared<InlineConstraintSet>(scope);
            std::shared_ptr<ConstraintLayoutPolicy> policy = std::make_shared<ConstraintLayoutPolicy>();
            std::shared_ptr<rt::State<int>> contentTracker = std::make_shared<rt::State<int>>(0);
        };
        auto* c = rt::Composition::Current();
        auto& holder = c->Remember<std::shared_ptr<Holder>>([] { return std::make_shared<Holder>(); }, {}, key);
        Holder* h = holder.get();
        h->policy->constraintSet = h->constraintSet;
        h->policy->contentTracker = h->contentTracker;
        if (h->policy->optimizationLevel != optimizationLevel) {
            h->policy->optimizationLevel = optimizationLevel;
            if (h->constraintSet->node) h->constraintSet->node->RequestRemeasure();
        }
        ui::LayoutNode& node = foundation::Layout(
            modifier, h->policy,
            [h, content = std::move(content)] {
                const int previousHelpersHashCode = h->scope.HelpersHashCode();
                h->scope.Reset();
                content(h->scope);
                if (h->scope.HelpersHashCode() != previousHelpersHashCode) h->constraintSet->knownDirty = true;
                // contentTracker.value = Unit: the measure policy reads it.
                rt::Observation::Untracked([h] { h->contentTracker->Set(h->contentTracker->Get() + 1); });
            },
            key);
        h->constraintSet->node = &node;
    }

    void ConstraintLayout(std::shared_ptr<ConstraintSet> constraintSet, const Modifier& modifier, kiln::Function<void()> content,
                          int optimizationLevel, rt::Key key) {
        struct Holder {
            std::shared_ptr<ConstraintLayoutPolicy> policy = std::make_shared<ConstraintLayoutPolicy>();
            std::shared_ptr<rt::State<int>> contentTracker = std::make_shared<rt::State<int>>(0);
            ui::LayoutNode* node = nullptr;
        };
        auto* c = rt::Composition::Current();
        auto& holder = c->Remember<std::shared_ptr<Holder>>([] { return std::make_shared<Holder>(); }, {}, key);
        Holder* h = holder.get();
        h->policy->contentTracker = h->contentTracker;
        const bool changed = h->policy->constraintSet != constraintSet || h->policy->optimizationLevel != optimizationLevel;
        h->policy->constraintSet = std::move(constraintSet);
        h->policy->optimizationLevel = optimizationLevel;
        if (changed && h->node) h->node->RequestRemeasure();
        h->node = &foundation::Layout(
            modifier, h->policy,
            [h, content = std::move(content)] {
                rt::Observation::Untracked([h] { h->contentTracker->Set(h->contentTracker->Get() + 1); });
                content();
            },
            key);
    }

}
