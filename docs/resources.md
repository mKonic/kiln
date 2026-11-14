# App resources

An app's strings, plurals, colours, dimensions and drawables live in an Android `res/`
folder, as in any Android app. `tools/kiln new` makes one and names it in the app's
`CMakeLists.txt`:

```cmake
kiln_add_app(shelf
    SOURCES src/main.cpp
    LIBS kiln_material3
    PACKAGE com.example.shelf
    RES res
)
```

```xml
<!-- res/values/strings.xml -->
<resources>
    <string name="app_name">Shelf</string>
    <string name="size">%.1f MB</string>
    <plurals name="chapters">
        <item quantity="one">%d chapter</item>
        <item quantity="other">%d chapters</item>
    </plurals>
</resources>
```

Translations go in `values-de/`, `values-ja/` and so on, and night-mode values in
`values-night/`.

## Reading resources

The build turns the folder's names into `kiln_res.h`, so `R::string::app_name` is checked
when the app compiles:

```cpp
#include <kiln/foundation/PainterResource.h>
#include <kiln/material3/Text.h>

#include "kiln_res.h"

void Header(int chapters, double megabytes) {
    using namespace kiln::ui;
    kiln::material3::Text(StringResource(R::string::app_name));
    kiln::material3::Text(PluralStringResource(R::plurals::chapters, chapters, chapters));
    kiln::material3::Text(StringResource(R::string::size, megabytes));
    kiln::foundation::Image(kiln::foundation::PainterResource(R::drawable::ic_badge), "Badge");
}
```

| Call | Resource |
|---|---|
| `StringResource(id, args...)` | `<string>`, formatted like `String.format` |
| `PluralStringResource(id, count, args...)` | `<plurals>`, the form for `count` |
| `StringArrayResource(id)`, `IntegerArrayResource(id)` | `<string-array>`, `<integer-array>` |
| `ColorResource(id)`, `DimensionResource(id)` | `<color>`, `<dimen>` (in dp) |
| `IntegerResource(id)`, `BooleanResource(id)` | `<integer>`, `<bool>` |
| `PainterResource(id)`, `VectorResource(id)`, `ImageResource(id)` | `drawable/`: vector drawables and PNG, JPEG or WebP images |

Reads inside content follow the configuration: when the language or night mode changes,
the text and colours update without restarting the app. A name the app has no value for
throws, as `Resources.NotFoundException` does.

## On Android and on the desktop

On a device the values come from Android's resources, so language fallback, plural
rules, number formatting and qualifiers such as `values-sw600dp` work as in any app.
Vector drawables can use the platform's theme attributes (`?android:attr/...`).

In a desktop window the build generates tables from the same folder. They cover
languages (`values-<language>`), night mode and the common plural rules. Other
qualifiers, theme attributes and nine-patch images apply on Android only.

## From C, kiln++ and Rust

`kiln_res_c.h` holds the same names as C strings (`R_string_app_name`). The C API reads
them with `kiln_string_resource`, `kiln_plural_string_resource`, `kiln_color_resource`,
`kiln_vector_resource` and the other `kiln_*_resource` calls. kiln++ has
`kpp::string_resource` and `kpp::plural_string_resource`, Rust `kiln::string_resource`.
