# Android apps and Gradle

A kiln app on Android is one shared library, loaded by kiln's `KilnActivity`. This page
shows the two ways to build one, and how kiln works with the platform.

## How an app starts

1. Android starts `dev.kiln.KilnActivity`.
2. The activity loads the native library named by its `dev.kiln.lib_name` meta-data.
3. kiln calls the library's `kiln_main`, which sets the content.

The activity handles configuration changes such as rotation, dark theme and font size in
the same process, and keeps saved state across process death.

## Build with tools/kiln

`tools/kiln` builds, packs and installs the apps declared with `kiln_add_android_app`
(see [Write your first app](first-app.md)). It writes the manifest itself, so it suits
apps that need no permissions or other manifest entries.

## Build with Gradle

For an existing Gradle project, or an app that needs its own manifest entries, use kiln as
an Android library (AAR).

### 1. Build the AAR

```sh
tools/build-aar
```

This writes `build/kiln.aar`, which holds the Java host, the manifest entry for
`KilnActivity`, and a Prefab package with the headers and a static `libkiln.a` for
arm64-v8a and x86_64. The AAR exposes the [C API and kiln++](c-api.md).

### 2. Add it to the app

In `build.gradle.kts`:

```kotlin
android {
    defaultConfig {
        minSdk = 31
        ndk { abiFilters += listOf("arm64-v8a", "x86_64") }
        externalNativeBuild { cmake { arguments += listOf("-DANDROID_STL=c++_static") } }
    }
    externalNativeBuild { cmake { path = file("src/main/cpp/CMakeLists.txt") } }
    buildFeatures { prefab = true }
    packaging { jniLibs { useLegacyPackaging = false } }
}

dependencies {
    implementation(files("libs/kiln.aar"))
}
```

### 3. Link the native library

In `src/main/cpp/CMakeLists.txt`:

```cmake
find_package(kiln REQUIRED CONFIG)

add_library(app SHARED main.cpp)
target_link_libraries(app PRIVATE -Wl,--whole-archive $<TARGET_FILE:kiln::kiln> -Wl,--no-whole-archive kiln::kiln)

get_target_property(KILN_INCLUDES kiln::kiln INTERFACE_INCLUDE_DIRECTORIES)
target_link_options(app PRIVATE -Wl,--no-undefined -Wl,-z,max-page-size=16384 -Wl,--icf=safe
                    -Wl,--version-script=${KILN_INCLUDES}/kiln/app.map)
```

kiln is linked whole because Java calls into it. The version script exports only what
Android needs, so the linker removes the parts of kiln the app does not use.

### 4. Declare the activity

In `src/main/AndroidManifest.xml`, point `KilnActivity` at the library:

```xml
<activity android:name="dev.kiln.KilnActivity" android:exported="true">
    <meta-data android:name="dev.kiln.lib_name" android:value="app" />
    <intent-filter>
        <action android:name="android.intent.action.MAIN" />
        <category android:name="android.intent.category.LAUNCHER" />
    </intent-filter>
</activity>
```

[samples/gradle-notes](https://github.com/mKonic/kiln/tree/master/samples/gradle-notes) is a complete project.

## Debug builds and Volition

A debug build can answer [Volition](https://github.com/mKonic/volition), which lets you
or an agent read the screen and drive the app by name from a terminal: list what is on
screen, click a button by its label, fill a field, check where the app is. With
`tools/kiln`, pass `--debug`. With Gradle, declare the provider in
`src/debug/AndroidManifest.xml`:

```xml
<provider android:name="dev.kiln.KilnVolition"
          android:authorities="${applicationId}.volition" android:exported="true" />
```

The provider answers only in debuggable builds.

## Platform services

kiln connects to these Android services without extra code:

| Service | What works |
|---|---|
| Accessibility | TalkBack, Switch Access, Select to Speak and other services, from the semantics tree |
| Keyboard | Composition, suggestions, handwriting, and keyboard animations that move content |
| Autofill | Fields marked with a content type, and saving what was typed |
| Content capture | The text on screen for the system's intelligence features |
| Window insets | Edge to edge drawing with insets for system bars, cutouts and the keyboard |
| Back | The predictive back gesture, with `BackHandler` and navigation |
| Text | The platform's fonts, line breaking, hyphenation and emoji |
| Drag and drop | Dragging content in from and out to other apps |

## Activity results and permissions

`RememberLauncherForActivityResult` starts another activity and receives its result,
also after the app was recreated in the meantime. The contracts in
`kiln::foundation::contracts` cover the document picker, the photo picker, the camera,
contacts and permission requests:

```cpp
auto& picked = kiln::rt::RememberSaveableState(std::string("none"));
auto openDocument = RememberLauncherForActivityResult<std::vector<std::string>, contracts::UriResult>(
    std::make_shared<contracts::OpenDocument>(),
    [&picked](const contracts::UriResult& uri) { picked.Set(uri.value_or("cancelled")); });
Button([openDocument] { openDocument.Launch({ "*/*" }); }, {}, {}, [] { Text("Open a document"); });
```

A permission must also be declared in the app's manifest, so apps that request
permissions build with Gradle.

## Video, camera and other surfaces

`AndroidExternalSurface` gives a native window to code that renders on its own, such as a
video decoder, a camera preview or a game engine. kiln composites it with the UI:

```cpp
AndroidExternalSurface(FillMax(Modifier{}, Axis::Both), true, {}, AndroidExternalSurfaceZOrder::Behind, false,
    [](AndroidExternalSurfaceScope& s) {
        s.OnSurface([](SurfaceCoroutineScope& scope, Surface surface, int width, int height) -> kiln::rt::Task<> {
            // Render into surface.nativeWindow (an ANativeWindow*) until scope.IsActive() is false.
            co_return;
        });
    });
```

`AndroidExternalSurface` is a separate layer, drawn behind or on top of the UI.
`AndroidEmbeddedExternalSurface` is drawn as part of the UI, so it can be clipped,
transformed and covered by other content.
