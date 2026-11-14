#pragma once

// Shared by the C ABI glue (FoundationCApi.cpp, the generated material3 glue): conversions from
// the C types to kiln's, callback ownership and the transient modifier arena.

#include <kiln/kiln.h>
#include <kiln/Function.h>

#include "kiln/animation/Spec.h"
#include "kiln/foundation/Arrangement.h"
#include "kiln/foundation/Border.h"
#include "kiln/foundation/Insets.h"
#include "kiln/foundation/Interaction.h"
#include "kiln/foundation/Layouts.h"
#include "kiln/graphics/shapes/RoundedPolygon.h"
#include "kiln/foundation/Lazy.h"
#include "kiln/foundation/Pager.h"
#include "kiln/foundation/Scroll.h"
#include "kiln/foundation/Shapes.h"
#include "kiln/foundation/text/TextFieldState.h"
#include "kiln/graphics/Image.h"
#include "kiln/graphics/vector/ImageVector.h"
#include "kiln/runtime/Composition.h"
#include "kiln/runtime/CoroutineScope.h"
#include "kiln/text/AnnotatedString.h"
#include "kiln/text/TextStyle.h"
#include "kiln/ui/Modifier.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <cmath>
#include <functional>
#include <memory>
#include <string>

namespace kiln::foundation { struct InlineTextContent; }

struct kiln_modifier { kiln::ui::Modifier m; };

// Refcounted handles.
struct kiln_shape { std::atomic<int> refs{ 1 }; kiln::foundation::ShapePtr shape; };
struct kiln_text_style { std::atomic<int> refs{ 1 }; kiln::text::TextStyle style; };
struct kiln_anim_spec { std::atomic<int> refs{ 1 }; kiln::anim::SpecPtr spec; };
struct kiln_image_bitmap { std::atomic<int> refs{ 1 }; kiln::gfx::ImageBitmap bitmap; };
struct kiln_image_vector { std::atomic<int> refs{ 1 }; kiln::gfx::vector::ImageVectorPtr vector; };
struct kiln_annotated_string { std::atomic<int> refs{ 1 }; kiln::text::AnnotatedString text; };
struct kiln_rounded_polygon { std::atomic<int> refs{ 1 }; kiln::gfx::shapes::RoundedPolygon polygon; };
struct kiln_window_insets { std::atomic<int> refs{ 1 }; kiln::foundation::WindowInsets insets; };
struct kiln_interaction_source { std::shared_ptr<kiln::foundation::MutableInteractionSource> source; };

// Remembered handles: the state and a scope for its suspending calls, cancelled when the call
// leaves the composition (before the state is freed).
struct kiln_scroll_state { kiln::foundation::ScrollState* state; std::shared_ptr<kiln::rt::CoroutineScope> scope; };
struct kiln_lazy_list_state { kiln::foundation::LazyListState* state; std::shared_ptr<kiln::rt::CoroutineScope> scope; };
struct kiln_lazy_grid_state { kiln::foundation::LazyGridState* state; std::shared_ptr<kiln::rt::CoroutineScope> scope; };
struct kiln_layout_coordinates { kiln::ui::LayoutCoordinates c; };
struct kiln_text_layout_result { std::atomic<int> refs{ 1 }; kiln::text::TextLayoutResult r; };
struct kiln_fling_behavior { kiln::foundation::FlingBehavior fling; };
struct kiln_pager_state {
    kiln::foundation::PagerState* state;
    std::shared_ptr<kiln::rt::CoroutineScope> scope;
    std::shared_ptr<const void> keep;   // what the pager state is a part of (a carousel's state), kept while a scroll runs
};
struct kiln_text_field_state { kiln::foundation::TextFieldState* state; };
struct kiln_nested_scroll_connection { std::shared_ptr<const void> p; };   // a ui::NestedScrollConnection

namespace kiln::capi {

    void LogError(const char* function, const char* what);

    // Every exported function body: exceptions stop here and are logged.
#define KILN_CAPI_TRY try
#define KILN_CAPI_CATCH(fallback)                                                           \
    catch (const std::exception& e) { ::kiln::capi::LogError(__func__, e.what()); return fallback; } \
    catch (...) { ::kiln::capi::LogError(__func__, "unknown exception"); return fallback; }

    // Marks a call into C: modifiers made inside live until the outermost callback returns.
    struct CallbackScope {
        CallbackScope();
        ~CallbackScope();
        CallbackScope(const CallbackScope&) = delete;
        CallbackScope& operator=(const CallbackScope&) = delete;
    };

    // A kiln_fn* owned by kiln: drop runs when the last copy goes.
    template<typename F>
    struct Owned {
        F f;
        explicit Owned(F fn) : f(fn) {}
        ~Owned() { if (f.drop) f.drop(f.user); }
        Owned(const Owned&) = delete;
        Owned& operator=(const Owned&) = delete;
    };
    // Null when the callback is absent (its drop, if any, has run).
    template<typename F>
    std::shared_ptr<Owned<F>> Own(F f) {
        if (!f.call) {
            if (f.drop) f.drop(f.user);
            return nullptr;
        }
        return std::make_shared<Owned<F>>(f);
    }

    // C callbacks as std::function (empty when absent).
    kiln::Function<void()> Fn(kiln_fn f);
    kiln::Function<void(bool)> Fn(kiln_fn_bool f);
    kiln::Function<void(int)> Fn(kiln_fn_int f);
    kiln::Function<void(const text::TextLayoutResult&)> Fn(kiln_fn_text_layout f);
    kiln::Function<void(ui::DrawScope&)> Fn(kiln_fn_draw f);
    foundation::InlineTextContent ToInlineContent(const kiln_inline_content& c);
    kiln::Function<void(float)> Fn(kiln_fn_float f);
    kiln::Function<void(const std::string&)> Fn(kiln_fn_str f);

    // Values.
    inline rt::Key ToKey(kiln_key k) {
        rt::Key r = rt::Key::Positional();
        r.value = k.value;
        r.explicitKey = k.explicit_key;
        return r;
    }
    inline rt::Keys ToKeys(const uint64_t* keys, size_t count) {
        rt::Keys r;
        for (size_t i = 0; keys && i < count; ++i) r.hashes.push_back(std::size_t(keys[i]));
        return r;
    }
    inline ui::Color ToColor(kiln_color c) { return c == KILN_COLOR_UNSPECIFIED ? ui::Color::Unspecified() : ui::Color(std::uint32_t(c)); }
    inline kiln_color FromColor(ui::Color c) { return c == ui::Color::Unspecified() ? KILN_COLOR_UNSPECIFIED : KILN_ARGB(c.argb); }
    inline ui::Dp ToDp(float v) { return std::isnan(v) ? ui::Dp::Unspecified() : ui::Dp(v); }
    inline float FromDp(ui::Dp d) { return d.value; }
    inline ui::Alignment ToAlignment(kiln_alignment a) { return { a.horizontal, a.vertical }; }
    inline std::string ToString(const char* s) { return s ? std::string(s) : std::string(); }
    inline std::optional<std::string> ToOptString(const char* s) { return s ? std::optional<std::string>(s) : std::nullopt; }
    // A string handed back to C: valid until the next string returned on this thread.
    const char* ReturnString(std::string s);

    inline const ui::Modifier& ToModifier(const kiln_modifier* m) {
        static const ui::Modifier empty;
        return m ? m->m : empty;
    }
    kiln_modifier* NewModifier(ui::Modifier m);

    // Options structs: a struct from an older header is shorter; read what it has and take the
    // defaults for the rest.
    template<typename T>
    T ReadOptions(const T* in, void (*init)(T*)) {
        T out;
        init(&out);
        if (in && in->size > sizeof(size_t)) {
            const size_t n = std::min(in->size, sizeof(T));
            std::memcpy(reinterpret_cast<char*>(&out) + sizeof(size_t), reinterpret_cast<const char*>(in) + sizeof(size_t), n - sizeof(size_t));
        }
        out.size = sizeof(T);
        return out;
    }

    foundation::ShapePtr ToShape(const kiln_shape* s);   // null: null
    const text::TextStyle* ToTextStyle(const kiln_text_style* s);   // null: null
    kiln_shape* NewShape(foundation::ShapePtr shape);
    kiln_text_style* NewTextStyle(text::TextStyle style);

    template<typename T>
    T* Retain(T* h) {
        if (h) h->refs.fetch_add(1, std::memory_order_relaxed);
        return h;
    }
    template<typename T>
    void Release(T* h) {
        if (h && h->refs.fetch_sub(1, std::memory_order_acq_rel) == 1) delete h;
    }

    // Handles inside values kiln returns (a struct of shapes, a theme's text styles): valid until
    // the outermost callback returns, like modifiers; retain one to keep it.
    kiln_shape* TransientShape(foundation::ShapePtr shape);         // null: null
    kiln_text_style* TransientTextStyle(text::TextStyle style);
    kiln_anim_spec* TransientSpec(anim::SpecPtr spec);              // null: null
    kiln_rounded_polygon* TransientPolygon(gfx::shapes::RoundedPolygon polygon);
    kiln_window_insets* TransientInsets(foundation::WindowInsets insets);
    kiln_interaction_source* TransientSource(std::shared_ptr<foundation::MutableInteractionSource> source);   // null: null
    inline text::TextUnit ToTextUnit(kiln_text_unit u) {
        return u.type == KILN_TEXT_UNIT_SP ? text::Sp(u.value) : u.type == KILN_TEXT_UNIT_EM ? text::Em(u.value) : text::TextUnit{};
    }
    inline kiln_text_unit FromTextUnit(text::TextUnit u) {
        return { u.value, u.IsSp() ? KILN_TEXT_UNIT_SP : u.IsEm() ? KILN_TEXT_UNIT_EM : KILN_TEXT_UNIT_UNSPECIFIED };
    }
    inline foundation::CornerSize ToCornerSize(kiln_corner_size c) { return { c.dp, c.px, c.percent }; }
    inline kiln_corner_size FromCornerSize(foundation::CornerSize c) { return { c.dp, c.px, c.percent }; }
    kiln_arrangement FromArrangement(const foundation::Arrangement& a);
    kiln_border_stroke FromBorderStroke(const foundation::BorderStroke& b);

    // A material3 state or scope handle: the object, an owner where it is shared, and the scope
    // its suspending methods run in (remembered handles only).
    template<typename T>
    struct Handle {
        std::shared_ptr<T> owner;
        T* ptr = nullptr;
        std::shared_ptr<rt::CoroutineScope> scope;
        // For a std::shared_ptr<T> parameter (not owning when the handle came from a T&).
        std::shared_ptr<T> Shared() const { return owner ? owner : std::shared_ptr<T>(std::shared_ptr<void>{}, ptr); }
    };

    foundation::Arrangement ToArrangement(const kiln_arrangement& a, bool vertical);
    foundation::PaddingValues ToPadding(const kiln_padding& p);
    kiln_padding FromPadding(const foundation::PaddingValues& p);
    // Strings and values inside results: valid until the outermost callback returns.
    const char* TransientString(std::string s);
    void KeepTransient(std::shared_ptr<void> value);
    // The pager state inside another state (a carousel's, a scroll field's): its scrolls run in
    // the owner's scope and keep the owner while they do.
    kiln_pager_state* TransientPager(const foundation::PagerState& state, std::shared_ptr<rt::CoroutineScope> scope, std::shared_ptr<const void> keep);
    // A C++ callback for C to call (not to keep or drop): valid until the outermost callback returns.
    kiln_fn TransientFn(kiln::Function<void()> fn);
    const foundation::ScrollIndicatorState& ToScrollIndicator(const kiln_scroll_indicator_state* s);   // null: sizes unknown
    kiln_fling_behavior* TransientFling(foundation::FlingBehavior fling);
    const kiln_layout_coordinates* TransientCoordinates(const ui::LayoutCoordinates& c);
    const kiln_text_layout_result* TransientTextLayout(const text::TextLayoutResult& r);   // null for an empty one
    template<typename T>
    T* NewTransient() {
        auto p = std::make_shared<T>();
        T* raw = p.get();
        KeepTransient(std::move(p));
        return raw;
    }

    // The composition running content; throws outside content.
    rt::Composition& CurrentComposition();

    // An opaque shared object handed to C (transient).
    template<typename C, typename T>
    C* TransientOpaque(const std::shared_ptr<T>& p) {
        if (!p) return nullptr;
        C* c = NewTransient<C>();
        c->p = std::const_pointer_cast<const T>(p);
        return c;
    }

    // Runs `body` in the call's own group when inside content (non-restartable, so it may return
    // values), or directly outside content.
    void InGroup(kiln_key key, const kiln::Function<void()>& body);
    // A suspending call: runs `task` in `scope`, then `done` (also when cancelled).
    void LaunchIn(const std::shared_ptr<rt::CoroutineScope>& scope, kiln::Function<rt::Task<>()> task, kiln_fn done);

}
