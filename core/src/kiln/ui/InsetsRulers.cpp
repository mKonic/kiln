#include "kiln/ui/InsetsRulers.h"

#include <array>

namespace kiln::ui {

    namespace {
        constexpr std::size_t kTypes = std::size_t(InsetsType::Count);
        // Animation source and target rulers per type (none for the waterfall).
        const std::array<RectRulers, kTypes>& Sources() { static const std::array<RectRulers, kTypes> r{}; return r; }
        const std::array<RectRulers, kTypes>& Targets() { static const std::array<RectRulers, kTypes> r{}; return r; }
        const std::array<RectRulers, 4>& CutoutRulers() { static const std::array<RectRulers, 4> r{}; return r; }
        const RectRulers& NeverProvided() { static const RectRulers r; return r; }

        void ProvideInsets(RulerScope& s, const RectRulers& r, const InsetsValue& v, int width, int height) {
            s.Provides(r.left, float(v.left));
            s.Provides(r.top, float(v.top));
            s.Provides(r.right, float(width - v.right));
            s.Provides(r.bottom, float(height - v.bottom));
        }
    }

    WindowInsetsRulers::WindowInsetsRulers(InsetsType type) : m_Type(type) {}
    WindowInsetsRulers::WindowInsetsRulers(std::vector<const WindowInsetsRulers*> parts) : m_Parts(std::move(parts)) {
        std::vector<RectRulers> current, maximum;
        for (const auto* p : m_Parts) { current.push_back(p->m_Current); maximum.push_back(p->m_Maximum); }
        m_Current = RectRulers::InnermostOf(current);
        m_Maximum = RectRulers::InnermostOf(maximum);
    }

#define KILN_INSETS_RULERS(Name, Type) \
    const WindowInsetsRulers& WindowInsetsRulers::Name() { static const WindowInsetsRulers r(InsetsType::Type); return r; }
    KILN_INSETS_RULERS(CaptionBar, CaptionBar)
    KILN_INSETS_RULERS(DisplayCutout, DisplayCutout)
    KILN_INSETS_RULERS(Ime, Ime)
    KILN_INSETS_RULERS(MandatorySystemGestures, MandatorySystemGestures)
    KILN_INSETS_RULERS(NavigationBars, NavigationBars)
    KILN_INSETS_RULERS(StatusBars, StatusBars)
    KILN_INSETS_RULERS(SystemGestures, SystemGestures)
    KILN_INSETS_RULERS(TappableElement, TappableElement)
    KILN_INSETS_RULERS(Waterfall, Waterfall)
#undef KILN_INSETS_RULERS

    const WindowInsetsRulers& WindowInsetsRulers::SystemBars() {
        static const WindowInsetsRulers r({ &StatusBars(), &NavigationBars(), &CaptionBar() });
        return r;
    }
    const WindowInsetsRulers& WindowInsetsRulers::SafeDrawing() {
        static const WindowInsetsRulers r({ &StatusBars(), &NavigationBars(), &CaptionBar(), &DisplayCutout(), &Ime(), &TappableElement() });
        return r;
    }
    const WindowInsetsRulers& WindowInsetsRulers::SafeGestures() {
        static const WindowInsetsRulers r({ &MandatorySystemGestures(), &SystemGestures(), &TappableElement(), &Waterfall() });
        return r;
    }
    const WindowInsetsRulers& WindowInsetsRulers::SafeContent() {
        static const WindowInsetsRulers r({ &StatusBars(), &NavigationBars(), &CaptionBar(), &Ime(), &SystemGestures(), &MandatorySystemGestures(),
                                            &TappableElement(), &DisplayCutout(), &Waterfall() });
        return r;
    }
    WindowInsetsRulers WindowInsetsRulers::InnermostOf(std::vector<const WindowInsetsRulers*> rulers) { return WindowInsetsRulers(std::move(rulers)); }

    WindowInsetsAnimationInfo WindowInsetsRulers::GetAnimation(PlacementScope& scope) const {
        WindowInsetsAnimationInfo info;
        info.source = info.target = NeverProvided();
        const WindowInsetsHolder* h = scope.Insets();
        if (!m_Parts.empty()) {
            // InnermostAnimationProperties: visible / animating when any part is.
            info.isVisible = false;
            for (const auto* p : m_Parts) {
                const auto a = p->GetAnimation(scope);
                info.isVisible = info.isVisible || a.isVisible;
                info.isAnimating = info.isAnimating || a.isAnimating;
            }
            return info;
        }
        if (m_Type == InsetsType::Waterfall) {
            info.isVisible = h && h->Get(InsetsType::Waterfall) != InsetsValue{};
            return info;
        }
        info.source = Sources()[std::size_t(m_Type)];
        info.target = Targets()[std::size_t(m_Type)];
        if (!h) { info.isVisible = false; return info; }
        info.isVisible = h->IsVisible(m_Type);
        if (const auto& a = h->Animation(m_Type)) {
            info.isAnimating = true;
            info.fraction = a->fraction;
            info.durationMillis = a->durationMillis;
            info.alpha = a->alpha;
        }
        return info;
    }

    std::vector<RectRulers> GetDisplayCutoutBounds(PlacementScope& scope) {
        std::vector<RectRulers> out;
        if (const WindowInsetsHolder* h = scope.Insets())
            for (std::size_t i = 0; i < h->CutoutRects().size() && i < CutoutRulers().size(); ++i) out.push_back(CutoutRulers()[i]);
        return out;
    }

    void ProvideWindowInsetsRulers(RulerScope& s, const WindowInsetsHolder& h, int width, int height) {
        if (!h.HasInsets()) return;   // nothing delivered yet: every ruler keeps its default
        for (std::size_t i = 0; i < kTypes; ++i) {
            const auto type = InsetsType(i);
            const WindowInsetsRulers* r = nullptr;
            switch (type) {
                case InsetsType::StatusBars: r = &WindowInsetsRulers::StatusBars(); break;
                case InsetsType::NavigationBars: r = &WindowInsetsRulers::NavigationBars(); break;
                case InsetsType::CaptionBar: r = &WindowInsetsRulers::CaptionBar(); break;
                case InsetsType::Ime: r = &WindowInsetsRulers::Ime(); break;
                case InsetsType::DisplayCutout: r = &WindowInsetsRulers::DisplayCutout(); break;
                case InsetsType::SystemGestures: r = &WindowInsetsRulers::SystemGestures(); break;
                case InsetsType::MandatorySystemGestures: r = &WindowInsetsRulers::MandatorySystemGestures(); break;
                case InsetsType::TappableElement: r = &WindowInsetsRulers::TappableElement(); break;
                case InsetsType::Waterfall: r = &WindowInsetsRulers::Waterfall(); break;
                default: break;
            }
            if (!r) continue;
            ProvideInsets(s, r->Current(), h.Get(type), width, height);
            if (type == InsetsType::Waterfall) {
                ProvideInsets(s, r->Maximum(), h.Get(type), width, height);
                continue;
            }
            if (type != InsetsType::Ime) ProvideInsets(s, r->Maximum(), h.GetIgnoringVisibility(type), width, height);
            if (const auto& a = h.Animation(type)) {
                ProvideInsets(s, Sources()[i], a->source, width, height);
                ProvideInsets(s, Targets()[i], a->target, width, height);
            }
        }
        const auto& cutouts = h.CutoutRects();
        for (std::size_t i = 0; i < cutouts.size() && i < CutoutRulers().size(); ++i) {
            const auto& c = cutouts[i];
            s.Provides(CutoutRulers()[i].left, float(c.left));
            s.Provides(CutoutRulers()[i].top, float(c.top));
            s.Provides(CutoutRulers()[i].right, float(c.right));
            s.Provides(CutoutRulers()[i].bottom, float(c.bottom));
        }
    }

}
