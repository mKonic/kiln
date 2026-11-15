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

## Kotlin, Java and Maven dependencies

An app that needs JVM code of its own, such as a Kotlin class or a library from Maven, is
created with `--jvm`:

```sh
tools/kiln new notes --jvm
```

Besides `CMakeLists.txt` and `src/main.cpp`, it gets a Gradle build: `settings.gradle.kts`,
`build.gradle.kts`, the Gradle wrapper, `src/main/AndroidManifest.xml` and
`src/main/kotlin/.../App.kt`. `tools/kiln build|apk|bundle|run notes` then runs Gradle, and so
can Android Studio or `./gradlew assembleRelease` in `apps/notes`.

* The C++ is still built by kiln's CMake, with the app's `CMakeLists.txt`. Gradle builds
  only the app's library.
* kiln's Java host (`KilnActivity` and the platform code) is compiled with the app's own
  sources.
* Maven dependencies go in `dependencies { }` in `build.gradle.kts`.
* The manifest is `src/main/AndroidManifest.xml`. Permissions, services and other elements
  go there instead of `kiln_add_app`'s `PERMISSIONS` and `*_XML` options.
* Release builds are signed with the key that `KILN_KEYSTORE`, `KILN_KEY_ALIAS`,
  `KILN_KEYSTORE_PASSWORD` and `KILN_KEY_PASSWORD` name, else with kiln's debug key.
* kiln needs CMake 3.24 or newer, which is newer than the SDK's 3.22. The build uses the
  `cmake` on `PATH`, unless `KILN_CMAKE_VERSION` names an installed version. `KILN_NDK`
  sets the NDK if it isn't in `/opt/android-ndk`.

## Calling Kotlin and Java

In an app made with `--jvm`, C++ and Kotlin call each other through interfaces declared in
Kotlin. `tools/gen-jvm` writes both sides when the app builds: the Kotlin half through
Gradle, the C++ half through `JVM_BRIDGE src/main/kotlin` in `CMakeLists.txt`.
[samples/bridge](../samples/bridge) has the whole example; `tools/bridge-test` runs its checks
on a device.

```kotlin
@CppStruct
data class Note(val id: Long, val title: String, val tags: List<String>, val body: String?)

// Implemented in C++, called from Kotlin as NoteStoreCpp.
@ImplementedInCpp
interface NoteStore {
    fun add(title: String, tags: List<String>, body: String?): Long
    fun find(id: Long): Note?
    suspend fun summarize(delayMs: Long): Summary
}

// Implemented in Kotlin, registered with PlatformCpp.register(impl), called from C++.
@CalledFromCpp
interface Platform {
    fun deviceName(): String
    suspend fun fetch(text: String, delayMs: Long): ByteArray
}
```

The annotations are in `dev.kiln.bridge`. In C++, `kiln_bridge.h` declares a struct for each
`@CppStruct` and a class for each interface, in a namespace named after the Kotlin package.
C++ implements `NoteStore` and supplies it with `MakeNoteStore`:

<!-- check: skip -->
```cpp
#include "kiln_bridge.h"
namespace app = com::example::notes;

class Store : public app::NoteStore {
public:
    std::int64_t Add(std::string title, std::vector<std::string> tags, std::optional<std::string> body) override;
    std::optional<app::Note> Find(std::int64_t id) override;
    kiln::rt::Task<app::Summary> Summarize(std::int64_t delayMs) override {
        co_await kiln::rt::Delay(delayMs);
        co_return app::Summary{};
    }
};
std::unique_ptr<app::NoteStore> app::MakeNoteStore() { return std::make_unique<Store>(); }

kiln::rt::Task<> Greet(kiln::rt::State<std::string>& text) {
    if (auto platform = app::Platform::Get()) {
        std::vector<std::uint8_t> bytes = co_await platform->Fetch("hi", 100);
        text.Set(platform->DeviceName() + ": " + std::string(bytes.begin(), bytes.end()));
    }
}
```

* Types that cross: `Int`, `Long`, `Float`, `Double`, `Boolean`, `String`, `ByteArray`,
  `List<T>`, nullable types (`std::optional`), `@CppStruct` data classes and enums, and `Unit`
  as a result. Values are copied.
* A plain function runs on the thread that calls it, so a C++ implementation called from
  several Kotlin threads must lock its own data. C++ can call Kotlin from any thread, including
  threads it started.
* A `suspend fun` is an `rt::Task` in C++. One that C++ implements runs on kiln's main
  dispatcher, with or without an activity (a service, a receiver). One that Kotlin implements
  runs on `Dispatchers.Main`. Cancelling the caller cancels the callee, in both directions.
* A C++ exception reaches Kotlin as `dev.kiln.bridge.CppException`, with the C++ type in its
  `type`. A Kotlin exception reaches C++ as `kiln::jvm::JavaException`, with the class name.
  An exception passing through and back keeps its identity: Kotlin catches the exception it
  threw.
* For JNI by hand, `kiln/jvm/Jvm.h` has the same helpers: `Env()` attaches the calling thread,
  `Local` and `Global` manage references, `FindClass` uses the app's class loader, `Check`
  turns a pending Java exception into `JavaException`, and `ToString`/`ToJava` convert strings
  and byte arrays.
* With R8 (`isMinifyEnabled`), keep `dev.kiln.**` and the generated `*Cpp` classes. Their
  methods are only called from native code.

## Google Play libraries

Google Play's libraries (billing, in-app review, in-app updates, Play Integrity) are Kotlin and
Java APIs. In an app made with `--jvm`, use them the way the JVM bridge uses any Kotlin:
1. Add the library to `dependencies { }` in `build.gradle.kts`.
2. Declare a small `@CalledFromCpp` interface with the calls the app needs.
3. Implement it in Kotlin over the library.

C++ then calls it, and suspend functions are tasks it can `co_await`. For example, asking for a
review:

```kotlin
// build.gradle.kts: implementation("com.google.android.play:review-ktx:2.0.2")
@CalledFromCpp
interface Reviews {
    /** Google Play decides whether the review card shows; this returns when it is done. */
    suspend fun ask()
}

class PlayReviews(private val context: Context, private val activity: () -> Activity?) : Reviews {
    override suspend fun ask() {
        val manager = ReviewManagerFactory.create(context)
        val info = manager.requestReview()
        activity()?.let { manager.launchReview(it, info) }
    }
}
// In Application.onCreate: ReviewsCpp.register(PlayReviews(this) { resumedActivity })
```

<!-- check: skip -->
```cpp
kiln::rt::Task<> AfterTenthNote() {
    if (auto reviews = app::Reviews::Get()) co_await reviews->Ask();
}
```

* Calls that open Google Play's UI, such as the review card, the purchase screen or the update
  prompt, need the activity in front. The Kotlin side can track it with
  `registerActivityLifecycleCallbacks` in the app's `Application`.
* Events the library delivers on its own, such as new purchases, go back to C++ through an
  `@ImplementedInCpp` interface that Kotlin calls.
* [samples/billing](../samples/billing) uses Google Play Billing 9 this way. It connects, lists
  products, opens the purchase screen, and acknowledges or consumes purchases. Purchases need
  the app on a testing track in the Play Console and a Play account on the device. Elsewhere,
  the sample shows billing's error code.
* Verify purchases and integrity verdicts on the app's server. On the device, the result is
  only a hint.

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

## Notifications

`kiln/ui/Notifications.h` posts notifications in a channel. Tapping one opens the app at its
`data`, a deep link that arrives as the activity's intent (`OnNewIntent` while the app runs);
action buttons do the same with their own links:

```cpp
#include <kiln/ui/Notifications.h>

void NotifySynced(int count) {
    kiln::ui::CreateNotificationChannel({ .id = "sync", .name = "Sync", .importance = kiln::ui::kImportanceLow });
    kiln::ui::Notify({
        .id = 1, .channelId = "sync",
        .title = "Notes synced", .text = std::to_string(count) + " notes are up to date",
        .data = "notes://list",
        .actions = { { .title = "Show", .data = "notes://list?recent" } },
    });
}
```

* Declare `android.permission.POST_NOTIFICATIONS` and request it with
  `contracts::RequestPermission`; on Android 13 and later nothing is shown until it is
  granted. `AreNotificationsEnabled()` tells whether notifications reach the user.
* A notification with the same `id` (and `tag`) replaces the previous one: update a progress
  bar (`progress`, `progressMax`, `indeterminate`) with `onlyAlertOnce` so it does not sound
  each time. `ongoing` keeps it from being swiped away.
* `smallIcon` names a white-on-transparent drawable in the app's `res/` for the status bar;
  without one the launcher icon is used.
* With `ACTIVITY_ATTRIBUTES "android:launchMode=\"singleTop\""` a tap reaches the running
  activity rather than starting another.

## Background work

`kiln::work` (`kiln/work/Work.h`) runs work when its constraints hold, even after the
app's process has gone. It follows WorkManager's model, on top of JobScheduler. Register a
worker at namespace scope, so that a process the system starts only to run a job also has
it:

<!-- prelude: std::string url; -->
```cpp
kiln::work::WorkerRegistration sync("sync", [](kiln::work::WorkContext c) -> kiln::rt::Task<kiln::work::Result> {
    try {
        kiln::net::HttpResponse r = co_await kiln::net::Fetch({ .url = c.input });
        co_return r.Ok() ? kiln::work::Result::Success() : kiln::work::Result::Retry();
    } catch (const kiln::net::HttpError&) {
        co_return kiln::work::Result::Retry();
    }
});

void ScheduleSync() {
    kiln::work::Enqueue("sync-notes", { .worker = "sync", .input = url,
                                        .constraints = { .requiredNetwork = kiln::work::NetworkType::Connected },
                                        .repeatIntervalMillis = 60 * 60 * 1000 });
}
```

* Work is unique by name. Enqueuing the same name replaces the scheduled work, or keeps it
  with `ExistingWorkPolicy::Keep`. `Cancel` and `CancelAll` remove work, and `Works()` lists
  what is scheduled or running.
* `Result::Retry()` runs the work again after the backoff delay. With exponential backoff
  (the default) the delay doubles each attempt; with linear backoff it grows by the delay
  each time. The default delay is 30 s and the most is 5 hours. `WorkContext::runAttemptCount`
  counts the attempts.
* Periodic work (`repeatIntervalMillis`, at least 15 minutes) runs once per interval, inside
  the `flexMillis` window at the interval's end. A run that fails still runs again next
  period.
* A worker runs on the main dispatcher, whether or not an activity is open. When the system
  stops it early, because a constraint stopped holding or its time ran out, the worker's task
  is cancelled and the job runs again later.
* Network constraints need `android.permission.ACCESS_NETWORK_STATE`. Jobs survive a reboot
  when the app holds `android.permission.RECEIVE_BOOT_COMPLETED`. A force stop cancels them,
  as does a reboot without that permission; kiln keeps each job and schedules it again when
  the app next starts, at once if it is due.
* On the desktop, work runs in the app's process when it is due, and constraints are
  ignored.

From C, `kiln_work_register_worker` and `kiln_work_enqueue` do the same; a C worker answers
through `kiln_work_complete`. Register C workers in a function marked
`__attribute__((constructor))`. A Rust cdylib registers them in its `JNI_OnLoad`, before it
calls `kiln_jni_onload`.

## Alarms

Work runs when the system finds it convenient. A reminder, a timer or a calendar event has to
run at its time. `kiln::alarm` (`kiln/work/Alarm.h`) sets alarms with AlarmManager, the way
AlarmManagerCompat does. As with workers, register the handler at namespace scope: when the app
isn't running, the system starts a process just for the alarm.

```cpp
#include <kiln/ui/Notifications.h>
#include <kiln/work/Alarm.h>

kiln::alarm::AlarmRegistration reminder("reminder", [](const kiln::alarm::Alarm& a) {
    kiln::ui::Notify({ .id = 7, .channelId = "reminders", .title = "Reminder", .text = a.payload });
});

void RemindAt(std::int64_t atMillis, std::string text) {
    kiln::alarm::Schedule({ .name = "reminder", .triggerAtMillis = atMillis, .payload = std::move(text) });
}
```

* An alarm's time is wall-clock milliseconds since the epoch. Scheduling a name again
  replaces its alarm. `Cancel` removes it and `Pending()` lists those still to go off.
* `kind` picks the AlarmManager call:
  * `ExactWhileIdle` (the default) goes off on time, even when the device is dozing.
  * `Exact` is on time but waits while the device dozes.
  * `AlarmClock` is the user's own alarm, shown in the status bar and on the lock screen.
    `showUri` is the deep link opened from there.
  * `Inexact`, `InexactWhileIdle` and `Window` let the system batch the alarm to save
    battery.
* The exact kinds need `android.permission.SCHEDULE_EXACT_ALARM`, which the user can turn off.
  When it is off, `Schedule` returns false. `CanScheduleExactAlarms()` checks the permission and
  `OpenExactAlarmSettings()` opens the page where the user allows it. Alarm-clock and calendar
  apps may declare `USE_EXACT_ALARM` instead.
* The platform forgets alarms on a reboot. kiln keeps them and sets them again after the
  reboot (with `android.permission.RECEIVE_BOOT_COMPLETED`), and also when exact alarms are
  allowed again. An alarm whose time passed while the device was off goes off as it starts.
  A force stop cancels the app's alarms too; they are set again when the app next starts.
* On the desktop, alarms go off in the app's process, on the main dispatcher's clock.

From C, `kiln_alarm_register_handler` and `kiln_alarm_schedule` do the same. kiln++ has
`kpp::register_alarm_handler` and `kpp::schedule_alarm`; Rust has `kiln::register_alarm_handler`
and `kiln::schedule_alarm`.

## Home-screen widgets

`kiln::glance` (`kiln/glance/Glance.h`) builds home-screen widgets in Glance's model. A widget's
content is a tree of `Box`, `Row`, `Column`, `Text`, `Image`, `Button`, `CheckBox`, `Switch`,
progress indicators and `LazyColumn`. kiln draws it as RemoteViews for each size the launcher
shows, and redraws it when the app calls `UpdateAll`:

```cpp
namespace g = kiln::glance;
std::atomic<int> taps{ 0 };

g::WidgetRegistration counter("counter", { .sizeMode = g::SizeMode::Exact, .content = [](const g::WidgetContext& w) {
    g::Column(g::Padding(g::FillMaxSize(g::Background(g::GlanceModifier{}, { 0xFFFFFFFF, 0xFF1D1B20 })), 12), g::Alignment::Center(), [&] {
        g::Text(std::to_string(taps.load()) + " taps", {}, { .fontSize = 18, .fontWeight = g::FontWeight::Bold });
        g::Button("Tap", g::RunCallback("tap"));
        if (w.size.width > 200) g::Button("Open", g::StartActivity("app://counter"));
    });
} });
g::ActionCallbackRegistration tap("tap", [](const g::ActionContext&) { ++taps; });
```

* Register widgets and callbacks at namespace scope. The system starts the app's process to
  draw a widget or run a callback, without an activity, and the registrations must exist
  there.
* Declare each widget with `WIDGETS counter` in `kiln_add_app`, and give its
  appwidget-provider info in `res/xml/counter_widget.xml` (its size, resize modes and
  category). A Gradle app declares a receiver class extending `dev.kiln.KilnWidgetReceiver`
  instead, with `<meta-data android:name="dev.kiln.widget" android:value="counter" />` and the
  provider info.
* `SizeMode::Single` draws once, at the widget's minimum size. `Exact` draws for every size
  the launcher uses. `Responsive` draws for the sizes you list, and the launcher picks among
  them. `WidgetContext::size` is the size being drawn.
* `RunCallback` runs a registered callback in the app's process, then redraws the widget.
  `StartActivity` opens the app at a deep link. A `CheckBox` or `Switch` reports its new state in
  `ActionContext::checked`.
* `glance::UpdateAll(name)` redraws every placed instance after the app's data changes, for
  example from a `kiln::work` job. `RequestPin(name)` asks the launcher to place one.
* A widget has up to four `LazyColumn`s. Images are app drawables by name, or encoded bytes.

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

## Speech

`kiln/speech` reads text aloud with the device's speech engine (`TextToSpeech`) and turns what
the user says into text (`SpeechRecognizer`):

```cpp
#include <kiln/foundation/activity/ActivityResult.h>
#include <kiln/speech/SpeechRecognizer.h>
#include <kiln/speech/TextToSpeech.h>

using namespace kiln;

speech::TextToSpeech& Voice() {
    static speech::TextToSpeech tts;   // binds to the engine once, on first use
    return tts;
}

void ReadAloud(const std::string& text) {
    Voice().Speak(text, { .queue = speech::QueueMode::Flush });
}

void SearchByVoice(rt::State<std::string>& query) {
    static speech::SpeechRecognizer recognizer;
    recognizer.SetListener({
        .onPartialResults = [&query](const speech::RecognitionResult& r) { query.Set(r.Best()); },
        .onResults = [&query](const speech::RecognitionResult& r) { query.Set(r.Best()); },
        .onError = [&query](speech::RecognitionError e) { query.Set(speech::ToString(e)); },
    });
    auto ask = foundation::RememberLauncherForActivityResult<std::string, bool>(
        std::make_shared<foundation::contracts::RequestPermission>(), [](bool granted) {
            if (granted) recognizer.Start({ .language = "en-US" });
        });
    (void)ask;   // ask.Launch("android.permission.RECORD_AUDIO") from a button
}
```

* **Text to speech.** The engine takes a moment to bind. Speech asked for during that time waits
  and is then spoken in order. `Status()` reports the binding, and `SetOnStatusChanged` reports
  when it is done.
  * `Speak` queues text, or with `QueueMode::Flush` interrupts what is playing. `SetListener`
    reports when each utterance starts, finishes, fails, or is stopped, and which words are being
    spoken.
  * `SetSpeechRate`, `SetPitch`, `SetLanguage` (a BCP 47 tag) and `SetVoice` (one of `Voices()`)
    change how later text sounds.
* **Speech to text.** It needs `android.permission.RECORD_AUDIO`, requested at run time.
  * `Start` listens until the user stops speaking. The listener receives partial results while
    they talk, then the final candidates, most likely first.
  * `Stop` ends listening early and still delivers the results; `Cancel` delivers none.
  * `IsAvailable()` tells whether the device has a recognition service at all. With
    `SpeechRecognizer(true)`, recognition happens on the device and works offline where
    supported.
* kiln's manifests already make the speech engines and recognition services visible to the app.
  Android 11 and later hide them otherwise.
* On the desktop there is no engine or service. `Status()` is `Failed`, and `Start` reports
  `NotAvailable`.

From C, `kiln_remember_tts` and `kiln_remember_speech_recognizer` do the same. kiln++ has
`kpp::TextToSpeech` and `kpp::SpeechRecognizer`; Rust has `kiln::remember_text_to_speech` and
`kiln::remember_speech_recognizer`.

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
`ui::CheckSelfPermission(permission)` (`kiln/ui/ActivityResult.h`, ContextCompat's) tells
whether the app holds a permission now, without asking. For example, an app can use it to record
with sound only when it is allowed to.

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

To share a file of your own, turn its path into a URI with `ui::GetUriForFile`
(`kiln/ui/FileProvider.h`, androidx's FileProvider):

```cpp
#include <kiln/ui/AppDirectories.h>
#include <kiln/ui/FileProvider.h>
#include <kiln/ui/Share.h>

void ShareExport() {
    const std::string path = kiln::ui::CacheDir() + "/exports/notes.pdf";
    kiln::ui::Share({ .streams = { kiln::ui::GetUriForFile(path, "Notes.pdf") }, .mimeType = "application/pdf" });
}
```

Files in `FilesDir`, `CacheDir`, `NoBackupFilesDir` and the app's folders on external storage
can be shared this way; anything else throws `std::invalid_argument`. Only the app you share
with can read the file, and only for that share.

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

### Playing media

`media::Player` (`kiln/media/Player.h`) plays audio and video files from assets, files,
`content://` URIs and `http(s)://` URLs. It decodes on its own thread and plays sound through
AAudio. `VideoSurface` (`kiln/foundation/VideoSurface.h`) shows the video, at the video's
aspect ratio:

```cpp
void Clip() {
    auto& c = *kiln::rt::Composition::Current();
    auto& player = c.RememberNew<kiln::media::Player>({}, {});
    c.LaunchedEffect(kiln::rt::Keys{}, [&player]() -> kiln::rt::Task<> {
        player.SetMediaUri("asset:///intro.mp4");
        player.Play();
        co_return;
    });
    const kiln::media::PlayerState& s = kiln::rt::CollectAsState(player.StateFlow()).Get();
    VideoSurface(FillMax(Modifier{}, Axis::Horizontal), player);
    Text(std::to_string(s.positionMs / 1000) + " s" + (s.state == kiln::media::PlaybackState::Ended ? ", done" : ""));
}
```

* `Play`, `Pause`, `SeekTo`, `SetRepeat` and `SetVolume` control playback. The state reports
  `Buffering`, `Ready` and `Ended`, the position and duration, the video's size and rotation,
  and an `error` when the media cannot be played.
* Video frames are timed against the audio clock, as media3's ExoPlayer times them: a frame
  more than 30 ms late is dropped. Without sound, or without an audio output, the video keeps
  its own time.
* Remember the player above its `VideoSurface`, so it outlives the surface.

### Playlists, speed and tracks

The player holds a playlist of `media::MediaItem`s, as media3's ExoPlayer does:

```cpp
void Episodes(kiln::media::Player& player) {
    kiln::media::MediaItem intro = kiln::media::MediaItem::FromUri("asset:///intro.mp4");
    intro.mediaMetadata.title = "Intro";
    kiln::media::MediaItem part = kiln::media::MediaItem::FromUri("https://example.com/episode.mp4");
    part.clippingConfiguration.startPositionMs = 30'000;   // skip the recap
    player.SetMediaItems({ intro, part });
    player.SetRepeatMode(kiln::media::RepeatMode::All);
    player.Prepare();
    player.Play();
}
```

* **The playlist.** `AddMediaItems`, `MoveMediaItems`, `RemoveMediaItems` and
  `ReplaceMediaItems` change it while it plays. `SeekToNext`, `SeekToPrevious` (to the start
  of the item after its first 3 s), `SeekBack` and `SeekForward` move through it.
  `SetShuffleModeEnabled` shuffles the order. The state has the current index, the item count,
  and whether there is a next or previous item. `PlayerListener::onMediaItemTransition` says
  why the item changed.
* **Speed.** `SetPlaybackSpeed(1.5f)` plays faster, keeping the pitch. The sound is
  time-stretched with media3's Sonic, as ExoPlayer does.
* **Tracks.** `PlayerState::tracks` lists the item's audio, video and text tracks, and which
  ones play. `SetTrackSelectionParameters` sets preferred audio and text languages, roles and
  labels, or turns a type off. Without preferences, a video with several audio languages plays
  its default track, then the device's language. An override picks one track:
  `params.SetOverrideForType({ .type = TrackType::Audio, .groupId = group.id, .trackIndices = { 0 } })`.
* **Errors.** `playerError` carries ExoPlayer's error codes (`PlaybackException`), for example
  `IoNetworkConnectionFailed` or `DecoderInitFailed`.
* **Staying awake.** While it plays, the player holds a partial wake lock (`WakeMode::Local`,
  as ExoPlayer does), so sound keeps going with the screen off. Declare
  `android.permission.WAKE_LOCK`. `WakeMode::Network` also holds Wi-Fi for streams.
* **Stuck playback.** Playback that makes no progress (10 s playing without moving, 10 min
  buffering) fails with `PlaybackException::Timeout` instead of hanging.
  `SetStuckDetectionTimeoutsMs` changes the limits.
* **Device volume.** With `SetDeviceVolumeControlEnabled(true)` the player reads and changes
  the device's volume for its stream (`deviceVolume`, `SetDeviceVolume`,
  `IncreaseDeviceVolume`, `SetDeviceMuted`).

### Subtitles

Give an item its subtitle files, WebVTT or SubRip, from the same kinds of URI as the media.
`VideoSurface` draws them over the video:

```cpp
kiln::media::MediaItem WithSubtitles() {
    kiln::media::MediaItem item = kiln::media::MediaItem::FromUri("asset:///film.mp4");
    item.subtitleConfigurations = {
        { .uri = "asset:///film.en.vtt", .mimeType = "text/vtt", .language = "en", .selectionFlags = kiln::media::kSelectionFlagDefault },
        { .uri = "asset:///film.de.srt", .mimeType = "application/x-subrip", .language = "de" },
    };
    return item;
}
```

* **Which one shows.** The player picks as ExoPlayer does: an app's preferred text language,
  else a track flagged default. A forced track shows in the language of the playing audio.
  When the user has turned captions on in the system settings, their language wins.
  `SetTrackTypeDisabled(TrackType::Text, true)` turns subtitles off.
* **How they look.** Cues are placed, styled and sized as the file says: WebVTT positions,
  alignment, `<b>`, `<i>`, `<u>`, colour classes and `STYLE` blocks, and SubRip's HTML and
  `{\an}` tags. They are drawn in the user's caption style and text size from the system
  settings. `Subtitles` (`kiln/foundation/Subtitles.h`) draws them over anything else, with
  your own style if you like.
* **Your own drawing.** `PlayerState::currentCues` and `PlayerListener::onCues` give the cues
  as `AnnotatedString`s.
* Subtitles inside the media file (MP4 tx3g, MKV text tracks) are listed in `tracks` but not
  shown. TTML and SSA files are not read.

### Audio focus and media controls

A music or podcast app should behave like other media apps: pause when another app starts
playing, wait out a call, duck under a navigation prompt, and pause when the headphones are
unplugged. It should also show its controls in the notification shade and on the lock screen,
and keep playing with the app in the background. The player and `media::MediaSession`
(`kiln/media/MediaSession.h`) do this as media3's ExoPlayer and MediaSessionService do:

<!-- prelude: void NextTrack(); -->
```cpp
// At process scope, so playback outlives the activity; made on first use (on the main thread,
// after kiln has installed the platform's engine).
kiln::media::Player& Music() {
    static kiln::media::Player player;
    return player;
}
kiln::media::MediaSession& Session() {
    static kiln::media::MediaSession session(Music(), { .onSkipToNext = [] { NextTrack(); } });
    return session;
}

void StartPlaying() {
    auto& player = Music();
    auto& session = Session();
    player.SetAudioAttributes({ .usage = kiln::media::AudioUsage::Media, .contentType = kiln::media::AudioContentType::Music }, true);
    player.SetHandleAudioBecomingNoisy(true);
    session.SetMetadata({ .title = "Song", .artist = "Band", .artworkUri = "asset:///cover.jpg" });
    player.SetMediaUri("asset:///song.mp3");
    player.Play();
}
```

* **Audio focus.** With focus handling on, the player asks for focus before it plays.
  - Another app taking focus pauses it, and `playWhenReady` turns false.
  - A call suppresses it until focus comes back (`suppression` is `TransientAudioFocusLoss`).
  - A notification ducks it to a fifth of its volume. Speech content pauses instead.
* **Headphones.** `SetHandleAudioBecomingNoisy(true)` pauses when the sound would move to the
  speaker.
* **The session.** It shows the title, artist and artwork in the system's media controls, with
  play/pause and the skip buttons you give callbacks for. Headset, Bluetooth and lock-screen
  buttons reach the player.
* **Background playback.** While the user listens, and for ten minutes after a pause, a media
  foreground service keeps the app running. Declare `android.permission.FOREGROUND_SERVICE`
  and `android.permission.FOREGROUND_SERVICE_MEDIA_PLAYBACK` in `PERMISSIONS`. A player
  remembered in content stops when its screen goes, so keep it at process scope to play on.

#### Extra buttons, the queue and requests from other devices

`SetMediaButtonPreferences` adds buttons to the media controls, as media3's
`setMediaButtonPreferences` does. A button runs either a custom action, which your
`onCustomCommand` receives, or a player command:

<!-- prelude: kiln::media::MediaSession& Session(); bool liked = false; -->
```cpp
using B = kiln::media::CommandButton;
Session().SetMediaButtonPreferences({
    { .icon = liked ? B::HeartFilled : B::HeartUnfilled, .displayName = "Like", .customAction = "like",
      .slots = { B::kSlotOverflow } },
    { .icon = B::ShuffleOff, .displayName = "Shuffle", .playerCommand = kiln::media::PlayerCommand::SetShuffleMode,
      .slots = { B::kSlotOverflow } },
});
```

* **Placement.** `slots` lists where a button may go, in order of preference. Without slots, the
  icon or command decides: play/pause goes in the centre, skip back and rewind go back, skip
  forward and fast forward go forward, and everything else goes in the overflow. A button in the
  back or forward slot replaces the previous or next button.
* **Icons.** The icons are media3's own drawables. Set `iconResource` to use a drawable of your
  app's instead.
* **The collapsed notification.** It shows the back, centre and forward buttons. Set
  `compactViewIndex` to choose its buttons yourself.
* **The queue.** Controllers (Android Auto, Wear, Bluetooth) see the playlist. Each item's title
  is its `displayTitle`, or else its title, artist and album. The queue's title is the playlist
  metadata's title.
* **Ratings.** The current item's `userRating` sets the rating kind the session offers, and
  `onSetRating` receives the ratings controllers send.
* **Requests from other devices.** An assistant or a car may ask to play something by media id,
  URI or search. `onAddMediaItems` turns the request into playable items, and the session plays
  them. Without the callback, requests are refused, as in media3.
* **Extras.** `SetSessionExtras` sets the session's extras.

### Camera

`CameraPreview` shows a camera's live picture, upright on the display. Ask for
`android.permission.CAMERA` first (`contracts::RequestPermission`):

<!-- prelude: std::function<void(std::string)> showError; -->
```cpp
if (auto back = kiln::media::FindCamera(kiln::media::LensFacing::Back))
    CameraPreview(FillMax(Modifier{}, Axis::Horizontal), back->id, [&](std::string error) { showError(error); });
```

`media::Cameras()` lists the cameras with their facing, sensor orientation, sizes, flash, zoom
range and exposure range. The preview is 4:3 within 1080p, as in CameraX, and a front camera's
preview is mirrored.

To take pictures and control the camera, open a `media::Camera` with still capture. It works
like CameraX's `Camera` from `bindToLifecycle`:

<!-- prelude: std::function<void(std::string)> show; -->
```cpp
auto& c = *kiln::rt::Composition::Current();
auto& camera = c.RememberNew<kiln::media::Camera>({}, {}, "0",
    kiln::media::CameraOptions{ .imageCapture = kiln::media::ImageCaptureOptions{} });
CameraPreview(FillMax(Modifier{}, Axis::Horizontal), camera);   // tap to focus, pinch to zoom
camera.SetZoomRatio(2);
camera.TakePicture({ .file = kiln::ui::CacheDir() + "/photo.jpg" }, [&](kiln::media::ImageCaptureResult r) {
    show(r.error == kiln::media::ImageCaptureResult::Error::None ? r.savedUri : r.message);
});
```

* **State.** `StateFlow()` reports whether the camera is open, why it failed, and its zoom,
  exposure and torch.
* **Controls.** `SetZoomRatio`, `SetLinearZoom`, `EnableTorch`,
  `SetExposureCompensationIndex`, `StartFocusAndMetering` and `CancelFocusAndMetering` follow
  CameraX's rules:
  - The state changes at once, and the callback runs when the camera has the new value.
  - A newer request cancels the older one.
  - A value out of range fails, and so does a torch request on a camera without a flash.
* **Focus and metering.** By default, focus locks on the point you give and exposure and white
  balance meter there. Focus gives up after 5 seconds, and the camera goes back to continuous
  focus after `autoCancelDurationMs`.
* **Pictures.** `TakePicture` runs CameraX's capture pipeline:
  - The flash mode decides whether the flash fires. In auto, it fires when exposure asks for it,
    after a precapture metering sequence.
  - `CaptureMode::MaximizeQuality` also locks focus and white balance before the shot.
  - The JPEG is turned upright for the display rotation you pass.
  - Pictures are taken one at a time and saved whole, or handed to you as bytes.
* **Analysis.** Add `.imageAnalysis = kiln::media::ImageAnalysisOptions{}` to the options and
  call `SetAnalyzer`. The analyzer gets each frame (YUV_420_888 by default, 640x480 or the size
  you ask for) on the camera's thread, along with the rotation that turns it upright. Frames
  that arrive while it runs are dropped, as CameraX's keep-only-latest strategy does.
  `AnalysisFormat::Rgba8888` converts frames the way CameraX does. To hand results to the UI,
  take `rt::Dispatcher::Main()` on the main thread, then call its `PostFromAnyThread`.
* **Video.** Add `.videoCapture = kiln::media::VideoCaptureOptions{}` to the options to bind
  CameraX's `Recorder`:
  - The quality selector picks FHD, then HD, then SD, falling back as CameraX does, and the
    bitrates follow CameraX's defaults.
  - `StartRecording` records H.264 video into an MP4 file, upright for the display rotation.
    With audio, it also records AAC from the microphone, which needs
    `android.permission.RECORD_AUDIO`.
  - It returns a `Recording` you can pause, resume, mute or stop. Destroying it stops it.
  - The listener gets CameraX's events: start, status (duration, bytes, audio state), pause,
    resume, and a final event with the file or one of CameraX's error codes.
  - A file size or duration limit ends the recording. A recording that captured nothing deletes
    its empty file.

### Your own rendering

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
