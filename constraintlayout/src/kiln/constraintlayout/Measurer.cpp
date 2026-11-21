// constraintlayout-compose's measurement and composables: Measurer2 (Measurer.kt), the
// ConstraintSet kinds (ConstraintSet.kt, DslConstraintSet.kt, JSONConstraintSet.kt), the
// inline DSL's constraint set (ConstraintSetForInlineDsl), placeWithFrameTransform and the
// ConstraintLayout composables (ConstraintLayout.kt), Modifier.layoutId with a tag
// (ConstraintLayoutTag.kt).

#include "kiln/constraintlayout/ConstraintLayout.h"
#include "kiln/constraintlayout/MeasurerInternal.h"

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

    using detail::AnyOrNullId;
    using detail::ChildData;
    using detail::ChildDataIn;
    using detail::ChildDataOf;
    using detail::ConstrainAsData;
    using detail::Measurer;

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
            void ApplyTo(core::state::Transition& transition, int type) override {
                core::state::ConstraintSetParser::LayoutVariables layoutVariables;
                ApplyLayoutVariables(layoutVariables);
                core::state::ConstraintSetParser::parseJSON(m_Content, transition, type);
            }
            using DerivedConstraintSet::ApplyTo;
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

        // RawConstraintSet: a copy of the inline DSL's constraints (animateChanges).
        class RawConstraintSet final : public ConstraintSet {
        public:
            explicit RawConstraintSet(std::shared_ptr<core::CLObject> clObject) : m_Object(std::move(clObject)) {}
            void ApplyTo(core::state::State& state, std::span<ui::Measurable* const>) override {
                core::state::ConstraintSetParser::populateState(*m_Object, state, m_LayoutVariables);
            }
            bool Equals(const ConstraintSet& other) const override {
                const auto* o = dynamic_cast<const RawConstraintSet*>(&other);
                return o != nullptr && m_Object->equals(*o->m_Object);
            }
            using ConstraintSet::ApplyTo;

        private:
            std::shared_ptr<core::CLObject> m_Object;
            core::state::ConstraintSetParser::LayoutVariables m_LayoutVariables;
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
        if (isAnimateChanges) {
            // MotionLayout does not evaluate the DSL: obtain the constraints now.
            ConstrainScope scope(ref.id, AsCLContainer(ref));
            (*block)(scope);
        }
        auto data = std::make_shared<const ConstrainAsData>(ConstrainAsData{ ref, std::move(block) });
        return modifier.With<ConstrainAsNode>(ConstrainAsParams{ std::move(data) });
    }

    std::shared_ptr<ConstraintSet> detail::MakeRawConstraintSet(std::shared_ptr<core::CLObject> clObject) {
        return std::make_shared<RawConstraintSet>(std::move(clObject));
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
