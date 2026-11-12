#include "kiln/foundation/Overscroll.h"

#include "kiln/runtime/Composition.h"
#include "kiln/runtime/Task.h"
#include "kiln/ui/Draw.h"

#include <chrono>
#include <cmath>

namespace kiln::foundation {

    using namespace ui;

    namespace {
        std::int64_t NowMillis() {
            return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
        }
        constexpr float FlingDestretchFactor = 4;
        // EdgeEffectCompat.flingDistance: the platform spline fling's travel for a velocity.
        float FlingDistance(float density, float velocity) {
            constexpr double GravityEarth = 9.80665, InchesPerMeter = 39.37, Inflection = 0.35, Friction = 0.015;
            const double DecelerationRate = std::log(0.78) / std::log(0.9), DecelMinusOne = DecelerationRate - 1.0;
            const double physical = GravityEarth * InchesPerMeter * density * 160.0 * 0.84;
            const double l = std::log(Inflection * std::abs(velocity) / (Friction * physical));
            return float(Friction * physical * std::exp(DecelerationRate / DecelMinusOne * l));
        }
    }

    std::int64_t OverscrollEffect::Now() const { return clock ? clock() : NowMillis(); }

    float OverscrollEffect::PullTop(float dy) {
        const float consumed = m_Top.OnPullDistance(dy / m_Size.height, 0.5f, Now()) * m_Size.height;
        return Stretched(m_Top) ? dy : consumed;
    }
    float OverscrollEffect::PullBottom(float dy) {
        const float consumed = -m_Bottom.OnPullDistance(-dy / m_Size.height, 0.5f, Now()) * m_Size.height;
        return Stretched(m_Bottom) ? dy : consumed;
    }
    float OverscrollEffect::PullLeft(float dx) {
        const float consumed = m_Left.OnPullDistance(dx / m_Size.width, 0.5f, Now()) * m_Size.width;
        return Stretched(m_Left) ? dx : consumed;
    }
    float OverscrollEffect::PullRight(float dx) {
        const float consumed = -m_Right.OnPullDistance(-dx / m_Size.width, 0.5f, Now()) * m_Size.width;
        return Stretched(m_Right) ? dx : consumed;
    }

    void OverscrollEffect::ReleaseAll() {
        const std::int64_t now = Now();
        bool animating = false;
        for (EdgeEffect* e : { &m_Top, &m_Bottom, &m_Left, &m_Right }) { e->OnRelease(now); animating = animating || !e->IsFinished(); }
        if (animating) Invalidate();
    }

    std::shared_ptr<OverscrollEffect> OverscrollEffect::WithoutVisualEffect(std::shared_ptr<OverscrollEffect> inner) {
        auto w = std::make_shared<OverscrollEffect>();
        w->m_Inner = std::move(inner);
        w->m_DrawsVisual = false;
        return w;
    }
    std::shared_ptr<OverscrollEffect> OverscrollEffect::WithoutEventHandling(std::shared_ptr<OverscrollEffect> inner) {
        auto w = std::make_shared<OverscrollEffect>();
        w->m_Inner = std::move(inner);
        w->m_HandlesEvents = false;
        return w;
    }
    OverscrollEffect* OverscrollEffect::Visual() {
        if (!m_Inner) return this;
        return m_DrawsVisual ? m_Inner->Visual() : nullptr;
    }

    bool OverscrollEffect::IsInProgress() const {
        if (m_Inner) return m_Inner->IsInProgress();
        return Stretched(m_Top) || Stretched(m_Bottom) || Stretched(m_Left) || Stretched(m_Right);
    }

    Offset OverscrollEffect::ApplyToScroll(Offset delta, NestedScrollSource source, const std::function<Offset(Offset)>& perform) {
        if (m_Inner) return m_HandlesEvents ? m_Inner->ApplyToScroll(delta, source, perform) : perform(delta);
        if (m_Size.width <= 0 || m_Size.height <= 0) return perform(delta);
        if (!m_Cycle) {
            // A new scroll catches an active stretch where it is.
            if (Stretched(m_Left)) PullLeft(0);
            if (Stretched(m_Right)) PullRight(0);
            if (Stretched(m_Top)) PullTop(0);
            if (Stretched(m_Bottom)) PullBottom(0);
            m_Cycle = true;
        }
        const float mult = source == NestedScrollSource::SideEffect ? FlingDestretchFactor : 1.0f;
        const Offset d{ delta.x * mult, delta.y * mult };
        float cy = 0, cx = 0;
        if (delta.y != 0) {
            if (Stretched(m_Top) && delta.y < 0) {
                const float c = PullTop(d.y);
                if (!Stretched(m_Top)) m_Top.Finish();
                cy = c == d.y ? delta.y : c / mult;
            } else if (Stretched(m_Bottom) && delta.y > 0) {
                const float c = PullBottom(d.y);
                if (!Stretched(m_Bottom)) m_Bottom.Finish();
                cy = c == d.y ? delta.y : c / mult;
            }
        }
        if (delta.x != 0) {
            if (Stretched(m_Left) && delta.x < 0) {
                const float c = PullLeft(d.x);
                if (!Stretched(m_Left)) m_Left.Finish();
                cx = c == d.x ? delta.x : c / mult;
            } else if (Stretched(m_Right) && delta.x > 0) {
                const float c = PullRight(d.x);
                if (!Stretched(m_Right)) m_Right.Finish();
                cx = c == d.x ? delta.x : c / mult;
            }
        }
        const Offset consumed{ cx, cy };
        if (!(consumed == Offset{})) Invalidate();
        const Offset left = delta - consumed;
        const Offset byScroll = perform(left);
        const Offset leftOver = left - byScroll;
        if (!(left == Offset{}) && !(byScroll == Offset{}) && IsInProgress()) ReleaseAll();
        bool invalidate = false;
        if (source == NestedScrollSource::UserInput) {
            if (leftOver.x > 0.5f) { PullLeft(leftOver.x); invalidate = true; }
            else if (leftOver.x < -0.5f) { PullRight(leftOver.x); invalidate = true; }
            if (leftOver.y > 0.5f) { PullTop(leftOver.y); invalidate = true; }
            else if (leftOver.y < -0.5f) { PullBottom(leftOver.y); invalidate = true; }
        }
        if (invalidate) Invalidate();
        return consumed + byScroll;
    }

    Offset OverscrollEffect::ConsumePreFling(Offset v) {
        if (m_Inner) return m_HandlesEvents ? m_Inner->ConsumePreFling(v) : Offset{};
        if (m_Size.width <= 0 || m_Size.height <= 0) return {};
        const std::int64_t now = Now();
        auto absorbToRelax = [&](EdgeEffect& e, float velocity, float length) {
            if (FlingDistance(density, velocity) <= e.Distance() * length) { e.OnAbsorb(int(std::lround(velocity)), now); return velocity; }
            return 0.0f;
        };
        float cx = 0, cy = 0;
        if (Stretched(m_Left) && v.x < 0) cx = absorbToRelax(m_Left, v.x, m_Size.width);
        else if (Stretched(m_Right) && v.x > 0) cx = -absorbToRelax(m_Right, -v.x, m_Size.width);
        if (Stretched(m_Top) && v.y < 0) cy = absorbToRelax(m_Top, v.y, m_Size.height);
        else if (Stretched(m_Bottom) && v.y > 0) cy = -absorbToRelax(m_Bottom, -v.y, m_Size.height);
        if (cx != 0 || cy != 0) Invalidate();
        return { cx, cy };
    }

    void OverscrollEffect::AbsorbPostFling(Offset left) {
        if (m_Inner) { if (m_HandlesEvents) m_Inner->AbsorbPostFling(left); return; }
        m_Cycle = false;
        const std::int64_t now = Now();
        if (left.x > 0) m_Left.OnAbsorb(int(std::lround(left.x)), now);
        else if (left.x < 0) m_Right.OnAbsorb(int(std::lround(-left.x)), now);
        if (left.y > 0) m_Top.OnAbsorb(int(std::lround(left.y)), now);
        else if (left.y < 0) m_Bottom.OnAbsorb(int(std::lround(-left.y)), now);
        ReleaseAll();
    }

    void OverscrollEffect::UpdateSize(Size s) {
        const bool initial = m_Size.width == 0 && m_Size.height == 0;
        const bool changed = !(s == m_Size);
        m_Size = s;
        if (changed) {
            m_Top.SetSize(s.width, s.height); m_Bottom.SetSize(s.width, s.height);
            m_Left.SetSize(s.height, s.width); m_Right.SetSize(s.height, s.width);
        }
        if (!initial && changed) ReleaseAll();
    }

    bool OverscrollEffect::Advance(std::int64_t now, float& sx, float& sy) {
        bool more = false;
        for (EdgeEffect* e : { &m_Top, &m_Bottom, &m_Left, &m_Right }) more = e->Update(now) || more;
        // Top/left stretch towards +, bottom/right towards - (the platform draws them rotated).
        sy = m_Top.StretchAmount() * (m_Top.Distance() != 0) - m_Bottom.StretchAmount() * (m_Bottom.Distance() != 0);
        sx = m_Left.StretchAmount() * (m_Left.Distance() != 0) - m_Right.StretchAmount() * (m_Right.Distance() != 0);
        return more;
    }

    namespace {
        struct OverscrollParams {
            std::shared_ptr<OverscrollEffect> effect;
            bool operator==(const OverscrollParams& o) const { return effect == o.effect; }
        };
        struct OverscrollNode final : ModifierNode, DrawModifier {
            OverscrollParams params;
            explicit OverscrollNode(OverscrollParams p) : params(std::move(p)) {}
            void OnParamsChanged() { InvalidateDraw(); }
            DrawModifier* AsDraw() override { return this; }
            void Draw(ContentDrawScope& s) override {
                auto& e = *params.effect;
                (void)e.RedrawSignal();   // read: redraws follow pulls and the recede spring
                e.density = s.density;
                e.UpdateSize(s.GetSize());
                float sx = 0, sy = 0;
                if (e.Advance(e.clock ? e.clock() : NowMillis(), sx, sy)) {
                    auto weak = std::weak_ptr<OverscrollEffect>(params.effect);
                    rt::Dispatcher::Main().Post([weak] { if (auto p = weak.lock()) p->Invalidate(); });
                }
                if (sx == 0 && sy == 0) { s.DrawContent(); return; }
                const Size size = s.GetSize();
                s.BeginLayer({ 0, 0, size.width, size.height }, 1);
                s.DrawContent();
                s.EndLayer(sx, sy);
            }
        };
    }

    ui::Modifier Overscroll(ui::Modifier m, std::shared_ptr<OverscrollEffect> effect) {
        if (!effect) return m;
        // A wrapper draws its inner effect (shared ownership keeps it alive), or nothing.
        OverscrollEffect* visual = effect->Visual();
        if (!visual) return m;
        if (visual != effect.get()) effect = std::shared_ptr<OverscrollEffect>(effect, visual);
        return m.With<OverscrollNode>(OverscrollParams{ std::move(effect) });
    }

    std::shared_ptr<OverscrollEffect> RememberOverscrollEffect(rt::Key key) {
        auto& c = *rt::Composition::Current();
        const auto& factory = c.Read(LocalOverscrollFactory);
        return c.Remember<std::shared_ptr<OverscrollEffect>>([&] { return factory ? factory->CreateOverscrollEffect() : nullptr; },
                                                             rt::Keys(reinterpret_cast<std::uintptr_t>(factory.get())), key);
    }

}
