# Command line tools and testing

The tools are in the [tools](https://github.com/mKonic/kiln/tree/master/tools) folder. They read the SDK and NDK locations from
`KILN_SDK` and `KILN_NDK` (see [Get started](get-started.md)) and use the device in
`ANDROID_SERIAL` when more than one is attached.

## tools/kiln

Creates apps, and builds, packs, installs and starts them.

| Command | Does |
|---|---|
| `tools/kiln new <name>` | Creates an app in `apps/<name>` (`--package id`, `--label text`) |
| `tools/kiln list` | Lists the apps |
| `tools/kiln build <app>` | Builds the app's library for each ABI |
| `tools/kiln apk <app>` | Builds and packs `build/apk/<app>.apk` |
| `tools/kiln run <app>` | Builds, packs, installs and starts the app |
| `tools/kiln version` | Prints the kiln version the tree is at |
| `tools/kiln update [version]` | Moves the tree to the newest release, or the one named |

| Option | Effect |
|---|---|
| `--abi arm64-v8a,x86_64` | The ABIs to build. `run` defaults to the device's, the others to both |
| `--debug` | A debuggable APK that answers [Volition](android.md#debug-builds-and-volition) |
| `--release` | An APK for publishing (the default) |
| `--keystore file --key-alias name` | Signs with your key; passwords from `KILN_KEYSTORE_PASSWORD` and `KILN_KEY_PASSWORD` |
| `--lto` | Link-time optimization: a 4-5% smaller library and a slower build, at the same speed |
| `--out file` | Where `apk` writes the APK |

`KILN_CONFIG` chooses the CMake build type, `RelWithDebInfo` by default.

## Other tools

| Tool | Does |
|---|---|
| `tools/build-android.sh [abi...]` | Builds kiln and every sample into `build/android/<abi>` |
| `tools/build-aar` | Builds `build/kiln.aar` for [Gradle projects](android.md#build-with-gradle) |
| `tools/kiln-pack` | Packs a native library into an APK without Gradle |
| `tools/device-test [filter...]` | Runs the test suite on a device |
| `tools/vtouchpad/run [script]` | Plays trackpad gestures on a rooted device that has no trackpad |
| `tools/docs-check [file.md...]` | Compiles the C++ and C code blocks in `docs/` against the host build |

## Testing

The test suite runs on the desktop and on devices. On the desktop:

```sh
cmake --build build/host --target kiln_tests -j8
build/host/kiln_tests            # every test
build/host/kiln_tests Lazy Text  # tests whose names contain Lazy or Text
```

On a device, with the platform's text, images and services:

```sh
ANDROID_SERIAL=<serial> tools/device-test
```

### Writing a test

A test is a `TEST(Name) { ... }` block with `CHECK` and `CHECK_EQ`, in a file in
[tests](https://github.com/mKonic/kiln/tree/master/tests). Tests compose content without a window, then check layout, semantics
or state. The existing tests show how to compose content, send input, advance frames and
read the semantics tree.

### Driving an app

A debug build answers Volition, so a script or an agent can test the running app by name:

```sh
volition ui                  # what is on screen
volition click "Add"         # click by label
volition wait route=detail   # wait until a condition holds
```

## Logging

`kiln::log` (`kiln/Log.h`) writes to logcat on Android and to stderr on the desktop:

```cpp
#include <kiln/Log.h>

void Loaded(int count, double ms) {
    kiln::log::Info("Notes", "loaded {} notes in {:.1f} ms", count, ms);
    kiln::log::Debug("Notes", "first id {}", 42);   // kept only for an enabled tag
}
```

Messages use `std::format`. As with `android.util.Log`, debug and verbose messages are kept
only for tags turned on with `adb shell setprop log.tag.Notes D`, and are not formatted
otherwise. From C, `kiln_log(KILN_LOG_INFO, "Notes", text)`; kiln++ has `kpp::log_info` and
Rust `kiln::log!(Info, "Notes", "loaded {} notes", n)`.

## Performance

kiln marks its frame with system trace sections, under the names Compose uses where there is
one: `Recomposer:animation`, `Recomposer:recompose` and `Compose:recompose`,
`AndroidOwner:measureAndLayout`, `AndroidOwner:onDraw`, then `kiln:submit` with
`kiln:acquire` (waiting for the next image to draw into) and `kiln:prefetch`. Record them with
Perfetto or `atrace`, for a debuggable or profileable app:

```sh
adb shell atrace --async_start -a com.example.notes view gfx
# ... use the app ...
adb shell atrace --async_dump -a com.example.notes > trace.txt
```

Add sections of your own with `kiln::trace::Section` (`kiln/Trace.h`): it lasts for its
scope, and costs one check when nothing is being recorded. `kiln::trace::Counter` plots a
value. From C, `kiln_trace_begin` and `kiln_trace_end`.

## App size

An app links kiln statically and exports only the entry points Android calls, so the
linker drops the parts of kiln the app does not reach and merges identical code. The
samples' stripped arm64 libraries:

| Sample | Size |
|---|---|
| `hello` | 2.7 MB |
| `gallery` (every Material 3 component) | 10.0 MB |
| `catalog` | 12.1 MB |
