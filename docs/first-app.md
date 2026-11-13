# Write your first app

This page builds a small counter app with a top app bar, a switch, a button and a list
that grows each time you press it.

## Create the app

From the kiln folder:

```sh
tools/kiln new counter --label "Counter"
```

This makes `apps/counter`, the app's own folder:

| File | Holds |
|---|---|
| `CMakeLists.txt` | The app's name, package, label, icon, version and permissions |
| `src/main.cpp` | The code |
| `assets/` | Images, fonts and other files the app reads |
| `res/icon.png` | The launcher icon |

kiln builds every app in `apps/` with itself. The `apps` folder is not part of kiln's
repository, so an app can be a repository of its own.

A kiln app is a shared library that exports one function, `kiln_main`. kiln calls it
when the app starts, and the app hands kiln its content. `src/main.cpp` starts as this
counter app:

```cpp
#include <kiln/kiln.hpp>
#include <kiln/foundation/Compose.h>
#include <kiln/foundation/Layouts.h>
#include <kiln/foundation/Lazy.h>
#include <kiln/material3/AppBar.h>
#include <kiln/material3/Button.h>
#include <kiln/material3/MaterialTheme.h>
#include <kiln/material3/Scaffold.h>
#include <kiln/material3/Switch.h>
#include <kiln/material3/Text.h>

#include <string>

using namespace kiln::foundation;
using namespace kiln::material3;
using namespace kiln::ui;

namespace {
    void Counter() {
        auto& count = RememberState(0);
        auto& fast = RememberState(false);
        ScaffoldOptions options;
        options.topBar = [] { TopAppBar([] { Text("Counter"); }, {}); };
        Scaffold({}, options, [&](const PaddingValues& padding) {
            Column(Padding(Padding(FillMax(Modifier{}, Axis::Both), padding), 16_dp),
                   Arrangement::SpacedBy(16_dp), AlignmentH::Start(), [&] {
                Text("Pressed " + std::to_string(count.Get()) + " times",
                     {}, { .style = MaterialTheme::typography().headlineMedium });
                Row(Modifier{}, Arrangement::SpacedBy(8_dp), AlignmentV::CenterVertically(), [&] {
                    Switch(fast.Get(), [&](bool on) { fast.Set(on); });
                    Text("Count by ten");
                });
                Button([&] { count.Set(count.Get() + (fast.Get() ? 10 : 1)); }, {}, {}, [] { Text("Add"); });
                auto& list = RememberLazyListState();
                LazyColumn(Weight(Modifier{}, 1), list, [&](LazyListScope& s) {
                    s.Items(count.Get(), [](int i) { Text("Row " + std::to_string(i)); });
                });
            });
        });
    }
}

extern "C" void kiln_main(kiln_app* app) {
    kiln::SetContent(app, [] { MaterialTheme([] { Counter(); }); });
}
```

What the code does:

* `kiln::SetContent` gives kiln the root composable. `MaterialTheme` provides the
  colors, type scale and shapes that the Material 3 components read.
* `RememberState` keeps a value across recompositions. Reading it with `Get()` subscribes
  the composable to it, and `Set()` schedules the composables that read it to run again.
* `Scaffold` places the top app bar and passes the padding that keeps content clear of
  it and of the system bars.
* Modifiers wrap one another: `Padding(FillMax(Modifier{}, Axis::Both), padding)` fills
  the screen, then pads.
* `LazyColumn` composes only the rows that are on screen.

## Configure the app

`apps/counter/CMakeLists.txt` describes the app:

```cmake
kiln_add_app(counter
    SOURCES src/main.cpp
    LIBS kiln_material3 kiln_navigation
    PACKAGE com.example.counter
    LABEL "Counter"
    ASSETS assets
    ICON res/icon.png
    VERSION_CODE 1
    VERSION_NAME 1.0
)
```

* `PACKAGE` is the Android application ID, `LABEL` the name under the launcher icon.
* `PERMISSIONS android.permission.CAMERA ...` declares permissions.
* `APPLICATION_XML file` and `MANIFEST_XML file` add elements to the manifest's
  `<application>` and `<manifest>`, for services, receivers or queries.
* Add source files to `SOURCES` as the app grows.
* `LIBS` takes any CMake target. The file is ordinary CMake, so a library the app needs can
  be added above `kiln_add_app` with `add_subdirectory` or `FetchContent`, and is then built
  for each ABI with the app.

## Run it

```sh
tools/kiln run counter
```

Change the code and run the command again: it rebuilds what changed and reinstalls the app.

To build an APK for publishing, signed with your key:

```sh
KILN_KEYSTORE_PASSWORD=... tools/kiln apk counter --release --keystore my.jks --key-alias upload --out counter.apk
```

`--abi arm64-v8a` builds one CPU architecture instead of both. The same app also runs in a
desktop window: build the host tree and run `build/host/app-counter/counter`.

## Next

* [kiln for Compose developers](compose-developers.md), if you know Jetpack Compose
* [Composables, state and effects](composables.md)
* [Layout and lists](layout.md)
