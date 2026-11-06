#include "kiln/foundation/AnimatedVisibility.h"

#include "kiln/foundation/Compose.h"

#include <algorithm>

namespace kiln::foundation {

    using namespace ui;

    namespace {
        EnterExitParts Combine(const EnterExitParts& a, const EnterExitParts& b) {
            EnterExitParts r = a;
            if (!r.fade) r.fade = b.fade;
            if (!r.scale) r.scale = b.scale;
            if (!r.slide) r.slide = b.slide;
            if (!r.changeSize) r.changeSize = b.changeSize;
            return r;
        }
        // Size and offset animations use a 1 px visibility threshold (EnterExitTransition.kt:1615,
        // :1822); here they run as a 0..1 progress, so the threshold is relative.
        anim::SpecPtr SizeSpring() { return anim::Spring(1.0f, anim::spring::StiffnessMediumLow, 0.001f); }
    }

    EnterTransition EnterTransition::operator+(const EnterTransition& o) const { return { Combine(parts, o.parts) }; }
    ExitTransition ExitTransition::operator+(const ExitTransition& o) const { return { Combine(parts, o.parts) }; }

    anim::SpecPtr DefaultEnterExitSpring() { return anim::Spring(1.0f, anim::spring::StiffnessMediumLow); }

    EnterTransition FadeIn(anim::SpecPtr s, float a) { EnterTransition t; t.parts.fade = { { a, std::move(s) } }; return t; }
    ExitTransition FadeOut(anim::SpecPtr s, float a) { ExitTransition t; t.parts.fade = { { a, std::move(s) } }; return t; }
    EnterTransition ScaleIn(anim::SpecPtr s, float sc, float ox, float oy) { EnterTransition t; t.parts.scale = EnterExitParts::Scale{ sc, ox, oy, std::move(s) }; return t; }
    ExitTransition ScaleOut(anim::SpecPtr s, float sc, float ox, float oy) { ExitTransition t; t.parts.scale = EnterExitParts::Scale{ sc, ox, oy, std::move(s) }; return t; }
    EnterTransition SlideIn(std::function<IntOffset(IntSize)> f, anim::SpecPtr s) { EnterTransition t; t.parts.slide = EnterExitParts::Slide{ std::move(f), s ? s : SizeSpring() }; return t; }
    ExitTransition SlideOut(std::function<IntOffset(IntSize)> f, anim::SpecPtr s) { ExitTransition t; t.parts.slide = EnterExitParts::Slide{ std::move(f), s ? s : SizeSpring() }; return t; }
    EnterTransition SlideInVertically(std::function<int(int)> f, anim::SpecPtr s) { return SlideIn([f](IntSize sz) { return IntOffset{ 0, f(sz.height) }; }, std::move(s)); }
    ExitTransition SlideOutVertically(std::function<int(int)> f, anim::SpecPtr s) { return SlideOut([f](IntSize sz) { return IntOffset{ 0, f(sz.height) }; }, std::move(s)); }
    EnterTransition SlideInHorizontally(std::function<int(int)> f, anim::SpecPtr s) { return SlideIn([f](IntSize sz) { return IntOffset{ f(sz.width), 0 }; }, std::move(s)); }
    ExitTransition SlideOutHorizontally(std::function<int(int)> f, anim::SpecPtr s) { return SlideOut([f](IntSize sz) { return IntOffset{ f(sz.width), 0 }; }, std::move(s)); }
    EnterTransition ExpandIn(anim::SpecPtr s, Alignment a, bool clip, std::function<IntSize(IntSize)> f) {
        EnterTransition t; t.parts.changeSize = EnterExitParts::ChangeSize{ a, std::move(f), clip, s }; return t;
    }
    ExitTransition ShrinkOut(anim::SpecPtr s, Alignment a, bool clip, std::function<IntSize(IntSize)> f) {
        ExitTransition t; t.parts.changeSize = EnterExitParts::ChangeSize{ a, std::move(f), clip, s }; return t;
    }
    EnterTransition ExpandVertically(anim::SpecPtr s, AlignmentV a, bool clip, std::function<int(int)> f) {
        return ExpandIn(std::move(s), Alignment{ 0, a.bias }, clip, [f](IntSize sz) { return IntSize{ sz.width, f(sz.height) }; });
    }
    ExitTransition ShrinkVertically(anim::SpecPtr s, AlignmentV a, bool clip, std::function<int(int)> f) {
        return ShrinkOut(std::move(s), Alignment{ 0, a.bias }, clip, [f](IntSize sz) { return IntSize{ sz.width, f(sz.height) }; });
    }
    EnterTransition ExpandHorizontally(anim::SpecPtr s, AlignmentH a, bool clip, std::function<int(int)> f) {
        return ExpandIn(std::move(s), Alignment{ a.bias, 0 }, clip, [f](IntSize sz) { return IntSize{ f(sz.width), sz.height }; });
    }
    ExitTransition ShrinkHorizontally(anim::SpecPtr s, AlignmentH a, bool clip, std::function<int(int)> f) {
        return ShrinkOut(std::move(s), Alignment{ a.bias, 0 }, clip, [f](IntSize sz) { return IntSize{ f(sz.width), sz.height }; });
    }

    namespace {
        // The enter/exit layout: content measured at full size, reported at the animated size
        // (aligned by the expand alignment), shifted by the slide.
        struct EnterExitParams {
            std::function<IntSize(IntSize)> sizeAt;     // null: full size
            std::function<IntOffset(IntSize)> slideAt;  // null: none
            Alignment alignment;
            bool operator==(const EnterExitParams&) const { return false; }   // new lambdas each frame
        };
        struct EnterExitNode final : ModifierNode, LayoutModifier {
            EnterExitParams params;
            explicit EnterExitNode(EnterExitParams p) : params(std::move(p)) {}
            void OnParamsChanged() { InvalidateMeasure(); }
            LayoutModifier* AsLayout() override { return this; }
            MeasureResult Measure(MeasureScope& s, Measurable& m, const Constraints& c) override {
                Placeable& p = m.Measure(c);
                const IntSize full{ p.Width(), p.Height() };
                const IntSize size = params.sizeAt ? c.Constrain(params.sizeAt(full)) : full;
                const IntOffset align = params.alignment.Align(full, size, s.Direction());
                const IntOffset slide = params.slideAt ? params.slideAt(full) : IntOffset{};
                return s.Layout(size.width, size.height, [&p, align, slide](PlacementScope& place) {
                    place.Place(p, align.x + slide.x, align.y + slide.y);
                });
            }
        };
        Modifier EnterExitLayout(Modifier m, EnterExitParams p) { return m.With<EnterExitNode>(std::move(p)); }
        IntSize Lerp(IntSize a, IntSize b, float t) {
            return { int(std::lround(float(a.width) + float(b.width - a.width) * t)), int(std::lround(float(a.height) + float(b.height - a.height) * t)) };
        }
    }

    void AnimatedVisibility(bool visible, const Modifier& modifier, std::function<void()> content,
                            EnterTransition enter, ExitTransition exit, rt::Key key) {
        auto& t = UpdateTransition(visible, key);
        const bool exiting = !t.TargetState();
        auto specFor = [&](auto part) -> anim::SpecPtr {
            // Entering uses the enter part's spec, exiting the exit part's.
            if (!exiting) { auto& e = enter.parts.*part; return e ? e->spec : DefaultEnterExitSpring(); }
            auto& x = exit.parts.*part; return x ? x->spec : DefaultEnterExitSpring();
        };
        const float alpha = AnimateFloat<bool>(t, [&](bool s) {
            if (s) return 1.0f;
            if (exiting) return exit.parts.fade ? exit.parts.fade->first : 1.0f;
            return enter.parts.fade ? enter.parts.fade->first : 1.0f;
        }, [&](const Transition<bool>::Segment&) {
            const auto& f = exiting ? exit.parts.fade : enter.parts.fade;
            return f ? f->second : DefaultEnterExitSpring();
        }, rt::Key("alpha"));
        const float scale = AnimateFloat<bool>(t, [&](bool s) {
            if (s) return 1.0f;
            if (exiting) return exit.parts.scale ? exit.parts.scale->scale : 1.0f;
            return enter.parts.scale ? enter.parts.scale->scale : 1.0f;
        }, [&](const Transition<bool>::Segment&) { return specFor(&EnterExitParts::scale); }, rt::Key("scale"));
        const float slideP = AnimateFloat<bool>(t, [](bool s) { return s ? 1.0f : 0.0f; },
                                                [&](const Transition<bool>::Segment&) { return specFor(&EnterExitParts::slide); }, rt::Key("slide"));
        const float sizeP = AnimateFloat<bool>(t, [](bool s) { return s ? 1.0f : 0.0f; },
                                               [&](const Transition<bool>::Segment&) { return specFor(&EnterExitParts::changeSize); }, rt::Key("size"));

        if (!(t.CurrentState() || t.TargetState() || t.IsRunning())) return;   // gone: not composed

        const auto& change = exiting ? exit.parts.changeSize : enter.parts.changeSize;
        const auto& slide = exiting ? exit.parts.slide : enter.parts.slide;
        const auto& sc = exiting ? exit.parts.scale : enter.parts.scale;
        GraphicsLayerParams layer;
        layer.alpha = alpha;
        layer.scaleX = layer.scaleY = scale;
        if (sc) { layer.originX = sc->originX; layer.originY = sc->originY; }
        layer.clip = change && change->clip;
        EnterExitParams p;
        p.alignment = change ? change->alignment : Alignment::TopStart();
        if (change) { auto f = change->size; p.sizeAt = [f, sizeP](IntSize full) { return Lerp(f(full), full, sizeP); }; }
        if (slide) {
            auto f = slide->offset;
            p.slideAt = [f, slideP](IntSize full) {
                const IntOffset o = f(full);
                return IntOffset{ int(std::lround(float(o.x) * (1 - slideP))), int(std::lround(float(o.y) * (1 - slideP))) };
            };
        }
        Box(EnterExitLayout(GraphicsLayer(modifier, layer), std::move(p)), Alignment::TopStart(), false, std::move(content), key);
    }

    void Crossfade(rt::Key target, const Modifier& modifier, std::function<void(rt::Key)> content, anim::SpecPtr spec, rt::Key key) {
        auto& c = *rt::Composition::Current();
        auto& t = UpdateTransition<std::uint64_t>(target.value, key);
        auto& items = c.Remember<std::vector<std::uint64_t>>([&] { return std::vector<std::uint64_t>{ target.value }; }, {}, key);
        if (std::find(items.begin(), items.end(), target.value) == items.end()) items.push_back(target.value);
        // Finished: only the target remains.
        if (!t.IsRunning() && t.CurrentState() == t.TargetState()) items = { target.value };
        Box(modifier, Alignment::TopStart(), false, [&, spec] {
            for (std::uint64_t k : items) {
                c.Child([&, k, spec] {
                    const float a = AnimateFloat<std::uint64_t>(t, [k](std::uint64_t s) { return s == k ? 1.0f : 0.0f; },
                                                                [spec](const Transition<std::uint64_t>::Segment&) { return spec; });
                    rt::Key itemKey = rt::Key::Positional();
                    itemKey.value = k;
                    Box(Alpha(Modifier{}, a), Alignment::TopStart(), false, [&content, itemKey] { content(itemKey); });
                }, rt::Key(std::int64_t(k)));
            }
        }, key);
    }

}
