#include "kiln/foundation/style/StyleResolver.h"

#include "kiln/runtime/Composition.h"

#include <algorithm>
#include <typeinfo>

namespace kiln::foundation {

    // --- StyleScope.kt ------------------------------------------------------------------------

    const anim::SpecPtr& AnimateStyleScope::DefaultSpec() {
        static const anim::SpecPtr spec = anim::Spring();   // DefaultSpringSpec: spring<Float>()
        return spec;
    }

    void State(StyleStateScope& scope, const StyleStateKey<bool>& key, const std::function<void()>& block) {
        scope.State<bool>(key, block, [](const StyleStateKey<bool>& k, const StyleState& s) { return s.Get(k); });
    }
    void Pressed(StyleStateScope& s, const std::function<void()>& b) {
        s.State<bool>(StyleStateKeys::Pressed(), b, [](const auto&, const StyleState& st) { return st.IsPressed(); });
    }
    void Hovered(StyleStateScope& s, const std::function<void()>& b) {
        s.State<bool>(StyleStateKeys::Hovered(), b, [](const auto&, const StyleState& st) { return st.IsHovered(); });
    }
    void Focused(StyleStateScope& s, const std::function<void()>& b) {
        s.State<bool>(StyleStateKeys::Focused(), b, [](const auto&, const StyleState& st) { return st.IsFocused(); });
    }
    void Selected(StyleStateScope& s, const std::function<void()>& b) {
        s.State<bool>(StyleStateKeys::Selected(), b, [](const auto&, const StyleState& st) { return st.IsSelected(); });
    }
    void Disabled(StyleStateScope& s, const std::function<void()>& b) {
        s.State<bool>(StyleStateKeys::Enabled(), b, [](const auto&, const StyleState& st) { return !st.IsEnabled(); });
    }
    void Checked(StyleStateScope& s, const std::function<void()>& b) {
        s.State<ui::ToggleableState>(StyleStateKeys::Toggle(), b, [](const auto&, const StyleState& st) { return st.IsChecked(); });
    }
    void TriStateToggleOn(StyleStateScope& s, const std::function<void()>& b) {
        s.State<ui::ToggleableState>(StyleStateKeys::Toggle(), b,
                                     [](const auto&, const StyleState& st) { return st.TriStateToggle() == ui::ToggleableState::On; });
    }
    void TriStateToggleOff(StyleStateScope& s, const std::function<void()>& b) {
        s.State<ui::ToggleableState>(StyleStateKeys::Toggle(), b,
                                     [](const auto&, const StyleState& st) { return st.TriStateToggle() == ui::ToggleableState::Off; });
    }
    void TriStateToggleIndeterminate(StyleStateScope& s, const std::function<void()>& b) {
        s.State<ui::ToggleableState>(StyleStateKeys::Toggle(), b,
                                     [](const auto&, const StyleState& st) { return st.TriStateToggle() == ui::ToggleableState::Indeterminate; });
    }

    namespace detail {

        using SpecMap = std::unordered_map<const StylePropertyBase*, anim::SpecPtr>;

        namespace {
            // AnimationSpec equality: kiln's specs have none, so two specs are the same when they
            // are one object, or of one kind with the same duration and curve.
            bool SameSpec(const anim::SpecPtr& a, const anim::SpecPtr& b) {
                if (a == b) return true;
                if (!a || !b || typeid(*a) != typeid(*b)) return false;
                const anim::Vec from{ 0, 0, 0, 0 }, to{ 1, 0, 0, 0 }, v0{ 0, 0, 0, 0 };
                const std::int64_t da = a->DurationNanos(from, to, v0, 1), db = b->DurationNanos(from, to, v0, 1);
                if (da != db) return false;
                for (int i = 1; i <= 8; ++i) {
                    const std::int64_t at = da / 8 * i;
                    if (a->ValueAt(at, from, to, v0, 1)[0] != b->ValueAt(at, from, to, v0, 1)[0]) return false;
                }
                return true;
            }
            const anim::SpecPtr& Find(const SpecMap* m, const StylePropertyBase* p) {
                static const anim::SpecPtr none;
                if (!m) return none;
                auto it = m->find(p);
                return it == m->end() ? none : it->second;
            }
        }

        std::unordered_set<const StylePropertyBase*> StyleValues::Diff(const StyleValues& other) const {
            std::unordered_set<const StylePropertyBase*> changes;
            for (const auto& [p, v] : m_Values) {
                const std::any* o = other.Find(*p);
                if (!o || !p->EqualAny(*o, v)) changes.insert(p);
            }
            for (const auto& [p, v] : other.m_Values)
                if (!Contains(*p)) changes.insert(p);
            return changes;
        }

        // --- StyleAnimations ----------------------------------------------------------------

        std::any StyleAnimations::AnimatedValueOrElse(const StylePropertyBase& p, const std::function<std::any()>& orElse) const {
            (void)m_Version.Get();
            auto it = m_Entries.find(&p);
            if (it == m_Entries.end()) return orElse();
            const Entry& e = *it->second;
            const float t = e.state == EntryState::Interrupted ? 0.0f : e.animation.Float();
            return p.LerpAny(e.start, e.end, t);
        }

        void StyleAnimations::Animate(Entry& e) {
            struct Run {
                static rt::Task<> Go(Entry* e, std::weak_ptr<const char> alive, StyleAnimations* self) {
                    // Cleanup after the animation ends or is cancelled (posted: not from inside
                    // the entry's own job).
                    struct Done {
                        std::weak_ptr<const char> alive;
                        StyleAnimations* self;
                        ~Done() {
                            rt::Dispatcher::Main().Post([a = alive, s = self] { if (!a.expired()) s->Cleanup(); });
                        }
                    } done{ alive, self };
                    if (e->state == EntryState::Interrupted) e->state = EntryState::Unchanged;
                    const anim::Vec velocity = e->animation.Velocity();
                    e->animation.SnapTo(0.0f);
                    co_await e->animation.AnimateTo(anim::Vec{ 1, 0, 0, 0 }, e->spec, velocity);
                }
            };
            e.job = rt::Launch(rt::Dispatcher::Main(), Run::Go(&e, m_Alive, this));
        }

        void StyleAnimations::Cleanup() {
            bool removed = false;
            for (auto it = m_Entries.begin(); it != m_Entries.end();) {
                const Entry& e = *it->second;
                if (e.state != EntryState::Interrupted && !e.animation.IsRunning() && e.job.Done()) {
                    it = m_Entries.erase(it);
                    removed = true;
                } else {
                    ++it;
                }
            }
            if (removed) m_Version.Set(m_Version.Peek() + 1);
        }

        void StyleAnimations::Record(const std::unordered_set<const StylePropertyBase*>& animating,
                                     const std::unordered_set<const StylePropertyBase*>& changes, const SpecMap* toSpecs, const SpecMap* fromSpecs,
                                     const SpecMap* previousFromSpecs, const StyleValues& start, const StyleValues& end) {
            // preRecordLocked
            for (auto& [p, e] : m_Entries)
                if (e->state == EntryState::Inserted || e->state == EntryState::Unchanged || e->state == EntryState::Changed) e->state = EntryState::Untouched;
            bool inserted = false;
            for (const StylePropertyBase* p : animating) {
                anim::SpecPtr spec = Find(toSpecs, p);
                if (!spec) spec = Find(previousFromSpecs, p);
                if (!spec) spec = Find(fromSpecs, p);
                if (!spec) spec = AnimateStyleScope::DefaultSpec();
                const bool changed = changes.contains(p);
                auto it = m_Entries.find(p);
                if (it != m_Entries.end()) {
                    Entry& e = *it->second;
                    if (changed || !SameSpec(e.spec, spec)) {
                        e.spec = spec;
                        // From what it shows now (mid-animation, the animated value).
                        e.start = AnimatedValueOrElse(*p, [&] { return start.Get(*p); });
                        e.state = EntryState::Changed;
                        e.end = end.Get(*p);
                    } else {
                        e.state = EntryState::Unchanged;
                    }
                } else if (changed) {
                    auto e = std::make_unique<Entry>();
                    e->spec = spec;
                    e->start = start.Get(*p);
                    e->end = end.Get(*p);
                    m_Entries.emplace(p, std::move(e));
                    inserted = true;
                }
            }
            if (inserted) m_Version.Set(m_Version.Peek() + 1);
            // postRecordLocked
            for (auto& [p, e] : m_Entries) {
                switch (e->state) {
                    case EntryState::Inserted: Animate(*e); break;
                    case EntryState::Untouched: e->state = EntryState::Removing; break;
                    case EntryState::Changed:
                        e->state = EntryState::Interrupted;
                        Animate(*e);
                        break;
                    default: break;
                }
            }
        }

        void StyleAnimations::Close() {
            for (auto& [p, e] : m_Entries) e->job.Cancel();
            if (!m_Entries.empty()) {
                m_Entries.clear();
                m_Version.Set(m_Version.Peek() + 1);
            }
        }

        // --- ResolvedProperties -------------------------------------------------------------

        bool ResolvedProperties::Contains(const StylePropertyBase& p) const {
            (void)m_Version.Get();
            return m_Map.contains(&p);
        }

        std::any ResolvedProperties::GetOrNull(const StylePropertyBase& p, const StyleAnimations& animations) const {
            (void)m_Version.Get();
            auto it = m_Map.find(&p);
            if (it == m_Map.end()) return {};
            const Value& v = *it->second;
            return animations.AnimatedValueOrElse(p, [&] { return v.current.Get(); });
        }

        void ResolvedProperties::Update(const StylePropertyBase& p, std::any value) {
            auto it = m_Map.find(&p);
            if (it == m_Map.end()) {
                auto v = std::make_unique<Value>();
                v->current.Set(std::move(value));
                m_Map.emplace(&p, std::move(v));
                m_Version.Set(m_Version.Peek() + 1);
            } else if (!p.EqualAny(it->second->current.Peek(), value)) {
                it->second->current.Set(std::move(value));
            }
        }

        std::vector<const StylePropertyBase*> ResolvedProperties::Keys() const {
            std::vector<const StylePropertyBase*> keys;
            for (const auto& [p, v] : m_Map) keys.push_back(p);
            return keys;
        }

        // --- StylePropertyCollector ---------------------------------------------------------

        class StylePropertyCollector final : public CommonStyleScope {
        public:
            const ui::Density& GetDensity() const override { return m_Density; }
            const ui::ModifierNode* LocalNode() const override { return m_Node; }
            const StyleState& State() const override {
                if (m_NestedStateOverride) return *m_NestedStateOverride;
                return *m_Node->Resolver().GetStyleState();
            }

            void ProvideAny(const StylePropertyBase& p, std::any value) override {
                // While an ancestor's style runs for a nested style key, only the matching
                // provideNestedStyle block provides. (Upstream's styleLayer change turned this
                // check into `nestedStyleKeys != null`, which stops all provides once any nested
                // style is recorded; the nested-styles change it came from checks the key.)
                if (m_NestedKey) return;
                auto it = m_Layers.find(&p);
                const int effective = it == m_Layers.end() ? 0 : it->second;
                if (m_Layer >= effective) {
                    RecordWrite(&p);
                    m_Properties->Set(p, std::move(value));
                    if (m_Layer > effective) m_Layers[&p] = m_Layer;
                }
            }
            void StateAny(const StyleStateKeyBase&, const std::function<void()>& block, const std::function<bool()>& active) override {
                if (active()) StyleLayer(block);
            }
            void StyleLayer(const std::function<void()>& block) override {
                ++m_Layer;
                struct Pop { int& l; ~Pop() { --l; } } pop{ m_Layer };
                block();
            }
            void Animate(anim::SpecPtr toSpec, anim::SpecPtr fromSpec, const std::function<void()>& block) override {
                auto prevTo = m_DefaultTo, prevFrom = m_DefaultFrom;
                const bool prevSpecified = m_Specified;
                m_DefaultTo = std::move(toSpec);
                m_DefaultFrom = std::move(fromSpec);
                m_Specified = true;
                struct Restore {
                    StylePropertyCollector* c;
                    anim::SpecPtr to, from;
                    bool specified;
                    ~Restore() { c->m_DefaultTo = to; c->m_DefaultFrom = from; c->m_Specified = specified; }
                } restore{ this, prevTo, prevFrom, prevSpecified };
                block();
            }
            void ApplyNestedStyle(const NestedStyleKey& key) override {
                const NestedStyleKey* previous = m_NestedKey;
                m_NestedKey = &key;
                struct Restore { const NestedStyleKey*& k; const NestedStyleKey* p; ~Restore() { k = p; } } restore{ m_NestedKey, previous };
                m_Node->Resolver().ResolveNestedStyle(key, [this](const CommonStyle& style, const StyleState& state) {
                    const StyleState* prevState = m_NestedStateOverride;
                    m_NestedStateOverride = &state;
                    struct R { const StyleState*& s; const StyleState* p; ~R() { s = p; } } r{ m_NestedStateOverride, prevState };
                    style.ApplyStyle(*this);
                });
            }
            void ProvideNestedStyle(const NestedStyleKey& key, const CommonStyle& style) override {
                if (m_NestedKey != &key) {
                    // Only record that the key is provided.
                    m_NestedKeys.insert(&key);
                    return;
                }
                m_NestedKey = nullptr;
                const StyleState* prevState = m_NestedStateOverride;
                m_NestedStateOverride = nullptr;
                struct R {
                    StylePropertyCollector* c;
                    const NestedStyleKey* key;
                    const StyleState* state;
                    ~R() { c->m_NestedKey = key; c->m_NestedStateOverride = state; }
                } r{ this, &key, prevState };
                style.ApplyStyle(*this);
            }

            void Collect(const CommonStyle& style, StyleResolverNode& node, const ui::Density& density) {
                StartCollect(node, density);
                style.ApplyStyle(*this);
                DoneCollect();
            }

        private:
            void RecordWrite(const StylePropertyBase* p) {
                // Outside animate { }: keep the specs a property had when it already animates.
                anim::SpecPtr to, from;
                if (m_Specified) {
                    to = m_DefaultTo;
                    from = m_DefaultFrom;
                } else if (m_AnimatingProperties.contains(p)) {
                    to = Find(&m_ToSpecs, p);
                    if (!to) to = AnimateStyleScope::DefaultSpec();
                    from = Find(&m_FromSpecs, p);
                    if (!from) from = AnimateStyleScope::DefaultSpec();
                }
                if (!to || to == AnimateStyleScope::DefaultSpec()) m_ToSpecs.erase(p);
                else m_ToSpecs[p] = to;
                if (!from || from == AnimateStyleScope::DefaultSpec()) m_FromSpecs.erase(p);
                else m_FromSpecs[p] = from;
                if (to && from) m_AnimatingProperties.insert(p);
            }

            void StartCollect(StyleResolverNode& node, const ui::Density& density) {
                m_Node = &node;
                m_Density = density;
                std::swap(m_Current, m_Previous);   // previous = what the last resolution provided
                m_Current.Clear();
                m_Properties = &m_Current;
                m_PreviousFromSpecs = std::move(m_FromSpecs);
                m_FromSpecs.clear();
                m_ToSpecs.clear();
                m_PreviousAnimating = std::move(m_AnimatingProperties);
                m_AnimatingProperties.clear();
                m_Layers.clear();
                m_NestedKeys.clear();
                m_Specified = false;
                m_DefaultTo = m_DefaultFrom = nullptr;
                m_Layer = 0;
            }

            void DoneCollect() {
                StyleResolverNode& node = *m_Node;
                m_Node = nullptr;
                m_Layers.clear();
                StyleResolver& resolver = node.Resolver();
                resolver.UpdateProperties(m_Current, &m_NestedKeys);
                StyleAnimations& animations = resolver.Animations();
                if (!animations.Empty() || !m_AnimatingProperties.empty() || !m_PreviousAnimating.empty()) {
                    const auto changes = m_Current.Diff(m_Previous);
                    std::unordered_set<const StylePropertyBase*> effective = m_AnimatingProperties;
                    effective.insert(m_PreviousAnimating.begin(), m_PreviousAnimating.end());
                    animations.Record(effective, changes, &m_ToSpecs, &m_FromSpecs, &m_PreviousFromSpecs, m_Previous, m_Current);
                }
                resolver.DiscardUnneededProperties(m_Current, m_Previous);
            }

            StyleResolverNode* m_Node = nullptr;
            ui::Density m_Density;
            StyleValues m_Current, m_Previous;
            StyleValues* m_Properties = &m_Current;
            std::unordered_map<const StylePropertyBase*, int> m_Layers;
            std::unordered_set<const StylePropertyBase*> m_AnimatingProperties, m_PreviousAnimating;
            SpecMap m_ToSpecs, m_FromSpecs, m_PreviousFromSpecs;
            const NestedStyleKey* m_NestedKey = nullptr;
            const StyleState* m_NestedStateOverride = nullptr;
            std::unordered_set<const NestedStyleKey*> m_NestedKeys;
            bool m_Specified = false;   // inside animate { }
            anim::SpecPtr m_DefaultTo, m_DefaultFrom;
            int m_Layer = 0;
        };

    }

    // --- StyleResolver --------------------------------------------------------------------------

    class StyleResolver::Scope final : public StyleResolverScope {
    public:
        explicit Scope(StyleResolver& r) : m_R(r) {}
        std::any ValueAny(const StylePropertyBase& p) const override {
            std::any v = GetOrNullAny(p);
            return v.has_value() ? v : p.DefaultAny();
        }
        std::any GetOrNullAny(const StylePropertyBase& p) const override {
            if (p.IsLocal()) {
                std::any v = m_R.m_Locals.GetOrNull(p, m_R.m_Animations);
                return v.has_value() ? v : m_R.ResolveLocal(p);
            }
            return m_R.m_Properties.GetOrNull(p, m_R.m_Animations);
        }
        bool IsSet(const StylePropertyBase& p) const override {
            if (!p.IsLocal()) return m_R.m_Properties.Contains(p);
            if (m_R.m_Locals.Contains(p)) return true;
            bool result = false;
            m_R.TraverseAncestors([&](StyleResolver& a) { result = a.m_Locals.Contains(p); return !result; });
            return result;
        }
        bool AnySet(const std::vector<const StylePropertyBase*>& properties) const override {
            bool anyLocals = false, result = false;
            for (const StylePropertyBase* p : properties) {
                if (p->IsLocal()) {
                    anyLocals = true;
                    if (m_R.m_Locals.Contains(*p)) { result = true; break; }
                } else if (m_R.m_Properties.Contains(*p)) {
                    result = true;
                    break;
                }
            }
            if (!result && anyLocals) {
                m_R.TraverseAncestors([&](StyleResolver& a) {
                    for (const StylePropertyBase* p : properties)
                        if (a.m_Locals.Contains(*p)) { result = true; break; }
                    return !result;
                });
            }
            return result;
        }
        const ui::Density& GetDensity() const override {
            m_Density = m_R.m_Node ? m_R.m_Node->RequireDensity() : ui::Density{};
            return m_Density;
        }
        const ui::ModifierNode* LocalNode() const override { return m_R.m_Node; }
    private:
        StyleResolver& m_R;
        mutable ui::Density m_Density;
    };

    StyleResolver::StyleResolver(CommonStyle style, std::shared_ptr<StyleState> styleState)
        : m_Style(std::move(style)), m_State(styleState ? std::move(styleState) : std::make_shared<MutableStyleState>()),
          m_Collector(std::make_unique<detail::StylePropertyCollector>()), m_Scope(std::make_unique<Scope>(*this)) {}

    StyleResolver::~StyleResolver() = default;

    const StyleResolverScope& StyleResolver::AccessorScope() {
        if (!m_Node) throw std::logic_error("Unbound style resolver. Cannot read a style resolver before it has been bound to a styleResolver modifier.");
        m_Node->EnsureResolved();
        if (!m_ResolvedAtLeastOnce)
            throw std::logic_error("Unresolved style properties. A style resolved property cannot be used until it is both bound and resolved.");
        return *m_Scope;
    }

    void StyleResolver::Bind(StyleResolverNode* node) {
        if (m_Node && m_Node != node) throw std::logic_error("Cannot bind a style resolver to more than one styleResolver modifier");
        m_Node = node;
    }
    void StyleResolver::Unbind() {
        m_Node = nullptr;
        m_ParentOverride = nullptr;
    }
    void StyleResolver::Dispose() {
        Unbind();
        m_Animations.Close();
    }
    void StyleResolver::Collect() {
        if (!m_Node) return;
        m_Collector->Collect(m_Style.Get(), *m_Node, m_Node->RequireDensity());
    }

    std::any StyleResolver::ResolveLocal(const StylePropertyBase& p) const {
        std::any result;
        TraverseAncestors([&](StyleResolver& a) {
            result = a.m_Locals.GetOrNull(p, a.m_Animations);
            return !result.has_value();
        });
        return result;
    }

    void StyleResolver::TraverseAncestors(const std::function<bool(StyleResolver&)>& block) const {
        if (m_ParentOverride) {
            for (StyleResolver* p = m_ParentOverride; p; p = p->m_ParentOverride)
                if (!block(*p)) return;
            return;
        }
        if (!m_Node) return;
        ui::TraverseAncestors(*m_Node, m_Node->TraverseKey(), [&](ui::TraversableNode& t) {
            auto* n = dynamic_cast<StyleResolverNode*>(&t);
            if (!n) return true;
            return block(n->Resolver());
        });
    }

    void StyleResolver::UpdateProperties(const detail::StyleValues& values, const std::unordered_set<const NestedStyleKey*>* nestedKeys) {
        for (const auto& [p, v] : values.Map()) {
            if (p->IsLocal()) m_Locals.Update(*p, v);
            else m_Properties.Update(*p, v);
        }
        // The nested style keys this style provides: unchanged when the same (readers depend on it).
        std::vector<const NestedStyleKey*> keys;
        if (nestedKeys) keys.assign(nestedKeys->begin(), nestedKeys->end());
        std::sort(keys.begin(), keys.end());
        if (keys != m_NestedKeys.Peek()) m_NestedKeys.Set(std::move(keys));
        m_ResolvedAtLeastOnce = true;
    }

    void StyleResolver::ResolveNestedStyle(const NestedStyleKey& key, const std::function<void(const CommonStyle&, const StyleState&)>& block) {
        // Ancestors that provide the key, applied from the furthest to the nearest.
        std::vector<StyleResolver*> resolvers;
        TraverseAncestors([&](StyleResolver& r) {
            const auto& keys = r.m_NestedKeys.Get();
            if (std::binary_search(keys.begin(), keys.end(), &key)) resolvers.push_back(&r);
            return true;
        });
        for (auto it = resolvers.rbegin(); it != resolvers.rend(); ++it) block((*it)->Style(), *(*it)->GetStyleState());
    }

    void StyleResolver::DiscardUnneededProperties(const detail::StyleValues& current, const detail::StyleValues& previous) {
        // Values no longer provided return to the default (locals: the inherited value) while
        // they may still animate there; then they go.
        for (const StylePropertyBase* p : m_Properties.Keys())
            if (!current.Contains(*p)) m_Properties.Update(*p, p->DefaultAny());
        m_Properties.RemoveIf([&](const StylePropertyBase& p) { return !current.Contains(p) && !previous.Contains(p) && !m_Animations.Animating(p); });
        for (const StylePropertyBase* p : m_Locals.Keys()) {
            if (current.Contains(*p)) continue;
            std::any inherited = ResolveLocal(*p);
            m_Locals.Update(*p, inherited.has_value() ? std::move(inherited) : p->DefaultAny());
        }
        m_Locals.RemoveIf([&](const StylePropertyBase& p) { return !current.Contains(p) && !previous.Contains(p) && !m_Animations.Animating(p); });
    }

    std::shared_ptr<StyleResolver> RememberStyleResolver(CommonStyle style, std::shared_ptr<StyleState> styleState) {
        auto* c = rt::Composition::Current();
        if (!c) return std::make_shared<StyleResolver>(std::move(style), std::move(styleState));
        if (!styleState) styleState = c->Remember<std::shared_ptr<StyleState>>([] { return std::make_shared<MutableStyleState>(); });
        auto& resolver = c->Remember<std::shared_ptr<StyleResolver>>([&] { return std::make_shared<StyleResolver>(style, styleState); });
        resolver->SetStyle(std::move(style));
        resolver->SetStyleState(std::move(styleState));
        return resolver;
    }

    // --- StyleResolverNode ----------------------------------------------------------------------

    namespace {
        const char kStyleResolverNodeKey = 0;
    }

    const void* StyleResolverNode::TraverseKey() const { return &kStyleResolverNodeKey; }

    void StyleResolverNode::OnAttach() {
        m_Bound = params.resolver;
        m_Bound->Bind(this);
    }

    void StyleResolverNode::OnDetach() {
        if (m_StopInteractions) {
            m_StopInteractions();
            m_StopInteractions = nullptr;
        }
        m_Source = nullptr;
        if (m_Bound) m_Bound->Dispose();
        m_Bound = nullptr;
        m_Resolved = false;
    }

    void StyleResolverNode::OnParamsChanged() {
        if (m_Bound == params.resolver) return;
        // A different resolver on an attached node: dispose the old one, bind the new one.
        if (m_Bound) {
            m_Bound->Dispose();
            m_Bound = params.resolver;
            m_Bound->Bind(this);
        }
        m_Resolved = false;
        InvalidateMeasure();
        InvalidateDraw();
    }

    void StyleResolverNode::EnsureResolved() {
        if (m_Resolved) return;
        m_Resolved = true;
        ResolveStyle();
    }

    ui::Density StyleResolverNode::RequireDensity() const {
        return Owner() ? Owner()->NodeDensity() : ui::Density{};
    }

    void StyleResolverNode::ResolveStyle() {
        StyleResolver& resolver = *params.resolver;
        ObserveReads([&] { resolver.Collect(); });
        const auto& state = resolver.GetStyleState();
        const auto& source = state ? state->InteractionSource() : nullptr;
        if (source != m_Source) {
            if (m_StopInteractions) m_StopInteractions();
            m_StopInteractions = nullptr;
            m_Source = source;
            if (source) m_StopInteractions = state->ProcessInteractions(source);
        }
    }

    Modifier StyleResolverMod(Modifier m, std::shared_ptr<StyleResolver> resolver) {
        return m.With<StyleResolverNode>(StyleResolverNode::Params{ std::move(resolver) });
    }

    // --- StyleProperties.kt ---------------------------------------------------------------------

    namespace style {
        namespace {
            StylePropertyRef<ui::Dp> DpProperty(const char* name) { return StylePropertyOf<ui::Dp>(name, [] { return ui::Dp(0); }); }
            StylePropertyRef<float> FloatProperty(const char* name, float def) { return StylePropertyOf<float>(name, [def] { return def; }); }
        }
#define KILN_STYLE_PROPERTY(Name, Type, ...)                       \
        const StylePropertyRef<Type>& Name() {                    \
            static const StylePropertyRef<Type> p = __VA_ARGS__; \
            return p;                                             \
        }
        KILN_STYLE_PROPERTY(ContentPaddingStart, ui::Dp, DpProperty("contentPaddingStart"))
        KILN_STYLE_PROPERTY(ContentPaddingEnd, ui::Dp, DpProperty("contentPaddingEnd"))
        KILN_STYLE_PROPERTY(ContentPaddingTop, ui::Dp, DpProperty("contentPaddingTop"))
        KILN_STYLE_PROPERTY(ContentPaddingBottom, ui::Dp, DpProperty("contentPaddingBottom"))
        KILN_STYLE_PROPERTY(ExternalPaddingStart, ui::Dp, DpProperty("externalPaddingStart"))
        KILN_STYLE_PROPERTY(ExternalPaddingEnd, ui::Dp, DpProperty("externalPaddingEnd"))
        KILN_STYLE_PROPERTY(ExternalPaddingTop, ui::Dp, DpProperty("externalPaddingTop"))
        KILN_STYLE_PROPERTY(ExternalPaddingBottom, ui::Dp, DpProperty("externalPaddingBottom"))
        // width / height: no interpolation argument upstream, so Breadth's own lerp.
        KILN_STYLE_PROPERTY(Width, Breadth, StylePropertyOf<Breadth>("width", [] { return Breadth::None(); }))
        KILN_STYLE_PROPERTY(Height, Breadth, StylePropertyOf<Breadth>("height", [] { return Breadth::None(); }))
        KILN_STYLE_PROPERTY(Left, ui::Dp, DpProperty("left"))
        KILN_STYLE_PROPERTY(Right, ui::Dp, DpProperty("right"))
        KILN_STYLE_PROPERTY(Top, ui::Dp, DpProperty("top"))
        KILN_STYLE_PROPERTY(Bottom, ui::Dp, DpProperty("bottom"))
        KILN_STYLE_PROPERTY(MinWidth, ui::Dp, DpProperty("minWidth"))
        KILN_STYLE_PROPERTY(MaxWidth, ui::Dp, DpProperty("maxWidth"))
        KILN_STYLE_PROPERTY(MinHeight, ui::Dp, DpProperty("minHeight"))
        KILN_STYLE_PROPERTY(MaxHeight, ui::Dp, DpProperty("maxHeight"))
        KILN_STYLE_PROPERTY(BorderFill, Fill, StylePropertyOf<Fill>("borderFill", [] { return Fill::None(); }))
        KILN_STYLE_PROPERTY(BorderWidth, ui::Dp, DpProperty("borderWidth"))
        KILN_STYLE_PROPERTY(Background, Fill, StylePropertyOf<Fill>("background", [] { return Fill::None(); }))
        KILN_STYLE_PROPERTY(Foreground, Fill, StylePropertyOf<Fill>("foreground", [] { return Fill::None(); }))
        KILN_STYLE_PROPERTY(Clip, bool, StylePropertyOf<bool>("clip", [] { return false; }))
        KILN_STYLE_PROPERTY(Shape, ShapePtr, StylePropertyOf<ShapePtr>("shape", [] { return RectangleShape(); }))
        KILN_STYLE_PROPERTY(DropShadow, Shadows, StylePropertyOf<Shadows>("dropShadow", [] { return Shadows::None(); }))
        KILN_STYLE_PROPERTY(InnerShadow, Shadows, StylePropertyOf<Shadows>("innerShadow", [] { return Shadows::None(); }))
        KILN_STYLE_PROPERTY(Alpha, float, FloatProperty("alpha", 1))
        KILN_STYLE_PROPERTY(ScaleX, float, FloatProperty("scaleX", 1))
        KILN_STYLE_PROPERTY(ScaleY, float, FloatProperty("scaleY", 1))
        KILN_STYLE_PROPERTY(TranslationX, float, FloatProperty("translationX", 0))
        KILN_STYLE_PROPERTY(TranslationY, float, FloatProperty("translationY", 0))
        KILN_STYLE_PROPERTY(RotationX, float, FloatProperty("rotationX", 0))
        KILN_STYLE_PROPERTY(RotationY, float, FloatProperty("rotationY", 0))
        KILN_STYLE_PROPERTY(RotationZ, float, FloatProperty("rotationZ", 0))
        KILN_STYLE_PROPERTY(TransformOriginX, float, FloatProperty("transformOriginX", 0.5f))   // TransformOrigin.Center
        KILN_STYLE_PROPERTY(TransformOriginY, float, FloatProperty("transformOriginY", 0.5f))
        KILN_STYLE_PROPERTY(CameraDistance, float, FloatProperty("cameraDistance", 1))
        KILN_STYLE_PROPERTY(ZIndex, float, FloatProperty("zIndex", 0))
        KILN_STYLE_PROPERTY(ColorFilter, std::optional<gfx::ColorFilter>,
                            StylePropertyOf<std::optional<gfx::ColorFilter>>("colorFilter", [] { return std::optional<gfx::ColorFilter>(); }))
        KILN_STYLE_PROPERTY(ContentFillLocal, Fill, StyleLocalOf<Fill>("contentFill", [] { return Fill::None(); }))
#undef KILN_STYLE_PROPERTY
    }

    Fill ContentFill(const StylePropertyAccessorScope& scope) { return scope.Value(style::ContentFillLocal()); }
    ui::Color ContentColor(const StylePropertyAccessorScope& scope) { return ContentFill(scope).AsColor(); }
    std::optional<gfx::Brush> ContentBrush(const StylePropertyAccessorScope& scope) { return ContentFill(scope).AsBrush(); }

}
