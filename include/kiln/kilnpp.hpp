// kiln++: header-only C++ over the kiln C ABI (kiln.h). Lambdas for callbacks, keys from the
// call site, RAII for refcounted handles, a chaining modifier builder. It uses only the C
// functions, so it works against a prebuilt libkiln (the AAR) as well as a source build.
// Material 3 is in kiln_m3.hpp (generated).
//
//   #include <kiln/kilnpp.hpp>
//   extern "C" void kiln_main(kiln_app* app) {
//       kpp::set_content(app, [] {
//           auto count = kpp::remember_state<int64_t>(0);
//           kpp::column(kpp::Modifier().padding(16), [=] {
//               kpp::basic_text("Count " + std::to_string(count.get()));
//               kpp::box(kpp::Modifier().size(48, 48).background(KILN_ARGB(0xFF6750A4))
//                            .clickable([=] { count.set(count.get() + 1); }));
//           });
//       });
//   }
//
// Content lambdas run again on their own when state they read changes, after the call that
// gave them has returned: capture by value (handles and states are pointers, cheap to copy).
#pragma once

#include <kiln/kiln.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <source_location>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace kpp {

    // --- Callbacks: any callable as any kiln_fn* struct ---------------------------------------

    namespace detail {
        template<typename Call, typename H> struct Trampoline;
        template<typename R, typename... A, typename H>
        struct Trampoline<R (*)(void*, A...), H> {
            static R call(void* user, A... a) { return (*static_cast<H*>(user))(a...); }
        };
        inline const char* Nz(const char* s) { return s ? s : ""; }
    }

    // fn<kiln_fn_bool>([](bool v) { ... }): kiln owns the copy and destroys it when done with it.
    template<typename CFn, typename F>
    CFn fn(F&& f) {
        using H = std::decay_t<F>;
        CFn out{};
        out.user = new H(std::forward<F>(f));
        out.call = &detail::Trampoline<decltype(out.call), H>::call;
        out.drop = [](void* u) { delete static_cast<H*>(u); };
        return out;
    }

    // A parameter taking a callable (or nothing) for the C callback struct CFn.
    template<typename CFn>
    class Callback {
    public:
        Callback() = default;
        Callback(std::nullptr_t) {}
        Callback(CFn raw) : m_Fn(raw) {}
        template<typename F, typename = std::enable_if_t<!std::is_same_v<std::decay_t<F>, Callback> && !std::is_same_v<std::decay_t<F>, CFn>
                                                         && !std::is_same_v<std::decay_t<F>, std::nullptr_t>>>
        Callback(F&& f) : m_Fn(fn<CFn>(std::forward<F>(f))) {}
        Callback(Callback&& o) noexcept : m_Fn(o.m_Fn) { o.m_Fn = CFn{}; }
        Callback(const Callback&) = delete;
        Callback& operator=(const Callback&) = delete;
        ~Callback() { if (m_Fn.drop) m_Fn.drop(m_Fn.user); }
        // Hands the callback to kiln (once).
        CFn release() { CFn f = m_Fn; m_Fn = CFn{}; return f; }
    private:
        CFn m_Fn{};
    };

    // A string parameter: a literal or a std::string.
    class Str {
    public:
        Str(const char* s) : m_S(s) {}
        Str(const std::string& s) : m_S(s.c_str()) {}
        Str(std::nullptr_t) : m_S(nullptr) {}
        const char* c_str() const { return m_S; }
    private:
        const char* m_S;
    };

    // --- Keys -----------------------------------------------------------------------------------

    // The call site by default; an explicit identity from an int or a string.
    struct Key {
        kiln_key k;
        Key(std::source_location loc = std::source_location::current()) : k(kiln_key_at(loc.file_name(), loc.line(), loc.column())) {}
        Key(int v) : k(kiln_key_int(v)) {}
        Key(int64_t v) : k(kiln_key_int(v)) {}
        Key(const char* s) : k(kiln_key_str(s)) {}
        Key(const std::string& s) : k(kiln_key_str(s.c_str())) {}
        operator kiln_key() const { return k; }
    };

    // Calls `content` with its own identity (key(k) { }).
    template<typename F>
    void group(Key key, F&& content) { kiln_group(key, fn<kiln_fn>(std::forward<F>(content))); }

    // --- Refcounted handles ---------------------------------------------------------------------

    template<typename T, T* (*Retain)(T*), void (*Release)(T*)>
    class Ref {
    public:
        Ref() = default;
        // Takes the reference a creating call returned.
        static Ref adopt(T* p) { Ref r; r.m_P = p; return r; }
        // Adds a reference to a borrowed (transient) handle.
        static Ref retain(T* p) { return adopt(p ? Retain(p) : nullptr); }
        Ref(const Ref& o) : m_P(o.m_P ? Retain(o.m_P) : nullptr) {}
        Ref(Ref&& o) noexcept : m_P(std::exchange(o.m_P, nullptr)) {}
        Ref& operator=(Ref o) noexcept { std::swap(m_P, o.m_P); return *this; }
        ~Ref() { if (m_P) Release(m_P); }
        T* get() const { return m_P; }
        operator T*() const { return m_P; }
        explicit operator bool() const { return m_P != nullptr; }
    private:
        T* m_P = nullptr;
    };
    using Shape = Ref<kiln_shape, kiln_shape_retain, kiln_shape_release>;
    using TextStyle = Ref<kiln_text_style, kiln_text_style_retain, kiln_text_style_release>;
    using AnimSpec = Ref<kiln_anim_spec, kiln_anim_spec_retain, kiln_anim_spec_release>;
    using ImageBitmap = Ref<kiln_image_bitmap, kiln_image_bitmap_retain, kiln_image_bitmap_release>;
    using ImageVector = Ref<kiln_image_vector, kiln_image_vector_retain, kiln_image_vector_release>;
    using WindowInsets = Ref<kiln_window_insets, kiln_window_insets_retain, kiln_window_insets_release>;
    using RoundedPolygon = Ref<kiln_rounded_polygon, kiln_rounded_polygon_retain, kiln_rounded_polygon_release>;

    namespace shapes {
        inline Shape rectangle() { return Shape::adopt(kiln_shape_rectangle()); }
        inline Shape circle() { return Shape::adopt(kiln_shape_circle()); }
        inline Shape rounded(float dp) { return Shape::adopt(kiln_shape_rounded(dp)); }
        inline Shape rounded_percent(float percent) { return Shape::adopt(kiln_shape_rounded_percent(percent)); }
        inline Shape rounded(float topStart, float topEnd, float bottomEnd, float bottomStart) {
            return Shape::adopt(kiln_shape_rounded_corners(topStart, topEnd, bottomEnd, bottomStart));
        }
        inline Shape cut(float dp) { return Shape::adopt(kiln_shape_cut(dp)); }
        inline Shape from_polygon(const kiln_rounded_polygon* p, int32_t startAngle = 0) { return Shape::adopt(kiln_shape_from_polygon(p, startAngle)); }
    }

    // TextStyle builder: text_style().size(20).color(KILN_ARGB(0xFF000000)).weight(700).build()
    class TextStyleBuilder {
    public:
        TextStyleBuilder() { kiln_text_style_options_init(&m_O); }
        TextStyleBuilder& color(kiln_color c) { m_O.color = c; return *this; }
        TextStyleBuilder& size(float sp) { m_O.font_size_sp = sp; return *this; }
        TextStyleBuilder& weight(int32_t w) { m_O.font_weight = w; return *this; }
        TextStyleBuilder& italic(bool v = true) { m_O.italic = { true, v }; return *this; }
        TextStyleBuilder& letter_spacing(float sp) { m_O.letter_spacing_sp = sp; return *this; }
        TextStyleBuilder& line_height(float sp) { m_O.line_height_sp = sp; return *this; }
        TextStyleBuilder& align(kiln_text_align a) { m_O.text_align = a; return *this; }
        TextStyleBuilder& decoration(kiln_text_decoration d) { m_O.decoration = d; return *this; }
        TextStyleBuilder& background(kiln_color c) { m_O.background = c; return *this; }
        TextStyle build() const { return TextStyle::adopt(kiln_text_style_new(&m_O)); }
        operator TextStyle() const { return build(); }
    private:
        kiln_text_style_options m_O;
    };
    inline TextStyleBuilder text_style() { return {}; }

    inline AnimSpec spring(float dampingRatio = NAN, float stiffness = NAN) { return AnimSpec::adopt(kiln_anim_spring(dampingRatio, stiffness)); }
    inline AnimSpec tween(int32_t durationMillis = 300, int32_t delayMillis = 0, kiln_easing e = KILN_EASING_FAST_OUT_SLOW_IN) {
        return AnimSpec::adopt(kiln_anim_tween(durationMillis, delayMillis, e));
    }

    namespace images {
        inline ImageBitmap from_asset(const char* path) { return ImageBitmap::adopt(kiln_image_bitmap_from_asset(path)); }
        inline ImageBitmap decode(const uint8_t* bytes, size_t length) { return ImageBitmap::adopt(kiln_image_bitmap_decode(bytes, length)); }
        inline ImageBitmap from_pixels(int32_t w, int32_t h, const uint32_t* argb) { return ImageBitmap::adopt(kiln_image_bitmap_from_pixels(w, h, argb)); }
        // A vector icon from SVG path data (24 dp, viewport 24 by default).
        inline ImageVector vector(std::initializer_list<const char*> paths, float size = 24, float viewport = 24, bool autoMirror = false) {
            std::vector<const char*> p(paths);
            return ImageVector::adopt(kiln_image_vector_from_paths(size, size, viewport, viewport, p.data(), p.size(), autoMirror));
        }
    }

    namespace insets {
        inline WindowInsets of(kiln_window_insets_type t) { return WindowInsets::adopt(kiln_window_insets_of(t)); }
        inline WindowInsets fixed(float l, float t, float r, float b) { return WindowInsets::adopt(kiln_window_insets_fixed(l, t, r, b)); }
        inline WindowInsets only(const kiln_window_insets* i, uint32_t sides) { return WindowInsets::adopt(kiln_window_insets_only(i, sides)); }
        inline WindowInsets union_of(const kiln_window_insets* a, const kiln_window_insets* b) { return WindowInsets::adopt(kiln_window_insets_union(a, b)); }
    }

    // --- Modifiers ------------------------------------------------------------------------------

    // Modifier().padding(8).fill_max_width().clickable([=] { ... }): valid inside the content
    // call that built it, like kiln_modifier*.
    class Modifier {
    public:
        Modifier() = default;
        Modifier(kiln_modifier* m) : m_M(m) {}
        operator const kiln_modifier*() const { return m_M; }
        kiln_modifier* get() const { return m_M; }

        Modifier then(const Modifier& o) const { return kiln_mod_then(m_M, o.m_M); }
        Modifier nested_scroll(const kiln_nested_scroll_connection* c) const { return kiln_mod_nested_scroll(m_M, c); }
        Modifier padding(float all) const { return kiln_mod_padding_all(m_M, all); }
        Modifier padding(float horizontal, float vertical) const { return kiln_mod_padding_hv(m_M, horizontal, vertical); }
        Modifier padding(float start, float top, float end, float bottom) const { return kiln_mod_padding(m_M, start, top, end, bottom); }
        Modifier size(float w, float h) const { return kiln_mod_size(m_M, w, h); }
        Modifier size(float s) const { return kiln_mod_size(m_M, s, s); }
        Modifier width(float w) const { return kiln_mod_width(m_M, w); }
        Modifier height(float h) const { return kiln_mod_height(m_M, h); }
        Modifier size_in(float minW, float minH, float maxW, float maxH) const { return kiln_mod_size_in(m_M, minW, minH, maxW, maxH); }
        Modifier required_size(float w, float h) const { return kiln_mod_required_size(m_M, w, h); }
        Modifier default_min_size(float w, float h) const { return kiln_mod_default_min_size(m_M, w, h); }
        Modifier fill_max_width(float f = 1) const { return kiln_mod_fill_max_width(m_M, f); }
        Modifier fill_max_height(float f = 1) const { return kiln_mod_fill_max_height(m_M, f); }
        Modifier fill_max_size(float f = 1) const { return kiln_mod_fill_max_size(m_M, f); }
        Modifier wrap_content_size(kiln_alignment a = KILN_ALIGN_CENTER) const { return kiln_mod_wrap_content_size(m_M, a); }
        Modifier aspect_ratio(float r) const { return kiln_mod_aspect_ratio(m_M, r); }
        Modifier offset(float x, float y) const { return kiln_mod_offset(m_M, x, y); }
        Modifier weight(float w, bool fill = true) const { return kiln_mod_weight(m_M, w, fill); }
        Modifier align(kiln_alignment a) const { return kiln_mod_align_in_box(m_M, a); }
        Modifier align_in_row(float verticalBias) const { return kiln_mod_align_in_row(m_M, verticalBias); }
        Modifier align_in_column(float horizontalBias) const { return kiln_mod_align_in_column(m_M, horizontalBias); }
        Modifier match_parent_size() const { return kiln_mod_match_parent_size(m_M); }
        Modifier focus_requester(kiln_focus_requester* r) const { return kiln_mod_focus_requester(m_M, r); }
        Modifier focusable(bool enabled = true) const { return kiln_mod_focusable(m_M, enabled); }
        template<typename F> Modifier on_focus_changed(F&& f) const { return kiln_mod_on_focus_changed(m_M, fn<kiln_fn_bool>(std::forward<F>(f))); }
        Modifier align_by(const kiln_alignment_line* line) const { return kiln_mod_align_by(m_M, line); }
        Modifier padding_from(const kiln_alignment_line* line, float before = KILN_DP_UNSPECIFIED, float after = KILN_DP_UNSPECIFIED) const {
            return kiln_mod_padding_from(m_M, line, before, after);
        }
        Modifier safe_drawing_padding() const { return kiln_mod_safe_drawing_padding(m_M); }
        Modifier system_bars_padding() const { return kiln_mod_system_bars_padding(m_M); }
        Modifier ime_padding() const { return kiln_mod_ime_padding(m_M); }
        Modifier window_insets_padding(const kiln_window_insets* i) const { return kiln_mod_window_insets_padding(m_M, i); }
        Modifier background(kiln_color c, const kiln_shape* shape = nullptr) const { return kiln_mod_background(m_M, c, shape); }
        Modifier border(float width, kiln_color c, const kiln_shape* shape = nullptr) const { return kiln_mod_border(m_M, width, c, shape); }
        Modifier clip(const kiln_shape* shape = nullptr) const { return kiln_mod_clip(m_M, shape); }
        Modifier alpha(float a) const { return kiln_mod_alpha(m_M, a); }
        Modifier rotate(float degrees) const { return kiln_mod_rotate(m_M, degrees); }
        Modifier scale(float sx, float sy) const { return kiln_mod_scale(m_M, sx, sy); }
        Modifier scale(float s) const { return kiln_mod_scale(m_M, s, s); }
        Modifier shadow(float elevation, const kiln_shape* shape = nullptr) const { return kiln_mod_shadow(m_M, elevation, shape); }
        Modifier z_index(float z) const { return kiln_mod_z_index(m_M, z); }
        Modifier clickable(Callback<kiln_fn> onClick, bool enabled = true) const { return kiln_mod_clickable(m_M, enabled, onClick.release()); }
        Modifier toggleable(bool value, Callback<kiln_fn_bool> onChange, bool enabled = true) const {
            return kiln_mod_toggleable(m_M, value, enabled, onChange.release());
        }
        Modifier selectable(bool selected, Callback<kiln_fn> onClick, bool enabled = true) const {
            return kiln_mod_selectable(m_M, selected, enabled, onClick.release());
        }
        Modifier vertical_scroll(kiln_scroll_state* s, bool enabled = true) const { return kiln_mod_vertical_scroll(m_M, s, enabled); }
        Modifier horizontal_scroll(kiln_scroll_state* s, bool enabled = true) const { return kiln_mod_horizontal_scroll(m_M, s, enabled); }
        Modifier test_tag(const char* tag) const { return kiln_mod_test_tag(m_M, tag); }
        Modifier content_description(const char* d) const { return kiln_mod_content_description(m_M, d); }
    private:
        kiln_modifier* m_M = nullptr;
    };

    // --- State ----------------------------------------------------------------------------------

    template<typename T> class State;
    template<> class State<int64_t> {
    public:
        explicit State(kiln_state_i64* s = nullptr) : m_S(s) {}
        int64_t get() const { return kiln_state_i64_get(m_S); }   // observes
        void set(int64_t v) const { kiln_state_i64_set(m_S, v); }
        kiln_state_i64* handle() const { return m_S; }
    private:
        kiln_state_i64* m_S;
    };
    template<> class State<double> {
    public:
        explicit State(kiln_state_f64* s = nullptr) : m_S(s) {}
        double get() const { return kiln_state_f64_get(m_S); }
        void set(double v) const { kiln_state_f64_set(m_S, v); }
        kiln_state_f64* handle() const { return m_S; }
    private:
        kiln_state_f64* m_S;
    };
    template<> class State<bool> {
    public:
        explicit State(kiln_state_i64* s = nullptr) : m_S(s) {}
        bool get() const { return kiln_state_i64_get(m_S) != 0; }
        void set(bool v) const { kiln_state_i64_set(m_S, v ? 1 : 0); }
        void toggle() const { set(!get()); }
        kiln_state_i64* handle() const { return m_S; }
    private:
        kiln_state_i64* m_S;
    };
    template<> class State<std::string> {
    public:
        explicit State(kiln_state_str* s = nullptr) : m_S(s) {}
        std::string get() const { return kiln_state_str_get(m_S); }
        void set(const std::string& v) const { kiln_state_str_set(m_S, v.c_str()); }
        kiln_state_str* handle() const { return m_S; }
    private:
        kiln_state_str* m_S;
    };

    // remember { mutableStateOf(initial) } for int64_t, double, bool and std::string.
    template<typename T>
    State<T> remember_state(const T& initial, Key key = {}) {
        if constexpr (std::is_same_v<T, bool>) return State<bool>(kiln_remember_state_i64(key, initial ? 1 : 0));
        else if constexpr (std::is_integral_v<T>) return State<int64_t>(kiln_remember_state_i64(key, int64_t(initial)));
        else if constexpr (std::is_floating_point_v<T>) return State<double>(kiln_remember_state_f64(key, double(initial)));
        else return State<std::string>(kiln_remember_state_str(key, std::string(initial).c_str()));
    }
    inline State<std::string> remember_state(const char* initial, Key key = {}) { return State<std::string>(kiln_remember_state_str(key, initial)); }
    // rememberSaveable: survives the activity being recreated.
    template<typename T>
    State<T> remember_saveable(const T& initial, Key key = {}) {
        if constexpr (std::is_same_v<T, bool>) return State<bool>(kiln_remember_saveable_i64(key, initial ? 1 : 0));
        else if constexpr (std::is_integral_v<T>) return State<int64_t>(kiln_remember_saveable_i64(key, int64_t(initial)));
        else if constexpr (std::is_floating_point_v<T>) return State<double>(kiln_remember_saveable_f64(key, double(initial)));
        else return State<std::string>(kiln_remember_saveable_str(key, std::string(initial).c_str()));
    }

    // remember(keys) { T(...) }: a value of any type, kept while the call stays.
    template<typename T, typename Init>
    T& remember(Init&& init, std::initializer_list<uint64_t> keys = {}, Key key = {}) {
        struct Ctx { Init* init; };
        Ctx ctx{ &init };
        kiln_remember_init ri{};
        ri.call = [](void* u) -> void* { return new T((*static_cast<Ctx*>(u)->init)()); };
        ri.user = &ctx;
        ri.drop_value = [](void* v) { delete static_cast<T*>(v); };
        return *static_cast<T*>(kiln_remember(key, keys.begin(), keys.size(), ri));
    }

    // --- Effects --------------------------------------------------------------------------------

    template<typename F>
    void side_effect(F&& effect, Key key = {}) { kiln_side_effect(key, fn<kiln_fn>(std::forward<F>(effect))); }

    // effect returns the disposer: disposable_effect({ id }, [=] { start(); return [=] { stop(); }; });
    template<typename F>
    void disposable_effect(std::initializer_list<uint64_t> keys, F&& effect, Key key = {}) {
        auto wrapped = [e = std::forward<F>(effect)]() mutable -> kiln_fn { return fn<kiln_fn>(e()); };
        kiln_disposable_effect(key, keys.begin(), keys.size(), fn<kiln_fn_effect>(std::move(wrapped)));
    }

    // A launched task's handle: ask for the next step before returning from a step.
    class Task {
    public:
        explicit Task(kiln_task* t) : m_T(t) {}
        template<typename F> void delay(int64_t millis, F&& next) const { kiln_task_delay(m_T, millis, fn<kiln_fn_task>(Adapt(std::forward<F>(next)))); }
        template<typename F> void next_frame(F&& next) const {
            auto a = [n = std::forward<F>(next)](kiln_task* t, int64_t time) mutable { n(Task(t), time); };
            kiln_task_next_frame(m_T, fn<kiln_fn_task_frame>(std::move(a)));
        }
    private:
        template<typename F> static auto Adapt(F&& f) { return [n = std::forward<F>(f)](kiln_task* t) mutable { n(Task(t)); }; }
        kiln_task* m_T;
    };
    // LaunchedEffect(keys): start(Task) runs first; steps continue it.
    template<typename F>
    void launched_effect(std::initializer_list<uint64_t> keys, F&& start, Key key = {}) {
        auto a = [s = std::forward<F>(start)](kiln_task* t) mutable { s(Task(t)); };
        kiln_launched_effect(key, keys.begin(), keys.size(), fn<kiln_fn_task>(std::move(a)));
    }

    // --- Layouts --------------------------------------------------------------------------------

    struct BoxOptions { kiln_alignment contentAlignment = KILN_ALIGN_TOP_START; bool propagateMinConstraints = false; };
    inline void box(Modifier m = {}, Callback<kiln_fn> content = {}, BoxOptions o = {}, Key key = {}) {
        kiln_box_options c;
        kiln_box_options_init(&c);
        c.content_alignment = o.contentAlignment;
        c.propagate_min_constraints = o.propagateMinConstraints;
        kiln_box(key, m, &c, content.release());
    }
    inline kiln_arrangement spaced_by(float dp, float alignBias = NAN) { return { KILN_ARRANGE_SPACED_BY, dp, alignBias }; }
    inline kiln_arrangement arrangement(kiln_arrangement_kind k) { return { k, 0, NAN }; }
    struct RowOptions { kiln_arrangement horizontalArrangement = { KILN_ARRANGE_START, 0, NAN }; float verticalAlignment = KILN_ALIGN_START; };
    inline void row(Modifier m, Callback<kiln_fn> content, RowOptions o = {}, Key key = {}) {
        kiln_row_options c;
        kiln_row_options_init(&c);
        c.horizontal_arrangement = o.horizontalArrangement;
        c.vertical_alignment = o.verticalAlignment;
        kiln_row(key, m, &c, content.release());
    }
    struct ColumnOptions { kiln_arrangement verticalArrangement = { KILN_ARRANGE_START, 0, NAN }; float horizontalAlignment = KILN_ALIGN_START; };
    inline void column(Modifier m, Callback<kiln_fn> content, ColumnOptions o = {}, Key key = {}) {
        kiln_column_options c;
        kiln_column_options_init(&c);
        c.vertical_arrangement = o.verticalArrangement;
        c.horizontal_alignment = o.horizontalAlignment;
        kiln_column(key, m, &c, content.release());
    }
    inline void spacer(Modifier m, Key key = {}) { kiln_spacer(key, m); }
    // AndroidView: an Android View inside kiln content. The callbacks get (JNIEnv*, jobject) as void*.
    struct AndroidViewOptions {
        Callback<kiln_fn_view> update, on_reset, on_release;
    };
    inline void android_view(Callback<kiln_fn_view_factory> factory, Modifier m = {}, AndroidViewOptions o = {}, Key key = {}) {
        kiln_android_view_options c{ o.update.release(), o.on_reset.release(), o.on_release.release() };
        kiln_android_view(key, m, factory.release(), &c);
    }
    inline void android_view(const char* class_name, Modifier m = {}, AndroidViewOptions o = {}, Key key = {}) {
        kiln_android_view_options c{ o.update.release(), o.on_reset.release(), o.on_release.release() };
        kiln_android_view_class(key, m, class_name, &c);
    }
    // content gets the incoming constraints in dp.
    inline void box_with_constraints(Modifier m, Callback<kiln_fn_constraints> content, BoxOptions o = {}, Key key = {}) {
        kiln_box_options c;
        kiln_box_options_init(&c);
        c.content_alignment = o.contentAlignment;
        c.propagate_min_constraints = o.propagateMinConstraints;
        kiln_box_with_constraints(key, m, &c, content.release());
    }

    // --- Text and images ------------------------------------------------------------------------

    struct TextOptions { kiln_text_overflow overflow = KILN_TEXT_OVERFLOW_CLIP; bool softWrap = true; int32_t maxLines = INT32_MAX; int32_t minLines = 1; };
    inline void basic_text(std::string_view text, Modifier m = {}, const kiln_text_style* style = nullptr, TextOptions o = {}, Key key = {}) {
        kiln_text_options c;
        kiln_text_options_init(&c);
        c.overflow = o.overflow;
        c.soft_wrap = o.softWrap;
        c.max_lines = o.maxLines;
        c.min_lines = o.minLines;
        const std::string s(text);
        kiln_basic_text(key, s.c_str(), m, style, &c);
    }
    struct ImageOptions { kiln_alignment alignment = KILN_ALIGN_CENTER; kiln_content_scale contentScale = KILN_CONTENT_SCALE_FIT; float alpha = 1; };
    inline void image(const kiln_image_bitmap* bitmap, const char* contentDescription, Modifier m = {}, ImageOptions o = {}, Key key = {}) {
        kiln_image_options c;
        kiln_image_options_init(&c);
        c.alignment = o.alignment;
        c.content_scale = o.contentScale;
        c.alpha = o.alpha;
        kiln_image(key, bitmap, contentDescription, m, &c);
    }

    class TextFieldState {
    public:
        explicit TextFieldState(kiln_text_field_state* s = nullptr) : m_S(s) {}
        std::string text() const { return kiln_text_field_state_text(m_S); }   // observes
        void set_text(const std::string& t) const { kiln_text_field_state_set_text(m_S, t.c_str()); }
        operator kiln_text_field_state*() const { return m_S; }
    private:
        kiln_text_field_state* m_S;
    };
    inline TextFieldState remember_text_field_state(const char* initial = "", Key key = {}) {
        return TextFieldState(kiln_remember_text_field_state(key, initial));
    }
    struct TextFieldOptions { bool enabled = true, readOnly = false, singleLine = false; const kiln_text_style* textStyle = nullptr; kiln_color cursorColor = 0; };
    inline void basic_text_field(TextFieldState state, Modifier m = {}, TextFieldOptions o = {}, Key key = {}) {
        kiln_text_field_options c;
        kiln_text_field_options_init(&c);
        c.enabled = o.enabled;
        c.read_only = o.readOnly;
        c.single_line = o.singleLine;
        c.text_style = o.textStyle;
        c.cursor_color = o.cursorColor;
        kiln_basic_text_field(key, state, m, &c);
    }

    // --- Scrolling, lazy lists, pagers ----------------------------------------------------------

    class ScrollState {
    public:
        explicit ScrollState(kiln_scroll_state* s = nullptr) : m_S(s) {}
        int32_t value() const { return kiln_scroll_state_value(m_S); }
        int32_t max_value() const { return kiln_scroll_state_max_value(m_S); }
        void animate_to(int32_t v, Callback<kiln_fn> done = {}) const { kiln_scroll_state_animate_to(m_S, v, done.release()); }
        void scroll_to(int32_t v) const { kiln_scroll_state_scroll_to(m_S, v, kiln_fn{}); }
        operator kiln_scroll_state*() const { return m_S; }
    private:
        kiln_scroll_state* m_S;
    };
    // rememberNestedScrollInteropConnection: kiln content inside a CoordinatorLayout (or another
    // nested-scrolling parent) scrolls the parent's app bar with it.
    inline const kiln_nested_scroll_connection* remember_nested_scroll_interop_connection(Key key = {}) {
        return kiln_remember_nested_scroll_interop_connection(key);
    }
    inline ScrollState remember_scroll_state(int32_t initial = 0, Key key = {}) { return ScrollState(kiln_remember_scroll_state(key, initial)); }

    class LazyListState {
    public:
        explicit LazyListState(kiln_lazy_list_state* s = nullptr) : m_S(s) {}
        int32_t first_visible_item_index() const { return kiln_lazy_list_state_first_visible_item_index(m_S); }
        int32_t first_visible_item_scroll_offset() const { return kiln_lazy_list_state_first_visible_item_scroll_offset(m_S); }
        bool can_scroll_forward() const { return kiln_lazy_list_state_can_scroll_forward(m_S); }
        bool can_scroll_backward() const { return kiln_lazy_list_state_can_scroll_backward(m_S); }
        void scroll_to_item(int32_t i, int32_t offset = 0) const { kiln_lazy_list_state_scroll_to_item(m_S, i, offset, kiln_fn{}); }
        void animate_scroll_to_item(int32_t i, int32_t offset = 0, Callback<kiln_fn> done = {}) const {
            kiln_lazy_list_state_animate_scroll_to_item(m_S, i, offset, done.release());
        }
        operator kiln_lazy_list_state*() const { return m_S; }
    private:
        kiln_lazy_list_state* m_S;
    };
    inline LazyListState remember_lazy_list_state(int32_t index = 0, int32_t offset = 0, Key key = {}) {
        return LazyListState(kiln_remember_lazy_list_state(key, index, offset));
    }

    // The items of a lazy list.
    class LazyScope {
    public:
        explicit LazyScope(kiln_lazy_scope* s) : m_S(s) {}
        void item(Callback<kiln_fn> content) const { kiln_lazy_item(m_S, nullptr, content.release()); }
        void item(Key key, Callback<kiln_fn> content) const { kiln_lazy_item(m_S, &key.k, content.release()); }
        void items(int32_t count, Callback<kiln_fn_int> content) const { kiln_lazy_items(m_S, count, content.release(), kiln_fn_item_key{}); }
        // key(index) gives each item's identity.
        template<typename K>
        void items(int32_t count, Callback<kiln_fn_int> content, K&& key) const {
            auto k = [f = std::forward<K>(key)](int32_t i) mutable -> kiln_key { return Key(f(i)); };
            kiln_lazy_items(m_S, count, content.release(), fn<kiln_fn_item_key>(std::move(k)));
        }
        void sticky_header(Callback<kiln_fn_int> content) const { kiln_lazy_sticky_header(m_S, nullptr, content.release()); }
    private:
        kiln_lazy_scope* m_S;
    };
    struct LazyListOptions {
        kiln_padding contentPadding{};
        bool hasArrangement = false;
        kiln_arrangement arrangement{ KILN_ARRANGE_START, 0, NAN };
        float alignment = KILN_ALIGN_START;
        bool reverseLayout = false;
        bool userScrollEnabled = true;
        const kiln_fling_behavior* flingBehavior = nullptr;
    };
    namespace detail {
        inline kiln_lazy_list_options LazyOptions(const LazyListOptions& o) {
            kiln_lazy_list_options c;
            kiln_lazy_list_options_init(&c);
            c.content_padding = o.contentPadding;
            c.has_arrangement = o.hasArrangement;
            c.arrangement = o.arrangement;
            c.alignment = o.alignment;
            c.reverse_layout = o.reverseLayout;
            c.user_scroll_enabled = o.userScrollEnabled;
            c.fling_behavior = o.flingBehavior;
            return c;
        }
        template<typename F> kiln_fn_lazy LazyContent(F&& f) {
            return fn<kiln_fn_lazy>([c = std::forward<F>(f)](kiln_lazy_scope* s) mutable { c(LazyScope(s)); });
        }
    }
    // content(LazyScope) declares the items: lazy_column(mod, [](kpp::LazyScope s) { s.items(100, ...); });
    template<typename F>
    void lazy_column(Modifier m, F&& content, LazyListState state = LazyListState(), LazyListOptions o = {}, Key key = {}) {
        const auto c = detail::LazyOptions(o);
        kiln_lazy_column(key, m, state, &c, detail::LazyContent(std::forward<F>(content)));
    }
    template<typename F>
    void lazy_row(Modifier m, F&& content, LazyListState state = LazyListState(), LazyListOptions o = {}, Key key = {}) {
        const auto c = detail::LazyOptions(o);
        kiln_lazy_row(key, m, state, &c, detail::LazyContent(std::forward<F>(content)));
    }

    class LazyGridState {
    public:
        explicit LazyGridState(kiln_lazy_grid_state* s = nullptr) : m_S(s) {}
        int32_t first_visible_item_index() const { return kiln_lazy_grid_state_first_visible_item_index(m_S); }
        int32_t first_visible_item_scroll_offset() const { return kiln_lazy_grid_state_first_visible_item_scroll_offset(m_S); }
        bool can_scroll_forward() const { return kiln_lazy_grid_state_can_scroll_forward(m_S); }
        bool can_scroll_backward() const { return kiln_lazy_grid_state_can_scroll_backward(m_S); }
        void scroll_to_item(int32_t i, int32_t offset = 0) const { kiln_lazy_grid_state_scroll_to_item(m_S, i, offset, kiln_fn{}); }
        void animate_scroll_to_item(int32_t i, int32_t offset = 0, Callback<kiln_fn> done = {}) const {
            kiln_lazy_grid_state_animate_scroll_to_item(m_S, i, offset, done.release());
        }
        operator kiln_lazy_grid_state*() const { return m_S; }
    private:
        kiln_lazy_grid_state* m_S;
    };
    inline LazyGridState remember_lazy_grid_state(int32_t index = 0, int32_t offset = 0, Key key = {}) {
        return LazyGridState(kiln_remember_lazy_grid_state(key, index, offset));
    }
    namespace grid_cells {
        inline kiln_grid_cells fixed(int32_t count) { return { KILN_GRID_CELLS_FIXED, count, 0 }; }
        inline kiln_grid_cells adaptive(float minSize) { return { KILN_GRID_CELLS_ADAPTIVE, 1, minSize }; }
        inline kiln_grid_cells fixed_size(float size) { return { KILN_GRID_CELLS_FIXED_SIZE, 1, size }; }
    }
    // The items of a lazy grid. span(index, maxCurrentLineSpan, maxLineSpan) gives an item's slots.
    class LazyGridScope {
    public:
        explicit LazyGridScope(kiln_lazy_grid_scope* s) : m_S(s) {}
        void item(Callback<kiln_fn> content) const { kiln_lazy_grid_item(m_S, nullptr, kiln_fn_grid_span{}, content.release()); }
        void item(Key key, Callback<kiln_fn> content) const { kiln_lazy_grid_item(m_S, &key.k, kiln_fn_grid_span{}, content.release()); }
        template<typename S>
        void item(Key key, S&& span, Callback<kiln_fn> content) const {
            kiln_lazy_grid_item(m_S, &key.k, fn<kiln_fn_grid_span>(std::forward<S>(span)), content.release());
        }
        void items(int32_t count, Callback<kiln_fn_int> content) const {
            kiln_lazy_grid_items(m_S, count, content.release(), kiln_fn_item_key{}, kiln_fn_grid_span{});
        }
        template<typename K>
        void items(int32_t count, Callback<kiln_fn_int> content, K&& key) const {
            auto k = [f = std::forward<K>(key)](int32_t i) mutable -> kiln_key { return Key(f(i)); };
            kiln_lazy_grid_items(m_S, count, content.release(), fn<kiln_fn_item_key>(std::move(k)), kiln_fn_grid_span{});
        }
        template<typename K, typename S>
        void items(int32_t count, Callback<kiln_fn_int> content, K&& key, S&& span) const {
            auto k = [f = std::forward<K>(key)](int32_t i) mutable -> kiln_key { return Key(f(i)); };
            kiln_lazy_grid_items(m_S, count, content.release(), fn<kiln_fn_item_key>(std::move(k)), fn<kiln_fn_grid_span>(std::forward<S>(span)));
        }
        void sticky_header(Callback<kiln_fn_int> content) const { kiln_lazy_grid_sticky_header(m_S, nullptr, content.release()); }
    private:
        kiln_lazy_grid_scope* m_S;
    };
    struct LazyGridOptions {
        kiln_padding contentPadding{};
        kiln_arrangement mainAxisArrangement{ KILN_ARRANGE_START, 0, NAN };
        kiln_arrangement crossAxisArrangement{ KILN_ARRANGE_START, 0, NAN };
        bool reverseLayout = false;
        bool userScrollEnabled = true;
        const kiln_fling_behavior* flingBehavior = nullptr;
    };
    namespace detail {
        inline kiln_lazy_grid_options GridOptions(const LazyGridOptions& o) {
            kiln_lazy_grid_options c;
            kiln_lazy_grid_options_init(&c);
            c.content_padding = o.contentPadding;
            c.main_axis_arrangement = o.mainAxisArrangement;
            c.cross_axis_arrangement = o.crossAxisArrangement;
            c.reverse_layout = o.reverseLayout;
            c.user_scroll_enabled = o.userScrollEnabled;
            c.fling_behavior = o.flingBehavior;
            return c;
        }
        template<typename F> kiln_fn_lazy_grid GridContent(F&& f) {
            return fn<kiln_fn_lazy_grid>([c = std::forward<F>(f)](kiln_lazy_grid_scope* s) mutable { c(LazyGridScope(s)); });
        }
    }
    template<typename F>
    void lazy_vertical_grid(Modifier m, kiln_grid_cells columns, F&& content, LazyGridState state = LazyGridState(), LazyGridOptions o = {},
                            Key key = {}) {
        const auto c = detail::GridOptions(o);
        kiln_lazy_vertical_grid(key, m, columns, state, &c, detail::GridContent(std::forward<F>(content)));
    }
    template<typename F>
    void lazy_horizontal_grid(Modifier m, kiln_grid_cells rows, F&& content, LazyGridState state = LazyGridState(), LazyGridOptions o = {},
                              Key key = {}) {
        const auto c = detail::GridOptions(o);
        kiln_lazy_horizontal_grid(key, m, rows, state, &c, detail::GridContent(std::forward<F>(content)));
    }

    class PagerState {
    public:
        explicit PagerState(kiln_pager_state* s = nullptr) : m_S(s) {}
        int32_t current_page() const { return kiln_pager_state_current_page(m_S); }
        int32_t settled_page() const { return kiln_pager_state_settled_page(m_S); }
        float current_page_offset_fraction() const { return kiln_pager_state_current_page_offset_fraction(m_S); }
        void animate_scroll_to_page(int32_t p, Callback<kiln_fn> done = {}) const { kiln_pager_state_animate_scroll_to_page(m_S, p, done.release()); }
        void scroll_to_page(int32_t p) const { kiln_pager_state_scroll_to_page(m_S, p, kiln_fn{}); }
        operator kiln_pager_state*() const { return m_S; }
    private:
        kiln_pager_state* m_S;
    };
    template<typename F>
    PagerState remember_pager_state(int32_t initialPage, F&& pageCount, Key key = {}) {
        return PagerState(kiln_remember_pager_state(key, initialPage, fn<kiln_fn_count>(std::forward<F>(pageCount))));
    }
    inline void horizontal_pager(PagerState state, Modifier m, Callback<kiln_fn_int> page, Key key = {}) {
        kiln_horizontal_pager(key, m, state, nullptr, page.release());
    }
    inline void vertical_pager(PagerState state, Modifier m, Callback<kiln_fn_int> page, Key key = {}) {
        kiln_vertical_pager(key, m, state, nullptr, page.release());
    }

    // --- Animation ------------------------------------------------------------------------------

    inline float animate_float(float target, const kiln_anim_spec* spec = nullptr, Key key = {}) { return kiln_animate_float(key, target, spec); }
    inline float animate_dp(float target, const kiln_anim_spec* spec = nullptr, Key key = {}) { return kiln_animate_dp(key, target, spec); }
    inline kiln_color animate_color(kiln_color target, const kiln_anim_spec* spec = nullptr, Key key = {}) { return kiln_animate_color(key, target, spec); }
    inline void animated_visibility(bool visible, Modifier m, Callback<kiln_fn> content, Key key = {}) {
        kiln_animated_visibility(key, visible, m, content.release());
    }

    // MutableTransitionState<Boolean>.
    // FocusRequester: request_focus moves focus to the focusable after .focus_requester(r).
    class FocusRequester {
    public:
        explicit FocusRequester(kiln_focus_requester* r = nullptr) : m_R(r) {}
        bool request_focus() const { return kiln_focus_requester_request_focus(m_R); }
        operator kiln_focus_requester*() const { return m_R; }
    private:
        kiln_focus_requester* m_R;
    };
    inline FocusRequester remember_focus_requester(Key key = {}) { return FocusRequester(kiln_remember_focus_requester(key)); }
    inline void clear_focus(bool force = false) { kiln_clear_focus(force); }

    class TransitionState {
    public:
        explicit TransitionState(kiln_transition_state* s = nullptr) : m_S(s) {}
        bool current() const { return kiln_transition_state_current(m_S); }
        bool target() const { return kiln_transition_state_target(m_S); }
        void set_target(bool t) const { kiln_transition_state_set_target(m_S, t); }
        bool is_idle() const { return kiln_transition_state_is_idle(m_S); }
        operator kiln_transition_state*() const { return m_S; }
    private:
        kiln_transition_state* m_S;
    };
    inline TransitionState remember_transition_state(bool initial, Key key = {}) { return TransitionState(kiln_remember_transition_state(key, initial)); }
    inline void animated_visibility(TransitionState state, Modifier m, Callback<kiln_fn> content, Key key = {}) {
        kiln_animated_visibility_state(key, state, m, content.release());
    }

    // --- Interaction sources --------------------------------------------------------------------

    class InteractionSource {
    public:
        explicit InteractionSource(kiln_interaction_source* s = nullptr) : m_S(s) {}
        bool is_pressed() const { return kiln_interaction_source_is_pressed(m_S); }
        bool is_hovered() const { return kiln_interaction_source_is_hovered(m_S); }
        bool is_focused() const { return kiln_interaction_source_is_focused(m_S); }
        bool is_dragged() const { return kiln_interaction_source_is_dragged(m_S); }
        operator kiln_interaction_source*() const { return m_S; }
    private:
        kiln_interaction_source* m_S;
    };
    inline InteractionSource remember_interaction_source(Key key = {}) { return InteractionSource(kiln_remember_interaction_source(key)); }

    // --- App ------------------------------------------------------------------------------------

    inline void set_content(kiln_app* app, Callback<kiln_fn> content) { kiln_app_set_content(app, content.release()); }

    // --- Volition (debug builds answer it; see kiln.h) -----------------------------------------

    class VolitionFields {
    public:
        explicit VolitionFields(kiln_volition_fields* f) : m_F(f) {}
        void string(const std::string& key, const std::string& value) { kiln_volition_field_string(m_F, key.c_str(), value.c_str()); }
        void number(const std::string& key, double value) { kiln_volition_field_number(m_F, key.c_str(), value); }
        void boolean(const std::string& key, bool value) { kiln_volition_field_bool(m_F, key.c_str(), value); }
        void strings(const std::string& key, const std::vector<std::string>& values) {
            std::vector<const char*> p;
            for (const auto& v : values) p.push_back(v.c_str());
            kiln_volition_field_strings(m_F, key.c_str(), p.data(), p.size());
        }
    private:
        kiln_volition_fields* m_F;
    };

    struct VolitionAdapter {
        std::function<void(VolitionFields&)> state;                 // `where` fields
        std::function<std::vector<std::string>()> destinations;     // "name:<argument>  what it does"
        // `go name[:argument]`: the answer, or nothing when `name` is not a destination here.
        std::function<std::optional<std::string>(const std::string& name, const std::optional<std::string>& argument)> go;
    };

    // Registered while it lives (move-only).
    class VolitionRegistration {
    public:
        VolitionRegistration() = default;
        explicit VolitionRegistration(kiln_volition_registration* r) : m_R(r) {}
        VolitionRegistration(VolitionRegistration&& o) noexcept : m_R(o.m_R) { o.m_R = nullptr; }
        VolitionRegistration& operator=(VolitionRegistration&& o) noexcept { std::swap(m_R, o.m_R); return *this; }
        ~VolitionRegistration() { if (m_R) kiln_volition_unregister(m_R); }
    private:
        kiln_volition_registration* m_R = nullptr;
    };

    inline VolitionRegistration volition_register(VolitionAdapter adapter) {
        kiln_volition_adapter a;
        kiln_volition_adapter_init(&a);
        if (adapter.state)
            a.state = fn<kiln_fn_volition_state>([f = std::move(adapter.state)](kiln_volition_fields* fields) { VolitionFields v(fields); f(v); });
        if (adapter.destinations)
            a.destinations = fn<kiln_fn_volition_lines>([f = std::move(adapter.destinations)](kiln_volition_lines* lines) {
                for (const auto& line : f()) kiln_volition_line(lines, line.c_str());
            });
        if (adapter.go)
            a.go = fn<kiln_fn_volition_go>([f = std::move(adapter.go), answer = std::string()](const char* name, const char* argument) mutable -> const char* {
                auto r = f(name, argument ? std::optional<std::string>(argument) : std::nullopt);
                if (!r) return nullptr;
                answer = std::move(*r);   // kiln copies it before the next call
                return answer.c_str();
            });
        return VolitionRegistration(kiln_volition_register(&a));
    }

}
