#include "kiln/foundation/Insets.h"

#include "kiln/runtime/Composition.h"
#include "kiln/ui/Locals.h"

#include <algorithm>

namespace kiln::foundation {

    // The insets expression (Compose's WindowInsets implementations).
    struct WindowInsets::Node {
        enum class Kind : std::uint8_t { Fixed, FixedDp, Padding, Types, Union, Add, Exclude, Only };
        enum class Variant : std::uint8_t { Current, IgnoringVisibility, AnimationSource, AnimationTarget };
        Kind kind = Kind::Fixed;
        int l = 0, t = 0, r = 0, b = 0;
        Dp ld{ 0 }, td{ 0 }, rd{ 0 }, bd{ 0 };
        PaddingValues padding;
        std::uint32_t types = 0;
        Variant variant = Variant::Current;
        std::shared_ptr<const Node> first, second;
        WindowInsetsSides sides;

        bool operator==(const Node& o) const {
            if (kind != o.kind) return false;
            switch (kind) {
                case Kind::Fixed: return l == o.l && t == o.t && r == o.r && b == o.b;
                case Kind::FixedDp: return ld == o.ld && td == o.td && rd == o.rd && bd == o.bd;
                case Kind::Padding: return padding == o.padding;
                case Kind::Types: return types == o.types && variant == o.variant;
                case Kind::Only: return sides == o.sides && *first == *o.first;
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
        enum class ConsumeKind : std::uint8_t { Padding, Union, AddPadding, Observe, TopHeight, BottomHeight, StartWidth, EndWidth };
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
                c.direction = owner->Direction();
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

            bool Consumes() const { return params.kind == ConsumeKind::Padding || params.kind == ConsumeKind::Union || params.kind == ConsumeKind::AddPadding; }
            WindowInsets Calculate(const WindowInsets& ancestor) const {
                switch (params.kind) {
                    case ConsumeKind::Padding:
                    case ConsumeKind::Union: return ancestor.Union(params.insets);
                    case ConsumeKind::AddPadding: return ancestor.Add(WindowInsets::FromPadding(params.padding));
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

            MeasureResult Measure(MeasureScope& s, Measurable& m, const Constraints& c) override {
                const InsetsContext ctx = ContextOf(*this);
                const WindowInsets ancestor = AncestorConsumed();
                if (params.kind == ConsumeKind::Observe && params.block && *params.block) (*params.block)(ancestor);
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

        Modifier With(Modifier m, ConsumeKind kind, WindowInsets insets, PaddingValues padding = {},
                      std::shared_ptr<std::function<void(const WindowInsets&)>> block = nullptr) {
            return m.With<InsetsNode>(InsetsParams{ kind, std::move(insets), padding, std::move(block) });
        }
    }

    Modifier WindowInsetsPadding(Modifier m, WindowInsets insets) { return With(std::move(m), ConsumeKind::Padding, std::move(insets)); }
    Modifier ConsumeWindowInsets(Modifier m, WindowInsets insets) { return With(std::move(m), ConsumeKind::Union, std::move(insets)); }
    Modifier ConsumeWindowInsets(Modifier m, PaddingValues padding) { return With(std::move(m), ConsumeKind::AddPadding, {}, padding); }
    Modifier OnConsumedWindowInsetsChanged(Modifier m, std::function<void(const WindowInsets&)> block) {
        return With(std::move(m), ConsumeKind::Observe, {}, {}, std::make_shared<std::function<void(const WindowInsets&)>>(std::move(block)));
    }
    Modifier WindowInsetsTopHeight(Modifier m, WindowInsets insets) { return With(std::move(m), ConsumeKind::TopHeight, std::move(insets)); }
    Modifier WindowInsetsBottomHeight(Modifier m, WindowInsets insets) { return With(std::move(m), ConsumeKind::BottomHeight, std::move(insets)); }
    Modifier WindowInsetsStartWidth(Modifier m, WindowInsets insets) { return With(std::move(m), ConsumeKind::StartWidth, std::move(insets)); }
    Modifier WindowInsetsEndWidth(Modifier m, WindowInsets insets) { return With(std::move(m), ConsumeKind::EndWidth, std::move(insets)); }

}
