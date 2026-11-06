/* kiln C ABI.
 *
 * M0 surface: an app entry point, a per-frame callback with immediate drawing, and platform text
 * layout. It is the scaffold the text-path prototype runs on; M1-M2 replace the drawing calls
 * with the retained node tree, keeping the entry point and lifetime rules.
 *
 * Rules that hold for every kiln call: no C++ types or exceptions cross this boundary; structs
 * passed in start with a `size` field so they can grow; every handle has one owner, named in the
 * comment of the call that returns it. */
#ifndef KILN_H
#define KILN_H

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

/* Frame drawing (M0). Coordinates are framebuffer pixels, +y down; colours are 0xAARRGGBB. */
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

#ifdef __cplusplus
}
#endif

#endif
