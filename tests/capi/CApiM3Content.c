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
