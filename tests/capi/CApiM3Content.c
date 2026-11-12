/* Material 3 content written against kiln_m3.h, in C: CApiTests.cpp composes and checks it. */
#include <kiln/kiln_m3.h>

#include <stdio.h>

kiln_color capi_m3_primary;      /* MaterialTheme.colorScheme.primary inside the theme */
kiln_color capi_m3_expected;     /* the dark scheme's primary given to the theme */

static void click(void* user) {
    kiln_state_i64* s = (kiln_state_i64*)user;
    kiln_state_i64_set(s, kiln_state_i64_get(s) + 1);
}
static void label(void* user) {
    kiln_state_i64* s = (kiln_state_i64*)user;
    char text[32];
    snprintf(text, sizeof text, "Clicked %lld", (long long)kiln_state_i64_get(s));
    kiln_m3_text(KILN_HERE, text, NULL, NULL);
}
static void toggle(void* user, bool checked) { kiln_state_i64_set((kiln_state_i64*)user, checked); }

static void body(void* user) {
    (void)user;
    kiln_m3_color_scheme scheme;
    kiln_m3_color_scheme_init(&scheme);
    kiln_m3_material_theme_color_scheme(KILN_HERE, &scheme);
    capi_m3_primary = scheme.primary;

    kiln_state_i64* clicks = kiln_remember_state_i64(KILN_HERE, 0);
    kiln_state_i64* checked = kiln_remember_state_i64(KILN_HERE, 0);
    kiln_m3_button_options options;
    kiln_m3_button_options_init(&options);
    kiln_m3_button(KILN_HERE, KILN_FN(click, clicks), kiln_mod_test_tag(NULL, "button"), &options, KILN_FN(label, clicks));
    kiln_fn_bool on_change = { toggle, checked, NULL };
    kiln_m3_checkbox(KILN_HERE, kiln_state_i64_get(checked) != 0, on_change, kiln_mod_test_tag(NULL, "checkbox"), NULL);
}

static void themed(void* user) {
    (void)user;
    kiln_column(KILN_HERE, NULL, NULL, KILN_FN(body, NULL));
}

void capi_m3(void* user) {
    (void)user;
    kiln_m3_color_scheme dark;
    kiln_m3_color_scheme_init(&dark);
    kiln_m3_fn_color_scheme no_customize = { NULL, NULL, NULL };
    kiln_m3_dark_color_scheme(KILN_HERE, no_customize, &dark);
    capi_m3_expected = dark.primary;
    kiln_m3_material_theme_args args;
    kiln_m3_material_theme_args_init(&args);
    args.color_scheme = &dark;
    kiln_m3_material_theme(KILN_HERE, &args, KILN_FN(themed, NULL));
}

/* Snackbars: a suspending call that reports its result; a provided content colour. */
kiln_m3_snackbar_host_state* capi_snack_host;
int capi_snack_result = -1;
kiln_color capi_provided_color;

static void read_color(void* user) {
    (void)user;
    capi_provided_color = kiln_m3_local_content_color_current(KILN_HERE);
}
static void snack_body(void* user) {
    (void)user;
    capi_snack_host = kiln_m3_remember_snackbar_host_state(KILN_HERE);
    kiln_m3_fn_snackbar_data default_snackbar = { NULL, NULL, NULL };
    kiln_m3_snackbar_host(KILN_HERE, capi_snack_host, kiln_mod_test_tag(NULL, "host"), default_snackbar);
    kiln_m3_local_content_color_provide(KILN_HERE, KILN_ARGB(0xFF123456), KILN_FN(read_color, NULL));
}
void capi_m3_snackbar(void* user) {
    (void)user;
    kiln_m3_material_theme(KILN_HERE, NULL, KILN_FN(snack_body, NULL));
}
static void on_result(void* user, kiln_m3_snackbar_result result) { (void)user; capi_snack_result = (int)result; }
void capi_m3_show_snackbar(void) {
    kiln_m3_fn_snackbar_result done = { on_result, NULL, NULL };
    kiln_m3_snackbar_host_state_show_snackbar(capi_snack_host, "Saved", "Undo", false, NULL, done);
}
void capi_m3_dismiss_snackbar(void) {
    kiln_m3_snackbar_data* data = kiln_m3_snackbar_host_state_current_snackbar_data(capi_snack_host);
    if (data) kiln_m3_snackbar_data_perform_action(data);
}

/* A top app bar that collapses as the list below it scrolls (enterAlways + nestedScroll). */
float capi_bar_offset;   /* read in content: the state handle is transient */

static void bar_title(void* user) { (void)user; kiln_m3_text(KILN_HERE, "Title", NULL, NULL); }
static void bar_row(void* user, int32_t i) {
    (void)user; (void)i;
    kiln_box(KILN_HERE, kiln_mod_height(kiln_mod_fill_max_width(NULL, 1), 50), NULL, KILN_FN(NULL, NULL));
}
static void bar_rows(void* user, kiln_lazy_scope* scope) {
    (void)user;
    kiln_fn_int row = { bar_row, NULL, NULL };
    kiln_fn_item_key no_key = { NULL, NULL, NULL };
    kiln_lazy_items(scope, 100, row, no_key);
}
static void bar_body(void* user) {
    (void)user;
    kiln_m3_fn_to_bool always = { NULL, NULL, NULL };
    kiln_m3_top_app_bar_scroll_behavior* behavior =
        kiln_m3_top_app_bar_defaults_enter_always_scroll_behavior(KILN_HERE, NULL, always, NULL, NULL, always);
    capi_bar_offset = kiln_m3_top_app_bar_state_height_offset(kiln_m3_top_app_bar_scroll_behavior_state(behavior));
    kiln_m3_top_app_bar_options options;
    kiln_m3_top_app_bar_options_init(&options);
    options.scroll_behavior = behavior;
    kiln_m3_top_app_bar(KILN_HERE, KILN_FN(bar_title, NULL), NULL, &options);
    kiln_modifier* list = kiln_mod_nested_scroll(kiln_mod_weight(kiln_mod_fill_max_width(NULL, 1), 1, true),
                                                 kiln_m3_top_app_bar_scroll_behavior_nested_scroll_connection(behavior));
    kiln_fn_lazy rows = { bar_rows, NULL, NULL };
    kiln_lazy_column(KILN_HERE, kiln_mod_test_tag(list, "bar-list"), NULL, NULL, rows);
}
static void bar_column(void* user) {
    (void)user;
    kiln_column(KILN_HERE, kiln_mod_fill_max_size(NULL, 1), NULL, KILN_FN(bar_body, NULL));
}
void capi_m3_app_bar(void* user) {
    (void)user;
    kiln_m3_material_theme(KILN_HERE, NULL, KILN_FN(bar_column, NULL));
}
float capi_bar_height_offset(void) { return capi_bar_offset; }
