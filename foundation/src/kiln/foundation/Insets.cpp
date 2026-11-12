#include "kiln/foundation/Insets.h"

#include "kiln/animation/Animatable.h"
#include "kiln/animation/Decay.h"
#include "kiln/runtime/Composition.h"
#include "kiln/ui/NestedScroll.h"
#include "kiln/ui/Locals.h"

#include <algorithm>

namespace kiln::foundation {

    // The insets expression (Compose's WindowInsets implementations).
    struct WindowInsets::Node {
        enum class Kind : std::uint8_t { Fixed, FixedDp, Padding, Types, Union, Add, Exclude, Only, State, Custom };
        enum class Variant : std::uint8_t { Current, IgnoringVisibility, AnimationSource, AnimationTarget };
        Kind kind = Kind::Fixed;
        int l = 0, t = 0, r = 0, b = 0;
        Dp ld{ 0 }, td{ 0 }, rd{ 0 }, bd{ 0 };
        PaddingValues padding;
        std::uint32_t types = 0;
        Variant variant = Variant::Current;
        std::shared_ptr<const Node> first, second;
        WindowInsetsSides sides;
        std::shared_ptr<const rt::State<WindowInsets>> state;
        std::shared_ptr<const WindowInsets::SideFunction> custom;
        const void* identity = nullptr;

        bool operator==(const Node& o) const {
            if (kind != o.kind) return false;
            switch (kind) {
                case Kind::Fixed: return l == o.l && t == o.t && r == o.r && b == o.b;
                case Kind::FixedDp: return ld == o.ld && td == o.td && rd == o.rd && bd == o.bd;
                case Kind::Padding: return padding == o.padding;
                case Kind::Types: return types == o.types && variant == o.variant;
                case Kind::Only: return sides == o.sides && *first == *o.first;
                case Kind::State: return state == o.state;
                case Kind::Custom: return identity ? identity == o.identity : custom == o.custom;
                default: return *first == *o.first && *second == *o.second;
            }
        }

        ui::InsetsValue Window(const InsetsContext& c) const {
            ui::InsetsValue v;
            if (!c.holder) return v;
            for (std::uint32_t i = 0; i < std::uint32_t(ui::InsetsType::Count); ++i) {
                if (!(types & (1u << i))) continue;
                const auto type = ui::InsetsType(i);
                switch (variant) {
                    case Variant::Current: v = v.Union(c.holder->Get(type)); break;
                    case Variant::IgnoringVisibility: v = v.Union(c.holder->GetIgnoringVisibility(type)); break;
                    case Variant::AnimationSource:
                    case Variant::AnimationTarget: {
                        const auto& a = c.holder->Animation(type);
                        v = v.Union(!a ? c.holder->Get(type) : variant == Variant::AnimationSource ? a->source : a->target);
                        break;
                    }
                }
            }
            return v;
        }

        // side: 0 left, 1 top, 2 right, 3 bottom.
        int Side(const InsetsContext& c, int side) const {
            switch (kind) {
                case Kind::Fixed: return side == 0 ? l : side == 1 ? t : side == 2 ? r : b;
                case Kind::FixedDp: return c.density.RoundToPx(side == 0 ? ld : side == 1 ? td : side == 2 ? rd : bd);
                case Kind::Padding: {
                    const bool rtl = padding.rtlAware && c.direction == LayoutDirection::Rtl;
                    const Dp left = rtl ? padding.end : padding.start, right = rtl ? padding.start : padding.end;
                    return c.density.RoundToPx(side == 0 ? left : side == 1 ? padding.top : side == 2 ? right : padding.bottom);
                }
                case Kind::Types: {
                    const auto v = Window(c);
                    return side == 0 ? v.left : side == 1 ? v.top : side == 2 ? v.right : v.bottom;
                }
                case Kind::Union: return std::max(first->Side(c, side), second->Side(c, side));
                case Kind::Add: return first->Side(c, side) + second->Side(c, side);
                case Kind::Exclude: return std::max(0, first->Side(c, side) - second->Side(c, side));
                case Kind::Only: {
                    const bool ltr = c.direction == LayoutDirection::Ltr;
                    bool allowed;
                    if (side == 0) allowed = sides.HasAny({ ltr ? WindowInsetsSides::kAllowLeftInLtr : WindowInsetsSides::kAllowLeftInRtl });
                    else if (side == 2) allowed = sides.HasAny({ ltr ? WindowInsetsSides::kAllowRightInLtr : WindowInsetsSides::kAllowRightInRtl });
                    else allowed = sides.HasAny(side == 1 ? WindowInsetsSides::Top() : WindowInsetsSides::Bottom());
                    return allowed ? first->Side(c, side) : 0;
                }
                case Kind::State: return state->Get().m_Node->Side(c, side);
                case Kind::Custom: return (*custom)(c, side);
            }
            return 0;
        }
    };

    namespace {
        using Node = WindowInsets::Node;
        std::shared_ptr<const Node> TypesNode(std::initializer_list<ui::InsetsType> types, Node::Variant variant = Node::Variant::Current) {
            auto n = std::make_shared<Node>();
            n->kind = Node::Kind::Types;
            for (auto t : types) n->types |= 1u << std::uint32_t(t);
            n->variant = variant;
            return n;
        }
        std::shared_ptr<const Node> Pair(Node::Kind kind, std::shared_ptr<const Node> a, std::shared_ptr<const Node> b) {
            auto n = std::make_shared<Node>();
            n->kind = kind;
            n->first = std::move(a);
            n->second = std::move(b);
            return n;
        }
    }

    WindowInsets::WindowInsets() : m_Node(std::make_shared<Node>()) {}
    int WindowInsets::Left(const InsetsContext& c) const { return m_Node->Side(c, 0); }
    int WindowInsets::Top(const InsetsContext& c) const { return m_Node->Side(c, 1); }
    int WindowInsets::Right(const InsetsContext& c) const { return m_Node->Side(c, 2); }
    int WindowInsets::Bottom(const InsetsContext& c) const { return m_Node->Side(c, 3); }
    bool WindowInsets::operator==(const WindowInsets& o) const { return m_Node == o.m_Node || *m_Node == *o.m_Node; }

    WindowInsets WindowInsets::Union(const WindowInsets& o) const { return WindowInsets(Pair(Node::Kind::Union, m_Node, o.m_Node)); }
    WindowInsets WindowInsets::Add(const WindowInsets& o) const { return WindowInsets(Pair(Node::Kind::Add, m_Node, o.m_Node)); }
    WindowInsets WindowInsets::Exclude(const WindowInsets& o) const { return WindowInsets(Pair(Node::Kind::Exclude, m_Node, o.m_Node)); }
    WindowInsets WindowInsets::Only(WindowInsetsSides sides) const {
        auto n = std::make_shared<Node>();
        n->kind = Node::Kind::Only;
        n->first = m_Node;
        n->sides = sides;
        return WindowInsets(n);
    }
    PaddingValues WindowInsets::AsPaddingValues(const InsetsContext& c) const {
        const float d = c.density.density;
        return { Dp(float(Left(c)) / d), Dp(float(Top(c)) / d), Dp(float(Right(c)) / d), Dp(float(Bottom(c)) / d), false };
    }

    WindowInsets WindowInsets::Fixed(int l, int t, int r, int b) {
        auto n = std::make_shared<Node>();
        n->l = l; n->t = t; n->r = r; n->b = b;
        return WindowInsets(n);
    }
    WindowInsets WindowInsets::Fixed(Dp l, Dp t, Dp r, Dp b) {
        auto n = std::make_shared<Node>();
        n->kind = Node::Kind::FixedDp;
        n->ld = l; n->td = t; n->rd = r; n->bd = b;
        return WindowInsets(n);
    }
    WindowInsets WindowInsets::FromState(std::shared_ptr<const rt::State<WindowInsets>> state) {
        auto n = std::make_shared<Node>();
        n->kind = Node::Kind::State;
        n->state = std::move(state);
        return WindowInsets(n);
    }
    WindowInsets WindowInsets::FromFunction(SideFunction side, const void* identity) {
        auto n = std::make_shared<Node>();
        n->kind = Node::Kind::Custom;
        n->custom = std::make_shared<const SideFunction>(std::move(side));
        n->identity = identity;
        return WindowInsets(n);
    }
    WindowInsets WindowInsets::FromPadding(PaddingValues p) {
        auto n = std::make_shared<Node>();
        n->kind = Node::Kind::Padding;
        n->padding = p;
        return WindowInsets(n);
    }

    using T = ui::InsetsType;
    WindowInsets WindowInsets::CaptionBar() { return WindowInsets(TypesNode({ T::CaptionBar })); }
    WindowInsets WindowInsets::DisplayCutout() { return WindowInsets(TypesNode({ T::DisplayCutout })); }
    WindowInsets WindowInsets::Ime() { return WindowInsets(TypesNode({ T::Ime })); }
    WindowInsets WindowInsets::MandatorySystemGestures() { return WindowInsets(TypesNode({ T::MandatorySystemGestures })); }
    WindowInsets WindowInsets::NavigationBars() { return WindowInsets(TypesNode({ T::NavigationBars })); }
    WindowInsets WindowInsets::StatusBars() { return WindowInsets(TypesNode({ T::StatusBars })); }
    WindowInsets WindowInsets::SystemGestures() { return WindowInsets(TypesNode({ T::SystemGestures })); }
    WindowInsets WindowInsets::TappableElement() { return WindowInsets(TypesNode({ T::TappableElement })); }
    WindowInsets WindowInsets::Waterfall() { return WindowInsets(TypesNode({ T::Waterfall })); }
    WindowInsets WindowInsets::SystemBars() { return WindowInsets(TypesNode({ T::StatusBars, T::NavigationBars, T::CaptionBar })); }
    WindowInsets WindowInsets::SafeDrawing() { return SystemBars().Union(Ime()).Union(DisplayCutout()); }
    WindowInsets WindowInsets::SafeGestures() { return TappableElement().Union(MandatorySystemGestures()).Union(SystemGestures()).Union(Waterfall()); }
    WindowInsets WindowInsets::SafeContent() { return SafeDrawing().Union(SafeGestures()); }
    WindowInsets WindowInsets::CaptionBarIgnoringVisibility() { return WindowInsets(TypesNode({ T::CaptionBar }, Node::Variant::IgnoringVisibility)); }
    WindowInsets WindowInsets::NavigationBarsIgnoringVisibility() { return WindowInsets(TypesNode({ T::NavigationBars }, Node::Variant::IgnoringVisibility)); }
    WindowInsets WindowInsets::StatusBarsIgnoringVisibility() { return WindowInsets(TypesNode({ T::StatusBars }, Node::Variant::IgnoringVisibility)); }
    WindowInsets WindowInsets::SystemBarsIgnoringVisibility() {
        return WindowInsets(TypesNode({ T::StatusBars, T::NavigationBars, T::CaptionBar }, Node::Variant::IgnoringVisibility));
    }
    WindowInsets WindowInsets::TappableElementIgnoringVisibility() { return WindowInsets(TypesNode({ T::TappableElement }, Node::Variant::IgnoringVisibility)); }
    WindowInsets WindowInsets::ImeAnimationSource() { return WindowInsets(TypesNode({ T::Ime }, Node::Variant::AnimationSource)); }
    WindowInsets WindowInsets::ImeAnimationTarget() { return WindowInsets(TypesNode({ T::Ime }, Node::Variant::AnimationTarget)); }

    namespace {
        bool Visible(std::initializer_list<ui::InsetsType> types) {
            auto* c = rt::Composition::Current();
            const ui::WindowInsetsHolder* h = c ? c->Read(ui::LocalWindowInsets) : nullptr;
            if (!h) return false;
            for (auto t : types) if (!h->IsVisible(t)) return false;
            return true;
        }
    }
    bool IsCaptionBarVisible() { return Visible({ T::CaptionBar }); }
    bool IsImeVisible() { return Visible({ T::Ime }); }
    bool AreStatusBarsVisible() { return Visible({ T::StatusBars }); }
    bool AreNavigationBarsVisible() { return Visible({ T::NavigationBars }); }
    // WindowInsetsCompat.isVisible(systemBars()): every bar type in the mask is visible.
    bool AreSystemBarsVisible() { return Visible({ T::StatusBars, T::NavigationBars, T::CaptionBar }); }
    bool IsTappableElementVisible() { return Visible({ T::TappableElement }); }

    // --- consumption ---------------------------------------------------------------------------

    namespace {
        enum class ConsumeKind : std::uint8_t { Padding, Union, AddPadding, Observe, Recalculate, TopHeight, BottomHeight, StartWidth, EndWidth };
        struct InsetsParams {
            ConsumeKind kind;
            WindowInsets insets;
            PaddingValues padding;
            std::shared_ptr<std::function<void(const WindowInsets&)>> block;
            bool operator==(const InsetsParams& o) const {
                return kind == o.kind && insets == o.insets && padding == o.padding && block == o.block;
            }
        };

        InsetsContext ContextOf(const ModifierNode& n) {
            InsetsContext c;
            auto* owner = n.Owner() ? n.Owner()->GetOwner() : nullptr;
            if (owner) {
                c.holder = &owner->Insets();
                c.density = owner->GetDensity();
                c.direction = n.Owner()->GetLayoutDirection();
            }
            return c;
        }

        // InsetsConsumingModifierNode: consumed = what the ancestors consumed, plus this node's.
        // The ancestors are found up the chain each measure (Compose pushes them down instead).
        struct InsetsNode final : ModifierNode, LayoutModifier {
            InsetsParams params;
            explicit InsetsNode(InsetsParams p) : params(std::move(p)) {}
            void OnParamsChanged() { InvalidateMeasure(); }
            LayoutModifier* AsLayout() override { return this; }

            // Recalculate: the distance to the root's edges, set while placing with fixed
            // constraints (nullopt: the ancestors' consumption passes through).
            std::optional<WindowInsets> recalculated;

            bool Consumes() const {
                return params.kind == ConsumeKind::Padding || params.kind == ConsumeKind::Union || params.kind == ConsumeKind::AddPadding ||
                       params.kind == ConsumeKind::Recalculate;
            }
            WindowInsets Calculate(const WindowInsets& ancestor) const {
                switch (params.kind) {
                    case ConsumeKind::Padding:
                    case ConsumeKind::Union: return ancestor.Union(params.insets);
                    case ConsumeKind::AddPadding: return ancestor.Add(WindowInsets::FromPadding(params.padding));
                    case ConsumeKind::Recalculate: return recalculated ? *recalculated : ancestor;
                    default: return ancestor;
                }
            }
            WindowInsets AncestorConsumed() const {
                std::vector<const InsetsNode*> chain;   // innermost first
                bool before = true;
                for (LayoutNode* cur = Owner(); cur; cur = cur->Parent(), before = false) {
                    std::vector<const InsetsNode*> here;
                    for (const auto* m : cur->ModifierNodes()) {
                        if (before && m == this) break;
                        if (auto* x = dynamic_cast<const InsetsNode*>(m); x && x->Consumes()) here.push_back(x);
                    }
                    chain.insert(chain.end(), here.rbegin(), here.rend());
                }
                WindowInsets consumed;
                for (auto it = chain.rbegin(); it != chain.rend(); ++it) consumed = (*it)->Calculate(consumed);
                return consumed;
            }

            MeasureResult MeasureRecalculating(MeasureScope& s, Measurable& m, const Constraints& c);
            MeasureResult Measure(MeasureScope& s, Measurable& m, const Constraints& c) override {
                const InsetsContext ctx = ContextOf(*this);
                const WindowInsets ancestor = AncestorConsumed();
                if (params.kind == ConsumeKind::Observe && params.block && *params.block) (*params.block)(ancestor);
                if (params.kind == ConsumeKind::Recalculate) return MeasureRecalculating(s, m, c);
                if (params.kind != ConsumeKind::Padding && params.kind < ConsumeKind::TopHeight) {
                    Placeable& p = m.Measure(c);
                    return s.Layout(p.Width(), p.Height(), [&p](PlacementScope& place) { place.Place(p, 0, 0); });
                }
                if (params.kind >= ConsumeKind::TopHeight) {
                    // DerivedHeightModifier / DerivedWidthModifier: the side minus what the
                    // ancestors consumed, as a fixed height / width.
                    const WindowInsets own = params.insets.Exclude(ancestor);
                    int width = -1, height = -1;
                    switch (params.kind) {
                        case ConsumeKind::TopHeight: height = own.Top(ctx); break;
                        case ConsumeKind::BottomHeight: height = own.Bottom(ctx); break;
                        case ConsumeKind::StartWidth: width = ctx.direction == LayoutDirection::Ltr ? own.Left(ctx) : own.Right(ctx); break;
                        default: width = ctx.direction == LayoutDirection::Ltr ? own.Right(ctx) : own.Left(ctx); break;
                    }
                    Constraints cc = c;
                    if (width >= 0) { const int w = c.ConstrainWidth(width); cc.minWidth = cc.maxWidth = w; }
                    if (height >= 0) { const int h = c.ConstrainHeight(height); cc.minHeight = cc.maxHeight = h; }
                    Placeable& p = m.Measure(cc);
                    return s.Layout(p.Width(), p.Height(), [&p](PlacementScope& place) { place.Place(p, 0, 0); });
                }
                const WindowInsets consumed = Calculate(ancestor);
                const int left = consumed.Left(ctx) - ancestor.Left(ctx), top = consumed.Top(ctx) - ancestor.Top(ctx);
                const int right = consumed.Right(ctx) - ancestor.Right(ctx), bottom = consumed.Bottom(ctx) - ancestor.Bottom(ctx);
                const int horizontal = left + right, vertical = top + bottom;
                Placeable& p = m.Measure(c.Offset(-horizontal, -vertical));
                return s.Layout(c.ConstrainWidth(p.Width() + horizontal), c.ConstrainHeight(p.Height() + vertical),
                                [&p, left, top](PlacementScope& place) { place.Place(p, left, top); });
            }
        };

        // RecalculateWindowInsetsModifierNode.measure: with fixed constraints the content is
        // measured while placing, after the consumed insets became this element's distance to
        // the root's edges (so it pads exactly for the parts of the window it overlaps).
        MeasureResult InsetsNode::MeasureRecalculating(MeasureScope& s, Measurable& m, const Constraints& c) {
            if (!c.HasFixedWidth() || !c.HasFixedHeight()) {
                recalculated.reset();
                Placeable& p = m.Measure(c);
                return s.Layout(p.Width(), p.Height(), [&p](PlacementScope& place) { place.Place(p, 0, 0); });
            }
            const int width = c.maxWidth, height = c.maxHeight;
            return s.Layout(width, height, [this, &m, width, height](PlacementScope& place) {
                if (const LayoutNode* node = Owner()) {
                    const Rect bounds = node->CoordinatesOf(this).BoundsInRoot();
                    const ui::Owner* owner = node->GetOwner();
                    const IntSize rootSize = owner ? owner->WindowSize() : node->Size();
                    recalculated = WindowInsets::Fixed(RoundJava(bounds.left), RoundJava(bounds.top), rootSize.width - RoundJava(bounds.right),
                                                       rootSize.height - RoundJava(bounds.bottom));
                }
                place.Place(m.Measure(Constraints::Fixed(width, height)), 0, 0);
            });
        }

        Modifier With(Modifier m, ConsumeKind kind, WindowInsets insets, PaddingValues padding = {},
                      std::shared_ptr<std::function<void(const WindowInsets&)>> block = nullptr) {
            return m.With<InsetsNode>(InsetsParams{ kind, std::move(insets), padding, std::move(block) });
        }
    }

    Modifier WindowInsetsPadding(Modifier m, WindowInsets insets) { return With(std::move(m), ConsumeKind::Padding, std::move(insets)); }
    Modifier ConsumeWindowInsets(Modifier m, WindowInsets insets) { return With(std::move(m), ConsumeKind::Union, std::move(insets)); }
    Modifier ConsumeWindowInsets(Modifier m, PaddingValues padding) { return With(std::move(m), ConsumeKind::AddPadding, {}, padding); }
    Modifier RecalculateWindowInsets(Modifier m) { return With(std::move(m), ConsumeKind::Recalculate, {}); }
    Modifier OnConsumedWindowInsetsChanged(Modifier m, std::function<void(const WindowInsets&)> block) {
        return With(std::move(m), ConsumeKind::Observe, {}, {}, std::make_shared<std::function<void(const WindowInsets&)>>(std::move(block)));
    }
    Modifier WindowInsetsTopHeight(Modifier m, WindowInsets insets) { return With(std::move(m), ConsumeKind::TopHeight, std::move(insets)); }
    Modifier WindowInsetsBottomHeight(Modifier m, WindowInsets insets) { return With(std::move(m), ConsumeKind::BottomHeight, std::move(insets)); }
    Modifier WindowInsetsStartWidth(Modifier m, WindowInsets insets) { return With(std::move(m), ConsumeKind::StartWidth, std::move(insets)); }
    Modifier WindowInsetsEndWidth(Modifier m, WindowInsets insets) { return With(std::move(m), ConsumeKind::EndWidth, std::move(insets)); }

    // --- imeNestedScroll -------------------------------------------------------------------------

    namespace {
        // WindowInsetsNestedScrollConnection with the bottom side calculator: content scrolling up
        // (finger moving up, negative y) shows the IME, scrolling down hides it.
        struct ImeNestedScrollNode final : ui::NestedScrollNode, ui::NestedScrollConnection, ui::InsetsControlListener {
            struct Params { bool operator==(const Params&) const = default; } params;
            explicit ImeNestedScrollNode(Params) { connection = this; }
            ~ImeNestedScrollNode() override { Dispose(); }
            void OnParamsChanged() {}
            void OnDetach() override { Dispose(); }

            ui::WindowInsetsHolder* Holder() const {
                auto* o = Owner() ? Owner()->GetOwner() : nullptr;
                return o ? &o->Insets() : nullptr;
            }
            static float Motion(Offset o) { return -o.y; }
            static float ShowMotion(Offset o) { return std::max(0.0f, Motion(o)); }
            static float HideMotion(Offset o) { return std::min(0.0f, Motion(o)); }
            static ui::InsetsValue Adjust(ui::InsetsValue v, int bottom) { v.bottom = bottom; return v; }

            Offset OnPreScroll(Offset available, ui::NestedScrollSource) override { return Scroll(available, HideMotion(available)); }
            Offset OnPostScroll(Offset, Offset available, ui::NestedScrollSource) override { return Scroll(available, ShowMotion(available)); }
            rt::Task<Offset> OnPreFling(Offset available) override { co_return Fling(available, HideMotion(available), false); }
            rt::Task<Offset> OnPostFling(Offset, Offset available) override { co_return Fling(available, ShowMotion(available), true); }

            Offset Scroll(Offset available, float amount) {
                m_Job.Cancel();
                auto* h = Holder();
                if (!h || !h->CanControl()) return {};
                if (amount == 0 || (h->IsVisible(ui::InsetsType::Ime) == (amount > 0) && !m_Ready)) return {};
                if (!m_Ready) {
                    m_Partial = 0;
                    Request();
                    return { 0, available.y };
                }
                const int current = h->ControlledInsets().bottom;
                const int target = amount > 0 ? m_Shown : m_Hidden;
                if (current == target) { m_Partial = 0; return {}; }
                const float total = float(current) + amount + m_Partial;
                const int next = std::clamp(int(std::lround(total)), m_Hidden, m_Shown);
                m_Partial = total - float(std::lround(total));
                if (next != current) h->SetControlledInsets(Adjust(h->ControlledInsets(), next));
                return { 0, available.y };
            }

            Offset Fling(Offset available, float amount, bool towardShown) {
                m_Job.Cancel();
                m_Partial = 0;
                auto* h = Holder();
                if (!h || !h->CanControl()) return {};
                if ((amount == 0 && !towardShown) || (!m_Ready && h->IsVisible(ui::InsetsType::Ime) == towardShown)) return {};
                if (!m_Ready) {
                    // getAnimationController: the fling runs once control is ready.
                    m_PendingFling = { amount, towardShown };
                    Request();
                    return { 0, available.y };
                }
                const float remaining = RunFling(amount);
                return { 0, available.y - remaining };
            }

            // Returns the velocity left over (the decay passing an end hands the rest back).
            float RunFling(float amount) {
                auto* h = Holder();
                const int current = h->ControlledInsets().bottom;
                if ((amount <= 0 && current == m_Hidden) || (amount >= 0 && current == m_Shown)) {
                    h->FinishControl(current == m_Shown);
                    m_Ready = false;
                    return 0;
                }
                auto* o = Owner() ? Owner()->GetOwner() : nullptr;
                auto spec = std::make_shared<anim::SplineDecay>(o ? o->GetDensity().density : 1.0f);
                const float distance = float(current) + spec->TargetValue(0, amount);
                const float endPercent = (distance - float(m_Hidden)) / float(m_Shown - m_Hidden);
                const bool targetShown = endPercent > 0.5f;
                const int target = targetShown ? m_Shown : m_Hidden;
                if (distance > float(m_Shown) || distance < float(m_Hidden)) {
                    // The velocity where the decay crosses the end.
                    float endVelocity = 0;
                    const std::int64_t duration = spec->DurationNanos(float(current), amount);
                    for (std::int64_t t = 0; t <= duration; t += 1'000'000) {
                        const float v = spec->ValueAt(t, float(current), amount);
                        if (v < float(m_Hidden) || v > float(m_Shown)) { endVelocity = spec->VelocityAt(t, float(current), amount); break; }
                    }
                    m_Job = rt::Launch(rt::Dispatcher::Main(), Decay(float(current), amount, spec, targetShown));
                    return endVelocity;
                }
                m_Job = rt::Launch(rt::Dispatcher::Main(), SpringTo(float(current), float(target), amount, targetShown));
                return 0;
            }

            rt::Task<> Decay(float from, float velocity, std::shared_ptr<anim::SplineDecay> spec, bool targetShown) {
                anim::Animatable a(from);
                a.UpdateBounds(anim::Vec{ float(m_Hidden), 0, 0, 0 }, anim::Vec{ float(m_Shown), 0, 0, 0 });
                co_await a.AnimateDecay(velocity, spec, [this](float v) { AdjustTo(v); });
                End(targetShown);
            }
            rt::Task<> SpringTo(float from, float to, float velocity, bool targetShown) {
                anim::Animatable a(from);
                co_await a.AnimateTo(anim::Vec{ to, 0, 0, 0 }, anim::Spring(), anim::Vec{ velocity, 0, 0, 0 },
                                     [this](const anim::Vec& v) { AdjustTo(v[0]); });
                End(targetShown);
            }
            void AdjustTo(float inset) {
                if (auto* h = Holder(); h && m_Ready) h->SetControlledInsets(Adjust(h->ControlledInsets(), int(std::lround(inset))));
            }
            void End(bool shown) {
                if (auto* h = Holder(); h && m_Ready) h->FinishControl(shown);
                m_Ready = false;
            }

            void Request() {
                if (m_Requested) return;
                auto* h = Holder();
                if (h && h->RequestControl(ui::InsetsType::Ime, this)) m_Requested = true;
            }
            void OnControlReady(const ui::InsetsValue& hidden, const ui::InsetsValue& shown, const ui::InsetsValue&) override {
                m_Ready = true;
                m_Requested = false;
                m_Hidden = hidden.bottom;
                m_Shown = shown.bottom;
                if (m_PendingFling) {
                    const auto f = *m_PendingFling;
                    m_PendingFling.reset();
                    RunFling(f.first);
                }
            }
            void OnControlEnded() override {
                // animationEnded: control is gone (finished by us or cancelled by the system).
                m_Ready = false;
                m_Requested = false;
                m_PendingFling.reset();
                m_Job.Cancel();
                m_Partial = 0;
            }
            void Dispose() {
                m_Job.Cancel();
                m_PendingFling.reset();
                if (auto* h = Holder()) {
                    // finish at the nearer state: shown unless at the hidden insets.
                    if (m_Ready) h->FinishControl(h->ControlledInsets().bottom != m_Hidden);
                    h->ReleaseControl(this);
                }
                m_Ready = m_Requested = false;
            }

            bool m_Ready = false, m_Requested = false;
            int m_Hidden = 0, m_Shown = 0;
            float m_Partial = 0;
            std::optional<std::pair<float, bool>> m_PendingFling;
            rt::Job m_Job;
        };
    }

    Modifier ImeNestedScroll(Modifier m) { return m.With<ImeNestedScrollNode>(ImeNestedScrollNode::Params{}); }

}
