# Write your first app

This page builds a small counter app with a top app bar, a switch, a button and a list
that grows each time you press it.

## Create the app

A kiln app is a shared library that exports one function, `kiln_main`. kiln calls it
when the app starts, and the app hands kiln its content.

Create `apps/counter/main.cpp`:

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

## Register the app

Add the app to `CMakeLists.txt`, next to the samples inside `if(ANDROID)`:

```cmake
kiln_add_android_app(counter SOURCES apps/counter/main.cpp LIBS kiln_material3
                     PACKAGE com.example.counter LABEL "Counter")
```

`PACKAGE` is the Android application ID and `LABEL` is the name under the launcher icon.
`ASSETS dir` adds a folder of assets, such as images and fonts.

To run it on the desktop as well, add it to the `else()` branch:

```cmake
add_executable(counter apps/counter/main.cpp)
target_link_libraries(counter PRIVATE kiln_hostdev kiln_material3)
```

## Run it

```sh
tools/kiln run counter
```

Change the code and run the command again: it rebuilds what changed and reinstalls the app.

To build an APK without installing it:

```sh
tools/kiln apk counter --out counter.apk
```

## Next

* [kiln for Compose developers](compose-developers.md), if you know Jetpack Compose
* [Composables, state and effects](composables.md)
* [Layout and lists](layout.md)
