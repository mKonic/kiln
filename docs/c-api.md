# The C API, kiln++ and Rust

kiln has three ways in:

| API | Header | For |
|---|---|---|
| C++ | `kiln/kiln.hpp` and the module headers | Apps built in the kiln tree, with the whole toolkit |
| C | `kiln/kiln.h`, `kiln/kiln_m3.h` | C, other languages, and apps built against the AAR |
| kiln++ | `kiln/kilnpp.hpp`, `kiln/kiln_m3.hpp` | C++ apps built against the AAR |

The C API and kiln++ are what the [AAR](android.md#build-with-gradle) ships. They cover
the runtime, layouts, text, images, lazy lists, pagers and Material 3.

## The C API

```c
#include <kiln/kiln_m3.h>

#include <stdio.h>

static kiln_state_i64* g_count;

static void add(void* user) {
    (void)user;
    kiln_state_i64_set(g_count, kiln_state_i64_get(g_count) + 1);
}

static void add_label(void* user) {
    (void)user;
    kiln_m3_text(KILN_HERE, "Add", NULL, NULL);
}

static void column(void* user) {
    (void)user;
    char label[64];
    snprintf(label, sizeof label, "Pressed %lld times", (long long)kiln_state_i64_get(g_count));
    kiln_m3_text(KILN_HERE, label, NULL, NULL);
    kiln_m3_button(KILN_HERE, KILN_FN(add, NULL), NULL, NULL, KILN_FN(add_label, NULL));
}

static void themed(void* user) {
    (void)user;
    g_count = kiln_remember_state_i64(KILN_HERE, 0);
    kiln_column(KILN_HERE, kiln_mod_padding_all(NULL, 16), NULL, KILN_FN(column, NULL));
}

static void app(void* user) {
    (void)user;
    kiln_m3_material_theme(KILN_HERE, NULL, KILN_FN(themed, NULL));
}

void kiln_main(kiln_app* a) {
    kiln_app_set_content(a, KILN_FN(app, NULL));
}
```

### Rules

* **Keys.** Every composable call takes a `kiln_key` first. `KILN_HERE` makes one from
  the file and line. In a loop, use `kiln_key_int(id)` so each item keeps its own state.
* **Callbacks.** A callback is a `kiln_fn` struct of a function, a user pointer and an
  optional `drop` function. `KILN_FN(fn, user)` makes one. kiln owns a callback from the
  call that takes it and calls `drop(user)` once when it no longer needs it.
* **Options.** Optional parameters are in options structs. Initialize one with its
  `_init` function, such as `kiln_row_options_init(&o)`, before setting fields. A zeroed
  struct is not valid. `NULL` means the defaults.
* **Modifiers.** `kiln_mod_*` functions chain, starting from `NULL`:
  `kiln_mod_padding_all(kiln_mod_fill_max_width(NULL, 1), 16)`. A modifier is valid
  until the content callback that made it returns.
* **Handles.** Shapes, text styles, animation specs and images are reference counted:
  the call that creates one returns a reference, and `_release` drops it.
* **Strings** are UTF-8 and copied. A string that kiln returns stays valid until the next
  call on that thread that returns a string.
* **Threads.** Composable calls work only inside content, on the main thread.
* **Errors.** No C++ exception crosses the API. A call that fails logs and does nothing.

## kiln++

kiln++ is a header-only C++ layer over the C API, so it works against the AAR. It adds
lambdas for callbacks, keys from the call site, a chaining modifier builder, and RAII for
handles:

```cpp
#include <kiln/kiln_m3.hpp>
#include <string>

extern "C" void kiln_main(kiln_app* app) {
    kpp::set_content(app, [] {
        kpp::m3::material_theme(nullptr, [] {
            auto count = kpp::remember_state<int64_t>(0);
            kpp::column(kpp::Modifier().padding(16), [=] {
                kpp::m3::text("Pressed " + std::to_string(count.get()) + " times");
                kpp::m3::button([=] { count.set(count.get() + 1); }, {}, nullptr, [] { kpp::m3::text("Add"); });
            });
        });
    });
}
```

Content lambdas run again on their own after the call that created them returns, so
capture by value. State and handles are small and cheap to copy.

## Rust

[bindings/rust](https://github.com/mKonic/kiln/tree/master/bindings/rust) has two crates:

* `kiln-sys`: the raw C API, generated with bindgen. Its build script builds kiln with
  CMake.
* `kiln`: a safe layer with closures for callbacks and keys from the call site.

The notes example runs on the desktop:

```sh
cd bindings/rust
cargo run --example notes
```

For Android, build the library form and pack it with `tools/kiln-pack`:

```sh
cargo build --example notes-lib --target aarch64-linux-android
```

`.cargo/config.toml` points Cargo at the NDK's linker for both Android targets.
