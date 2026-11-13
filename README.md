<h1 align="center">kiln</h1>

kiln is a UI toolkit for building native Android apps in C++, C and Rust, with the
Jetpack Compose programming model and the Material 3 components. kiln draws every pixel
itself on Vulkan, works inside existing Android apps, and is free and open source.

## Documentation

* [Get started](https://mkonic.github.io/kiln/get-started.html)
* [kiln documentation](https://mkonic.github.io/kiln/)
* [Samples](samples)

## About kiln

We think kiln will help you create fast, polished Android apps in native code, whether
you write C++, C or Rust, and whether you start a new app or add screens to an existing
one.

### Beautiful user experiences

kiln gives you control over every pixel on the screen: layout, text, shapes, shadows,
blur and animation are all drawn by kiln. It includes the full set of [Material 3
components][components] ported from Compose Material 3, from buttons and app bars to
sheets, date pickers and carousels, with dynamic color and the expressive motion scheme,
along with support for customizing or creating entirely new components.

### Fast results

kiln is fast. It renders with Vulkan through [hearth], and lays out text with Android's
own text stack, so line breaking, fonts and emoji match the platform. kiln compiles to
native code for arm64 and x86_64, and an app links only the parts of kiln it uses.

### Productive development

kiln follows Compose: composable functions, remembered state, modifiers, lazy lists,
animation and navigation, so what you know from Compose carries over. `tools/kiln run`
builds, installs and opens an app in one step, and the same code runs in a desktop
window on Linux. Debug builds answer [Volition], so you or an agent can drive the app by
name from a terminal.

### Extensible and open model

kiln builds with CMake and the Android NDK, and also ships as an Android library (AAR)
for Gradle projects. A [C API][c api] covers the toolkit for C and other languages, with
[Rust bindings] built on it. Existing Android apps add kiln screens as activities, kiln
can host video, camera and other surfaces, and accessibility, autofill, content capture, input methods
and window insets work the way Android expects.

[components]: https://mkonic.github.io/kiln/material3.html
[hearth]: https://github.com/mKonic/hearth
[Volition]: https://github.com/mKonic/volition
[c api]: https://mkonic.github.io/kiln/c-api.html
[Rust bindings]: bindings/rust
