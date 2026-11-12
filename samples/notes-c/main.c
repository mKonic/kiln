/* A notes list in C over kiln.h and kiln_m3.h: a text field and an add button, the notes in a
 * lazy list, tap a note to remove it. The notes live in the app's own memory; a revision
 * state tells kiln when they changed. */
#include <kiln/kiln_m3.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_NOTES 256

typedef struct Notes {
    char* text[MAX_NOTES];
    int64_t id[MAX_NOTES];
    int count;
    int64_t next_id;
    kiln_state_i64* revision;      /* bumped on every change */
    kiln_text_field_state* field;
} Notes;

static Notes g_notes;

static void changed(void) { kiln_state_i64_set(g_notes.revision, kiln_state_i64_get(g_notes.revision) + 1); }

static void add_note(void* user) {
    (void)user;
    const char* text = kiln_text_field_state_text(g_notes.field);
    if (!text[0] || g_notes.count == MAX_NOTES) return;
    g_notes.text[g_notes.count] = strdup(text);
    g_notes.id[g_notes.count] = g_notes.next_id++;
    ++g_notes.count;
    kiln_text_field_state_set_text(g_notes.field, "");
    changed();
}

static void remove_note(void* user) {
    const int64_t id = (int64_t)(intptr_t)user;
    for (int i = 0; i < g_notes.count; ++i) {
        if (g_notes.id[i] != id) continue;
        free(g_notes.text[i]);
        memmove(&g_notes.text[i], &g_notes.text[i + 1], sizeof(char*) * (size_t)(g_notes.count - i - 1));
        memmove(&g_notes.id[i], &g_notes.id[i + 1], sizeof(int64_t) * (size_t)(g_notes.count - i - 1));
        --g_notes.count;
        changed();
        return;
    }
}

static void title(void* user) { (void)user; kiln_m3_text(KILN_HERE, "Notes (C)", NULL, NULL); }
static void add_label(void* user) { (void)user; kiln_m3_text(KILN_HERE, "Add", NULL, NULL); }
static void top_bar(void* user) {
    (void)user;
    kiln_m3_top_app_bar(KILN_HERE, KILN_FN(title, NULL), NULL, NULL);
}

static void note_text(void* user) {
    const int index = (int)(intptr_t)user;
    kiln_m3_text(KILN_HERE, index < g_notes.count ? g_notes.text[index] : "", NULL, NULL);
}
static void note(void* user, int32_t index) {
    (void)user;
    kiln_modifier* m = kiln_mod_clickable(NULL, true, KILN_FN(remove_note, (void*)(intptr_t)g_notes.id[index]));
    kiln_m3_list_item(KILN_HERE, KILN_FN(note_text, (void*)(intptr_t)index), m, NULL);
}
static kiln_key note_key(void* user, int32_t index) { (void)user; return kiln_key_int(g_notes.id[index]); }
static void notes(void* user, kiln_lazy_scope* scope) {
    (void)user;
    kiln_fn_int content = { note, NULL, NULL };
    kiln_fn_item_key key = { note_key, NULL, NULL };
    kiln_lazy_items(scope, g_notes.count, content, key);
}

static void input_row(void* user) {
    (void)user;
    kiln_m3_text_field_options field;
    kiln_m3_text_field_options_init(&field);
    kiln_m3_outlined_text_field(KILN_HERE, g_notes.field, kiln_mod_weight(NULL, 1, true), &field);
    kiln_m3_button(KILN_HERE, KILN_FN(add_note, NULL), NULL, NULL, KILN_FN(add_label, NULL));
}

static void column_content(void* user) {
    (void)user;
    kiln_row_options row;
    kiln_row_options_init(&row);
    row.horizontal_arrangement.kind = KILN_ARRANGE_SPACED_BY;
    row.horizontal_arrangement.spacing = 8;
    row.vertical_alignment = KILN_ALIGN_MIDDLE;
    kiln_row(KILN_HERE, kiln_mod_padding_hv(kiln_mod_fill_max_width(NULL, 1), 16, 0), &row, KILN_FN(input_row, NULL));
    kiln_state_i64_get(g_notes.revision);   /* the list recomposes when the notes change */
    kiln_fn_lazy list = { notes, NULL, NULL };
    kiln_lazy_column(KILN_HERE, kiln_mod_weight(kiln_mod_fill_max_width(NULL, 1), 1, true), NULL, NULL, list);
}

static void body(void* user, kiln_padding padding) {
    (void)user;
    kiln_modifier* m = kiln_mod_padding(kiln_mod_fill_max_size(NULL, 1), padding.start, padding.top, padding.end, padding.bottom);
    kiln_column_options column;
    kiln_column_options_init(&column);
    column.vertical_arrangement.kind = KILN_ARRANGE_SPACED_BY;
    column.vertical_arrangement.spacing = 8;
    kiln_column(KILN_HERE, m, &column, KILN_FN(column_content, NULL));
}

static void themed(void* user) {
    (void)user;
    g_notes.revision = kiln_remember_state_i64(KILN_HERE, 0);
    g_notes.field = kiln_remember_text_field_state(KILN_HERE, "");
    kiln_m3_scaffold_options options;
    kiln_m3_scaffold_options_init(&options);
    options.top_bar = KILN_FN(top_bar, NULL);
    kiln_m3_fn_padding_values content = { body, NULL, NULL };
    kiln_m3_scaffold(KILN_HERE, NULL, &options, content);
}

static void app(void* user) {
    (void)user;
    kiln_m3_material_theme(KILN_HERE, NULL, KILN_FN(themed, NULL));
}

void kiln_main(kiln_app* a) {
    kiln_app_set_content(a, KILN_FN(app, NULL));
}
