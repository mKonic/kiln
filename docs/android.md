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

`tools/kiln` builds, packs and installs the apps in `apps/` (see
[Write your first app](first-app.md)). It writes the manifest from the app's
`CMakeLists.txt`: permissions, icon, version, and any extra manifest elements.

## Build with Gradle

For an existing Gradle project, use kiln as an Android library (AAR).

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
`tools/kiln`, pass `--debug`. With Gradle, link the `kiln_volition` library in debug builds:

```cmake
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_link_libraries(app PRIVATE -Wl,--whole-archive $<TARGET_FILE:kiln::kiln_volition> -Wl,--no-whole-archive)
endif()
```

and declare the provider in `src/debug/AndroidManifest.xml`:

```xml
<provider android:name="dev.kiln.KilnVolition"
          android:authorities="${applicationId}.volition" android:exported="true" />
```

Release builds leave both out. A `NavHost` tells Volition its route and back stack by
itself; other apps can describe their screen with `kiln_volition_register` (see
[The C API](c-api.md#volition)).

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
| Memory pressure | GPU caches and decoded images freed when the system asks for memory |
| Foldables | Folds and hinges for the adaptive scaffolds and posture |
| High-contrast text | Text outlined and given a contrasting colour, with a background behind it from Android 16, as the platform draws it |

### App shortcuts

`kiln/ui/Shortcuts.h` sets the actions the launcher shows on a long press of the app's icon.
Each opens the app with a deep link, which arrives as the activity's intent
(`ui::ActivityIntent`, `OnNewIntent`, or [navigation's deep links](navigation.md#deep-links)):

```cpp
#include <kiln/ui/Shortcuts.h>

void SetShortcuts() {
    kiln::ui::SetDynamicShortcuts({
        { .id = "new", .shortLabel = "New note", .data = "notes://new", .icon = "ic_add" },
        { .id = "search", .shortLabel = "Search", .data = "notes://search", .rank = 1 },
    });
}
```

`icon` names a drawable or mipmap in the app's `res/`. `PushDynamicShortcut`,
`RemoveDynamicShortcuts`, `ReportShortcutUsed` and `RequestPinShortcut` (pin to the home
screen) follow `ShortcutManager`. Shortcuts that never change can be declared in
`res/xml/shortcuts.xml` and named in `ACTIVITY_XML` with
`<meta-data android:name="android.app.shortcuts" android:resource="@xml/shortcuts" />`.

## Device features

Vibration patterns, sensors, location and the biometric prompt, each in `kiln/ui`:

```cpp
#include <kiln/runtime/Flow.h>
#include <kiln/ui/Biometric.h>
#include <kiln/ui/Location.h>
#include <kiln/ui/Sensors.h>
#include <kiln/ui/Vibration.h>

using namespace kiln;

rt::Task<> Unlock(rt::State<bool>& unlocked) {
    ui::Vibrate(ui::VibrationEffect::CreatePredefined(ui::VibrationEffect::EffectClick));
    ui::BiometricResult r = co_await ui::Authenticate({ .title = "Unlock your notes" });
    unlocked.Set(r.succeeded);
}

rt::Task<> TagWithPlace(rt::State<std::string>& place) {
    if (auto here = co_await ui::CurrentLocation())
        place.Set(std::to_string(here->latitude) + ", " + std::to_string(here->longitude));
}

void Tilt() {
    // In content: the accelerometer's latest event, collected while composed.
    auto& tilt = rt::CollectAsState(ui::SensorEvents(ui::SensorType::Accelerometer), ui::SensorEvent{});
    (void)tilt.Get().values[0];
}
```

* **Vibration** (`android.permission.VIBRATE`): `OneShot`, `Waveform` (off and on durations
  with amplitudes, repeating from an index), `CreatePredefined` (click, tick, heavy click) and
  `Compose` (primitives, API 30). `HasAmplitudeControl` tells whether amplitudes are kept.
* **Sensors**: `SensorEvents(type, period)` is a flow of events while collected, and
  `ListenToSensor` a subscription that stops when it goes. Values are `SensorEvent.values`.
* **Location** (`ACCESS_FINE_LOCATION` or `ACCESS_COARSE_LOCATION`, requested at run time with
  `contracts::RequestPermission`): `CurrentLocation()` is a fresh fix, `LastKnownLocation()` an
  immediate but possibly old one, and `LocationUpdates(provider, interval)` a flow of fixes.
  Without the permission they answer nothing.
* **Biometric prompt** (`android.permission.USE_BIOMETRIC`): `Authenticate` shows the system's
  prompt and returns when it closes; `CanAuthenticate()` is 0 when it can be used, 11 when
  nothing is enrolled. Allow `kDeviceCredential` to accept the device's PIN as well.

## Picture-in-picture and orientation

An activity that declares picture-in-picture can enter it and show less while it is small:

```cmake
kiln_add_app(player
    SOURCES src/main.cpp
    LIBS kiln_material3
    PACKAGE com.example.player
    ACTIVITY_ATTRIBUTES "android:supportsPictureInPicture=\"true\""
)
```

```cpp
#include <kiln/material3/Button.h>
#include <kiln/material3/Text.h>
#include <kiln/ui/PictureInPicture.h>

using namespace kiln;
using namespace kiln::material3;

void Player() {
    if (ui::IsInPictureInPictureMode()) {
        Text("Playing");   // the video alone, without controls
        return;
    }
    Button([] { ui::EnterPictureInPictureMode({ .aspectWidth = 16, .aspectHeight = 9 }); }, {}, {}, [] { Text("Picture-in-picture"); });
}
```

* `IsInPictureInPictureMode()` is state: content that reads it recomposes when the window
  enters or leaves picture-in-picture.
* `SetPictureInPictureParams` sets the params ahead of time. With `autoEnter` the window
  enters by itself when the user goes home, and `sourceRectHint` (window pixels) is the part
  of the screen the system animates from.
* Android allows aspect ratios from 2.39:1 to 1:2.39; entering with another fails and
  returns false, as it does when the activity does not declare picture-in-picture.
* `ui::SetRequestedOrientation(ui::ScreenOrientation::SensorLandscape)` asks for an
  orientation, as `Activity.setRequestedOrientation` does.

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

A permission must also be declared in the manifest: `PERMISSIONS` in the app's
`CMakeLists.txt`, or the Gradle project's manifest. When the user has turned a permission
down before, `ui::ShouldShowRequestPermissionRationale(permission)` (`kiln/ui/ActivityIntent.h`)
is true: explain why the app needs it before asking again.

### Sharing and opening links

`kiln/ui/Share.h` opens the system's share sheet and other apps:

```cpp
#include <kiln/ui/Share.h>

void ShareNote(const std::string& title, const std::string& link) {
    kiln::ui::Share({ .text = link, .subject = title }, "Share the note");
}

void SharePhoto(const std::string& contentUri) {
    kiln::ui::Share({ .streams = { contentUri }, .mimeType = "image/jpeg" });
}
```

* `streams` are `content://` URIs; the app that receives them may read them.
* `ui::OpenUri(url)` opens a link in the browser or the app that handles it.
* `ui::LaunchCustomTab(url, { .toolbarColor = ... })` (`kiln/ui/CustomTabs.h`) opens a page
  in a Custom Tab: the user's browser, inside the app, in the app's colours.
* `ui::StartActivity(intent)` starts any activity without waiting for a result, and
  `ui::CreateChooser(intent, title)` wraps an intent in the system's chooser. An intent's
  extras can hold other intents and lists of URIs.

### Reading what was picked

Pickers, shares and other apps hand over `content://` URIs. `kiln/ui/ContentResolver.h`
opens them:

```cpp
#include <kiln/ui/ContentResolver.h>

std::string Describe(const std::string& uri) {
    const auto& resolver = kiln::ui::GetContentResolver();
    const auto info = resolver.Query(uri);   // display name, size, MIME type
    const auto bytes = kiln::ui::ReadContent(uri);
    return info ? info->displayName.value_or("?") + ": " + std::to_string(bytes ? bytes->size() : 0) + " bytes" : "unreadable";
}
```

* `OpenFileDescriptor(uri, mode)` gives a file descriptor to read with `read` or hand to
  a library (an archive or image decoder); the caller closes it. `WriteContent` writes a
  `CreateDocument` result.
* Each call is safe from any thread, so read large files on a background dispatcher.
* `TakePersistableUriPermission` keeps access to an `OpenDocument` result after the app
  restarts.
* On the desktop the same calls take file paths and `file://` URIs.

## Video, camera and other surfaces

`AndroidExternalSurface` gives a native window to code that renders on its own, such as a
video decoder, a camera preview or a game engine. kiln composites it with the UI:

```cpp
AndroidExternalSurface(FillMax(Modifier{}, Axis::Both), true, {}, AndroidExternalSurfaceZOrder::Behind, false,
    [](AndroidExternalSurfaceScope& s) {
        s.OnSurface([](SurfaceCoroutineScope& scope, kiln::ui::PlatformSurface surface, int width, int height) -> kiln::rt::Task<> {
            // Render into surface.nativeWindow (an ANativeWindow*) until scope.IsActive() is false.
            co_return;
        });
    });
```

`AndroidExternalSurface` is a separate layer, drawn behind or on top of the UI.
`AndroidEmbeddedExternalSurface` is drawn as part of the UI, so it can be clipped,
transformed and covered by other content.

## Android Views inside kiln

`AndroidView` puts an Android View, such as a `WebView` or a map, in kiln content. The View
is a real View of the window, so touches, focus, the keyboard and accessibility work as for
any View. It is drawn as part of kiln's frame, so kiln content can clip it and draw over it.

```cpp
AndroidView(AndroidViewClass("android.webkit.WebView"), Height(FillMax(Modifier{}, Axis::Horizontal), 300_dp), {
    .update = [](void* env, void* view) {
        // JNI on the View (a jobject): load a page, apply settings.
    },
});
```

* The factory gets the `JNIEnv*` and the window's `Context` and returns a new View.
  `AndroidViewClass(name)` constructs a View class through its `Context` constructor.
  In a factory of your own, load the app's classes (androidx, a map SDK) with
  `context.getClassLoader().loadClass(name)`: JNI's `FindClass` only finds platform classes
  there.
* `update` runs after the View is made, after each recomposition, and when state it reads
  changes.
* With `onReset`, a View in a lazy list item is kept when the item is reused, and reset
  there. `onRelease` runs when the View leaves.
* A View that scrolls with nested scrolling, such as a `ScrollView`, a `RecyclerView` or a
  `NestedScrollView`, scrolls kiln's scrollables around it once it reaches its end.
* From C it is `kiln_android_view` and `kiln_android_view_class`, from kiln++
  `kpp::android_view`, and from Rust `kiln::android_view`.
* A `SurfaceView` inside the View (a video player's, a camera preview's, a map's) shows under
  the rest of the View and under kiln content drawn over it, as it does in a window.

## kiln inside your own layout

An app with its own Activity can place kiln in any layout with `KilnHost`, such as under a
`CoordinatorLayout`'s app bar:

```java
KilnView kiln = KilnHost.create(this, "myapp", savedInstanceState);   // loads libmyapp.so
CoordinatorLayout.LayoutParams params = new CoordinatorLayout.LayoutParams(MATCH_PARENT, MATCH_PARENT);
params.setBehavior(new AppBarLayout.ScrollingViewBehavior());
root.addView(kiln, params);
```

`KilnHost` follows the activity's lifecycle and saves the content's state with the
activity's. To let kiln's scrolling move the app bar, give the scrolling content the interop
connection: `NestedScroll(modifier, RememberNestedScrollInteropConnection())` in C++, or
`kpp::Modifier().nested_scroll(kpp::remember_nested_scroll_interop_connection())` in kiln++.
[samples/gradle-coordinator](https://github.com/mKonic/kiln/tree/master/samples/gradle-coordinator)
is a complete app. Activity results and permission requests need `KilnActivity`.

Menus, tooltips and other popups from a `KilnView` placed this way open in windows of their
own, so they can reach past the view (over the app bar, for example), as they do from a
`ComposeView`. Popups in a dialog do the same.
