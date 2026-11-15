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
        // f(kiln_measure_scope*, kiln_measurable*, kiln_int_constraints) -> kiln_measure_result*: Modifier.layout.
        template<typename F> Modifier layout(F&& f) const { return kiln_mod_layout(m_M, fn<kiln_fn_layout_modifier>(std::forward<F>(f))); }
        Modifier layout_id(const char* id) const { return kiln_mod_layout_id(m_M, id); }
        // f(kiln_draw_scope*): drawBehind / drawWithContent (f calls kiln_draw_content).
        template<typename F> Modifier draw_behind(F&& f) const { return kiln_mod_draw_behind(m_M, fn<kiln_fn_draw>(std::forward<F>(f))); }
        template<typename F> Modifier draw_with_content(F&& f) const { return kiln_mod_draw_with_content(m_M, fn<kiln_fn_draw>(std::forward<F>(f))); }
        // f(const kiln_layout_coordinates*): valid during the call.
        template<typename F> Modifier on_globally_positioned(F&& f) const {
            return kiln_mod_on_globally_positioned(m_M, fn<kiln_fn_layout_coordinates>(std::forward<F>(f)));
        }
        template<typename F> Modifier on_placed(F&& f) const { return kiln_mod_on_placed(m_M, fn<kiln_fn_layout_coordinates>(std::forward<F>(f))); }
        template<typename F> Modifier on_size_changed(F&& f) const { return kiln_mod_on_size_changed(m_M, fn<kiln_fn_size>(std::forward<F>(f))); }
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

    struct AsyncImageOptions {
        kiln_alignment alignment = KILN_ALIGN_CENTER;
        kiln_content_scale contentScale = KILN_CONTENT_SCALE_FIT;
        float alpha = 1;
        kiln_color placeholderColor = KILN_COLOR_UNSPECIFIED, errorColor = KILN_COLOR_UNSPECIFIED;
        const kiln_image_bitmap* placeholder = nullptr;
        const kiln_image_bitmap* error = nullptr;
    };
    // See kiln_async_image; onState gets 0 (loading), 1 (loaded) or 2 (failed).
    inline void async_image(const std::string& model, const char* contentDescription, Modifier m = {}, AsyncImageOptions o = {},
                            Callback<kiln_fn_int> onState = {}, Key key = {}) {
        kiln_async_image_options c;
        kiln_async_image_options_init(&c);
        c.alignment = o.alignment;
        c.content_scale = o.contentScale;
        c.alpha = o.alpha;
        c.placeholder_color = o.placeholderColor;
        c.error_color = o.errorColor;
        c.placeholder = o.placeholder;
        c.error = o.error;
        c.on_state = onState.release();
        kiln_async_image(key, model.c_str(), contentDescription, m, &c);
    }

    // A media player remembered at the call; see kiln_remember_player.
    class Player {
    public:
        explicit Player(Key key = {}) : m_P(kiln_remember_player(key)) {}
        void setMediaUri(const std::string& uri) { kiln_player_set_media_uri(m_P, uri.c_str()); }
        void play() { kiln_player_play(m_P); }
        void pause() { kiln_player_pause(m_P); }
        void seekTo(int64_t ms) { kiln_player_seek_to(m_P, ms); }
        void setRepeat(bool repeat) { kiln_player_set_repeat(m_P, repeat); }
        void setVolume(float volume) { kiln_player_set_volume(m_P, volume); }
        // usage / contentType: AudioAttributes.USAGE_* / CONTENT_TYPE_* (1 media, 2 music).
        void setAudioAttributes(int32_t usage, int32_t contentType, bool handleAudioFocus) {
            kiln_player_set_audio_attributes(m_P, usage, contentType, handleAudioFocus);
        }
        void setHandleAudioBecomingNoisy(bool handle) { kiln_player_set_handle_audio_becoming_noisy(m_P, handle); }
        kiln_player_state state() const {
            kiln_player_state s;
            kiln_player_get_state(m_P, &s);
            return s;
        }
        // The playlist; see kiln_player_set_media_items.
        void setMediaItems(const std::vector<kiln_media_item>& items, bool resetPosition = true) {
            kiln_player_set_media_items(m_P, items.data(), items.size(), resetPosition);
        }
        void addMediaItems(const std::vector<kiln_media_item>& items, int32_t index = -1) { kiln_player_add_media_items(m_P, index, items.data(), items.size()); }
        void moveMediaItems(int32_t from, int32_t to, int32_t newIndex) { kiln_player_move_media_items(m_P, from, to, newIndex); }
        void removeMediaItems(int32_t from, int32_t to) { kiln_player_remove_media_items(m_P, from, to); }
        void prepare() { kiln_player_prepare(m_P); }
        void stop() { kiln_player_stop(m_P); }
        void seekTo(int32_t mediaItemIndex, int64_t ms) { kiln_player_seek_to_media_item(m_P, mediaItemIndex, ms); }
        void seekToNext() { kiln_player_seek_to_next(m_P); }
        void seekToPrevious() { kiln_player_seek_to_previous(m_P); }
        void seekBack() { kiln_player_seek_back(m_P); }
        void seekForward() { kiln_player_seek_forward(m_P); }
        void setRepeatMode(kiln_repeat_mode mode) { kiln_player_set_repeat_mode(m_P, mode); }
        void setShuffleModeEnabled(bool enabled) { kiln_player_set_shuffle_mode_enabled(m_P, enabled); }
        void setPlaybackSpeed(float speed) { kiln_player_set_playback_parameters(m_P, speed, 1.0f); }
        void setPlaybackParameters(float speed, float pitch) { kiln_player_set_playback_parameters(m_P, speed, pitch); }
        // Tracks; see kiln_player_get_track.
        struct Track {
            std::string groupId, language, label, mimeType;
            int32_t type = 0, index = 0;
            bool supported = false, selected = false;
        };
        std::vector<Track> tracks() const {
            std::vector<Track> out;
            for (size_t i = 0, n = kiln_player_track_count(m_P); i < n; ++i) {
                kiln_track t;
                if (kiln_player_get_track(m_P, i, &t)) out.push_back({ t.group_id, t.language, t.label, t.mime_type, t.type, t.index, t.supported, t.selected });
            }
            return out;
        }
        void setPreferredAudioLanguages(const std::vector<const char*>& languages) {
            kiln_player_set_preferred_audio_languages(m_P, languages.data(), languages.size());
        }
        void setPreferredTextLanguages(const std::vector<const char*>& languages) {
            kiln_player_set_preferred_text_languages(m_P, languages.data(), languages.size());
        }
        void setTrackTypeDisabled(int32_t type, bool disabled) { kiln_player_set_track_type_disabled(m_P, type, disabled); }
        void selectTrack(int32_t type, const char* groupId, int32_t track) { kiln_player_select_track(m_P, type, groupId, track); }
        kiln_player* get() const { return m_P; }

    private:
        kiln_player* m_P;
    };
    // Text to speech, remembered at the call; see kiln_remember_tts. f(event, utteranceId, a, b).
    class TextToSpeech {
    public:
        explicit TextToSpeech(const char* engine = nullptr, Key key = {}) : m_T(kiln_remember_tts(key, engine)) {}
        template<typename F> void setListener(F&& f) { kiln_tts_set_listener(m_T, fn<kiln_fn_tts_event>(std::forward<F>(f))); }
        bool speak(const std::string& text, bool flush = false, const char* utteranceId = nullptr) {
            return kiln_tts_speak(m_T, text.c_str(), flush, utteranceId);
        }
        int32_t status() const { return kiln_tts_status(m_T); }   // 0 binding, 1 ready, 2 no engine
        void stop() { kiln_tts_stop(m_T); }
        bool isSpeaking() const { return kiln_tts_is_speaking(m_T); }
        void setSpeechRate(float rate) { kiln_tts_set_speech_rate(m_T, rate); }
        void setPitch(float pitch) { kiln_tts_set_pitch(m_T, pitch); }
        int32_t setLanguage(const char* languageTag) { return kiln_tts_set_language(m_T, languageTag); }
        kiln_tts* get() const { return m_T; }

    private:
        kiln_tts* m_T;
    };
    // Speech to text, remembered at the call; see kiln_remember_speech_recognizer. f(event, text, value).
    class SpeechRecognizer {
    public:
        explicit SpeechRecognizer(bool onDevice = false, Key key = {}) : m_R(kiln_remember_speech_recognizer(key, onDevice)) {}
        template<typename F> void setListener(F&& f) { kiln_speech_recognizer_set_listener(m_R, fn<kiln_fn_recognition>(std::forward<F>(f))); }
        void start(const char* language = nullptr, bool partialResults = true) { kiln_speech_recognizer_start(m_R, language, partialResults); }
        void stop() { kiln_speech_recognizer_stop(m_R); }
        void cancel() { kiln_speech_recognizer_cancel(m_R); }
        static bool available(bool onDevice = false) { return kiln_speech_recognition_available(onDevice); }
        kiln_speech_recognizer* get() const { return m_R; }

    private:
        kiln_speech_recognizer* m_R;
    };
    // A media session for a player, remembered at the call (its options are taken the first
    // time); see kiln_remember_media_session.
    class MediaSession {
    public:
        struct Options {
            Callback<kiln_fn> onSkipToPrevious, onSkipToNext;
            const char* sessionActivityUri = nullptr;
            bool notification = true;
            Callback<kiln_fn_str> onCustomCommand;                // (const char* action)
            Callback<kiln_fn_rating> onSetRating;                 // (kind, rated, liked, maxStars, value)
            Callback<kiln_fn_media_request> onMediaRequest;       // (mediaId, uri, query) -> the URI to play, or nullptr
        };
        // A CommandButton (kiln_command_button): a custom action or a player command.
        struct Button {
            int32_t icon = 0;
            std::string displayName, customAction, iconResource;
            int32_t playerCommand = 0;
            std::vector<int32_t> slots;
            bool enabled = true;
            int32_t compactViewIndex = -1;
        };
        explicit MediaSession(const Player& player) : MediaSession(player, Options{}) {}
        MediaSession(const Player& player, Options o, Key key = {}) {
            kiln_media_session_options c;
            kiln_media_session_options_init(&c);
            c.on_skip_to_previous = o.onSkipToPrevious.release();
            c.on_skip_to_next = o.onSkipToNext.release();
            c.session_activity_uri = o.sessionActivityUri;
            c.notification = o.notification;
            c.on_custom_command = o.onCustomCommand.release();
            c.on_set_rating = o.onSetRating.release();
            c.on_media_request = o.onMediaRequest.release();
            m_S = kiln_remember_media_session(key, player.get(), &c);
        }
        void setMediaButtonPreferences(const std::vector<Button>& buttons) {
            std::vector<kiln_command_button> c(buttons.size());
            for (std::size_t i = 0; i < buttons.size(); ++i) {
                const Button& b = buttons[i];
                kiln_command_button_init(&c[i]);
                c[i].icon = b.icon;
                c[i].icon_resource = b.iconResource.empty() ? nullptr : b.iconResource.c_str();
                c[i].display_name = b.displayName.c_str();
                c[i].custom_action = b.customAction.empty() ? nullptr : b.customAction.c_str();
                c[i].player_command = b.playerCommand;
                c[i].slots = b.slots.data();
                c[i].slot_count = b.slots.size();
                c[i].enabled = b.enabled;
                c[i].compact_view_index = b.compactViewIndex;
            }
            kiln_media_session_set_media_button_preferences(m_S, c.data(), c.size());
        }
        void setExtras(const std::vector<std::pair<std::string, std::string>>& extras) {
            std::vector<const char*> k, v;
            for (const auto& [a, b] : extras) {
                k.push_back(a.c_str());
                v.push_back(b.c_str());
            }
            kiln_media_session_set_extras(m_S, k.data(), v.data(), k.size());
        }
        void setMetadata(const std::string& title, const std::string& artist = {}, const std::string& albumTitle = {},
                         const std::string& artworkUri = {}) {
            kiln_media_session_set_metadata(m_S, title.c_str(), artist.c_str(), albumTitle.c_str(), artworkUri.c_str());
        }

    private:
        kiln_media_session* m_S;
    };
    inline void video_surface(const Player& player, Modifier m = {}, bool keepAspectRatio = true, Key key = {}) {
        kiln_video_surface(key, m, player.get(), keepAspectRatio);
    }
    inline void camera_preview(const char* cameraId, Modifier m = {}, Key key = {}) { kiln_camera_preview(key, m, cameraId); }
    // A camera with its use cases, remembered at the call; see kiln_remember_camera.
    class Camera {
    public:
        explicit Camera(const char* cameraId, bool imageCapture = true, int32_t captureMode = 1, int32_t flashMode = 2, Key key = {})
            : m_C(kiln_remember_camera(key, cameraId, imageCapture, captureMode, flashMode)) {}
        kiln_camera_state state() const {
            kiln_camera_state s;
            kiln_camera_get_state(m_C, &s);
            return s;
        }
        void setZoomRatio(float ratio, Callback<kiln_fn_camera_done> done = {}) { kiln_camera_set_zoom_ratio(m_C, ratio, done.release()); }
        void setLinearZoom(float z, Callback<kiln_fn_camera_done> done = {}) { kiln_camera_set_linear_zoom(m_C, z, done.release()); }
        void enableTorch(bool on, Callback<kiln_fn_camera_done> done = {}) { kiln_camera_enable_torch(m_C, on, done.release()); }
        void setExposureCompensationIndex(int32_t i, Callback<kiln_fn_camera_done> done = {}) {
            kiln_camera_set_exposure_compensation_index(m_C, i, done.release());
        }
        void focusAndMeter(float x, float y, Callback<kiln_fn_bool> done = {}) { kiln_camera_focus_and_meter(m_C, x, y, done.release()); }
        void cancelFocusAndMetering() { kiln_camera_cancel_focus_and_metering(m_C); }
        void setFlashMode(int32_t mode) { kiln_camera_set_flash_mode(m_C, mode); }
        // done(error: -1 none, uri or message).
        void takePicture(const std::string& file, Callback<kiln_fn_picture> done, int32_t displayRotation = 0) {
            kiln_camera_take_picture(m_C, file.c_str(), displayRotation, done.release());
        }
        void preview(Modifier m = {}, bool tapToFocus = true, bool pinchToZoom = true, Key key = {}) {
            kiln_camera_preview_of(key, m, m_C, tapToFocus, pinchToZoom);
        }
        kiln_camera* get() const { return m_C; }

    private:
        kiln_camera* m_C;
    };

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
    // Layout: measure(kiln_measure_scope*, kiln_measurable* const*, size_t, kiln_int_constraints) -> kiln_measure_result*.
    template<typename M> void layout(Modifier m, Callback<kiln_fn> content, M&& measure, Key key = {}) {
        kiln_layout(key, m, content.release(), fn<kiln_fn_measure>(std::forward<M>(measure)));
    }
    // kiln_measure_scope_layout with a callable placement.
    template<typename P> kiln_measure_result* measure_layout(kiln_measure_scope* s, int32_t w, int32_t h, P&& place) {
        return kiln_measure_scope_layout(s, w, h, fn<kiln_fn_place>(std::forward<P>(place)));
    }
    // Content from pickers and shares (see kiln_content_open_fd): the bytes, or nullopt.
    inline std::optional<std::vector<uint8_t>> read_content(const char* uri) {
        size_t n = 0;
        uint8_t* p = kiln_content_read(uri, &n);
        if (!p) return std::nullopt;
        std::vector<uint8_t> out(p, p + n);
        kiln_content_free(p);
        return out;
    }
    inline bool write_content(const char* uri, const std::vector<uint8_t>& bytes, const char* mode = "wt") {
        return kiln_content_write(uri, bytes.data(), bytes.size(), mode);
    }
    inline std::string content_display_name(const char* uri) { const char* s = kiln_content_display_name(uri); return s ? s : ""; }

    // The activity's launch intent (NULL off Android) and OnNewIntent; see kiln_activity_intent.
    inline const kiln_intent* activity_intent() { return kiln_activity_intent(); }
    template<typename F> void on_new_intent(F&& f, Key key = {}) { kiln_on_new_intent(key, fn<kiln_fn_intent>(std::forward<F>(f))); }

    // App resources by name (kiln_res_c.h's R_string_app_name...); see kiln_string_resource.
    namespace detail {
        inline kiln_format_arg FormatArgOf(const char* s) { return { KILN_FORMAT_STRING, s, 0, 0 }; }
        inline kiln_format_arg FormatArgOf(const std::string& s) { return { KILN_FORMAT_STRING, s.c_str(), 0, 0 }; }
        template<typename T> requires std::is_integral_v<T> kiln_format_arg FormatArgOf(T v) { return { KILN_FORMAT_INT, nullptr, int64_t(v), 0 }; }
        template<typename T> requires std::is_floating_point_v<T> kiln_format_arg FormatArgOf(T v) { return { KILN_FORMAT_DOUBLE, nullptr, 0, double(v) }; }
        inline std::string Str(const char* s) { return s ? std::string(s) : std::string(); }
    }
    template<typename... A> std::string string_resource(const char* name, const A&... args) {
        const kiln_format_arg a[] = { detail::FormatArgOf(args)..., kiln_format_arg{} };
        return detail::Str(kiln_string_resource(name, a, sizeof...(A)));
    }
    template<typename... A> std::string plural_string_resource(const char* name, int32_t quantity, const A&... args) {
        const kiln_format_arg a[] = { detail::FormatArgOf(args)..., kiln_format_arg{} };
        return detail::Str(kiln_plural_string_resource(name, quantity, a, sizeof...(A)));
    }
    // The app's private directories; see kiln_files_dir.
    inline std::string files_dir() { return detail::Str(kiln_files_dir()); }
    inline std::string cache_dir() { return detail::Str(kiln_cache_dir()); }
    inline std::string no_backup_files_dir() { return detail::Str(kiln_no_backup_files_dir()); }
    inline std::string database_path(const char* name) { return detail::Str(kiln_database_path(name)); }

    // Preferences (DataStore); see kiln_preferences_open. The getters are for content.
    class Preferences {
    public:
        explicit Preferences(const char* name) : m_P(kiln_preferences_open(name)) {}
        bool get(const char* name, bool fallback, Key key = {}) const { return kiln_preferences_bool(key, m_P, name, fallback); }
        int64_t get(const char* name, int64_t fallback, Key key = {}) const { return kiln_preferences_i64(key, m_P, name, fallback); }
        int64_t get(const char* name, int fallback, Key key = {}) const { return kiln_preferences_i64(key, m_P, name, fallback); }
        double get(const char* name, double fallback, Key key = {}) const { return kiln_preferences_f64(key, m_P, name, fallback); }
        std::string get(const char* name, const char* fallback, Key key = {}) const { return detail::Str(kiln_preferences_str(key, m_P, name, fallback)); }
        void set(const char* name, bool v) const { kiln_preferences_set_bool(m_P, name, v); }
        void set(const char* name, int64_t v) const { kiln_preferences_set_i64(m_P, name, v); }
        void set(const char* name, int v) const { kiln_preferences_set_i64(m_P, name, v); }
        void set(const char* name, double v) const { kiln_preferences_set_f64(m_P, name, v); }
        void set(const char* name, const char* v) const { kiln_preferences_set_str(m_P, name, v); }
        void set(const char* name, const std::string& v) const { kiln_preferences_set_str(m_P, name, v.c_str()); }
        void remove(const char* name) const { kiln_preferences_remove(m_P, name); }
        void clear() const { kiln_preferences_clear(m_P); }
    private:
        kiln_preferences* m_P;
    };

    // Secrets at rest; see kiln_secure_store_open.
    class SecureStore {
    public:
        explicit SecureStore(const char* name) : m_S(kiln_secure_store_open(name)) {}
        std::optional<std::string> get(const char* key) const {
            const char* v = kiln_secure_store_get(m_S, key);
            return v ? std::optional<std::string>(v) : std::nullopt;
        }
        void set(const char* key, const std::string& value) const { kiln_secure_store_set(m_S, key, value.c_str()); }
        void remove(const char* key) const { kiln_secure_store_remove(m_S, key); }
        void clear() const { kiln_secure_store_clear(m_S); }
    private:
        kiln_secure_store* m_S;
    };

    // HTTP; see kiln_http_fetch. `done` runs on the main thread with the response (status 0 and
    // an error when there was none).
    struct HttpRequest {
        std::string url;
        std::string method = "GET";
        std::vector<std::pair<std::string, std::string>> headers;
        std::string body;
        std::string body_file, response_file;
        int connect_timeout_ms = 0, read_timeout_ms = 0;   // 0: 10 s; negative: no limit
        bool follow_redirects = true;
    };
    struct HttpResponse {
        int status = 0;
        std::string status_text, url, body, error;
        const kiln_http_response* raw = nullptr;   // during the callback
        bool ok() const { return status >= 200 && status < 300; }
        std::string header(const char* name) const { return raw ? detail::Str(kiln_http_response_header(raw, name)) : std::string(); }
    };
    template<typename F>
    uint64_t http_fetch(const HttpRequest& request, F&& done) {
        std::vector<const char*> headers;
        for (const auto& [name, value] : request.headers) { headers.push_back(name.c_str()); headers.push_back(value.c_str()); }
        kiln_http_request r{};
        r.size = sizeof r;
        r.url = request.url.c_str();
        r.method = request.method.c_str();
        r.headers = headers.data();
        r.header_count = request.headers.size();
        r.body = request.body.data();
        r.body_size = request.body.size();
        r.body_file = request.body_file.empty() ? nullptr : request.body_file.c_str();
        r.response_file = request.response_file.empty() ? nullptr : request.response_file.c_str();
        r.connect_timeout_ms = request.connect_timeout_ms;
        r.read_timeout_ms = request.read_timeout_ms;
        r.no_redirects = !request.follow_redirects;
        return kiln_http_fetch(&r, fn<kiln_fn_http>([done = std::forward<F>(done)](const kiln_http_response* c) mutable {
            HttpResponse out;
            out.status = c->status;
            out.status_text = detail::Nz(c->status_text);
            out.url = detail::Nz(c->url);
            out.body.assign(c->body ? c->body : "", c->body_size);
            out.error = detail::Nz(c->error);
            out.raw = c;
            done(out);
        }));
    }
    inline void http_cancel(uint64_t request) { kiln_http_cancel(request); }
    // See kiln_share.
    inline bool share(const std::string& text, const std::string& subject = {}, const std::vector<std::string>& streams = {},
                      const std::string& mimeType = {}, const char* chooserTitle = nullptr) {
        std::vector<const char*> s;
        for (const auto& u : streams) s.push_back(u.c_str());
        return kiln_share(text.c_str(), subject.c_str(), s.data(), s.size(), mimeType.c_str(), chooserTitle);
    }
    inline bool open_uri(const std::string& uri) { return kiln_open_uri(uri.c_str()); }
    // See kiln_set_application_locales: "de-DE", "fr-CA,fr", or "" for the system's.
    inline void set_application_locales(const std::string& tags) { kiln_set_application_locales(tags.c_str()); }
    inline std::string get_application_locales() {
        const char* t = kiln_get_application_locales();
        return t ? t : "";
    }
    // See kiln_get_uri_for_file; empty for a file outside the app's folders.
    inline std::string get_uri_for_file(const std::string& path, const std::string& displayName = {}) {
        const char* u = kiln_get_uri_for_file(path.c_str(), displayName.c_str());
        return u ? u : "";
    }
    inline bool should_show_permission_rationale(const char* permission) { return kiln_should_show_permission_rationale(permission); }

    // See kiln_launch_custom_tab and kiln_set_dynamic_shortcuts.
    inline bool launch_custom_tab(const std::string& url, kiln_color toolbarColor = KILN_COLOR_UNSPECIFIED, bool showTitle = false) {
        return kiln_launch_custom_tab(url.c_str(), toolbarColor, showTitle);
    }
    inline bool set_dynamic_shortcuts(const std::vector<kiln_shortcut>& shortcuts) { return kiln_set_dynamic_shortcuts(shortcuts.data(), shortcuts.size()); }
    inline void remove_all_dynamic_shortcuts() { kiln_remove_all_dynamic_shortcuts(); }
    inline void report_shortcut_used(const char* id) { kiln_report_shortcut_used(id); }

    // Notifications; see kiln_notify.
    inline bool create_notification_channel(const char* id, const char* name, int32_t importance = 3) { return kiln_create_notification_channel(id, name, importance); }
    inline bool notify(int32_t id, const char* channel, const std::string& title, const std::string& text, const std::string& data = {}) {
        return kiln_notify(id, channel, title.c_str(), text.c_str(), data.c_str());
    }
    inline void cancel_notification(int32_t id) { kiln_cancel_notification(id); }

    // Device; see kiln_vibrate_waveform and the calls after it.
    inline bool vibrate(const std::vector<int64_t>& timingsMs, const std::vector<int32_t>& amplitudes = {}, int32_t repeat = -1) {
        return kiln_vibrate_waveform(timingsMs.data(), amplitudes.empty() ? nullptr : amplitudes.data(), timingsMs.size(), repeat);
    }
    template<typename F> uint64_t sensor_listen(int32_t type, int32_t periodUs, F&& f) { return kiln_sensor_listen(type, periodUs, fn<kiln_fn_sensor>(std::forward<F>(f))); }
    inline void sensor_stop(uint64_t id) { kiln_sensor_stop(id); }
    template<typename F> void current_location(F&& f) { kiln_current_location(fn<kiln_fn_location>(std::forward<F>(f))); }
    template<typename F> void authenticate(const char* title, F&& f, int32_t authenticators = 0) {
        kiln_authenticate(title, nullptr, nullptr, authenticators, fn<kiln_fn_biometric>(std::forward<F>(f)));
    }

    // Background work; see kiln_work_enqueue. f(name, input, attempt, done) answers with
    // kiln_work_complete(done, ...).
    template<typename F> void register_worker(const char* name, F&& f) {
        kiln_work_register_worker(name, fn<kiln_fn_worker>(std::forward<F>(f)));
    }
    inline void enqueue_work(const char* name, const kiln_work_request& request, bool keepExisting = false) {
        kiln_work_enqueue(name, &request, keepExisting);
    }
    inline void cancel_work(const char* name) { kiln_work_cancel(name); }

    // Alarms at a time; see kiln_alarm_schedule. f(name, payload, triggerAtMs) runs when it goes off.
    template<typename F> void register_alarm_handler(const char* name, F&& f) {
        kiln_alarm_register_handler(name, fn<kiln_fn_alarm>(std::forward<F>(f)));
    }
    inline bool schedule_alarm(const char* name, int64_t triggerAtMs, const char* payload = nullptr,
                               kiln_alarm_kind kind = KILN_ALARM_EXACT_WHILE_IDLE) {
        kiln_alarm a{};
        a.name = name;
        a.trigger_at_ms = triggerAtMs;
        a.kind = kind;
        a.payload = payload;
        return kiln_alarm_schedule(&a);
    }
    inline void cancel_alarm(const char* name) { kiln_alarm_cancel(name); }
    inline bool alarm_pending(const char* name) { return kiln_alarm_is_pending(name); }

    // Home-screen widgets; see kiln_glance_register_widget. Content functions take lambdas.
    namespace glance {
        template<typename F> void box(const kiln_glance_modifier& m, kiln_alignment a, F&& content) { kiln_glance_box(&m, a, fn<kiln_fn>(std::forward<F>(content))); }
        template<typename F> void row(const kiln_glance_modifier& m, kiln_alignment a, F&& content) { kiln_glance_row(&m, a, fn<kiln_fn>(std::forward<F>(content))); }
        template<typename F> void column(const kiln_glance_modifier& m, kiln_alignment a, F&& content) { kiln_glance_column(&m, a, fn<kiln_fn>(std::forward<F>(content))); }
        inline void text(const std::string& t, const kiln_glance_modifier& m = {}, float fontSize = 0, uint32_t color = 0, int32_t weight = 400, int32_t maxLines = 0) {
            kiln_glance_text(t.c_str(), &m, fontSize, color, weight, maxLines);
        }
        inline void button(const std::string& t, kiln_glance_action onClick, const kiln_glance_modifier& m = {}) { kiln_glance_button(t.c_str(), onClick, &m); }
        inline kiln_glance_action run_callback(const char* key, const char* params = "") { return { 2, nullptr, key, params }; }
        inline kiln_glance_action open_app(const char* uri = "") { return { 1, uri, nullptr, nullptr }; }
        template<typename F> void register_widget(const char* name, int32_t sizeMode, F&& content) {
            kiln_glance_register_widget(name, sizeMode, fn<kiln_fn_glance_content>(std::forward<F>(content)));
        }
        template<typename F> void register_action(const char* key, F&& action) {
            kiln_glance_register_action(key, fn<kiln_fn_glance_action>(std::forward<F>(action)));
        }
        inline void update_all(const char* name) { kiln_glance_update_all(name); }
    }

    // See kiln_keep_splash_screen.
    inline void keep_splash_screen(bool keep) { kiln_keep_splash_screen(keep); }

    // Logging; see kiln_log. Messages below info are kept only for enabled tags.
    inline void log(kiln_log_level level, const char* tag, const std::string& message) {
        if (kiln_log_is_loggable(level, tag)) kiln_log(level, tag, message.c_str());
    }
    inline void log_info(const char* tag, const std::string& message) { log(KILN_LOG_INFO, tag, message); }
    inline void log_warn(const char* tag, const std::string& message) { log(KILN_LOG_WARN, tag, message); }
    inline void log_error(const char* tag, const std::string& message) { log(KILN_LOG_ERROR, tag, message); }
    inline void log_debug(const char* tag, const std::string& message) { log(KILN_LOG_DEBUG, tag, message); }

    // A trace section for the scope (kiln_trace_begin / kiln_trace_end).
    class TraceSection {
    public:
        explicit TraceSection(const char* name) { kiln_trace_begin(name); }
        ~TraceSection() { kiln_trace_end(); }
        TraceSection(const TraceSection&) = delete;
        TraceSection& operator=(const TraceSection&) = delete;
    };

    // See kiln_enter_picture_in_picture.
    inline bool enter_picture_in_picture(int32_t aspectWidth = 0, int32_t aspectHeight = 0) { return kiln_enter_picture_in_picture(aspectWidth, aspectHeight); }
    inline bool set_picture_in_picture_params(int32_t aspectWidth, int32_t aspectHeight, bool autoEnter) {
        return kiln_set_picture_in_picture_params(aspectWidth, aspectHeight, autoEnter);
    }
    inline bool is_in_picture_in_picture() { return kiln_is_in_picture_in_picture(); }
    inline void set_requested_orientation(int32_t orientation) { kiln_set_requested_orientation(orientation); }
    inline bool network_connected(Key key = {}) { return kiln_network_connected(key); }

    inline std::vector<std::string> string_array_resource(const char* name) {
        std::vector<std::string> out;
        for (size_t i = 0, n = kiln_string_array_resource_size(name); i < n; ++i) out.push_back(detail::Str(kiln_string_array_resource(name, i)));
        return out;
    }
    inline kiln_color color_resource(const char* name) { kiln_color c = KILN_COLOR_UNSPECIFIED; kiln_color_resource(name, &c); return c; }
    inline float dimension_resource(const char* name) { float d = 0; kiln_dimension_resource(name, &d); return d; }
    inline int32_t integer_resource(const char* name) { int32_t v = 0; kiln_integer_resource(name, &v); return v; }
    inline bool bool_resource(const char* name) { bool v = false; kiln_bool_resource(name, &v); return v; }

    // Canvas: a box that only draws, with on_draw(kiln_draw_scope*).
    template<typename F> void canvas(Modifier m, F&& onDraw, Key key = {}) { kiln_canvas(key, m, fn<kiln_fn_draw>(std::forward<F>(onDraw))); }
    // A path owned by this object.
    class Path {
    public:
        Path() : m_P(kiln_path_new()) {}
        ~Path() { kiln_path_free(m_P); }
        Path(const Path&) = delete;
        Path& operator=(const Path&) = delete;
        Path& move_to(float x, float y) { kiln_path_move_to(m_P, x, y); return *this; }
        Path& line_to(float x, float y) { kiln_path_line_to(m_P, x, y); return *this; }
        Path& quadratic_to(float x1, float y1, float x2, float y2) { kiln_path_quadratic_to(m_P, x1, y1, x2, y2); return *this; }
        Path& cubic_to(float x1, float y1, float x2, float y2, float x3, float y3) { kiln_path_cubic_to(m_P, x1, y1, x2, y2, x3, y3); return *this; }
        Path& add_rect(kiln_rect r) { kiln_path_add_rect(m_P, r); return *this; }
        Path& add_oval(kiln_rect r) { kiln_path_add_oval(m_P, r); return *this; }
        Path& add_round_rect(kiln_rect r, float radius) { kiln_path_add_round_rect(m_P, r, radius); return *this; }
        Path& close() { kiln_path_close(m_P); return *this; }
        operator const kiln_path*() const { return m_P; }
    private:
        kiln_path* m_P;
    };

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
