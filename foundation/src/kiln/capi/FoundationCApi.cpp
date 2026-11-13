// The C ABI (include/kiln/kiln.h) over the runtime and foundation.

#include "kiln/capi/CApiSupport.h"
#include <kiln/Function.h>

#include "kiln/App.h"
#include "kiln/Assets.h"
#include "kiln/foundation/Animate.h"
#include "kiln/foundation/AnimatedVisibility.h"
#include "kiln/foundation/Border.h"
#include "kiln/foundation/Compose.h"
#include "kiln/foundation/Gestures.h"
#include "kiln/foundation/Image.h"
#include "kiln/foundation/Insets.h"
#include "kiln/foundation/Layouts.h"
#include "kiln/foundation/Lazy.h"
#include "kiln/foundation/Pager.h"
#include "kiln/foundation/Scroll.h"
#include "kiln/foundation/Semantics.h"
#include "kiln/foundation/Subcompose.h"
#include "kiln/foundation/text/BasicText.h"
#include "kiln/foundation/text/BasicTextField.h"
#include "kiln/runtime/CoroutineScope.h"
#include "kiln/runtime/Saveable.h"

#include <climits>
#include <cstdio>
#include <deque>

#ifdef __ANDROID__
#include <android/log.h>
#endif

using namespace kiln;
using namespace kiln::capi;
namespace fd = kiln::foundation;

struct kiln_lazy_scope { fd::LazyListScope* scope; };

// A LaunchedEffect's next step.
struct kiln_task {
    enum class Next { None, Delay, Frame } next = Next::None;
    std::int64_t millis = 0;
    std::shared_ptr<Owned<kiln_fn_task>> step;
    std::shared_ptr<Owned<kiln_fn_task_frame>> frameStep;
};

namespace kiln::capi {

    void LogError(const char* function, const char* what) {
#ifdef __ANDROID__
        __android_log_print(ANDROID_LOG_ERROR, "kiln", "%s: %s", function, what);
#else
        std::fprintf(stderr, "kiln: %s: %s\n", function, what);
#endif
    }

    namespace {
        struct Arena {
            int depth = 0;
            std::deque<kiln_modifier> modifiers;
            std::vector<kiln_shape*> shapes;
            std::vector<kiln_text_style*> styles;
            std::vector<kiln_anim_spec*> specs;
            std::vector<kiln_rounded_polygon*> polygons;
            std::vector<kiln_window_insets*> insets;
            std::deque<std::string> strings;
            std::vector<std::shared_ptr<void>> values;
        };
        thread_local Arena t_Arena;
        thread_local std::string t_ReturnedString;
    }

    CallbackScope::CallbackScope() { ++t_Arena.depth; }
    CallbackScope::~CallbackScope() {
        if (--t_Arena.depth == 0) {
            t_Arena.modifiers.clear();
            for (auto* h : t_Arena.shapes) Release(h);
            for (auto* h : t_Arena.styles) Release(h);
            for (auto* h : t_Arena.specs) Release(h);
            t_Arena.shapes.clear();
            t_Arena.styles.clear();
            t_Arena.specs.clear();
            for (auto* h : t_Arena.polygons) Release(h);
            for (auto* h : t_Arena.insets) Release(h);
            t_Arena.polygons.clear();
            t_Arena.insets.clear();
            t_Arena.strings.clear();
            t_Arena.values.clear();
        }
    }

    kiln_shape* TransientShape(fd::ShapePtr shape) {
        if (!shape) return nullptr;
        auto* h = NewShape(std::move(shape));
        t_Arena.shapes.push_back(h);
        return h;
    }
    kiln_text_style* TransientTextStyle(text::TextStyle style) {
        auto* h = NewTextStyle(std::move(style));
        t_Arena.styles.push_back(h);
        return h;
    }
    kiln_anim_spec* TransientSpec(anim::SpecPtr spec) {
        if (!spec) return nullptr;
        auto* h = new kiln_anim_spec;
        h->spec = std::move(spec);
        t_Arena.specs.push_back(h);
        return h;
    }

    fd::Arrangement ToArrangement(const kiln_arrangement& a, bool vertical) {
        switch (a.kind) {
        case KILN_ARRANGE_END: return vertical ? fd::Arrangement::Bottom() : fd::Arrangement::End();
        case KILN_ARRANGE_CENTER: return fd::Arrangement::Center();
        case KILN_ARRANGE_SPACE_EVENLY: return fd::Arrangement::SpaceEvenly();
        case KILN_ARRANGE_SPACE_BETWEEN: return fd::Arrangement::SpaceBetween();
        case KILN_ARRANGE_SPACE_AROUND: return fd::Arrangement::SpaceAround();
        case KILN_ARRANGE_SPACED_BY: {
            const float bias = std::isnan(a.align_bias) ? -1.0f : a.align_bias;
            return vertical ? fd::Arrangement::SpacedBy(ui::Dp(a.spacing), ui::AlignmentV{ bias })
                            : fd::Arrangement::SpacedBy(ui::Dp(a.spacing), ui::AlignmentH{ bias });
        }
        case KILN_ARRANGE_START:
        default: return vertical ? fd::Arrangement::Top() : fd::Arrangement::Start();
        }
    }
    fd::PaddingValues ToPadding(const kiln_padding& p) { return { ui::Dp(p.start), ui::Dp(p.top), ui::Dp(p.end), ui::Dp(p.bottom) }; }
    kiln_padding FromPadding(const fd::PaddingValues& p) { return { p.start.value, p.top.value, p.end.value, p.bottom.value }; }

    const char* TransientString(std::string s) {
        t_Arena.strings.push_back(std::move(s));
        return t_Arena.strings.back().c_str();
    }
    void KeepTransient(std::shared_ptr<void> value) { t_Arena.values.push_back(std::move(value)); }

    kiln_rounded_polygon* TransientPolygon(gfx::shapes::RoundedPolygon polygon) {
        auto* h = new kiln_rounded_polygon{ {}, std::move(polygon) };
        h->refs = 1;
        t_Arena.polygons.push_back(h);
        return h;
    }
    kiln_window_insets* TransientInsets(fd::WindowInsets insets) {
        auto* h = new kiln_window_insets;
        h->insets = std::move(insets);
        t_Arena.insets.push_back(h);
        return h;
    }
    kiln_interaction_source* TransientSource(std::shared_ptr<fd::MutableInteractionSource> source) {
        if (!source) return nullptr;
        auto* h = NewTransient<kiln_interaction_source>();
        h->source = std::move(source);
        return h;
    }
    kiln_arrangement FromArrangement(const fd::Arrangement& a) {
        using K = fd::Arrangement::Kind;
        switch (a.kind) {
        case K::End: case K::AbsRight: return { KILN_ARRANGE_END, 0, NAN };
        case K::Center: case K::AbsCenter: return { KILN_ARRANGE_CENTER, 0, NAN };
        case K::SpaceEvenly: case K::AbsSpaceEvenly: return { KILN_ARRANGE_SPACE_EVENLY, 0, NAN };
        case K::SpaceBetween: case K::AbsSpaceBetween: return { KILN_ARRANGE_SPACE_BETWEEN, 0, NAN };
        case K::SpaceAround: case K::AbsSpaceAround: return { KILN_ARRANGE_SPACE_AROUND, 0, NAN };
        case K::Spaced: return { KILN_ARRANGE_SPACED_BY, a.space.value, a.alignBias.value_or(NAN) };
        default: return { KILN_ARRANGE_START, 0, NAN };
        }
    }
    kiln_border_stroke FromBorderStroke(const fd::BorderStroke& b) {
        // Solid strokes only; a gradient's first colour stands in.
        return { b.width.value, b.brush.colors.empty() ? KILN_COLOR_UNSPECIFIED : FromColor(b.brush.colors.front()) };
    }

    rt::Composition& CurrentComposition() {
        rt::Composition* c = rt::Composition::Current();
        if (!c) throw std::logic_error("a composable call outside content");
        return *c;
    }

    void InGroup(kiln_key key, const kiln::Function<void()>& body) {
        if (rt::Composition* c = rt::Composition::Current()) c->Section(body, ToKey(key));
        else body();
    }

    void LaunchIn(const std::shared_ptr<rt::CoroutineScope>& scope, kiln::Function<rt::Task<>()> task, kiln_fn done) {
        auto onDone = Fn(done);
        struct Finally {
            kiln::Function<void()> fn;
            ~Finally() { if (fn) fn(); }
        };
        scope->Launch([](kiln::Function<rt::Task<>()> task, kiln::Function<void()> onDone) -> rt::Task<> {
            Finally f{ std::move(onDone) };
            co_await task();
        }(std::move(task), std::move(onDone)));
    }



    kiln_modifier* NewModifier(ui::Modifier m) {
        t_Arena.modifiers.push_back({ std::move(m) });
        return &t_Arena.modifiers.back();
    }

    const char* ReturnString(std::string s) {
        t_ReturnedString = std::move(s);
        return t_ReturnedString.c_str();
    }

    kiln::Function<void()> Fn(kiln_fn f) {
        auto o = Own(f);
        if (!o) return {};
        return [o] { CallbackScope s; o->f.call(o->f.user); };
    }
    kiln::Function<void(bool)> Fn(kiln_fn_bool f) {
        auto o = Own(f);
        if (!o) return {};
        return [o](bool v) { CallbackScope s; o->f.call(o->f.user, v); };
    }
    kiln::Function<void(int)> Fn(kiln_fn_int f) {
        auto o = Own(f);
        if (!o) return {};
        return [o](int v) { CallbackScope s; o->f.call(o->f.user, v); };
    }
    kiln::Function<void(float)> Fn(kiln_fn_float f) {
        auto o = Own(f);
        if (!o) return {};
        return [o](float v) { CallbackScope s; o->f.call(o->f.user, v); };
    }
    kiln::Function<void(const std::string&)> Fn(kiln_fn_str f) {
        auto o = Own(f);
        if (!o) return {};
        return [o](const std::string& v) { CallbackScope s; o->f.call(o->f.user, v.c_str()); };
    }

    fd::ShapePtr ToShape(const kiln_shape* s) { return s ? s->shape : nullptr; }
    const text::TextStyle* ToTextStyle(const kiln_text_style* s) { return s ? &s->style : nullptr; }
    kiln_shape* NewShape(fd::ShapePtr shape) {
        auto* s = new kiln_shape;
        s->shape = std::move(shape);
        return s;
    }
    kiln_text_style* NewTextStyle(text::TextStyle style) {
        auto* s = new kiln_text_style;
        s->style = std::move(style);
        return s;
    }

}

namespace {

    rt::Composition& Current() {
        rt::Composition* c = rt::Composition::Current();
        if (!c) throw std::logic_error("a composable call outside content");
        return *c;
    }



    rt::Task<> RunTask(std::shared_ptr<Owned<kiln_fn_task>> start) {
        kiln_task t;
        {
            CallbackScope s;
            start->f.call(start->f.user, &t);
        }
        start.reset();
        while (t.next != kiln_task::Next::None) {
            const auto next = t.next;
            t.next = kiln_task::Next::None;
            if (next == kiln_task::Next::Delay) {
                auto step = std::move(t.step);
                co_await rt::Delay(t.millis);
                CallbackScope s;
                step->f.call(step->f.user, &t);
            } else {
                auto step = std::move(t.frameStep);
                const std::int64_t time = co_await rt::NextFrame{};
                CallbackScope s;
                step->f.call(step->f.user, &t, time);
            }
        }
    }

}

#define TRY KILN_CAPI_TRY
#define CATCH(fallback) KILN_CAPI_CATCH(fallback)
#define CATCH_VOID KILN_CAPI_CATCH()

// The functions below were declared extern "C" in kiln.h.

KILN_API int kiln_abi_version(void) { return KILN_ABI_VERSION; }

// --- Content ------------------------------------------------------------------------------------

KILN_API void kiln_app_set_content(kiln_app* app, kiln_fn content) {
    TRY {
        if (!app) return;
        app->impl->SetContent(Fn(content));
    } CATCH_VOID
}

KILN_API void kiln_group(kiln_key key, kiln_fn content) {
    TRY {
        auto fn = Fn(content);
        Current().Section([fn] { if (fn) fn(); }, ToKey(key));
    } CATCH_VOID
}

// --- State --------------------------------------------------------------------------------------

namespace {
    template<typename T> struct StateOf;
    template<> struct StateOf<kiln_state_i64> { using type = rt::State<std::int64_t>; };
    template<> struct StateOf<kiln_state_f64> { using type = rt::State<double>; };
    template<> struct StateOf<kiln_state_str> { using type = rt::State<std::string>; };
    template<typename H> H* StateHandle(typename StateOf<H>::type& s) { return reinterpret_cast<H*>(&s); }
    template<typename H> typename StateOf<H>::type* Cell(H* h) { return reinterpret_cast<typename StateOf<H>::type*>(h); }
    template<typename H> const typename StateOf<H>::type* Cell(const H* h) { return reinterpret_cast<const typename StateOf<H>::type*>(h); }
}

KILN_API kiln_state_i64* kiln_remember_state_i64(kiln_key key, int64_t initial) {
    TRY { return StateHandle<kiln_state_i64>(Current().RememberState<std::int64_t>(initial, ToKey(key))); } CATCH(nullptr)
}
KILN_API kiln_state_f64* kiln_remember_state_f64(kiln_key key, double initial) {
    TRY { return StateHandle<kiln_state_f64>(Current().RememberState<double>(initial, ToKey(key))); } CATCH(nullptr)
}
KILN_API kiln_state_str* kiln_remember_state_str(kiln_key key, const char* initial) {
    TRY { return StateHandle<kiln_state_str>(Current().RememberState<std::string>(ToString(initial), ToKey(key))); } CATCH(nullptr)
}
KILN_API kiln_state_i64* kiln_remember_saveable_i64(kiln_key key, int64_t initial) {
    TRY {
        Current();
        return StateHandle<kiln_state_i64>(rt::RememberSaveableState<std::int64_t>(initial, rt::AutoSaver<std::int64_t>(), ToKey(key)));
    } CATCH(nullptr)
}
KILN_API kiln_state_f64* kiln_remember_saveable_f64(kiln_key key, double initial) {
    TRY {
        Current();
        return StateHandle<kiln_state_f64>(rt::RememberSaveableState<double>(initial, rt::AutoSaver<double>(), ToKey(key)));
    } CATCH(nullptr)
}
KILN_API kiln_state_str* kiln_remember_saveable_str(kiln_key key, const char* initial) {
    TRY {
        Current();
        return StateHandle<kiln_state_str>(rt::RememberSaveableState<std::string>(ToString(initial), rt::AutoSaver<std::string>(), ToKey(key)));
    } CATCH(nullptr)
}

KILN_API int64_t kiln_state_i64_get(const kiln_state_i64* s) { return s ? Cell(s)->Get() : 0; }
KILN_API void kiln_state_i64_set(kiln_state_i64* s, int64_t v) { TRY { if (s) Cell(s)->Set(v); } CATCH_VOID }
KILN_API double kiln_state_f64_get(const kiln_state_f64* s) { return s ? Cell(s)->Get() : 0; }
KILN_API void kiln_state_f64_set(kiln_state_f64* s, double v) { TRY { if (s) Cell(s)->Set(v); } CATCH_VOID }
KILN_API const char* kiln_state_str_get(const kiln_state_str* s) {
    TRY { return s ? ReturnString(Cell(s)->Get()) : ReturnString({}); } CATCH("")
}
KILN_API void kiln_state_str_set(kiln_state_str* s, const char* v) { TRY { if (s) Cell(s)->Set(ToString(v)); } CATCH_VOID }

KILN_API void* kiln_remember(kiln_key key, const uint64_t* keys, size_t keyCount, kiln_remember_init init) {
    TRY {
        struct Value {
            void* value = nullptr;
            void (*drop)(void*) = nullptr;
            ~Value() { if (drop && value) drop(value); }
        };
        auto& v = Current().Remember<std::shared_ptr<Value>>([&] {
            auto out = std::make_shared<Value>();
            CallbackScope s;
            out->value = init.call ? init.call(init.user) : nullptr;
            out->drop = init.drop_value;
            return out;
        }, ToKeys(keys, keyCount), ToKey(key));
        return v->value;
    } CATCH(nullptr)
}

KILN_API kiln_interaction_source* kiln_remember_interaction_source(kiln_key key) {
    TRY {
        return &Current().Remember<kiln_interaction_source>([] { return kiln_interaction_source{ std::make_shared<fd::MutableInteractionSource>() }; },
                                                             {}, ToKey(key));
    } CATCH(nullptr)
}
KILN_API bool kiln_interaction_source_is_pressed(const kiln_interaction_source* s) { return s && s->source->IsPressed(); }
KILN_API bool kiln_interaction_source_is_hovered(const kiln_interaction_source* s) { return s && s->source->IsHovered(); }
KILN_API bool kiln_interaction_source_is_focused(const kiln_interaction_source* s) { return s && s->source->IsFocused(); }
KILN_API bool kiln_interaction_source_is_dragged(const kiln_interaction_source* s) { return s && s->source->IsDragged(); }

// --- Effects ------------------------------------------------------------------------------------

KILN_API void kiln_side_effect(kiln_key, kiln_fn effect) {
    TRY {
        auto fn = Fn(effect);
        if (fn) Current().SideEffect(std::move(fn));
    } CATCH_VOID
}

KILN_API void kiln_disposable_effect(kiln_key key, const uint64_t* keys, size_t keyCount, kiln_fn_effect effect) {
    TRY {
        auto o = Own(effect);
        Current().DisposableEffect(ToKeys(keys, keyCount), [o]() -> kiln::Function<void()> {
            if (!o) return {};
            kiln_fn disposer;
            {
                CallbackScope s;
                disposer = o->f.call(o->f.user);
            }
            return Fn(disposer);
        }, ToKey(key));
    } CATCH_VOID
}

KILN_API void kiln_launched_effect(kiln_key key, const uint64_t* keys, size_t keyCount, kiln_fn_task start) {
    TRY {
        auto o = Own(start);
        Current().LaunchedEffect(ToKeys(keys, keyCount), [o]() -> rt::Task<> {
            if (!o) return [](void) -> rt::Task<> { co_return; }();
            return RunTask(o);
        }, ToKey(key));
    } CATCH_VOID
}

KILN_API void kiln_task_delay(kiln_task* task, int64_t millis, kiln_fn_task next) {
    if (!task) return;
    task->step = Own(next);
    task->frameStep.reset();
    task->millis = millis;
    task->next = task->step ? kiln_task::Next::Delay : kiln_task::Next::None;
}
KILN_API void kiln_task_next_frame(kiln_task* task, kiln_fn_task_frame next) {
    if (!task) return;
    task->frameStep = Own(next);
    task->step.reset();
    task->next = task->frameStep ? kiln_task::Next::Frame : kiln_task::Next::None;
}

// --- Shapes, text styles, specs, images ---------------------------------------------------------

KILN_API kiln_shape* kiln_shape_rectangle(void) { return NewShape(fd::RectangleShape()); }
KILN_API kiln_shape* kiln_shape_circle(void) { return NewShape(fd::CircleShape()); }
KILN_API kiln_shape* kiln_shape_rounded(float r) { return NewShape(fd::RoundedCorner(ui::Dp(r))); }
KILN_API kiln_shape* kiln_shape_rounded_percent(float p) { return NewShape(fd::RoundedCornerPercent(p)); }
KILN_API kiln_shape* kiln_shape_rounded_corners(float ts, float te, float be, float bs) {
    using fd::CornerSize;
    return NewShape(fd::RoundedCorner(CornerSize::Of(ui::Dp(ts)), CornerSize::Of(ui::Dp(te)), CornerSize::Of(ui::Dp(be)), CornerSize::Of(ui::Dp(bs))));
}
KILN_API kiln_shape* kiln_shape_cut(float s) { return NewShape(fd::CutCorner(ui::Dp(s))); }
KILN_API kiln_shape* kiln_shape_cut_corners(float ts, float te, float be, float bs) {
    using fd::CornerSize;
    return NewShape(fd::CutCorner(CornerSize::Of(ui::Dp(ts)), CornerSize::Of(ui::Dp(te)), CornerSize::Of(ui::Dp(be)), CornerSize::Of(ui::Dp(bs))));
}
KILN_API kiln_shape* kiln_shape_retain(kiln_shape* s) { return Retain(s); }
KILN_API void kiln_shape_release(kiln_shape* s) { Release(s); }

KILN_API void kiln_text_style_options_init(kiln_text_style_options* o) {
    if (!o) return;
    *o = {};
    o->size = sizeof(*o);
    o->font_size_sp = NAN;
    o->letter_spacing_sp = NAN;
    o->line_height_sp = NAN;
}
KILN_API kiln_text_style* kiln_text_style_new(const kiln_text_style_options* in) {
    TRY {
        const auto o = ReadOptions(in, kiln_text_style_options_init);
        text::TextStyle s;
        s.span.color = ToColor(o.color);
        if (!std::isnan(o.font_size_sp)) s.span.fontSize = text::Sp(o.font_size_sp);
        if (o.font_weight > 0) s.span.fontWeight = text::FontWeight{ o.font_weight };
        if (o.italic.set) s.span.fontStyle = o.italic.value ? text::FontStyle::Italic : text::FontStyle::Normal;
        if (!std::isnan(o.letter_spacing_sp)) s.span.letterSpacing = text::Sp(o.letter_spacing_sp);
        if (!std::isnan(o.line_height_sp)) s.paragraph.lineHeight = text::Sp(o.line_height_sp);
        s.paragraph.textAlign = text::TextAlign(o.text_align);
        switch (o.decoration) {
        case KILN_TEXT_DECORATION_NONE: s.span.textDecoration = text::TextDecoration::None; break;
        case KILN_TEXT_DECORATION_UNDERLINE: s.span.textDecoration = text::TextDecoration::Underline; break;
        case KILN_TEXT_DECORATION_LINE_THROUGH: s.span.textDecoration = text::TextDecoration::LineThrough; break;
        default: break;
        }
        s.span.background = ToColor(o.background);
        return NewTextStyle(std::move(s));
    } CATCH(nullptr)
}
KILN_API kiln_text_style* kiln_text_style_merge(const kiln_text_style* base, const kiln_text_style* other) {
    TRY {
        text::TextStyle s = base ? base->style : text::TextStyle{};
        if (other) s = s.Merge(other->style);
        return NewTextStyle(std::move(s));
    } CATCH(nullptr)
}
KILN_API kiln_text_style* kiln_text_style_retain(kiln_text_style* s) { return Retain(s); }
KILN_API void kiln_text_style_release(kiln_text_style* s) { Release(s); }

KILN_API kiln_anim_spec* kiln_anim_spring(float damping, float stiffness) {
    TRY {
        auto* s = new kiln_anim_spec;
        s->spec = anim::Spring(std::isnan(damping) ? anim::spring::DampingRatioNoBouncy : damping,
                               std::isnan(stiffness) ? anim::spring::StiffnessMedium : stiffness);
        return s;
    } CATCH(nullptr)
}
KILN_API kiln_anim_spec* kiln_anim_tween(int32_t duration, int32_t delay, kiln_easing easing) {
    TRY {
        anim::Easing e;
        switch (easing) {
        case KILN_EASING_LINEAR_OUT_SLOW_IN: e = anim::LinearOutSlowIn(); break;
        case KILN_EASING_FAST_OUT_LINEAR_IN: e = anim::FastOutLinearIn(); break;
        case KILN_EASING_LINEAR: e = anim::Linear(); break;
        default: e = anim::FastOutSlowIn(); break;
        }
        auto* s = new kiln_anim_spec;
        s->spec = anim::Tween(duration, delay, e);
        return s;
    } CATCH(nullptr)
}
KILN_API kiln_anim_spec* kiln_anim_spec_retain(kiln_anim_spec* s) { return Retain(s); }
KILN_API void kiln_anim_spec_release(kiln_anim_spec* s) { Release(s); }

namespace {
    kiln_image_bitmap* NewBitmap(gfx::ImageBitmap b) {
        if (b.Width() <= 0) return nullptr;
        auto* h = new kiln_image_bitmap;
        h->bitmap = std::move(b);
        return h;
    }
}
KILN_API kiln_image_bitmap* kiln_image_bitmap_decode(const uint8_t* bytes, size_t length) {
    TRY {
        if (!bytes) return nullptr;
        return NewBitmap(gfx::ImageBitmap::Decode({ bytes, length }));
    } CATCH(nullptr)
}
KILN_API kiln_image_bitmap* kiln_image_bitmap_from_asset(const char* path) {
    TRY {
        if (!path) return nullptr;
        const auto bytes = ReadAsset(path);
        return bytes ? NewBitmap(gfx::ImageBitmap::Decode(*bytes)) : nullptr;
    } CATCH(nullptr)
}
KILN_API kiln_image_bitmap* kiln_image_bitmap_from_pixels(int32_t w, int32_t h, const uint32_t* argb) {
    TRY {
        if (!argb || w <= 0 || h <= 0) return nullptr;
        std::vector<std::uint8_t> rgba(std::size_t(w) * std::size_t(h) * 4);
        for (std::size_t i = 0; i < std::size_t(w) * std::size_t(h); ++i) {
            const std::uint32_t p = argb[i];
            rgba[i * 4 + 0] = std::uint8_t(p >> 16);
            rgba[i * 4 + 1] = std::uint8_t(p >> 8);
            rgba[i * 4 + 2] = std::uint8_t(p);
            rgba[i * 4 + 3] = std::uint8_t(p >> 24);
        }
        return NewBitmap(gfx::ImageBitmap::FromPixels(w, h, rgba));
    } CATCH(nullptr)
}
KILN_API int32_t kiln_image_bitmap_width(const kiln_image_bitmap* b) { return b ? b->bitmap.Width() : 0; }
KILN_API int32_t kiln_image_bitmap_height(const kiln_image_bitmap* b) { return b ? b->bitmap.Height() : 0; }
KILN_API kiln_image_bitmap* kiln_image_bitmap_retain(kiln_image_bitmap* b) { return Retain(b); }
KILN_API void kiln_image_bitmap_release(kiln_image_bitmap* b) { Release(b); }

KILN_API kiln_shape* kiln_shape_from_polygon(const kiln_rounded_polygon* p, int32_t startAngle) {
    TRY { return p ? NewShape(fd::ToShape(p->polygon, startAngle)) : nullptr; } CATCH(nullptr)
}
KILN_API kiln_rounded_polygon* kiln_rounded_polygon_retain(kiln_rounded_polygon* p) { return Retain(p); }
KILN_API void kiln_rounded_polygon_release(kiln_rounded_polygon* p) { Release(p); }

namespace {
    kiln_window_insets* NewInsets(fd::WindowInsets i) {
        auto* h = new kiln_window_insets;
        h->insets = std::move(i);
        return h;
    }
    const fd::WindowInsets& InsetsOf(const kiln_window_insets* h) {
        static const fd::WindowInsets none;
        return h ? h->insets : none;
    }
}
KILN_API kiln_window_insets* kiln_window_insets_of(kiln_window_insets_type type) {
    TRY {
        using W = fd::WindowInsets;
        switch (type) {
        case KILN_INSETS_SYSTEM_BARS: return NewInsets(W::SystemBars());
        case KILN_INSETS_STATUS_BARS: return NewInsets(W::StatusBars());
        case KILN_INSETS_NAVIGATION_BARS: return NewInsets(W::NavigationBars());
        case KILN_INSETS_CAPTION_BAR: return NewInsets(W::CaptionBar());
        case KILN_INSETS_IME: return NewInsets(W::Ime());
        case KILN_INSETS_DISPLAY_CUTOUT: return NewInsets(W::DisplayCutout());
        case KILN_INSETS_SAFE_DRAWING: return NewInsets(W::SafeDrawing());
        case KILN_INSETS_SAFE_GESTURES: return NewInsets(W::SafeGestures());
        case KILN_INSETS_SAFE_CONTENT: return NewInsets(W::SafeContent());
        case KILN_INSETS_SYSTEM_GESTURES: return NewInsets(W::SystemGestures());
        case KILN_INSETS_MANDATORY_SYSTEM_GESTURES: return NewInsets(W::MandatorySystemGestures());
        case KILN_INSETS_TAPPABLE_ELEMENT: return NewInsets(W::TappableElement());
        case KILN_INSETS_WATERFALL: return NewInsets(W::Waterfall());
        default: return NewInsets(W());
        }
    } CATCH(nullptr)
}
KILN_API kiln_window_insets* kiln_window_insets_fixed(float l, float t, float r, float b) {
    TRY { return NewInsets(fd::WindowInsets::Fixed(ui::Dp(l), ui::Dp(t), ui::Dp(r), ui::Dp(b))); } CATCH(nullptr)
}
KILN_API kiln_window_insets* kiln_window_insets_union(const kiln_window_insets* a, const kiln_window_insets* b) {
    TRY { return NewInsets(InsetsOf(a).Union(InsetsOf(b))); } CATCH(nullptr)
}
KILN_API kiln_window_insets* kiln_window_insets_add(const kiln_window_insets* a, const kiln_window_insets* b) {
    TRY { return NewInsets(InsetsOf(a).Add(InsetsOf(b))); } CATCH(nullptr)
}
KILN_API kiln_window_insets* kiln_window_insets_exclude(const kiln_window_insets* a, const kiln_window_insets* b) {
    TRY { return NewInsets(InsetsOf(a).Exclude(InsetsOf(b))); } CATCH(nullptr)
}
KILN_API kiln_window_insets* kiln_window_insets_only(const kiln_window_insets* i, uint32_t sides) {
    TRY { return NewInsets(InsetsOf(i).Only(fd::WindowInsetsSides{ std::uint8_t(sides) })); } CATCH(nullptr)
}
KILN_API kiln_window_insets* kiln_window_insets_retain(kiln_window_insets* i) { return Retain(i); }
KILN_API void kiln_window_insets_release(kiln_window_insets* i) { Release(i); }

KILN_API kiln_image_vector* kiln_image_vector_from_paths(float w, float h, float vw, float vh, const char* const* paths, size_t count,
                                                         bool autoMirror) {
    TRY {
        gfx::vector::ImageVector::Builder b("", ui::Dp(w), ui::Dp(h), vw, vh, ui::Color::Unspecified(), gfx::BlendMode::SrcIn, autoMirror);
        for (size_t i = 0; paths && i < count; ++i)
            if (paths[i]) b.Path(gfx::vector::ParsePathString(paths[i]), gfx::Brush::SolidColor(ui::Color::Black()));
        auto* v = new kiln_image_vector;
        v->vector = b.Build();
        return v;
    } CATCH(nullptr)
}
KILN_API kiln_image_vector* kiln_image_vector_retain(kiln_image_vector* v) { return Retain(v); }
KILN_API void kiln_image_vector_release(kiln_image_vector* v) { Release(v); }

// --- Modifiers ----------------------------------------------------------------------------------

#define MOD(expr) TRY { const ui::Modifier& base = ToModifier(m); (void)base; return NewModifier(expr); } CATCH(m)

KILN_API kiln_modifier* kiln_mod_then(kiln_modifier* m, const kiln_modifier* other) { MOD(base.Then(ToModifier(other))) }
KILN_API kiln_modifier* kiln_mod_padding(kiln_modifier* m, float s, float t, float e, float b) {
    MOD(fd::Padding(base, fd::PaddingValues{ ui::Dp(s), ui::Dp(t), ui::Dp(e), ui::Dp(b) }))
}
KILN_API kiln_modifier* kiln_mod_padding_all(kiln_modifier* m, float all) { MOD(fd::Padding(base, ui::Dp(all))) }
KILN_API kiln_modifier* kiln_mod_padding_hv(kiln_modifier* m, float h, float v) { MOD(fd::Padding(base, ui::Dp(h), ui::Dp(v))) }
KILN_API kiln_modifier* kiln_mod_size(kiln_modifier* m, float w, float h) { MOD(fd::SizeMod(base, ToDp(w), ToDp(h))) }
KILN_API kiln_modifier* kiln_mod_width(kiln_modifier* m, float w) { MOD(fd::Width(base, ToDp(w))) }
KILN_API kiln_modifier* kiln_mod_height(kiln_modifier* m, float h) { MOD(fd::Height(base, ToDp(h))) }
KILN_API kiln_modifier* kiln_mod_size_in(kiln_modifier* m, float minW, float minH, float maxW, float maxH) {
    MOD(fd::SizeIn(base, ToDp(minW), ToDp(minH), ToDp(maxW), ToDp(maxH)))
}
KILN_API kiln_modifier* kiln_mod_required_size(kiln_modifier* m, float w, float h) { MOD(fd::RequiredSize(base, ToDp(w), ToDp(h))) }
KILN_API kiln_modifier* kiln_mod_default_min_size(kiln_modifier* m, float w, float h) { MOD(fd::DefaultMinSize(base, ToDp(w), ToDp(h))) }
KILN_API kiln_modifier* kiln_mod_fill_max_width(kiln_modifier* m, float f) { MOD(fd::FillMax(base, fd::Axis::Horizontal, f)) }
KILN_API kiln_modifier* kiln_mod_fill_max_height(kiln_modifier* m, float f) { MOD(fd::FillMax(base, fd::Axis::Vertical, f)) }
KILN_API kiln_modifier* kiln_mod_fill_max_size(kiln_modifier* m, float f) { MOD(fd::FillMax(base, fd::Axis::Both, f)) }
KILN_API kiln_modifier* kiln_mod_wrap_content_size(kiln_modifier* m, kiln_alignment a) { MOD(fd::WrapContent(base, fd::Axis::Both, ToAlignment(a))) }
KILN_API kiln_modifier* kiln_mod_aspect_ratio(kiln_modifier* m, float r) { MOD(fd::AspectRatio(base, r)) }
KILN_API kiln_modifier* kiln_mod_offset(kiln_modifier* m, float x, float y) { MOD(fd::OffsetMod(base, ui::Dp(x), ui::Dp(y))) }
KILN_API kiln_modifier* kiln_mod_weight(kiln_modifier* m, float w, bool fill) { MOD(fd::Weight(base, w, fill)) }
KILN_API kiln_modifier* kiln_mod_align_in_box(kiln_modifier* m, kiln_alignment a) { MOD(fd::AlignInBox(base, ToAlignment(a))) }
KILN_API kiln_modifier* kiln_mod_align_in_row(kiln_modifier* m, float bias) { MOD(fd::AlignInRow(base, ui::AlignmentV{ bias })) }
KILN_API kiln_modifier* kiln_mod_align_in_column(kiln_modifier* m, float bias) { MOD(fd::AlignInColumn(base, ui::AlignmentH{ bias })) }
KILN_API kiln_modifier* kiln_mod_match_parent_size(kiln_modifier* m) { MOD(fd::MatchParentSize(base)) }
KILN_API kiln_modifier* kiln_mod_safe_drawing_padding(kiln_modifier* m) { MOD(fd::SafeDrawingPadding(base)) }
KILN_API kiln_modifier* kiln_mod_system_bars_padding(kiln_modifier* m) { MOD(fd::SystemBarsPadding(base)) }
KILN_API kiln_modifier* kiln_mod_ime_padding(kiln_modifier* m) { MOD(fd::ImePadding(base)) }
KILN_API kiln_modifier* kiln_mod_window_insets_padding(kiln_modifier* m, const kiln_window_insets* i) {
    MOD(fd::WindowInsetsPadding(base, InsetsOf(i)))
}
KILN_API kiln_modifier* kiln_mod_background(kiln_modifier* m, kiln_color c, const kiln_shape* s) {
    MOD(fd::Background(base, ToColor(c), s ? s->shape : fd::RectangleShape()))
}
KILN_API kiln_modifier* kiln_mod_border(kiln_modifier* m, float w, kiln_color c, const kiln_shape* s) {
    MOD(fd::Border(base, ui::Dp(w), ToColor(c), s ? s->shape : fd::RectangleShape()))
}
KILN_API kiln_modifier* kiln_mod_clip(kiln_modifier* m, const kiln_shape* s) {
    MOD(s ? fd::Clip(base, s->shape) : fd::ClipToBounds(base))
}
KILN_API kiln_modifier* kiln_mod_alpha(kiln_modifier* m, float a) { MOD(fd::Alpha(base, a)) }
KILN_API kiln_modifier* kiln_mod_rotate(kiln_modifier* m, float d) { MOD(fd::Rotate(base, d)) }
KILN_API kiln_modifier* kiln_mod_scale(kiln_modifier* m, float sx, float sy) {
    fd::GraphicsLayerParams p;
    p.scaleX = sx;
    p.scaleY = sy;
    MOD(fd::GraphicsLayer(base, p))
}
KILN_API kiln_modifier* kiln_mod_shadow(kiln_modifier* m, float e, const kiln_shape* s) {
    MOD(fd::ShadowMod(base, ui::Dp(e), s ? s->shape : fd::RectangleShape()))
}
KILN_API kiln_modifier* kiln_mod_z_index(kiln_modifier* m, float z) { MOD(fd::ZIndex(base, z)) }
KILN_API kiln_modifier* kiln_mod_clickable(kiln_modifier* m, bool enabled, kiln_fn onClick) {
    fd::ClickableOptions o;
    o.enabled = enabled;
    MOD(fd::Clickable(base, Fn(onClick), o))
}
KILN_API kiln_modifier* kiln_mod_toggleable(kiln_modifier* m, bool value, bool enabled, kiln_fn_bool onChange) {
    fd::ClickableOptions o;
    o.enabled = enabled;
    MOD(fd::Toggleable(base, value, Fn(onChange), o))
}
KILN_API kiln_modifier* kiln_mod_selectable(kiln_modifier* m, bool selected, bool enabled, kiln_fn onClick) {
    fd::ClickableOptions o;
    o.enabled = enabled;
    MOD(fd::Selectable(base, selected, Fn(onClick), o))
}
KILN_API kiln_modifier* kiln_mod_nested_scroll(kiln_modifier* m, const kiln_nested_scroll_connection* c) {
    if (!c) return m;
    auto conn = std::const_pointer_cast<ui::NestedScrollConnection>(std::static_pointer_cast<const ui::NestedScrollConnection>(c->p));
    MOD(fd::NestedScroll(base, conn))
}
KILN_API kiln_modifier* kiln_mod_vertical_scroll(kiln_modifier* m, kiln_scroll_state* s, bool enabled) {
    if (!s) return m;
    MOD(fd::VerticalScroll(base, *s->state, enabled))
}
KILN_API kiln_modifier* kiln_mod_horizontal_scroll(kiln_modifier* m, kiln_scroll_state* s, bool enabled) {
    if (!s) return m;
    MOD(fd::HorizontalScroll(base, *s->state, enabled))
}
KILN_API kiln_modifier* kiln_mod_test_tag(kiln_modifier* m, const char* tag) {
    std::string t = ToString(tag);
    MOD(fd::Semantics(base, [t](ui::SemanticsConfiguration& c) { c.testTag = t; }))
}
KILN_API kiln_modifier* kiln_mod_content_description(kiln_modifier* m, const char* d) {
    std::string t = ToString(d);
    MOD(fd::Semantics(base, [t](ui::SemanticsConfiguration& c) { c.contentDescription.push_back(t); }))
}

// --- Layouts ------------------------------------------------------------------------------------

KILN_API void kiln_box_options_init(kiln_box_options* o) {
    if (!o) return;
    *o = {};
    o->size = sizeof(*o);
    o->content_alignment = KILN_ALIGN_TOP_START;
}
KILN_API void kiln_box(kiln_key key, const kiln_modifier* m, const kiln_box_options* in, kiln_fn content) {
    TRY {
        const auto o = ReadOptions(in, kiln_box_options_init);
        fd::Box(ToModifier(m), ToAlignment(o.content_alignment), o.propagate_min_constraints, Fn(content), ToKey(key));
    } CATCH_VOID
}

KILN_API void kiln_row_options_init(kiln_row_options* o) {
    if (!o) return;
    *o = {};
    o->size = sizeof(*o);
    o->horizontal_arrangement = { KILN_ARRANGE_START, 0, NAN };
    o->vertical_alignment = KILN_ALIGN_START;
}
KILN_API void kiln_row(kiln_key key, const kiln_modifier* m, const kiln_row_options* in, kiln_fn content) {
    TRY {
        const auto o = ReadOptions(in, kiln_row_options_init);
        auto fn = Fn(content);
        fd::Row(ToModifier(m), ToArrangement(o.horizontal_arrangement, false), ui::AlignmentV{ o.vertical_alignment },
                fn ? fn : [] {}, ToKey(key));
    } CATCH_VOID
}

KILN_API void kiln_column_options_init(kiln_column_options* o) {
    if (!o) return;
    *o = {};
    o->size = sizeof(*o);
    o->vertical_arrangement = { KILN_ARRANGE_START, 0, NAN };
    o->horizontal_alignment = KILN_ALIGN_START;
}
KILN_API void kiln_column(kiln_key key, const kiln_modifier* m, const kiln_column_options* in, kiln_fn content) {
    TRY {
        const auto o = ReadOptions(in, kiln_column_options_init);
        auto fn = Fn(content);
        fd::Column(ToModifier(m), ToArrangement(o.vertical_arrangement, true), ui::AlignmentH{ o.horizontal_alignment },
                   fn ? fn : [] {}, ToKey(key));
    } CATCH_VOID
}

KILN_API void kiln_spacer(kiln_key key, const kiln_modifier* m) {
    TRY { fd::Spacer(ToModifier(m), ToKey(key)); } CATCH_VOID
}

KILN_API void kiln_box_with_constraints(kiln_key key, const kiln_modifier* m, const kiln_box_options* in, kiln_fn_constraints content) {
    TRY {
        const auto o = ReadOptions(in, kiln_box_options_init);
        auto own = Own(content);
        fd::BoxWithConstraints(ToModifier(m), ToAlignment(o.content_alignment), o.propagate_min_constraints,
            [own](const fd::BoxWithConstraintsScope& s) {
                if (!own) return;
                const kiln_constraints c{ s.MinWidth().value, s.MaxWidth().value, s.MinHeight().value, s.MaxHeight().value };
                CallbackScope cs;
                own->f.call(own->f.user, &c);
            }, ToKey(key));
    } CATCH_VOID
}

// --- Text and images ----------------------------------------------------------------------------

KILN_API void kiln_text_options_init(kiln_text_options* o) {
    if (!o) return;
    *o = {};
    o->size = sizeof(*o);
    o->overflow = KILN_TEXT_OVERFLOW_CLIP;
    o->soft_wrap = true;
    o->max_lines = INT32_MAX;
    o->min_lines = 1;
}
KILN_API void kiln_basic_text(kiln_key key, const char* utf8, const kiln_modifier* m, const kiln_text_style* style,
                              const kiln_text_options* in) {
    TRY {
        const auto o = ReadOptions(in, kiln_text_options_init);
        fd::BasicTextOptions bo;
        bo.overflow = text::TextOverflow(o.overflow);
        bo.softWrap = o.soft_wrap;
        bo.maxLines = o.max_lines;
        bo.minLines = o.min_lines;
        fd::BasicText(ToString(utf8), ToModifier(m), style ? style->style : text::TextStyle{}, std::move(bo), ToKey(key));
    } CATCH_VOID
}

struct kiln_text_builder { text::AnnotatedStringBuilder b; };
namespace {
    text::LinkInteractionListener LinkListener(kiln_fn_str onClick, bool url) {
        auto fn = Fn(onClick);
        if (!fn) return {};
        return [fn, url](const text::LinkAnnotation& l) { fn(url ? l.url : l.tag); };
    }
}
KILN_API kiln_text_builder* kiln_text_builder_new(void) { TRY { return new kiln_text_builder; } CATCH(nullptr) }
KILN_API void kiln_text_builder_append(kiln_text_builder* b, const char* t) { TRY { if (b && t) b->b.Append(t); } CATCH_VOID }
KILN_API void kiln_text_builder_push_style(kiln_text_builder* b, const kiln_text_style* s) {
    TRY { if (b) b->b.PushStyle(s ? s->style.span : text::SpanStyle{}); } CATCH_VOID
}
KILN_API void kiln_text_builder_push_link_url(kiln_text_builder* b, const char* url, kiln_fn_str onClick) {
    TRY { if (b) b->b.PushLink(text::LinkAnnotation::Url(ToString(url), {}, LinkListener(onClick, true))); } CATCH_VOID
}
KILN_API void kiln_text_builder_push_link_clickable(kiln_text_builder* b, const char* tag, kiln_fn_str onClick) {
    TRY { if (b) b->b.PushLink(text::LinkAnnotation::Clickable(ToString(tag), {}, LinkListener(onClick, false))); } CATCH_VOID
}
KILN_API void kiln_text_builder_pop(kiln_text_builder* b) { TRY { if (b) b->b.Pop(); } CATCH_VOID }
KILN_API kiln_annotated_string* kiln_text_builder_build(const kiln_text_builder* b) {
    TRY {
        auto* s = new kiln_annotated_string;
        if (b) s->text = b->b.ToAnnotatedString();
        return s;
    } CATCH(nullptr)
}
KILN_API void kiln_text_builder_free(kiln_text_builder* b) { delete b; }
KILN_API kiln_annotated_string* kiln_annotated_string_retain(kiln_annotated_string* s) { return Retain(s); }
KILN_API void kiln_annotated_string_release(kiln_annotated_string* s) { Release(s); }
KILN_API void kiln_basic_text_annotated(kiln_key key, const kiln_annotated_string* t, const kiln_modifier* m, const kiln_text_style* style,
                                        const kiln_text_options* in) {
    TRY {
        if (!t) return;
        const auto o = ReadOptions(in, kiln_text_options_init);
        fd::BasicTextOptions bo;
        bo.overflow = text::TextOverflow(o.overflow);
        bo.softWrap = o.soft_wrap;
        bo.maxLines = o.max_lines;
        bo.minLines = o.min_lines;
        fd::BasicText(t->text, ToModifier(m), style ? style->style : text::TextStyle{}, std::move(bo), ToKey(key));
    } CATCH_VOID
}

KILN_API void kiln_image_options_init(kiln_image_options* o) {
    if (!o) return;
    *o = {};
    o->size = sizeof(*o);
    o->alignment = KILN_ALIGN_CENTER;
    o->content_scale = KILN_CONTENT_SCALE_FIT;
    o->alpha = 1;
}
KILN_API void kiln_image(kiln_key key, const kiln_image_bitmap* bitmap, const char* description, const kiln_modifier* m,
                         const kiln_image_options* in) {
    TRY {
        if (!bitmap) return;
        const auto o = ReadOptions(in, kiln_image_options_init);
        fd::ContentScale scale;
        switch (o.content_scale) {
        case KILN_CONTENT_SCALE_CROP: scale = fd::ContentScale::Crop(); break;
        case KILN_CONTENT_SCALE_FILL_HEIGHT: scale = fd::ContentScale::FillHeight(); break;
        case KILN_CONTENT_SCALE_FILL_WIDTH: scale = fd::ContentScale::FillWidth(); break;
        case KILN_CONTENT_SCALE_INSIDE: scale = fd::ContentScale::Inside(); break;
        case KILN_CONTENT_SCALE_NONE: scale = fd::ContentScale::None(); break;
        case KILN_CONTENT_SCALE_FILL_BOUNDS: scale = fd::ContentScale::FillBounds(); break;
        default: scale = fd::ContentScale::Fit(); break;
        }
        const gfx::ImageBitmap bmp = bitmap->bitmap;
        const auto desc = ToOptString(description);
        const ui::Modifier mod = ToModifier(m);
        InGroup(key, [&] { fd::Image(bmp, desc, mod, ToAlignment(o.alignment), scale, o.alpha); });
    } CATCH_VOID
}

KILN_API kiln_text_field_state* kiln_remember_text_field_state(kiln_key key, const char* initial) {
    TRY {
        kiln_text_field_state* out = nullptr;
        const std::string init = ToString(initial);
        InGroup(key, [&] {
            auto& c = *rt::Composition::Current();
            auto& s = c.RememberNew<fd::TextFieldState>({}, {}, init);
            out = &c.Remember<kiln_text_field_state>([&] { return kiln_text_field_state{ &s }; });
        });
        return out;
    } CATCH(nullptr)
}
KILN_API const char* kiln_text_field_state_text(const kiln_text_field_state* s) {
    TRY { return ReturnString(s ? s->state->TextUtf8() : std::string()); } CATCH("")
}
KILN_API void kiln_text_field_state_set_text(kiln_text_field_state* s, const char* text) {
    TRY { if (s) s->state->SetTextAndPlaceCursorAtEnd(ToString(text)); } CATCH_VOID
}
KILN_API void kiln_text_field_options_init(kiln_text_field_options* o) {
    if (!o) return;
    *o = {};
    o->size = sizeof(*o);
    o->enabled = true;
}
KILN_API void kiln_basic_text_field(kiln_key key, kiln_text_field_state* s, const kiln_modifier* m, const kiln_text_field_options* in) {
    TRY {
        if (!s) return;
        const auto o = ReadOptions(in, kiln_text_field_options_init);
        fd::BasicTextFieldOptions bo;
        bo.enabled = o.enabled;
        bo.readOnly = o.read_only;
        if (o.single_line) bo.lineLimits = fd::TextFieldLineLimits::SingleLine();
        if (o.text_style) bo.textStyle = o.text_style->style;
        if (o.cursor_color != KILN_COLOR_UNSPECIFIED) bo.cursorBrush = gfx::Brush::SolidColor(ToColor(o.cursor_color));
        fd::BasicTextField(*s->state, ToModifier(m), std::move(bo), ToKey(key));
    } CATCH_VOID
}

// --- Scrolling ----------------------------------------------------------------------------------

KILN_API kiln_scroll_state* kiln_remember_scroll_state(kiln_key key, int32_t initial) {
    TRY {
        kiln_scroll_state* out = nullptr;
        InGroup(key, [&] {
            auto& state = fd::RememberScrollState(initial);
            auto scope = rt::RememberCoroutineScope();
            out = &rt::Composition::Current()->Remember<kiln_scroll_state>([&] { return kiln_scroll_state{ &state, scope }; });
        });
        return out;
    } CATCH(nullptr)
}
KILN_API int32_t kiln_scroll_state_value(const kiln_scroll_state* s) { return s ? s->state->Value() : 0; }
KILN_API kiln_scrollable_state* kiln_scroll_state_scrollable(kiln_scroll_state* s) {
    return s ? reinterpret_cast<kiln_scrollable_state*>(static_cast<fd::ScrollableState*>(s->state)) : nullptr;
}
KILN_API kiln_scrollable_state* kiln_lazy_list_state_scrollable(kiln_lazy_list_state* s) {
    return s ? reinterpret_cast<kiln_scrollable_state*>(static_cast<fd::ScrollableState*>(s->state)) : nullptr;
}
KILN_API int32_t kiln_scroll_state_max_value(const kiln_scroll_state* s) { return s ? s->state->MaxValue() : 0; }
KILN_API void kiln_scroll_state_animate_to(kiln_scroll_state* s, int32_t value, kiln_fn done) {
    TRY {
        if (!s) { Fn(done); return; }
        fd::ScrollState* st = s->state;
        LaunchIn(s->scope, [st, value] { return st->AnimateScrollTo(value); }, done);
    } CATCH_VOID
}
KILN_API void kiln_scroll_state_scroll_to(kiln_scroll_state* s, int32_t value, kiln_fn done) {
    TRY {
        auto fn = Fn(done);
        if (s) s->state->ScrollTo(value);
        if (fn) fn();
    } CATCH_VOID
}

KILN_API kiln_lazy_list_state* kiln_remember_lazy_list_state(kiln_key key, int32_t index, int32_t offset) {
    TRY {
        kiln_lazy_list_state* out = nullptr;
        InGroup(key, [&] {
            auto& state = fd::RememberLazyListState(index, offset);
            auto scope = rt::RememberCoroutineScope();
            out = &rt::Composition::Current()->Remember<kiln_lazy_list_state>([&] { return kiln_lazy_list_state{ &state, scope }; });
        });
        return out;
    } CATCH(nullptr)
}
KILN_API int32_t kiln_lazy_list_state_first_visible_item_index(const kiln_lazy_list_state* s) { return s ? s->state->FirstVisibleItemIndex() : 0; }
KILN_API int32_t kiln_lazy_list_state_first_visible_item_scroll_offset(const kiln_lazy_list_state* s) {
    return s ? s->state->FirstVisibleItemScrollOffset() : 0;
}
KILN_API bool kiln_lazy_list_state_can_scroll_forward(const kiln_lazy_list_state* s) { return s && s->state->CanScrollForward(); }
KILN_API bool kiln_lazy_list_state_can_scroll_backward(const kiln_lazy_list_state* s) { return s && s->state->CanScrollBackward(); }
KILN_API void kiln_lazy_list_state_scroll_to_item(kiln_lazy_list_state* s, int32_t index, int32_t offset, kiln_fn done) {
    TRY {
        auto fn = Fn(done);
        if (s) s->state->ScrollToItem(index, offset);
        if (fn) fn();
    } CATCH_VOID
}
KILN_API void kiln_lazy_list_state_animate_scroll_to_item(kiln_lazy_list_state* s, int32_t index, int32_t offset, kiln_fn done) {
    TRY {
        if (!s) { Fn(done); return; }
        fd::LazyListState* st = s->state;
        LaunchIn(s->scope, [st, index, offset] { return st->AnimateScrollToItem(index, offset); }, done);
    } CATCH_VOID
}

KILN_API void kiln_lazy_item(kiln_lazy_scope* scope, const kiln_key* key, kiln_fn content) {
    TRY {
        if (!scope) return;
        auto fn = Fn(content);
        scope->scope->Item(fn ? fn : [] {}, key ? ToKey(*key) : rt::Key::Positional());
    } CATCH_VOID
}
KILN_API void kiln_lazy_items(kiln_lazy_scope* scope, int32_t count, kiln_fn_int content, kiln_fn_item_key itemKey) {
    TRY {
        if (!scope) return;
        auto fn = Fn(content);
        kiln::Function<rt::Key(int)> keyFn;
        if (auto k = Own(itemKey)) keyFn = [k](int i) { CallbackScope s; return ToKey(k->f.call(k->f.user, i)); };
        scope->scope->Items(count, fn ? fn : [](int) {}, std::move(keyFn));
    } CATCH_VOID
}
KILN_API void kiln_lazy_sticky_header(kiln_lazy_scope* scope, const kiln_key* key, kiln_fn_int content) {
    TRY {
        if (!scope) return;
        auto fn = Fn(content);
        scope->scope->StickyHeader(fn ? fn : [](int) {}, key ? ToKey(*key) : rt::Key::Positional());
    } CATCH_VOID
}

KILN_API void kiln_lazy_list_options_init(kiln_lazy_list_options* o) {
    if (!o) return;
    *o = {};
    o->size = sizeof(*o);
    o->arrangement = { KILN_ARRANGE_START, 0, NAN };
    o->alignment = KILN_ALIGN_START;
    o->user_scroll_enabled = true;
}

namespace {
    void LazyList(bool vertical, kiln_key key, const kiln_modifier* m, kiln_lazy_list_state* state, const kiln_lazy_list_options* in,
                  kiln_fn_lazy content) {
        const auto o = ReadOptions(in, kiln_lazy_list_options_init);
        fd::LazyListOptions lo;
        lo.contentPadding = ToPadding(o.content_padding);
        if (o.has_arrangement) lo.arrangement = ToArrangement(o.arrangement, vertical);
        lo.reverseLayout = o.reverse_layout;
        lo.horizontalAlignment = ui::AlignmentH{ o.alignment };
        lo.verticalAlignment = ui::AlignmentV{ o.alignment };
        lo.userScrollEnabled = o.user_scroll_enabled;
        auto own = Own(content);
        auto items = [own](fd::LazyListScope& s) {
            if (!own) return;
            kiln_lazy_scope scope{ &s };
            CallbackScope cs;
            own->f.call(own->f.user, &scope);
        };
        const ui::Modifier mod = ToModifier(m);
        InGroup(key, [&] {
            fd::LazyListState& st = state ? *state->state : fd::RememberLazyListState();
            if (vertical) fd::LazyColumn(mod, st, items, lo);
            else fd::LazyRow(mod, st, items, lo);
        });
    }
}
KILN_API void kiln_lazy_column(kiln_key key, const kiln_modifier* m, kiln_lazy_list_state* state, const kiln_lazy_list_options* o,
                               kiln_fn_lazy content) {
    TRY { LazyList(true, key, m, state, o, content); } CATCH_VOID
}
KILN_API void kiln_lazy_row(kiln_key key, const kiln_modifier* m, kiln_lazy_list_state* state, const kiln_lazy_list_options* o,
                            kiln_fn_lazy content) {
    TRY { LazyList(false, key, m, state, o, content); } CATCH_VOID
}

KILN_API kiln_pager_state* kiln_remember_pager_state(kiln_key key, int32_t initialPage, kiln_fn_count pageCount) {
    TRY {
        auto own = Own(pageCount);
        kiln::Function<int()> count = [own] {
            if (!own) return 0;
            CallbackScope s;
            return int(own->f.call(own->f.user));
        };
        kiln_pager_state* out = nullptr;
        InGroup(key, [&] {
            auto& state = fd::RememberPagerState(initialPage, 0, count);
            auto scope = rt::RememberCoroutineScope();
            out = &rt::Composition::Current()->Remember<kiln_pager_state>([&] { return kiln_pager_state{ &state, scope }; });
        });
        return out;
    } CATCH(nullptr)
}
KILN_API int32_t kiln_pager_state_current_page(const kiln_pager_state* s) { return s ? s->state->CurrentPage() : 0; }
KILN_API int32_t kiln_pager_state_settled_page(const kiln_pager_state* s) { return s ? s->state->SettledPage() : 0; }
KILN_API float kiln_pager_state_current_page_offset_fraction(const kiln_pager_state* s) { return s ? s->state->CurrentPageOffsetFraction() : 0; }
KILN_API void kiln_pager_state_animate_scroll_to_page(kiln_pager_state* s, int32_t page, kiln_fn done) {
    TRY {
        if (!s) { Fn(done); return; }
        fd::PagerState* st = s->state;
        LaunchIn(s->scope, [st, page] { return st->AnimateScrollToPage(page); }, done);
    } CATCH_VOID
}
KILN_API void kiln_pager_state_scroll_to_page(kiln_pager_state* s, int32_t page, kiln_fn done) {
    TRY {
        auto fn = Fn(done);
        if (s) s->state->ScrollToPage(page);
        if (fn) fn();
    } CATCH_VOID
}
KILN_API void kiln_pager_options_init(kiln_pager_options* o) {
    if (!o) return;
    *o = {};
    o->size = sizeof(*o);
    o->user_scroll_enabled = true;
}
namespace {
    void Pager(bool horizontal, kiln_key key, const kiln_modifier* m, kiln_pager_state* state, const kiln_pager_options* in, kiln_fn_int page) {
        if (!state) return;
        const auto o = ReadOptions(in, kiln_pager_options_init);
        fd::PagerOptions po;
        po.contentPadding = ToPadding(o.content_padding);
        po.pageSpacing = ui::Dp(o.page_spacing);
        po.beyondViewportPageCount = o.beyond_viewport_page_count;
        po.userScrollEnabled = o.user_scroll_enabled;
        po.reverseLayout = o.reverse_layout;
        auto fn = Fn(page);
        if (!fn) fn = [](int) {};
        if (horizontal) fd::HorizontalPager(ToModifier(m), *state->state, fn, std::move(po), ToKey(key));
        else fd::VerticalPager(ToModifier(m), *state->state, fn, std::move(po), ToKey(key));
    }
}
KILN_API void kiln_horizontal_pager(kiln_key key, const kiln_modifier* m, kiln_pager_state* s, const kiln_pager_options* o, kiln_fn_int page) {
    TRY { Pager(true, key, m, s, o, page); } CATCH_VOID
}
KILN_API void kiln_vertical_pager(kiln_key key, const kiln_modifier* m, kiln_pager_state* s, const kiln_pager_options* o, kiln_fn_int page) {
    TRY { Pager(false, key, m, s, o, page); } CATCH_VOID
}

// --- Animation ----------------------------------------------------------------------------------

KILN_API float kiln_animate_float(kiln_key key, float target, const kiln_anim_spec* spec) {
    TRY {
        Current();
        return fd::AnimateFloatAsState(target, spec ? spec->spec : anim::Spring(), {}, ToKey(key));
    } CATCH(target)
}
KILN_API float kiln_animate_dp(kiln_key key, float target, const kiln_anim_spec* spec) {
    TRY {
        Current();
        return spec ? fd::AnimateDpAsState(ui::Dp(target), spec->spec, {}, ToKey(key)).value
                    : fd::AnimateDpAsState(ui::Dp(target), anim::Spring(1.0f, anim::spring::StiffnessMedium, 0.4f), {}, ToKey(key)).value;
    } CATCH(target)
}
KILN_API kiln_color kiln_animate_color(kiln_key key, kiln_color target, const kiln_anim_spec* spec) {
    TRY {
        Current();
        return FromColor(fd::AnimateColorAsState(ToColor(target), spec ? spec->spec : anim::Spring(), {}, ToKey(key)));
    } CATCH(target)
}

KILN_API void kiln_animated_visibility(kiln_key key, bool visible, const kiln_modifier* m, kiln_fn content) {
    TRY {
        auto fn = Fn(content);
        fd::AnimatedVisibility(visible, ToModifier(m), fn ? fn : [] {}, fd::FadeIn() + fd::ExpandIn(), fd::ShrinkOut() + fd::FadeOut(), ToKey(key));
    } CATCH_VOID
}

