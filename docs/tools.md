# Command line tools and testing

The tools are in the [tools](https://github.com/mKonic/kiln/tree/master/tools) folder. They read the SDK and NDK locations from
`KILN_SDK` and `KILN_NDK` (see [Get started](get-started.md)) and use the device in
`ANDROID_SERIAL` when more than one is attached.

## tools/kiln

Builds, packs, installs and starts the apps declared with `kiln_add_android_app`.

| Command | Does |
|---|---|
| `tools/kiln list` | Lists the apps |
| `tools/kiln build <app>` | Builds the app's library for each ABI |
| `tools/kiln apk <app>` | Builds and packs `build/apk/<app>.apk` |
| `tools/kiln run <app>` | Builds, packs, installs and starts the app |

| Option | Effect |
|---|---|
| `--abi arm64-v8a,x86_64` | The ABIs to build. `run` defaults to the device's, the others to both |
| `--debug` | A debuggable APK that answers [Volition](android.md#debug-builds-and-volition) |
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

## App size

An app links kiln statically and exports only the entry points Android calls, so the
linker drops the parts of kiln the app does not reach and merges identical code. The
samples' stripped arm64 libraries:

| Sample | Size |
|---|---|
| `hello` | 2.7 MB |
| `gallery` (every Material 3 component) | 10.0 MB |
| `catalog` | 12.1 MB |
