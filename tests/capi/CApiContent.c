/* Content written against the C ABI, in C: CApiTests.cpp composes and checks it. */
#include <kiln/kiln.h>

#include <stdio.h>
#include <stdlib.h>

/* Read by the tests. */
int capi_live_callbacks;      /* callbacks handed to kiln and not dropped yet */
int capi_effect_starts;
int capi_effect_disposals;
int capi_task_frames;
int capi_items_composed;
kiln_state_i64* capi_count;   /* the counter's state */

static void drop_counted(void* user) { (void)user; --capi_live_callbacks; }
static kiln_fn counted(void (*call)(void*), void* user) {
    ++capi_live_callbacks;
    kiln_fn f = { call, user, drop_counted };
    return f;
}

static void increment(void* user) {
    kiln_state_i64* s = (kiln_state_i64*)user;
    kiln_state_i64_set(s, kiln_state_i64_get(s) + 1);
}

static void dispose_extra(void* user) { (void)user; ++capi_effect_disposals; }
static kiln_fn start_extra(void* user) {
    (void)user;
    ++capi_effect_starts;
    return KILN_FN(dispose_extra, NULL);
}

/* A task that waits for three frames, then ends. */
static void on_frame(void* user, kiln_task* task, int64_t time) {
    (void)user; (void)time;
    if (++capi_task_frames < 3) {
        kiln_fn_task_frame next = { on_frame, NULL, NULL };
        kiln_task_next_frame(task, next);
    }
}
static void start_task(void* user, kiln_task* task) {
    (void)user;
    kiln_fn_task_frame next = { on_frame, NULL, NULL };
    kiln_task_next_frame(task, next);
}

static void extra_content(void* user) {
    (void)user;
    kiln_fn_effect effect = { start_extra, NULL, NULL };
    kiln_disposable_effect(KILN_HERE, NULL, 0, effect);
    kiln_box(KILN_HERE, kiln_mod_test_tag(kiln_mod_size(NULL, 10, 10), "extra"), NULL, KILN_FN(NULL, NULL));
}

static void column_content(void* user) {
    kiln_state_i64* count = (kiln_state_i64*)user;
    char label[32];
    const int64_t n = kiln_state_i64_get(count);
    snprintf(label, sizeof label, "Count %lld", (long long)n);
    kiln_basic_text(KILN_HERE, label, kiln_mod_test_tag(NULL, "label"), NULL, NULL);
    kiln_modifier* button = kiln_mod_size(NULL, 40, 40);
    button = kiln_mod_test_tag(button, "button");
    button = kiln_mod_clickable(button, true, counted(increment, count));
    kiln_box(KILN_HERE, button, NULL, KILN_FN(NULL, NULL));
    /* An effect and a box only while the count is odd. */
    if (n % 2 == 1) kiln_group(kiln_key_str("extra"), counted(extra_content, NULL));
}

void capi_counter(void* user) {
    (void)user;
    kiln_state_i64* count = kiln_remember_saveable_i64(KILN_HERE, 0);
    capi_count = count;
    kiln_fn_task task = { start_task, NULL, NULL };
    kiln_launched_effect(KILN_HERE, NULL, 0, task);
    kiln_column_options options;
    kiln_column_options_init(&options);
    options.vertical_arrangement.kind = KILN_ARRANGE_SPACED_BY;
    options.vertical_arrangement.spacing = 8;
    kiln_column(KILN_HERE, kiln_mod_padding_all(NULL, 4), &options, counted(column_content, count));
}

static void item(void* user, int32_t index) {
    (void)user;
    char tag[32];
    snprintf(tag, sizeof tag, "item %d", index);
    ++capi_items_composed;
    kiln_box(KILN_HERE, kiln_mod_test_tag(kiln_mod_fill_max_width(kiln_mod_height(NULL, 20), 1), tag), NULL, KILN_FN(NULL, NULL));
}
static kiln_key item_key(void* user, int32_t index) { (void)user; return kiln_key_int(index); }
static void items(void* user, kiln_lazy_scope* scope) {
    (void)user;
    kiln_fn_int content = { item, NULL, NULL };
    kiln_fn_item_key key = { item_key, NULL, NULL };
    kiln_lazy_items(scope, 1000, content, key);
}

void capi_list(void* user) {
    (void)user;
    kiln_lazy_list_state* state = kiln_remember_lazy_list_state(KILN_HERE, 0, 0);
    kiln_fn_lazy content = { items, NULL, NULL };
    kiln_lazy_column(KILN_HERE, kiln_mod_fill_max_size(NULL, 1), state, NULL, content);
}

/* Refcounted handles and options defaults. */
int capi_handles_ok(void) {
    kiln_shape* s = kiln_shape_rounded(8);
    kiln_shape* again = kiln_shape_retain(s);
    kiln_shape_release(s);
    kiln_shape_release(again);
    kiln_text_style_options o;
    kiln_text_style_options_init(&o);
    if (o.size != sizeof o || !isnan(o.font_size_sp)) return 0;
    o.font_size_sp = 20;
    o.color = KILN_ARGB(0xFF112233);
    kiln_text_style* style = kiln_text_style_new(&o);
    kiln_text_style* merged = kiln_text_style_merge(style, NULL);
    kiln_text_style_release(style);
    kiln_text_style_release(merged);
    uint32_t px[4] = { 0xFFFF0000u, 0xFF00FF00u, 0xFF0000FFu, 0x80FFFFFFu };
    kiln_image_bitmap* b = kiln_image_bitmap_from_pixels(2, 2, px);
    const int ok = b && kiln_image_bitmap_width(b) == 2 && kiln_image_bitmap_height(b) == 2;
    kiln_image_bitmap_release(b);
    return ok && kiln_abi_version() == KILN_ABI_VERSION;
}

/* Styled text with a link, built in C. */
static void on_link(void* user, const char* tag) { (void)user; (void)tag; }
void capi_annotated(void* user) {
    (void)user;
    kiln_text_builder* b = kiln_text_builder_new();
    kiln_text_builder_append(b, "Read the ");
    kiln_text_style_options o;
    kiln_text_style_options_init(&o);
    o.font_weight = 700;
    kiln_text_style* bold = kiln_text_style_new(&o);
    kiln_text_builder_push_style(b, bold);
    kiln_fn_str click = { on_link, NULL, NULL };
    kiln_text_builder_push_link_clickable(b, "terms", click);
    kiln_text_builder_append(b, "terms");
    kiln_text_builder_pop(b);
    kiln_text_builder_pop(b);
    kiln_text_builder_append(b, ".");
    kiln_annotated_string* text = kiln_text_builder_build(b);
    kiln_text_builder_free(b);
    kiln_text_style_release(bold);
    kiln_basic_text_annotated(KILN_HERE, text, kiln_mod_test_tag(NULL, "rich"), NULL, NULL);
    kiln_annotated_string_release(text);
}
