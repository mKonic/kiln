#include "kiln/material3/internal/Helpers.h"
#include <kiln/Function.h>

#include "kiln/runtime/Composition.h"

#include <algorithm>
#include <cmath>

namespace kiln::material3::internal {

    using foundation::CornerBasedShape;
    using foundation::Interaction;

    // --- animateElevation ------------------------------------------------------------------------

    rt::Task<> AnimateElevation(anim::Animatable& animatable, Dp target, std::optional<Interaction::Kind> from, std::optional<Interaction::Kind> to) {
        static const anim::Easing outgoing = anim::Easing::CubicBezier(0.40f, 0.00f, 0.60f, 1.00f);
        auto incomingFor = [](Interaction::Kind k) -> anim::SpecPtr {
            switch (k) {
                case Interaction::Kind::Press: case Interaction::Kind::Drag: case Interaction::Kind::Hover: case Interaction::Kind::Focus:
                    return anim::Tween(120, 0, anim::FastOutSlowIn());
                default: return nullptr;
            }
        };
        auto outgoingFor = [](Interaction::Kind k) -> anim::SpecPtr {
            switch (k) {
                case Interaction::Kind::Press: case Interaction::Kind::Drag: case Interaction::Kind::Focus: return anim::Tween(150, 0, outgoing);
                case Interaction::Kind::Hover: return anim::Tween(120, 0, outgoing);
                default: return nullptr;
            }
        };
        const anim::SpecPtr spec = to ? incomingFor(*to) : from ? outgoingFor(*from) : nullptr;
        if (spec) co_await animatable.AnimateTo(target.value, spec);
        else animatable.SnapTo(target.value);
    }

    // --- horizontalCenterOptically ---------------------------------------------------------------

    namespace {
        struct CenterParams {
            std::shared_ptr<const CornerBasedShape> corners;
            std::shared_ptr<const ShapeWithHorizontalCenterOptically> shape;
            Dp maxStart, maxEnd;
            bool operator==(const CenterParams&) const = default;
        };
        struct CenterNode final : ui::ModifierNode, ui::LayoutModifier {
            CenterParams params;
            explicit CenterNode(CenterParams p) : params(std::move(p)) {}
            ui::LayoutModifier* AsLayout() override { return this; }
            void OnParamsChanged() { InvalidateMeasure(); }
            ui::MeasureResult Measure(ui::MeasureScope& s, ui::Measurable& m, const ui::Constraints& c) override {
                ui::Placeable& placeable = m.Measure(c);
                const int width = placeable.Width(), height = placeable.Height();
                const ui::Size size{ float(width), float(height) };
                const float maxStartPx = -s.ToPx(params.maxStart), maxEndPx = s.ToPx(params.maxEnd);
                float offset = 0;
                if (params.corners) {
                    const auto& sh = *params.corners;
                    const float avgStart = (sh.topStart.ToPx(size, s) + sh.bottomStart.ToPx(size, s)) / 2;
                    const float avgEnd = (sh.topEnd.ToPx(size, s) + sh.bottomEnd.ToPx(size, s)) / 2;
                    offset = CenterOpticallyCoefficient * (avgStart - avgEnd);
                } else if (params.shape) {
                    offset = params.shape->Offset(size, s);
                }
                const float coerced = std::clamp(offset, maxStartPx, std::max(maxStartPx, maxEndPx));
                return s.Layout(width, height, [&placeable, coerced](ui::PlacementScope& p) {
                    p.PlaceRelative(placeable, ui::RoundJava(coerced), 0);
                });
            }
        };
    }

    ui::Modifier HorizontalCenterOptically(ui::Modifier m, std::shared_ptr<const CornerBasedShape> shape, Dp maxStartOffset, Dp maxEndOffset) {
        return m.With<CenterNode>(CenterParams{ std::move(shape), nullptr, maxStartOffset, maxEndOffset });
    }
    ui::Modifier HorizontalCenterOptically(ui::Modifier m, std::shared_ptr<const ShapeWithHorizontalCenterOptically> shape, Dp maxStartOffset,
                                           Dp maxEndOffset) {
        return m.With<CenterNode>(CenterParams{ nullptr, std::move(shape), maxStartOffset, maxEndOffset });
    }

    // --- AnimatedShape ---------------------------------------------------------------------------

    namespace {
        std::shared_ptr<const CornerBasedShape> LerpCorners(const std::shared_ptr<const CornerBasedShape>& a,
                                                            const std::shared_ptr<const CornerBasedShape>& b, float t) {
            auto r = std::dynamic_pointer_cast<const CornerBasedShape>(foundation::Lerp(a, b, t));
            return r ? r : (t < 0.5f ? a : b);
        }
        bool SameShape(const std::shared_ptr<const CornerBasedShape>& a, const std::shared_ptr<const CornerBasedShape>& b) {
            return a == b || (a && b && a->Equals(*b));
        }
    }

    AnimatedShapeState::AnimatedShapeState(std::shared_ptr<const CornerBasedShape> initialShape, anim::SpecPtr s)
        : startShape(initialShape), targetShape(initialShape), spec(std::move(s)) {}

    std::shared_ptr<const CornerBasedShape> AnimatedShapeState::MorphedShape() {
        const float p = progress.Float();
        if (p == m_CachedProgress && m_Cached) return m_Cached;
        m_Cached = LerpCorners(startShape, targetShape, p);
        m_CachedProgress = p;
        return m_Cached;
    }

    rt::Task<> AnimatedShapeState::AnimateToShape(std::shared_ptr<const CornerBasedShape> newTarget) {
        if (SameShape(targetShape, newTarget)) co_return;
        if (SameShape(newTarget, startShape)) {
            // Back to where it came from: reverse in place, keeping the velocity.
            startShape = targetShape;
            targetShape = newTarget;
            m_Cached.reset();
            const float p = progress.Value()[0], v = progress.Velocity()[0];
            progress.SnapTo(1.0f - p);
            co_await progress.AnimateTo(anim::Vec{ 1, 0, 0, 0 }, spec, anim::Vec{ -v, 0, 0, 0 });
        } else {
            const float p = progress.Value()[0];
            const auto capturedStart = startShape, capturedTarget = targetShape;
            startShape = p == 1 ? capturedTarget : p == 0 ? capturedStart : LerpCorners(capturedStart, capturedTarget, p);
            targetShape = std::move(newTarget);
            m_Cached.reset();
            progress.SnapTo(0.0f);
            co_await progress.AnimateTo(1.0f, spec);
        }
    }

    namespace {
        class AnimatedShape final : public ShapeWithHorizontalCenterOptically {
        public:
            explicit AnimatedShape(std::shared_ptr<AnimatedShapeState> s) : m_State(std::move(s)) {}
            gfx::Outline CreateOutline(ui::Size size, LayoutDirection dir, const ui::Density& d) const override {
                return m_State->MorphedShape()->CreateOutline(size, dir, d);
            }
            float Offset(ui::Size size, const ui::Density& d) const override {
                const auto sh = m_State->MorphedShape();
                const float hi = size.height / 2;
                auto c = [&](const foundation::CornerSize& cs) { return std::clamp(cs.ToPx(size, d), 0.0f, std::max(0.0f, hi)); };
                return CenterOpticallyCoefficient * (((c(sh->topStart) + c(sh->bottomStart)) / 2) - ((c(sh->topEnd) + c(sh->bottomEnd)) / 2));
            }
            bool Equals(const gfx::Shape& o) const override { return this == &o; }
        private:
            std::shared_ptr<AnimatedShapeState> m_State;
        };

        std::size_t CornerHash(const std::shared_ptr<const CornerBasedShape>& s) {
            if (!s) return 0;
            const CornerBasedShape& shape = *s;
            std::size_t h = std::hash<std::string>{}(typeid(shape).name());
            for (const auto* c : { &s->topStart, &s->topEnd, &s->bottomEnd, &s->bottomStart })
                for (float v : { c->dp, c->px, c->percent }) h = h * 31 + std::hash<float>{}(v);
            return h;
        }
    }

    std::shared_ptr<const ShapeWithHorizontalCenterOptically> RememberAnimatedShape(const std::shared_ptr<AnimatedShapeState>& state) {
        return rt::Composition::Current()->Remember<std::shared_ptr<const ShapeWithHorizontalCenterOptically>>(
            [&] { return std::make_shared<AnimatedShape>(state); }, rt::Keys(std::hash<const void*>{}(state.get())));
    }

    std::shared_ptr<const ShapeWithHorizontalCenterOptically> RememberAnimatedShape(std::shared_ptr<const CornerBasedShape> currentShape, anim::SpecPtr spec,
                                                                                    std::size_t key) {
        auto& c = *rt::Composition::Current();
        auto& state = c.Remember<std::shared_ptr<AnimatedShapeState>>([&] { return std::make_shared<AnimatedShapeState>(currentShape, spec); },
                                                                     rt::Keys(std::hash<const void*>{}(spec.get()), key));
        // LaunchedEffect(currentShape, state): keyed by the shape's value, as Kotlin's equality.
        std::shared_ptr<AnimatedShapeState> s = state;
        c.LaunchedEffect(rt::Keys(CornerHash(currentShape), std::hash<const void*>{}(s.get())),
                         [s, currentShape] { return s->AnimateToShape(currentShape); });
        return RememberAnimatedShape(s);
    }

    // --- MenuPosition ----------------------------------------------------------------------------

    namespace {
        // BiasAlignment / BiasAbsoluteAlignment.align: round((space - size) / 2 * (1 + bias)).
        int Align(float bias, int size, int space) { return ui::RoundJava(float(space - size) / 2.0f * (1 + bias)); }
        float Relative(float bias, LayoutDirection dir) { return dir == LayoutDirection::Ltr ? bias : -bias; }
        int Width(IntRect r) { return r.right - r.left; }
        int Height(IntRect r) { return r.bottom - r.top; }

        // AnchorAlignmentOffsetPosition
        MenuPosition::Horizontal AnchorH(float menuBias, float anchorBias) {
            return [=](IntRect anchor, IntSize, int menuWidth, LayoutDirection dir) {
                const int anchorOffset = Align(Relative(anchorBias, dir), 0, Width(anchor));
                const int menuOffset = -Align(Relative(menuBias, dir), 0, menuWidth);
                return anchor.left + anchorOffset + menuOffset;
            };
        }
        MenuPosition::Vertical AnchorV(float menuBias, float anchorBias) {
            return [=](IntRect anchor, IntSize, int menuHeight) {
                return anchor.top + Align(anchorBias, 0, Height(anchor)) - Align(menuBias, 0, menuHeight);
            };
        }
        // WindowAlignmentMarginPosition (absolute for left / right)
        MenuPosition::Horizontal WindowH(float bias) {
            return [=](IntRect, IntSize window, int menuWidth, LayoutDirection) { return Align(bias, menuWidth, window.width); };
        }
        MenuPosition::Vertical WindowV(float bias) {
            return [=](IntRect, IntSize window, int menuHeight) { return Align(bias, menuHeight, window.height); };
        }
    }

    namespace MenuPosition {
        const Horizontal startToAnchorStart = AnchorH(-1, -1), startToAnchorEnd = AnchorH(-1, 1), endToAnchorEnd = AnchorH(1, 1),
                         endToAnchorStart = AnchorH(1, -1), leftToWindowLeft = WindowH(-1), rightToWindowRight = WindowH(1);
        const Vertical topToAnchorBottom = AnchorV(-1, 1), topToAnchorTop = AnchorV(-1, -1), bottomToAnchorTop = AnchorV(1, -1),
                       bottomToAnchorBottom = AnchorV(1, 1), centerToAnchorTop = AnchorV(0, -1), topToWindowTop = WindowV(-1),
                       bottomToWindowBottom = WindowV(1);
    }

    namespace {
        std::vector<int> Xs(const MenuPositionScope& s, std::initializer_list<const MenuPosition::Horizontal*> c) {
            std::vector<int> out;
            for (const auto* h : c) out.push_back((*h)(s.anchorBounds, s.windowSize, s.menuSize.width, s.layoutDirection));
            return out;
        }
        std::vector<int> Ys(const MenuPositionScope& s, std::initializer_list<const MenuPosition::Vertical*> c) {
            std::vector<int> out;
            for (const auto* v : c) out.push_back((*v)(s.anchorBounds, s.windowSize, s.menuSize.height));
            return out;
        }
        using namespace MenuPosition;
        const Horizontal* WindowEdgeX(const MenuPositionScope& s) {
            return (s.anchorBounds.left + s.anchorBounds.right) / 2 < s.windowSize.width / 2 ? &leftToWindowLeft : &rightToWindowRight;
        }
        const Vertical* WindowEdgeY(const MenuPositionScope& s) {
            return (s.anchorBounds.top + s.anchorBounds.bottom) / 2 < s.windowSize.height / 2 ? &topToWindowTop : &bottomToWindowBottom;
        }
        std::vector<int> StartEndX(const MenuPositionScope& s) { return Xs(s, { &startToAnchorStart, &endToAnchorEnd, WindowEdgeX(s) }); }
        std::vector<int> BesideStartX(const MenuPositionScope& s) { return Xs(s, { &endToAnchorStart, &startToAnchorEnd, WindowEdgeX(s) }); }
        std::vector<int> BesideEndX(const MenuPositionScope& s) { return Xs(s, { &startToAnchorEnd, &endToAnchorStart, WindowEdgeX(s) }); }
        std::vector<int> SideY(const MenuPositionScope& s) { return Ys(s, { &topToAnchorTop, &bottomToAnchorBottom, WindowEdgeY(s) }); }
    }

    const MenuAnchorPosition& MenuAnchorPosition::Above() {
        static const MenuAnchorPosition p{ StartEndX, [](const MenuPositionScope& s) {
            return Ys(s, { &bottomToAnchorTop, &topToAnchorBottom, &centerToAnchorTop, WindowEdgeY(s) }); } };
        return p;
    }
    const MenuAnchorPosition& MenuAnchorPosition::Below() {
        static const MenuAnchorPosition p{ StartEndX, [](const MenuPositionScope& s) {
            return Ys(s, { &topToAnchorBottom, &bottomToAnchorTop, &centerToAnchorTop, WindowEdgeY(s) }); } };
        return p;
    }
    const MenuAnchorPosition& MenuAnchorPosition::Left() { static const MenuAnchorPosition p{ BesideStartX, SideY }; return p; }
    const MenuAnchorPosition& MenuAnchorPosition::Right() { static const MenuAnchorPosition p{ BesideEndX, SideY }; return p; }
    const MenuAnchorPosition& MenuAnchorPosition::Start() { static const MenuAnchorPosition p{ BesideStartX, SideY }; return p; }
    const MenuAnchorPosition& MenuAnchorPosition::End() { static const MenuAnchorPosition p{ BesideEndX, SideY }; return p; }

    TransformOrigin CalculateTransformOrigin(IntRect anchor, IntRect menu) {
        auto pivot = [](int anchorStart, int anchorEnd, int menuStart, int menuEnd) {
            const int size = menuEnd - menuStart;
            if (menuStart >= anchorEnd) return 0.0f;
            if (menuEnd <= anchorStart) return 1.0f;
            if (size == 0) return 0.0f;
            const int intersectionCenter = (std::max(anchorStart, menuStart) + std::min(anchorEnd, menuEnd)) / 2;
            return float(intersectionCenter - menuStart) / float(size);
        };
        return { pivot(anchor.left, anchor.right, menu.left, menu.right), pivot(anchor.top, anchor.bottom, menu.top, menu.bottom) };
    }

    DropdownMenuPositionProvider::DropdownMenuPositionProvider(ui::DpOffset contentOffset, const ui::Density& density, MenuAnchorPosition anchorPosition,
                                                               kiln::Function<void(IntRect, IntRect)> onPositionCalculated)
        : verticalMargin(density.RoundToPx(MenuVerticalMargin)), horizontalMargin(density.RoundToPx(MenuHorizontalMargin)),
          m_ContentOffset(contentOffset), m_Density(density), m_AnchorPosition(std::move(anchorPosition)),
          m_OnPositionCalculated(std::move(onPositionCalculated)) {}

    ui::IntOffset DropdownMenuPositionProvider::CalculatePosition(IntRect anchor, IntSize window, LayoutDirection dir, IntSize content) const {
        const MenuPositionScope scope{ anchor, window, content, dir };
        const std::vector<int> xs = m_AnchorPosition.xCandidates(scope), ys = m_AnchorPosition.yCandidates(scope);
        const int offsetX = m_Density.RoundToPx(m_ContentOffset.x) * (dir == LayoutDirection::Ltr ? 1 : -1);
        const int offsetY = m_Density.RoundToPx(m_ContentOffset.y);
        auto choose = [](const std::vector<int>& candidates, int offset, int menu, int space, int margin, float centerBias) {
            for (std::size_t i = 0; i < candidates.size(); ++i) {
                const int c = candidates[i] + offset;
                if (c >= margin && c + menu <= space - margin) return c;
                if (i + 1 == candidates.size()) {
                    if (menu >= space - 2 * margin) return Align(centerBias, menu, space);
                    return std::clamp(c, margin, std::max(margin, space - margin - menu));
                }
            }
            return 0;
        };
        const int x = choose(xs, offsetX, content.width, window.width, horizontalMargin, 0);
        const int y = choose(ys, offsetY, content.height, window.height, verticalMargin, 0);
        const IntRect menu{ x, y, x + content.width, y + content.height };
        m_TransformOrigin.Set(CalculateTransformOrigin(anchor, menu));
        if (m_OnPositionCalculated) m_OnPositionCalculated(anchor, menu);
        return { x, y };
    }

}
