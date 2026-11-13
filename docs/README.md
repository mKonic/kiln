# kiln documentation

kiln is a UI toolkit for native Android apps, written in C++. It follows the Jetpack
Compose programming model and includes the Material 3 components. These pages show how
to set it up, build a UI and ship it in an app.

## Get started

* [Set up and run a sample](get-started.md)
* [Write your first app](first-app.md)
* [kiln for Compose developers](compose-developers.md)

## Build a UI

* [Composables, state and effects](composables.md)
* [Layout and lists](layout.md)
* [Text](text.md)
* [Interactivity and text input](interactivity.md)
* [Animation](animation.md)
* [Navigation](navigation.md)
* [Material 3 components and theming](material3.md)

## Platform integration

* [Android apps and Gradle](android.md)
* [The C API, kiln++ and Rust](c-api.md)

## Tools

* [Command line tools and testing](tools.md)

## Samples

The [samples](https://github.com/mKonic/kiln/tree/master/samples) folder holds complete apps:

| Sample | What it shows |
|---|---|
| `hello` | The C API drawing text and shapes directly |
| `list` | A lazy list with sticky headers and item animations |
| `gallery` | Every Material 3 component, with interactive demos |
| `catalog` | The androidx Material 3 catalog, ported example by example |
| `notes-c` | A notes app over the C API |
| `notes-cpp` | The same app in C++ with kiln++ |
| `gradle-notes` | The C notes app built by Gradle against the kiln AAR |
