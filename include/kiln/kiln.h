/* kiln C ABI: the app entry point, composition (keys, state, effects), modifiers, shapes, text
 * styles and foundation's layouts, text, images, lazy lists and pagers. Material 3 is in
 * kiln_m3.h. The rules:
 *
 * - No C++ types or exceptions cross this boundary. A call that fails logs and does nothing.
 * - Composable calls (layouts, components, remember, effects) are valid only inside a content
 *   callback, on the main thread. Each takes a kiln_key first: KILN_HERE, or a key of your own in
 *   loops. The call gets its own keyed group, so a call under a condition never takes another
 *   call's remembered state.
 * - Structs passed in start with `size_t size`. Options structs have `<name>_init(&o)`, which sets
 *   `size` and kiln's defaults; a zeroed struct is not valid. A NULL options pointer means the
 *   defaults.
 * - Callbacks are kiln_fn* structs { call, user, drop }. kiln owns them from the call that takes
 *   them and calls drop(user) once, when it no longer needs the callback. call == NULL: absent.
 * - Strings are UTF-8 and copied. A string kiln returns stays valid until the next call that
 *   returns a string on that thread.
 * - kiln_modifier* is transient: valid until the content callback that made it returns.
 *   kiln_shape*, kiln_text_style*, kiln_anim_spec* and kiln_image_bitmap* are refcounted: the
 *   creating call returns one reference, `_release` drops it, and kiln keeps its own while it
 *   uses one. Remembered state handles live as long as their call stays in the composition. */
#ifndef KILN_H
#define KILN_H

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32)
#define KILN_API __declspec(dllexport)
#else
#define KILN_API __attribute__((visibility("default")))
#endif

/* A struct literal in C and C++. */
#ifdef __cplusplus
#define KILN_LITERAL_(T) T
#else
#define KILN_LITERAL_(T) (T)
#endif

#define KILN_ABI_VERSION 1
/* The ABI version this library implements; a newer header against an older library fails here. */
KILN_API int kiln_abi_version(void);

/* ---------------------------------------------------------------------------------------------
 * App and frame (M0 immediate drawing; still the way to draw without a composition)
 * ------------------------------------------------------------------------------------------- */

typedef struct kiln_app kiln_app;            /* owned by kiln for the life of the process */
typedef struct kiln_frame kiln_frame;        /* valid only inside on_frame */
typedef struct kiln_paragraph kiln_paragraph; /* owned by the caller: kiln_paragraph_free */

typedef struct kiln_app_callbacks {
    size_t size;      /* sizeof(kiln_app_callbacks) */
    void* user;
    /* Called once per frame while the app is visible and a frame was requested.
     * time_seconds is the vsync time of the frame. */
    void (*on_frame)(void* user, kiln_frame* frame, double time_seconds);
} kiln_app_callbacks;

/* The app's entry point, provided by the app's shared library and called once on the main
 * thread before the first frame. */
void kiln_main(kiln_app* app);

KILN_API void kiln_app_set_callbacks(kiln_app* app, const kiln_app_callbacks* callbacks);
KILN_API void kiln_app_request_frame(kiln_app* app);
/* Pixels per dp, and the font scale the user chose (sp = dp * font_scale, nonlinear above 1.3
 * on Android 14+: use kiln_text_layout's size in sp rather than multiplying yourself). */
KILN_API float kiln_app_density(const kiln_app* app);
KILN_API float kiln_app_font_scale(const kiln_app* app);

/* Frame drawing. Coordinates are framebuffer pixels, +y down; colours are 0xAARRGGBB. */
KILN_API void kiln_frame_size(const kiln_frame* frame, float* width, float* height);
KILN_API void kiln_frame_clear(kiln_frame* frame, uint32_t argb);
KILN_API void kiln_frame_fill_rect(kiln_frame* frame, float x, float y, float w, float h,
                                   uint32_t argb, float corner_radius);
KILN_API void kiln_frame_draw_paragraph(kiln_frame* frame, const kiln_paragraph* paragraph,
                                        float x, float y, uint32_t argb);

/* Text. Lays out UTF-8 text at size_sp wrapped to width_px with the platform's line breaker. */
KILN_API kiln_paragraph* kiln_text_layout(kiln_app* app, const char* utf8, float size_sp,
                                          float width_px);
KILN_API float kiln_paragraph_width(const kiln_paragraph* paragraph);
KILN_API float kiln_paragraph_height(const kiln_paragraph* paragraph);
KILN_API int kiln_paragraph_line_count(const kiln_paragraph* paragraph);
KILN_API void kiln_paragraph_free(kiln_paragraph* paragraph);

/* ---------------------------------------------------------------------------------------------
 * Values
 * ------------------------------------------------------------------------------------------- */

/* Colours: 0 is unspecified (the component's default), KILN_ARGB(0xAARRGGBB) otherwise. */
typedef uint64_t kiln_color;
#define KILN_COLOR_UNSPECIFIED ((kiln_color)0)
#define KILN_ARGB(argb) ((kiln_color)(0x100000000ull | (uint32_t)(argb)))

/* Dp and sp are floats; NaN where Compose has Dp.Unspecified / TextUnit.Unspecified. */
#define KILN_DP_UNSPECIFIED ((float)NAN)
#define KILN_SP_UNSPECIFIED ((float)NAN)

typedef struct kiln_opt_bool { bool set; bool value; } kiln_opt_bool;
typedef struct kiln_opt_int { bool set; int32_t value; } kiln_opt_int;
typedef struct kiln_opt_float { bool set; float value; } kiln_opt_float;
typedef struct kiln_opt_i64 { bool set; int64_t value; } kiln_opt_i64;

/* Alignment in a box: biases from -1 (start / top) to 1 (end / bottom). */
typedef struct kiln_alignment { float horizontal, vertical; } kiln_alignment;
#define KILN_ALIGN_TOP_START (KILN_LITERAL_(kiln_alignment){ -1, -1 })
#define KILN_ALIGN_TOP_CENTER (KILN_LITERAL_(kiln_alignment){ 0, -1 })
#define KILN_ALIGN_TOP_END (KILN_LITERAL_(kiln_alignment){ 1, -1 })
#define KILN_ALIGN_CENTER_START (KILN_LITERAL_(kiln_alignment){ -1, 0 })
#define KILN_ALIGN_CENTER (KILN_LITERAL_(kiln_alignment){ 0, 0 })
#define KILN_ALIGN_CENTER_END (KILN_LITERAL_(kiln_alignment){ 1, 0 })
#define KILN_ALIGN_BOTTOM_START (KILN_LITERAL_(kiln_alignment){ -1, 1 })
#define KILN_ALIGN_BOTTOM_CENTER (KILN_LITERAL_(kiln_alignment){ 0, 1 })
#define KILN_ALIGN_BOTTOM_END (KILN_LITERAL_(kiln_alignment){ 1, 1 })
/* One-axis alignment (a Row's vertical, a Column's horizontal): a bias from -1 to 1. */
#define KILN_ALIGN_START (-1.0f)
#define KILN_ALIGN_MIDDLE 0.0f
#define KILN_ALIGN_END 1.0f

typedef enum kiln_arrangement_kind {
    KILN_ARRANGE_START, KILN_ARRANGE_END, KILN_ARRANGE_CENTER, KILN_ARRANGE_SPACE_EVENLY,
    KILN_ARRANGE_SPACE_BETWEEN, KILN_ARRANGE_SPACE_AROUND,
    KILN_ARRANGE_SPACED_BY        /* `spacing` dp between children, then `align_bias` */
} kiln_arrangement_kind;
typedef struct kiln_arrangement {
    kiln_arrangement_kind kind;
    float spacing;       /* KILN_ARRANGE_SPACED_BY */
    float align_bias;    /* KILN_ARRANGE_SPACED_BY: where leftover space goes; NaN: start */
} kiln_arrangement;

typedef struct kiln_padding { float start, top, end, bottom; } kiln_padding;
typedef struct kiln_border_stroke { float width; kiln_color color; } kiln_border_stroke;
typedef struct kiln_dp_size { float width, height; } kiln_dp_size;
typedef struct kiln_dp_offset { float x, y; } kiln_dp_offset;
typedef struct kiln_int_size { int32_t width, height; } kiln_int_size;
typedef struct kiln_rect { float left, top, right, bottom; } kiln_rect;
typedef struct kiln_offset { float x, y; } kiln_offset;   /* px */
typedef struct kiln_size { float width, height; } kiln_size;   /* px */
/* A corner's size: dp + px + percent of the shape's smaller side. */
typedef struct kiln_corner_size { float dp, px, percent; } kiln_corner_size;
typedef enum kiln_text_unit_type { KILN_TEXT_UNIT_UNSPECIFIED, KILN_TEXT_UNIT_SP, KILN_TEXT_UNIT_EM } kiln_text_unit_type;
typedef struct kiln_text_unit { float value; kiln_text_unit_type type; } kiln_text_unit;

/* ---------------------------------------------------------------------------------------------
 * Callbacks
 * ------------------------------------------------------------------------------------------- */

typedef struct kiln_fn { void (*call)(void* user); void* user; void (*drop)(void* user); } kiln_fn;
typedef struct kiln_fn_bool { void (*call)(void* user, bool value); void* user; void (*drop)(void* user); } kiln_fn_bool;
typedef struct kiln_fn_int { void (*call)(void* user, int32_t value); void* user; void (*drop)(void* user); } kiln_fn_int;
typedef struct kiln_fn_float { void (*call)(void* user, float value); void* user; void (*drop)(void* user); } kiln_fn_float;
typedef struct kiln_fn_size { void (*call)(void* user, kiln_int_size size); void* user; void (*drop)(void* user); } kiln_fn_size;
typedef struct kiln_fn_str { void (*call)(void* user, const char* utf8); void* user; void (*drop)(void* user); } kiln_fn_str;

/* A callback made of a function and a pointer only (no drop). */
#define KILN_FN(fn, user) (KILN_LITERAL_(kiln_fn){ (fn), (user), NULL })

/* ---------------------------------------------------------------------------------------------
 * Keys
 * ------------------------------------------------------------------------------------------- */

/* Where a composable call is, or an explicit identity. */
typedef struct kiln_key { uint64_t value; bool explicit_key; } kiln_key;

static inline uint64_t kiln_hash_str_(const char* s) {
    uint64_t h = 0xcbf29ce484222325ull;   /* FNV-1a */
    while (s && *s) { h ^= (unsigned char)*s++; h *= 0x100000001b3ull; }
    return h;
}
/* The call site: file, line and column (0 where the language has none). */
static inline kiln_key kiln_key_at(const char* file, uint32_t line, uint32_t column) {
    kiln_key k = { (kiln_hash_str_(file) ^ ((uint64_t)line << 20) ^ column) | 1u, false };
    return k;
}
static inline kiln_key kiln_key_int(int64_t v) {
    kiln_key k = { (uint64_t)v * 0x9E3779B97F4A7C15ull + 1, true };
    return k;
}
static inline kiln_key kiln_key_str(const char* s) { kiln_key k = { kiln_hash_str_(s) | 1u, true }; return k; }
/* The key of a call at this line. Two calls on one line need kiln_key_int / _str. */
#define KILN_HERE kiln_key_at(__FILE__, __LINE__, 0)

/* ---------------------------------------------------------------------------------------------
 * Content
 * ------------------------------------------------------------------------------------------- */

/* The app's declarative content, composed into the window. Replaces earlier content. */
KILN_API void kiln_app_set_content(kiln_app* app, kiln_fn content);

/* A keyed group around content (Compose's key(k) { }): calls inside get their own identity. */
KILN_API void kiln_group(kiln_key key, kiln_fn content);

/* ---------------------------------------------------------------------------------------------
 * State. Reading a state inside content subscribes the calling group: setting it recomposes
 * what read it. Setting is allowed from any callback on the main thread.
 * ------------------------------------------------------------------------------------------- */

typedef struct kiln_state_i64 kiln_state_i64;
typedef struct kiln_state_f64 kiln_state_f64;
typedef struct kiln_state_str kiln_state_str;

/* remember { mutableStateOf(initial) }. The *_saveable forms survive the activity being
 * recreated (rememberSaveable). */
KILN_API kiln_state_i64* kiln_remember_state_i64(kiln_key key, int64_t initial);
KILN_API kiln_state_f64* kiln_remember_state_f64(kiln_key key, double initial);
KILN_API kiln_state_str* kiln_remember_state_str(kiln_key key, const char* initial);
KILN_API kiln_state_i64* kiln_remember_saveable_i64(kiln_key key, int64_t initial);
KILN_API kiln_state_f64* kiln_remember_saveable_f64(kiln_key key, double initial);
KILN_API kiln_state_str* kiln_remember_saveable_str(kiln_key key, const char* initial);

KILN_API int64_t kiln_state_i64_get(const kiln_state_i64* state);
KILN_API void kiln_state_i64_set(kiln_state_i64* state, int64_t value);
KILN_API double kiln_state_f64_get(const kiln_state_f64* state);
KILN_API void kiln_state_f64_set(kiln_state_f64* state, double value);
KILN_API const char* kiln_state_str_get(const kiln_state_str* state);
KILN_API void kiln_state_str_set(kiln_state_str* state, const char* value);

/* remember(keys) { init() }: a value of your own, made on first composition (and again when
 * `keys` change), dropped with drop(value) when the call leaves or the keys change. Not
 * observable; keep observable values in states. */
typedef struct kiln_remember_init {
    void* (*call)(void* user);
    void* user;
    void (*drop_value)(void* value);
} kiln_remember_init;
KILN_API void* kiln_remember(kiln_key key, const uint64_t* keys, size_t key_count, kiln_remember_init init);

/* An interaction source (pressed, hovered, focused, dragged), for components that take one. */
typedef struct kiln_interaction_source kiln_interaction_source;
KILN_API kiln_interaction_source* kiln_remember_interaction_source(kiln_key key);
KILN_API bool kiln_interaction_source_is_pressed(const kiln_interaction_source* source);   /* observes */
KILN_API bool kiln_interaction_source_is_hovered(const kiln_interaction_source* source);
KILN_API bool kiln_interaction_source_is_focused(const kiln_interaction_source* source);
KILN_API bool kiln_interaction_source_is_dragged(const kiln_interaction_source* source);

/* ---------------------------------------------------------------------------------------------
 * Effects
 * ------------------------------------------------------------------------------------------- */

/* SideEffect: runs after every successful composition of this call. */
KILN_API void kiln_side_effect(kiln_key key, kiln_fn effect);

/* DisposableEffect(keys): effect runs when the call enters (and when keys change); the
 * kiln_fn it returns runs when the call leaves (or before the effect runs again). */
typedef struct kiln_fn_effect { kiln_fn (*call)(void* user); void* user; void (*drop)(void* user); } kiln_fn_effect;
KILN_API void kiln_disposable_effect(kiln_key key, const uint64_t* keys, size_t key_count, kiln_fn_effect effect);

/* LaunchedEffect(keys): an asynchronous task tied to the call, cancelled when the call leaves
 * or keys change. `start` runs first; a step continues the task by asking for its next step
 * with kiln_task_delay / kiln_task_next_frame before it returns. The task ends after a step
 * that asks for none. A cancelled task's pending steps are dropped without being called. */
typedef struct kiln_task kiln_task;
typedef struct kiln_fn_task { void (*call)(void* user, kiln_task* task); void* user; void (*drop)(void* user); } kiln_fn_task;
typedef struct kiln_fn_task_frame { void (*call)(void* user, kiln_task* task, int64_t frame_time_nanos); void* user; void (*drop)(void* user); } kiln_fn_task_frame;
KILN_API void kiln_launched_effect(kiln_key key, const uint64_t* keys, size_t key_count, kiln_fn_task start);
KILN_API void kiln_task_delay(kiln_task* task, int64_t millis, kiln_fn_task next);
KILN_API void kiln_task_next_frame(kiln_task* task, kiln_fn_task_frame next);

/* ---------------------------------------------------------------------------------------------
 * Shapes, text styles, animation specs, images (refcounted)
 * ------------------------------------------------------------------------------------------- */

typedef struct kiln_shape kiln_shape;
KILN_API kiln_shape* kiln_shape_rectangle(void);
KILN_API kiln_shape* kiln_shape_circle(void);
KILN_API kiln_shape* kiln_shape_rounded(float radius_dp);
KILN_API kiln_shape* kiln_shape_rounded_percent(float percent);
KILN_API kiln_shape* kiln_shape_rounded_corners(float top_start, float top_end, float bottom_end, float bottom_start);
KILN_API kiln_shape* kiln_shape_cut(float size_dp);
KILN_API kiln_shape* kiln_shape_cut_corners(float top_start, float top_end, float bottom_end, float bottom_start);
KILN_API kiln_shape* kiln_shape_retain(kiln_shape* shape);
KILN_API void kiln_shape_release(kiln_shape* shape);

typedef enum kiln_text_align {
    KILN_TEXT_ALIGN_UNSPECIFIED, KILN_TEXT_ALIGN_LEFT, KILN_TEXT_ALIGN_RIGHT, KILN_TEXT_ALIGN_CENTER,
    KILN_TEXT_ALIGN_JUSTIFY, KILN_TEXT_ALIGN_START, KILN_TEXT_ALIGN_END
} kiln_text_align;
typedef enum kiln_text_overflow {
    KILN_TEXT_OVERFLOW_CLIP, KILN_TEXT_OVERFLOW_ELLIPSIS, KILN_TEXT_OVERFLOW_VISIBLE,
    KILN_TEXT_OVERFLOW_START_ELLIPSIS, KILN_TEXT_OVERFLOW_MIDDLE_ELLIPSIS
} kiln_text_overflow;
typedef enum kiln_text_decoration {
    KILN_TEXT_DECORATION_UNSET, KILN_TEXT_DECORATION_NONE, KILN_TEXT_DECORATION_UNDERLINE,
    KILN_TEXT_DECORATION_LINE_THROUGH
} kiln_text_decoration;

typedef struct kiln_text_style kiln_text_style;
typedef struct kiln_text_style_options {
    size_t size;
    kiln_color color;
    float font_size_sp;          /* NaN: unset */
    int32_t font_weight;         /* 1..1000; 0: unset */
    kiln_opt_bool italic;
    float letter_spacing_sp;     /* NaN: unset */
    float line_height_sp;        /* NaN: unset */
    kiln_text_align text_align;
    kiln_text_decoration decoration;
    kiln_color background;
} kiln_text_style_options;
KILN_API void kiln_text_style_options_init(kiln_text_style_options* options);
KILN_API kiln_text_style* kiln_text_style_new(const kiln_text_style_options* options);
/* base.merge(other): other's set fields win. */
KILN_API kiln_text_style* kiln_text_style_merge(const kiln_text_style* base, const kiln_text_style* other);
KILN_API kiln_text_style* kiln_text_style_retain(kiln_text_style* style);
KILN_API void kiln_text_style_release(kiln_text_style* style);

typedef enum kiln_easing {
    KILN_EASING_FAST_OUT_SLOW_IN, KILN_EASING_LINEAR_OUT_SLOW_IN, KILN_EASING_FAST_OUT_LINEAR_IN,
    KILN_EASING_LINEAR
} kiln_easing;
typedef struct kiln_anim_spec kiln_anim_spec;
/* spring(dampingRatio, stiffness): NaN takes Compose's default (no bounce, medium stiffness). */
KILN_API kiln_anim_spec* kiln_anim_spring(float damping_ratio, float stiffness);
KILN_API kiln_anim_spec* kiln_anim_tween(int32_t duration_millis, int32_t delay_millis, kiln_easing easing);
KILN_API kiln_anim_spec* kiln_anim_spec_retain(kiln_anim_spec* spec);
KILN_API void kiln_anim_spec_release(kiln_anim_spec* spec);

typedef struct kiln_image_bitmap kiln_image_bitmap;
/* PNG / JPEG / WebP bytes, or an app asset (assets/ in the APK, next to the binary on the
 * host). NULL when it cannot be decoded. */
KILN_API kiln_image_bitmap* kiln_image_bitmap_decode(const uint8_t* bytes, size_t length);
KILN_API kiln_image_bitmap* kiln_image_bitmap_from_asset(const char* path);
/* Unpremultiplied 0xAARRGGBB pixels, row by row. */
KILN_API kiln_image_bitmap* kiln_image_bitmap_from_pixels(int32_t width, int32_t height, const uint32_t* argb);
KILN_API int32_t kiln_image_bitmap_width(const kiln_image_bitmap* bitmap);
KILN_API int32_t kiln_image_bitmap_height(const kiln_image_bitmap* bitmap);
KILN_API kiln_image_bitmap* kiln_image_bitmap_retain(kiln_image_bitmap* bitmap);
KILN_API void kiln_image_bitmap_release(kiln_image_bitmap* bitmap);

/* Rounded polygons (MaterialShapes' shapes): as a shape to clip or fill with. */
typedef struct kiln_rounded_polygon kiln_rounded_polygon;
KILN_API kiln_shape* kiln_shape_from_polygon(const kiln_rounded_polygon* polygon, int32_t start_angle_degrees);
KILN_API kiln_rounded_polygon* kiln_rounded_polygon_retain(kiln_rounded_polygon* polygon);
KILN_API void kiln_rounded_polygon_release(kiln_rounded_polygon* polygon);

/* Window insets: the system's (by type) or fixed ones, combined per side. */
typedef enum kiln_window_insets_type {
    KILN_INSETS_NONE, KILN_INSETS_SYSTEM_BARS, KILN_INSETS_STATUS_BARS, KILN_INSETS_NAVIGATION_BARS,
    KILN_INSETS_CAPTION_BAR, KILN_INSETS_IME, KILN_INSETS_DISPLAY_CUTOUT, KILN_INSETS_SAFE_DRAWING,
    KILN_INSETS_SAFE_GESTURES, KILN_INSETS_SAFE_CONTENT, KILN_INSETS_SYSTEM_GESTURES,
    KILN_INSETS_MANDATORY_SYSTEM_GESTURES, KILN_INSETS_TAPPABLE_ELEMENT, KILN_INSETS_WATERFALL
} kiln_window_insets_type;
/* Sides for kiln_window_insets_only (WindowInsetsSides). */
#define KILN_SIDES_START 0x09u
#define KILN_SIDES_END 0x06u
#define KILN_SIDES_TOP 0x10u
#define KILN_SIDES_BOTTOM 0x20u
#define KILN_SIDES_LEFT 0x0Au
#define KILN_SIDES_RIGHT 0x05u
#define KILN_SIDES_HORIZONTAL (KILN_SIDES_LEFT | KILN_SIDES_RIGHT)
#define KILN_SIDES_VERTICAL (KILN_SIDES_TOP | KILN_SIDES_BOTTOM)
typedef struct kiln_window_insets kiln_window_insets;
KILN_API kiln_window_insets* kiln_window_insets_of(kiln_window_insets_type type);
KILN_API kiln_window_insets* kiln_window_insets_fixed(float left, float top, float right, float bottom);
KILN_API kiln_window_insets* kiln_window_insets_union(const kiln_window_insets* a, const kiln_window_insets* b);
KILN_API kiln_window_insets* kiln_window_insets_add(const kiln_window_insets* a, const kiln_window_insets* b);
KILN_API kiln_window_insets* kiln_window_insets_exclude(const kiln_window_insets* a, const kiln_window_insets* b);
KILN_API kiln_window_insets* kiln_window_insets_only(const kiln_window_insets* insets, uint32_t sides);
KILN_API kiln_window_insets* kiln_window_insets_retain(kiln_window_insets* insets);
KILN_API void kiln_window_insets_release(kiln_window_insets* insets);

/* Vector images (icons): SVG path data in a viewport, filled black. Icons tint them. */
typedef struct kiln_image_vector kiln_image_vector;
KILN_API kiln_image_vector* kiln_image_vector_from_paths(float default_width_dp, float default_height_dp,
                                                         float viewport_width, float viewport_height,
                                                         const char* const* path_data, size_t path_count, bool auto_mirror);
KILN_API kiln_image_vector* kiln_image_vector_retain(kiln_image_vector* vector);
KILN_API void kiln_image_vector_release(kiln_image_vector* vector);

/* ---------------------------------------------------------------------------------------------
 * Modifiers. Each call returns `m` followed by one element; m == NULL starts an empty chain.
 * ------------------------------------------------------------------------------------------- */

typedef struct kiln_modifier kiln_modifier;
typedef struct kiln_scroll_state kiln_scroll_state;

KILN_API kiln_modifier* kiln_mod_then(kiln_modifier* m, const kiln_modifier* other);
/* Size */
KILN_API kiln_modifier* kiln_mod_padding(kiln_modifier* m, float start, float top, float end, float bottom);
KILN_API kiln_modifier* kiln_mod_padding_all(kiln_modifier* m, float all);
KILN_API kiln_modifier* kiln_mod_padding_hv(kiln_modifier* m, float horizontal, float vertical);
KILN_API kiln_modifier* kiln_mod_size(kiln_modifier* m, float width, float height);
KILN_API kiln_modifier* kiln_mod_width(kiln_modifier* m, float width);
KILN_API kiln_modifier* kiln_mod_height(kiln_modifier* m, float height);
KILN_API kiln_modifier* kiln_mod_size_in(kiln_modifier* m, float min_width, float min_height, float max_width, float max_height);
KILN_API kiln_modifier* kiln_mod_required_size(kiln_modifier* m, float width, float height);
KILN_API kiln_modifier* kiln_mod_default_min_size(kiln_modifier* m, float min_width, float min_height);
KILN_API kiln_modifier* kiln_mod_fill_max_width(kiln_modifier* m, float fraction);
KILN_API kiln_modifier* kiln_mod_fill_max_height(kiln_modifier* m, float fraction);
KILN_API kiln_modifier* kiln_mod_fill_max_size(kiln_modifier* m, float fraction);
KILN_API kiln_modifier* kiln_mod_wrap_content_size(kiln_modifier* m, kiln_alignment align);
KILN_API kiln_modifier* kiln_mod_aspect_ratio(kiln_modifier* m, float ratio);
KILN_API kiln_modifier* kiln_mod_offset(kiln_modifier* m, float x, float y);
/* Parent data: weight in a Row / Column, alignment in a Box, Row (vertical bias) or Column
 * (horizontal bias), matchParentSize in a Box. */
KILN_API kiln_modifier* kiln_mod_weight(kiln_modifier* m, float weight, bool fill);
KILN_API kiln_modifier* kiln_mod_align_in_box(kiln_modifier* m, kiln_alignment align);
KILN_API kiln_modifier* kiln_mod_align_in_row(kiln_modifier* m, float vertical_bias);
KILN_API kiln_modifier* kiln_mod_align_in_column(kiln_modifier* m, float horizontal_bias);
KILN_API kiln_modifier* kiln_mod_match_parent_size(kiln_modifier* m);
/* Alignment lines: a layout's lines by address (kiln_first_baseline, a component's lines such
 * as kiln_m3_minimum_interactive_top_alignment_line). Static; never freed. */
typedef struct kiln_alignment_line kiln_alignment_line;
KILN_API const kiln_alignment_line* kiln_first_baseline(void);
KILN_API const kiln_alignment_line* kiln_last_baseline(void);
/* alignBy: a Row / Column child aligns on the line with its siblings that use the same line. */
KILN_API kiln_modifier* kiln_mod_align_by(kiln_modifier* m, const kiln_alignment_line* line);
/* paddingFrom: space so the line sits `before` from the top (start) and `after` from the bottom
 * (end); KILN_DP_UNSPECIFIED for either leaves that side alone. */
KILN_API kiln_modifier* kiln_mod_padding_from(kiln_modifier* m, const kiln_alignment_line* line, float before, float after);
/* Layout coordinates: where an element sits, in px. Valid until the callback that gave them
 * returns (as Compose's, a view read when asked). */
typedef struct kiln_layout_coordinates kiln_layout_coordinates;
typedef struct kiln_fn_layout_coordinates { void (*call)(void* user, const kiln_layout_coordinates* coordinates); void* user; void (*drop)(void* user); } kiln_fn_layout_coordinates;
KILN_API bool kiln_layout_coordinates_is_attached(const kiln_layout_coordinates* c);
KILN_API kiln_int_size kiln_layout_coordinates_size(const kiln_layout_coordinates* c);
KILN_API kiln_offset kiln_layout_coordinates_position_in_root(const kiln_layout_coordinates* c);
KILN_API kiln_rect kiln_layout_coordinates_bounds_in_root(const kiln_layout_coordinates* c);
KILN_API kiln_offset kiln_layout_coordinates_position_in_window(const kiln_layout_coordinates* c);
KILN_API kiln_rect kiln_layout_coordinates_bounds_in_window(const kiln_layout_coordinates* c);
KILN_API kiln_offset kiln_layout_coordinates_position_in_parent(const kiln_layout_coordinates* c);
KILN_API kiln_rect kiln_layout_coordinates_bounds_in_parent(const kiln_layout_coordinates* c);
/* localPositionOf / localBoundingBoxOf: `source`'s point / bounds in c's space. */
KILN_API kiln_offset kiln_layout_coordinates_local_position_of(const kiln_layout_coordinates* c, const kiln_layout_coordinates* source,
                                                               kiln_offset relative_to_source);
KILN_API kiln_rect kiln_layout_coordinates_local_bounding_box_of(const kiln_layout_coordinates* c, const kiln_layout_coordinates* source);
/* onGloballyPositioned (after any move in the window) / onPlaced (when placed in its parent). */
KILN_API kiln_modifier* kiln_mod_on_globally_positioned(kiln_modifier* m, kiln_fn_layout_coordinates on_positioned);
KILN_API kiln_modifier* kiln_mod_on_placed(kiln_modifier* m, kiln_fn_layout_coordinates on_placed);
/* onSizeChanged: px. */
KILN_API kiln_modifier* kiln_mod_on_size_changed(kiln_modifier* m, kiln_fn_size on_size_changed);
/* Window insets */
KILN_API kiln_modifier* kiln_mod_safe_drawing_padding(kiln_modifier* m);
KILN_API kiln_modifier* kiln_mod_system_bars_padding(kiln_modifier* m);
KILN_API kiln_modifier* kiln_mod_ime_padding(kiln_modifier* m);
KILN_API kiln_modifier* kiln_mod_window_insets_padding(kiln_modifier* m, const kiln_window_insets* insets);
/* Drawing. shape NULL: a rectangle. */
KILN_API kiln_modifier* kiln_mod_background(kiln_modifier* m, kiln_color color, const kiln_shape* shape);
KILN_API kiln_modifier* kiln_mod_border(kiln_modifier* m, float width, kiln_color color, const kiln_shape* shape);
KILN_API kiln_modifier* kiln_mod_clip(kiln_modifier* m, const kiln_shape* shape);
KILN_API kiln_modifier* kiln_mod_alpha(kiln_modifier* m, float alpha);
KILN_API kiln_modifier* kiln_mod_rotate(kiln_modifier* m, float degrees);
KILN_API kiln_modifier* kiln_mod_scale(kiln_modifier* m, float scale_x, float scale_y);
KILN_API kiln_modifier* kiln_mod_shadow(kiln_modifier* m, float elevation, const kiln_shape* shape);
KILN_API kiln_modifier* kiln_mod_z_index(kiln_modifier* m, float z_index);
/* Input */
KILN_API kiln_modifier* kiln_mod_clickable(kiln_modifier* m, bool enabled, kiln_fn on_click);
KILN_API kiln_modifier* kiln_mod_toggleable(kiln_modifier* m, bool value, bool enabled, kiln_fn_bool on_value_change);
KILN_API kiln_modifier* kiln_mod_selectable(kiln_modifier* m, bool selected, bool enabled, kiln_fn on_click);
/* Focus. A focus requester moves focus to the focusable after kiln_mod_focus_requester in its
 * chain (or inside it); remembered at the call. */
typedef struct kiln_focus_requester kiln_focus_requester;
KILN_API kiln_focus_requester* kiln_remember_focus_requester(kiln_key key);
KILN_API bool kiln_focus_requester_request_focus(kiln_focus_requester* requester);   /* false: nothing focusable */
KILN_API kiln_modifier* kiln_mod_focus_requester(kiln_modifier* m, kiln_focus_requester* requester);
KILN_API kiln_modifier* kiln_mod_focusable(kiln_modifier* m, bool enabled);
/* on_change gets whether the first focus target after it is focused. */
KILN_API kiln_modifier* kiln_mod_on_focus_changed(kiln_modifier* m, kiln_fn_bool on_change);
/* LocalFocusManager.clearFocus: valid inside content. */
KILN_API void kiln_clear_focus(bool force);
/* Nested scrolling: a connection (a top app bar's scroll behaviour gives one) sees the scrolls of
 * the content below the modified element. */
typedef struct kiln_nested_scroll_connection kiln_nested_scroll_connection;
KILN_API kiln_modifier* kiln_mod_nested_scroll(kiln_modifier* m, const kiln_nested_scroll_connection* connection);
/* rememberNestedScrollInteropConnection: passes the nested scroll of the content below on to the
 * host view's nested-scrolling parents (a CoordinatorLayout's app bar), in View pixels. Without
 * such a parent it consumes nothing. Valid until the content callback returns. */
KILN_API kiln_nested_scroll_connection* kiln_remember_nested_scroll_interop_connection(kiln_key key);
KILN_API kiln_modifier* kiln_mod_vertical_scroll(kiln_modifier* m, kiln_scroll_state* state, bool enabled);
KILN_API kiln_modifier* kiln_mod_horizontal_scroll(kiln_modifier* m, kiln_scroll_state* state, bool enabled);
/* Semantics */
KILN_API kiln_modifier* kiln_mod_test_tag(kiln_modifier* m, const char* tag);
KILN_API kiln_modifier* kiln_mod_content_description(kiln_modifier* m, const char* description);

/* ---------------------------------------------------------------------------------------------
 * Drawing (DrawScope). Coordinates are px in the element's own space, +y down. A draw scope is
 * valid during the callback that gets it.
 * ------------------------------------------------------------------------------------------- */

typedef struct kiln_draw_scope kiln_draw_scope;
typedef struct kiln_fn_draw { void (*call)(void* user, kiln_draw_scope* scope); void* user; void (*drop)(void* user); } kiln_fn_draw;

typedef enum kiln_stroke_cap { KILN_STROKE_CAP_BUTT, KILN_STROKE_CAP_ROUND, KILN_STROKE_CAP_SQUARE } kiln_stroke_cap;
typedef enum kiln_stroke_join { KILN_STROKE_JOIN_MITER, KILN_STROKE_JOIN_ROUND, KILN_STROKE_JOIN_BEVEL } kiln_stroke_join;
/* Stroke(width, miter, cap, join); a NULL style fills. */
typedef struct kiln_draw_style { float width; float miter; kiln_stroke_cap cap; kiln_stroke_join join; } kiln_draw_style;
#define KILN_STROKE(w) (KILN_LITERAL_(kiln_draw_style){ (w), 4.0f, KILN_STROKE_CAP_BUTT, KILN_STROKE_JOIN_MITER })

/* A path, built by the caller (kiln_path_free). */
typedef struct kiln_path kiln_path;
KILN_API kiln_path* kiln_path_new(void);
KILN_API void kiln_path_free(kiln_path* path);
KILN_API void kiln_path_move_to(kiln_path* path, float x, float y);
KILN_API void kiln_path_line_to(kiln_path* path, float x, float y);
KILN_API void kiln_path_quadratic_to(kiln_path* path, float x1, float y1, float x2, float y2);
KILN_API void kiln_path_cubic_to(kiln_path* path, float x1, float y1, float x2, float y2, float x3, float y3);
/* Degrees clockwise from 3 o'clock. */
KILN_API void kiln_path_arc_to(kiln_path* path, kiln_rect oval, float start_angle, float sweep_angle, bool force_move_to);
KILN_API void kiln_path_add_rect(kiln_path* path, kiln_rect rect);
KILN_API void kiln_path_add_oval(kiln_path* path, kiln_rect oval);
KILN_API void kiln_path_add_round_rect(kiln_path* path, kiln_rect rect, float radius);
KILN_API void kiln_path_close(kiln_path* path);
KILN_API void kiln_path_reset(kiln_path* path);

KILN_API kiln_size kiln_draw_scope_size(const kiln_draw_scope* scope);
KILN_API float kiln_draw_scope_density(const kiln_draw_scope* scope);   /* px per dp */
KILN_API bool kiln_draw_scope_is_rtl(const kiln_draw_scope* scope);
KILN_API void kiln_draw_rect(kiln_draw_scope* scope, kiln_color color, kiln_offset top_left, kiln_size size, const kiln_draw_style* style);
KILN_API void kiln_draw_round_rect(kiln_draw_scope* scope, kiln_color color, kiln_offset top_left, kiln_size size, float corner_radius,
                                   const kiln_draw_style* style);
/* radius < 0: half the smaller side. */
KILN_API void kiln_draw_circle(kiln_draw_scope* scope, kiln_color color, kiln_offset center, float radius, const kiln_draw_style* style);
KILN_API void kiln_draw_oval(kiln_draw_scope* scope, kiln_color color, kiln_offset top_left, kiln_size size, const kiln_draw_style* style);
KILN_API void kiln_draw_arc(kiln_draw_scope* scope, kiln_color color, float start_angle, float sweep_angle, bool use_center,
                            kiln_offset top_left, kiln_size size, const kiln_draw_style* style);
KILN_API void kiln_draw_line(kiln_draw_scope* scope, kiln_color color, kiln_offset start, kiln_offset end, float stroke_width,
                             kiln_stroke_cap cap);
KILN_API void kiln_draw_path(kiln_draw_scope* scope, const kiln_path* path, kiln_color color, const kiln_draw_style* style);
KILN_API void kiln_draw_image(kiln_draw_scope* scope, const kiln_image_bitmap* image, kiln_offset top_left, float alpha);
/* Transform and clip until the matching restore. */
KILN_API void kiln_draw_save(kiln_draw_scope* scope);
KILN_API void kiln_draw_restore(kiln_draw_scope* scope);
KILN_API void kiln_draw_translate(kiln_draw_scope* scope, float dx, float dy);
KILN_API void kiln_draw_rotate(kiln_draw_scope* scope, float degrees, kiln_offset pivot);
KILN_API void kiln_draw_scale(kiln_draw_scope* scope, float sx, float sy, kiln_offset pivot);
KILN_API void kiln_draw_clip_rect(kiln_draw_scope* scope, kiln_rect rect, float corner_radius);
KILN_API void kiln_draw_clip_path(kiln_draw_scope* scope, const kiln_path* path);
KILN_API void kiln_draw_multiply_alpha(kiln_draw_scope* scope, float alpha);
/* drawWithContent's scope: draws what the modifier wraps (nothing in other scopes). */
KILN_API void kiln_draw_content(kiln_draw_scope* scope);

/* drawBehind: under the content. drawWithContent: on_draw draws the content itself
 * (kiln_draw_content), anything before or after it. Canvas: a box that only draws. */
KILN_API kiln_modifier* kiln_mod_draw_behind(kiln_modifier* m, kiln_fn_draw on_draw);
KILN_API kiln_modifier* kiln_mod_draw_with_content(kiln_modifier* m, kiln_fn_draw on_draw);
KILN_API void kiln_canvas(kiln_key key, const kiln_modifier* modifier, kiln_fn_draw on_draw);

/* ---------------------------------------------------------------------------------------------
 * Layouts
 * ------------------------------------------------------------------------------------------- */

typedef struct kiln_box_options {
    size_t size;
    kiln_alignment content_alignment;   /* top start */
    bool propagate_min_constraints;     /* false */
} kiln_box_options;
KILN_API void kiln_box_options_init(kiln_box_options* options);
KILN_API void kiln_box(kiln_key key, const kiln_modifier* modifier, const kiln_box_options* options, kiln_fn content);

typedef struct kiln_row_options {
    size_t size;
    kiln_arrangement horizontal_arrangement;   /* start */
    float vertical_alignment;                  /* KILN_ALIGN_START (top) */
} kiln_row_options;
KILN_API void kiln_row_options_init(kiln_row_options* options);
KILN_API void kiln_row(kiln_key key, const kiln_modifier* modifier, const kiln_row_options* options, kiln_fn content);

typedef struct kiln_column_options {
    size_t size;
    kiln_arrangement vertical_arrangement;     /* top */
    float horizontal_alignment;                /* KILN_ALIGN_START */
} kiln_column_options;
KILN_API void kiln_column_options_init(kiln_column_options* options);
KILN_API void kiln_column(kiln_key key, const kiln_modifier* modifier, const kiln_column_options* options, kiln_fn content);

KILN_API void kiln_spacer(kiln_key key, const kiln_modifier* modifier);

/* AndroidView: an Android View (a WebView, a map, a RecyclerView) inside kiln content. It is a
 * real View of the window, so touches, focus, the keyboard and accessibility work as for any
 * View, and one that scrolls takes part in nested scrolling with kiln's scrollables around it.
 * The callbacks get the JNIEnv* and jobjects as void*. On a host without Views the element is
 * empty. */
/* factory(env, context): a local reference to a new View for the window's Context. It can run
 * outside a Java call, where FindClass only finds platform classes: load the app's own classes
 * (androidx, a map SDK) with context.getClassLoader().loadClass(name). */
typedef struct kiln_fn_view_factory { void* (*call)(void* user, void* env, void* context); void* user; void (*drop)(void* user); } kiln_fn_view_factory;
typedef struct kiln_fn_view { void (*call)(void* user, void* env, void* view); void* user; void (*drop)(void* user); } kiln_fn_view;
typedef struct kiln_android_view_options {
    kiln_fn_view update;       /* after the View is made and after each recomposition */
    kiln_fn_view on_reset;     /* set: the View is kept with a reused lazy item and reset here */
    kiln_fn_view on_release;   /* the View leaves for good */
} kiln_android_view_options;
KILN_API void kiln_android_view(kiln_key key, const kiln_modifier* modifier, kiln_fn_view_factory factory, const kiln_android_view_options* options);
/* The same for a View class with a (Context) constructor, such as "android.webkit.WebView". */
KILN_API void kiln_android_view_class(kiln_key key, const kiln_modifier* modifier, const char* class_name, const kiln_android_view_options* options);

/* BoxWithConstraints: content gets the incoming constraints in dp (max may be INFINITY). */
typedef struct kiln_constraints { float min_width, max_width, min_height, max_height; } kiln_constraints;
typedef struct kiln_fn_constraints { void (*call)(void* user, const kiln_constraints* constraints); void* user; void (*drop)(void* user); } kiln_fn_constraints;
KILN_API void kiln_box_with_constraints(kiln_key key, const kiln_modifier* modifier, const kiln_box_options* options,
                                        kiln_fn_constraints content);

/* ---------------------------------------------------------------------------------------------
 * Text and images
 * ------------------------------------------------------------------------------------------- */

typedef struct kiln_text_options {
    size_t size;
    kiln_text_overflow overflow;   /* clip */
    bool soft_wrap;                /* true */
    int32_t max_lines;             /* INT32_MAX */
    int32_t min_lines;             /* 1 */
} kiln_text_options;
KILN_API void kiln_text_options_init(kiln_text_options* options);
/* BasicText. style NULL: the default style (no theme; Material's Text is kiln_m3_text). */
KILN_API void kiln_basic_text(kiln_key key, const char* utf8, const kiln_modifier* modifier,
                              const kiln_text_style* style, const kiln_text_options* options);

/* Text with styled ranges and links (AnnotatedString), made with a builder. A style pushed
 * applies to the text appended until its pop; text styles contribute their span part (colour,
 * size, weight, decoration, ...). */
typedef struct kiln_annotated_string kiln_annotated_string;   /* refcounted */
typedef struct kiln_text_builder kiln_text_builder;           /* owned by the caller */
KILN_API kiln_text_builder* kiln_text_builder_new(void);
KILN_API void kiln_text_builder_append(kiln_text_builder* b, const char* utf8);
KILN_API void kiln_text_builder_push_style(kiln_text_builder* b, const kiln_text_style* style);
/* A link: opened by the platform, or `on_click` (called with the url) handles it. */
KILN_API void kiln_text_builder_push_link_url(kiln_text_builder* b, const char* url, kiln_fn_str on_click);
/* A clickable range: on_click gets the tag. */
KILN_API void kiln_text_builder_push_link_clickable(kiln_text_builder* b, const char* tag, kiln_fn_str on_click);
KILN_API void kiln_text_builder_pop(kiln_text_builder* b);
KILN_API kiln_annotated_string* kiln_text_builder_build(const kiln_text_builder* b);
KILN_API void kiln_text_builder_free(kiln_text_builder* b);
KILN_API kiln_annotated_string* kiln_annotated_string_retain(kiln_annotated_string* s);
KILN_API void kiln_annotated_string_release(kiln_annotated_string* s);
KILN_API void kiln_basic_text_annotated(kiln_key key, const kiln_annotated_string* text, const kiln_modifier* modifier,
                                        const kiln_text_style* style, const kiln_text_options* options);

typedef enum kiln_content_scale {
    KILN_CONTENT_SCALE_FIT, KILN_CONTENT_SCALE_CROP, KILN_CONTENT_SCALE_FILL_HEIGHT,
    KILN_CONTENT_SCALE_FILL_WIDTH, KILN_CONTENT_SCALE_INSIDE, KILN_CONTENT_SCALE_NONE,
    KILN_CONTENT_SCALE_FILL_BOUNDS
} kiln_content_scale;
typedef struct kiln_image_options {
    size_t size;
    kiln_alignment alignment;          /* center */
    kiln_content_scale content_scale;  /* fit */
    float alpha;                       /* 1 */
} kiln_image_options;
KILN_API void kiln_image_options_init(kiln_image_options* options);
/* content_description NULL: decorative. */
KILN_API void kiln_image(kiln_key key, const kiln_image_bitmap* bitmap, const char* content_description,
                         const kiln_modifier* modifier, const kiln_image_options* options);

/* BasicTextField over a remembered text field state. */
typedef struct kiln_text_field_state kiln_text_field_state;
KILN_API kiln_text_field_state* kiln_remember_text_field_state(kiln_key key, const char* initial);
KILN_API const char* kiln_text_field_state_text(const kiln_text_field_state* state);   /* observes */
KILN_API void kiln_text_field_state_set_text(kiln_text_field_state* state, const char* text);
typedef struct kiln_text_field_options {
    size_t size;
    bool enabled;                 /* true */
    bool read_only;               /* false */
    bool single_line;             /* false */
    const kiln_text_style* text_style;   /* NULL: default */
    kiln_color cursor_color;      /* unspecified: black */
} kiln_text_field_options;
KILN_API void kiln_text_field_options_init(kiln_text_field_options* options);
KILN_API void kiln_basic_text_field(kiln_key key, kiln_text_field_state* state, const kiln_modifier* modifier,
                                    const kiln_text_field_options* options);

/* ---------------------------------------------------------------------------------------------
 * Scrolling, lazy lists, pagers
 * ------------------------------------------------------------------------------------------- */

KILN_API kiln_scroll_state* kiln_remember_scroll_state(kiln_key key, int32_t initial);
KILN_API int32_t kiln_scroll_state_value(const kiln_scroll_state* state);       /* px; observes */
/* Any scrollable's state, for components that follow one (a top app bar's scroll behaviour). */
typedef struct kiln_scrollable_state kiln_scrollable_state;
KILN_API kiln_scrollable_state* kiln_scroll_state_scrollable(kiln_scroll_state* state);
/* What a scrollbar draws of a scrollable (scrollIndicatorState): NULL when the scrollable has
 * none. Valid while the state is. */
typedef struct kiln_scroll_indicator_state kiln_scroll_indicator_state;
KILN_API const kiln_scroll_indicator_state* kiln_scrollable_state_scroll_indicator(const kiln_scrollable_state* state);
/* px along the scroll axis, INT32_MAX while unknown; observe. */
KILN_API int32_t kiln_scroll_indicator_state_scroll_offset(const kiln_scroll_indicator_state* state);
KILN_API int32_t kiln_scroll_indicator_state_content_size(const kiln_scroll_indicator_state* state);
KILN_API int32_t kiln_scroll_indicator_state_viewport_size(const kiln_scroll_indicator_state* state);
KILN_API int32_t kiln_scroll_state_max_value(const kiln_scroll_state* state);   /* px; observes */
/* Suspending calls: `done` (optional) runs when the scroll finishes or is cancelled. */
KILN_API void kiln_scroll_state_animate_to(kiln_scroll_state* state, int32_t value, kiln_fn done);
KILN_API void kiln_scroll_state_scroll_to(kiln_scroll_state* state, int32_t value, kiln_fn done);

/* A fling behaviour (a carousel's snapping, from kiln_m3_carousel_defaults_*): made by kiln,
 * valid until the content callback returns, so ask for it again each composition as Compose's
 * @Composable defaults do. */
typedef struct kiln_fling_behavior kiln_fling_behavior;

typedef struct kiln_lazy_list_state kiln_lazy_list_state;
KILN_API kiln_lazy_list_state* kiln_remember_lazy_list_state(kiln_key key, int32_t first_visible_item_index,
                                                             int32_t first_visible_item_scroll_offset);
KILN_API int32_t kiln_lazy_list_state_first_visible_item_index(const kiln_lazy_list_state* state);    /* observes */
KILN_API int32_t kiln_lazy_list_state_first_visible_item_scroll_offset(const kiln_lazy_list_state* state);
KILN_API bool kiln_lazy_list_state_can_scroll_forward(const kiln_lazy_list_state* state);
KILN_API bool kiln_lazy_list_state_can_scroll_backward(const kiln_lazy_list_state* state);
KILN_API kiln_scrollable_state* kiln_lazy_list_state_scrollable(kiln_lazy_list_state* state);
KILN_API void kiln_lazy_list_state_scroll_to_item(kiln_lazy_list_state* state, int32_t index, int32_t offset, kiln_fn done);
KILN_API void kiln_lazy_list_state_animate_scroll_to_item(kiln_lazy_list_state* state, int32_t index, int32_t offset, kiln_fn done);

/* The items of a lazy list, declared in its content callback. */
typedef struct kiln_lazy_scope kiln_lazy_scope;
typedef struct kiln_fn_lazy { void (*call)(void* user, kiln_lazy_scope* scope); void* user; void (*drop)(void* user); } kiln_fn_lazy;
typedef struct kiln_fn_item_key { kiln_key (*call)(void* user, int32_t index); void* user; void (*drop)(void* user); } kiln_fn_item_key;
KILN_API void kiln_lazy_item(kiln_lazy_scope* scope, const kiln_key* key, kiln_fn content);   /* key NULL: by position */
/* item_key.call NULL: items keyed by position. */
KILN_API void kiln_lazy_items(kiln_lazy_scope* scope, int32_t count, kiln_fn_int content, kiln_fn_item_key item_key);
KILN_API void kiln_lazy_sticky_header(kiln_lazy_scope* scope, const kiln_key* key, kiln_fn_int content);

typedef struct kiln_lazy_list_options {
    size_t size;
    kiln_padding content_padding;   /* 0 */
    bool has_arrangement;           /* false: top / start (bottom / end when reversed) */
    kiln_arrangement arrangement;
    float alignment;                /* cross axis bias: KILN_ALIGN_START */
    bool reverse_layout;            /* false */
    bool user_scroll_enabled;       /* true */
    const kiln_fling_behavior* fling_behavior;   /* NULL: the platform's decay */
} kiln_lazy_list_options;
KILN_API void kiln_lazy_list_options_init(kiln_lazy_list_options* options);
/* state NULL: a remembered state at this call. */
KILN_API void kiln_lazy_column(kiln_key key, const kiln_modifier* modifier, kiln_lazy_list_state* state,
                               const kiln_lazy_list_options* options, kiln_fn_lazy content);
KILN_API void kiln_lazy_row(kiln_key key, const kiln_modifier* modifier, kiln_lazy_list_state* state,
                            const kiln_lazy_list_options* options, kiln_fn_lazy content);

/* Lazy grids (LazyVerticalGrid / LazyHorizontalGrid). */
typedef struct kiln_lazy_grid_state kiln_lazy_grid_state;
KILN_API kiln_lazy_grid_state* kiln_remember_lazy_grid_state(kiln_key key, int32_t first_visible_item_index,
                                                             int32_t first_visible_item_scroll_offset);
KILN_API int32_t kiln_lazy_grid_state_first_visible_item_index(const kiln_lazy_grid_state* state);    /* observes */
KILN_API int32_t kiln_lazy_grid_state_first_visible_item_scroll_offset(const kiln_lazy_grid_state* state);
KILN_API bool kiln_lazy_grid_state_can_scroll_forward(const kiln_lazy_grid_state* state);
KILN_API bool kiln_lazy_grid_state_can_scroll_backward(const kiln_lazy_grid_state* state);
KILN_API kiln_scrollable_state* kiln_lazy_grid_state_scrollable(kiln_lazy_grid_state* state);
KILN_API void kiln_lazy_grid_state_scroll_to_item(kiln_lazy_grid_state* state, int32_t index, int32_t offset, kiln_fn done);
KILN_API void kiln_lazy_grid_state_animate_scroll_to_item(kiln_lazy_grid_state* state, int32_t index, int32_t offset, kiln_fn done);

/* GridCells: how the cross axis splits into slots. FIXED: `count` slots; ADAPTIVE: as many as fit
 * with at least `size` dp each; FIXED_SIZE: slots of exactly `size` dp. */
typedef enum kiln_grid_cells_kind { KILN_GRID_CELLS_FIXED, KILN_GRID_CELLS_ADAPTIVE, KILN_GRID_CELLS_FIXED_SIZE } kiln_grid_cells_kind;
typedef struct kiln_grid_cells { kiln_grid_cells_kind kind; int32_t count; float size; } kiln_grid_cells;

typedef struct kiln_lazy_grid_scope kiln_lazy_grid_scope;
typedef struct kiln_fn_lazy_grid { void (*call)(void* user, kiln_lazy_grid_scope* scope); void* user; void (*drop)(void* user); } kiln_fn_lazy_grid;
/* An item's span in slots, from what is left on the current line and the slots per line
 * (return max_line_span for a full line). index: the item's index (0 for a single item). */
typedef struct kiln_fn_grid_span {
    int32_t (*call)(void* user, int32_t index, int32_t max_current_line_span, int32_t max_line_span);
    void* user; void (*drop)(void* user);
} kiln_fn_grid_span;
/* key NULL: by position. span.call NULL: one slot. */
KILN_API void kiln_lazy_grid_item(kiln_lazy_grid_scope* scope, const kiln_key* key, kiln_fn_grid_span span, kiln_fn content);
KILN_API void kiln_lazy_grid_items(kiln_lazy_grid_scope* scope, int32_t count, kiln_fn_int content, kiln_fn_item_key item_key,
                                   kiln_fn_grid_span span);
/* A full-line header that sticks while its items scroll. */
KILN_API void kiln_lazy_grid_sticky_header(kiln_lazy_grid_scope* scope, const kiln_key* key, kiln_fn_int content);

typedef struct kiln_lazy_grid_options {
    size_t size;
    kiln_padding content_padding;           /* 0 */
    kiln_arrangement main_axis_arrangement; /* between lines: top (vertical grid) / start */
    kiln_arrangement cross_axis_arrangement;/* the slots: start (vertical grid) / top */
    bool reverse_layout;                    /* false */
    bool user_scroll_enabled;               /* true */
    const kiln_fling_behavior* fling_behavior;   /* NULL: the platform's decay */
} kiln_lazy_grid_options;
KILN_API void kiln_lazy_grid_options_init(kiln_lazy_grid_options* options);
/* state NULL: a remembered state at this call. */
KILN_API void kiln_lazy_vertical_grid(kiln_key key, const kiln_modifier* modifier, kiln_grid_cells columns, kiln_lazy_grid_state* state,
                                      const kiln_lazy_grid_options* options, kiln_fn_lazy_grid content);
KILN_API void kiln_lazy_horizontal_grid(kiln_key key, const kiln_modifier* modifier, kiln_grid_cells rows, kiln_lazy_grid_state* state,
                                        const kiln_lazy_grid_options* options, kiln_fn_lazy_grid content);

typedef struct kiln_pager_state kiln_pager_state;
typedef struct kiln_fn_count { int32_t (*call)(void* user); void* user; void (*drop)(void* user); } kiln_fn_count;
KILN_API kiln_pager_state* kiln_remember_pager_state(kiln_key key, int32_t initial_page, kiln_fn_count page_count);
KILN_API int32_t kiln_pager_state_current_page(const kiln_pager_state* state);   /* observes */
KILN_API int32_t kiln_pager_state_settled_page(const kiln_pager_state* state);
KILN_API float kiln_pager_state_current_page_offset_fraction(const kiln_pager_state* state);
KILN_API void kiln_pager_state_animate_scroll_to_page(kiln_pager_state* state, int32_t page, kiln_fn done);
KILN_API void kiln_pager_state_scroll_to_page(kiln_pager_state* state, int32_t page, kiln_fn done);
typedef struct kiln_pager_options {
    size_t size;
    kiln_padding content_padding;
    float page_spacing;            /* 0 dp */
    int32_t beyond_viewport_page_count;   /* 0 */
    bool user_scroll_enabled;      /* true */
    bool reverse_layout;           /* false */
} kiln_pager_options;
KILN_API void kiln_pager_options_init(kiln_pager_options* options);
KILN_API void kiln_horizontal_pager(kiln_key key, const kiln_modifier* modifier, kiln_pager_state* state,
                                    const kiln_pager_options* options, kiln_fn_int page);
KILN_API void kiln_vertical_pager(kiln_key key, const kiln_modifier* modifier, kiln_pager_state* state,
                                  const kiln_pager_options* options, kiln_fn_int page);

/* ---------------------------------------------------------------------------------------------
 * Animation
 * ------------------------------------------------------------------------------------------- */

/* animate*AsState: the animated value now, heading for `target`. spec NULL: the default spring. */
KILN_API float kiln_animate_float(kiln_key key, float target, const kiln_anim_spec* spec);
KILN_API float kiln_animate_dp(kiln_key key, float target, const kiln_anim_spec* spec);
KILN_API kiln_color kiln_animate_color(kiln_key key, kiln_color target, const kiln_anim_spec* spec);

/* AnimatedVisibility with Compose's default transitions (fade and expand / shrink). */
KILN_API void kiln_animated_visibility(kiln_key key, bool visible, const kiln_modifier* modifier, kiln_fn content);

/* MutableTransitionState<Boolean>: a visibility (or any two-state transition) whose target is set
 * by hand and whose current state follows once the transition gets there. Remembered at the
 * call; a component's (a tooltip's) lives as long as that component's state. */
typedef struct kiln_transition_state kiln_transition_state;
KILN_API kiln_transition_state* kiln_remember_transition_state(kiln_key key, bool initial);
KILN_API bool kiln_transition_state_current(const kiln_transition_state* state);   /* observes */
KILN_API bool kiln_transition_state_target(const kiln_transition_state* state);    /* observes */
KILN_API void kiln_transition_state_set_target(kiln_transition_state* state, bool target);
KILN_API bool kiln_transition_state_is_idle(const kiln_transition_state* state);  /* current == target, not running */
/* AnimatedVisibility(visibleState): enters when the target turns true (an initial current of false
 * with a target of true animates in on first composition). */
KILN_API void kiln_animated_visibility_state(kiln_key key, kiln_transition_state* state, const kiln_modifier* modifier, kiln_fn content);

/* ---------------------------------------------------------------------------------------------
 * Volition (github.com/mKonic/volition): debug builds answer its commands by name. An app tells
 * `where` about itself (state fields) and lists where `go` can send it, as a NavHost does in
 * C++. Registered until kiln_volition_unregister; the newest registration is asked first.
 * Release builds keep the registrations and answer nothing.
 * ------------------------------------------------------------------------------------------- */

typedef struct kiln_volition_fields kiln_volition_fields;
typedef struct kiln_volition_lines kiln_volition_lines;
typedef struct kiln_volition_registration kiln_volition_registration;

/* `where`: adds fields to the answer (a later registration's field replaces an earlier one's). */
typedef struct kiln_fn_volition_state { void (*call)(void* user, kiln_volition_fields* fields); void* user; void (*drop)(void* user); } kiln_fn_volition_state;
/* `destinations`: one line each, `name` or `name:<argument>`, two spaces, what it does. */
typedef struct kiln_fn_volition_lines { void (*call)(void* user, kiln_volition_lines* lines); void* user; void (*drop)(void* user); } kiln_fn_volition_lines;
/* `go name[:argument]` (argument NULL when absent): the answer ("ok: ..." or "error: ..."),
 * copied at once, or NULL when `name` is not one of this registration's destinations. */
typedef struct kiln_fn_volition_go { const char* (*call)(void* user, const char* name, const char* argument); void* user; void (*drop)(void* user); } kiln_fn_volition_go;

typedef struct kiln_volition_adapter {
    size_t size;
    kiln_fn_volition_state state;           /* absent: no fields */
    kiln_fn_volition_lines destinations;    /* absent: none */
    kiln_fn_volition_go go;                 /* absent: goes nowhere */
} kiln_volition_adapter;
KILN_API void kiln_volition_adapter_init(kiln_volition_adapter* adapter);
KILN_API kiln_volition_registration* kiln_volition_register(const kiln_volition_adapter* adapter);
/* Ends the registration and drops its callbacks. */
KILN_API void kiln_volition_unregister(kiln_volition_registration* registration);

KILN_API void kiln_volition_field_string(kiln_volition_fields* fields, const char* key, const char* value);
KILN_API void kiln_volition_field_number(kiln_volition_fields* fields, const char* key, double value);
KILN_API void kiln_volition_field_bool(kiln_volition_fields* fields, const char* key, bool value);
KILN_API void kiln_volition_field_strings(kiln_volition_fields* fields, const char* key, const char* const* values, size_t count);
KILN_API void kiln_volition_line(kiln_volition_lines* lines, const char* line);

#ifdef __cplusplus
}
#endif

#endif
