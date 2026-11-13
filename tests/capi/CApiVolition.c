/* A Volition registration in C: CApiTests.cpp checks what `where`, `destinations` and `go` answer. */
#include <kiln/kiln.h>

#include <stdio.h>
#include <string.h>

int capi_volition_dropped = 0;

static void state(void* user, kiln_volition_fields* fields) {
    static const char* const stack[] = { "home", "item" };
    (void)user;
    kiln_volition_field_string(fields, "screen", "item");
    kiln_volition_field_number(fields, "page", 4);
    kiln_volition_field_bool(fields, "editing", false);
    kiln_volition_field_strings(fields, "stack", stack, 2);
}

static void destinations(void* user, kiln_volition_lines* lines) {
    (void)user;
    kiln_volition_line(lines, "item:<id>  opens an item");
}

static const char* go(void* user, const char* name, const char* argument) {
    static char answer[64];
    (void)user;
    if (strcmp(name, "item") != 0) return NULL;
    snprintf(answer, sizeof answer, "ok: opened item %s", argument ? argument : "(none)");
    return answer;
}

static void drop(void* user) { (void)user; ++capi_volition_dropped; }

kiln_volition_registration* capi_volition_register(void) {
    kiln_volition_adapter a;
    kiln_volition_adapter_init(&a);
    a.state.call = state;
    a.state.drop = drop;
    a.destinations.call = destinations;
    a.go.call = go;
    return kiln_volition_register(&a);
}
