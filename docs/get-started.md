# Set up and run a sample

This page sets up a Linux machine to build kiln, runs a sample on an Android device, and
runs the same sample in a desktop window.

## Requirements

**Device**

* Android 12 (API 31) or newer
* A 64-bit CPU (arm64-v8a or x86_64)
* Vulkan 1.1

The Android Emulator works too, with an x86_64 system image.

**Build machine**

* Linux
* CMake 3.22 or newer, and Ninja
* The Android NDK r30 and the Android SDK (platform 36, build tools, platform tools)
* JDK 17 or newer
* `glslc`, from the Vulkan SDK or the `shaderc` package
* Python 3

For the desktop window, also install the development packages for GLFW, HarfBuzz,
FreeType, libpng, libjpeg and the Vulkan loader.

## Get the source

kiln uses Git submodules:

```sh
git clone --recurse-submodules https://github.com/mKonic/kiln.git
cd kiln
```

## Point kiln at the SDK and NDK

The tools look for the NDK in `/opt/android-ndk` and the SDK in `~/Android/Sdk`. If yours
are elsewhere, set these variables:

| Variable | Default |
|---|---|
| `KILN_NDK` (or `ANDROID_NDK_ROOT`) | `/opt/android-ndk` |
| `KILN_SDK` (or `ANDROID_HOME`) | `~/Android/Sdk` |

## Run a sample on a device

Connect a device with USB debugging on, then:

```sh
tools/kiln run gallery
```

This builds the gallery for the device's CPU, packs an APK, installs it and starts it.
With more than one device attached, choose one with `ANDROID_SERIAL`:

```sh
ANDROID_SERIAL=emulator-5554 tools/kiln run gallery
```

`tools/kiln list` prints every app the tree builds.

## Run a sample on the desktop

The samples also build for Linux, where they open in a desktop window:

```sh
cmake -S . -B build/host -G Ninja
cmake --build build/host -j8
build/host/gallery
```

On the desktop, text is shaped with HarfBuzz and FreeType, so it is close to Android's
but not identical. Platform services such as the keyboard, accessibility and autofill
exist only on Android.

## Run the tests

The same host build has the test suite:

```sh
cmake --build build/host --target kiln_tests -j8
build/host/kiln_tests
```

Pass a name to run only matching tests, for example `build/host/kiln_tests Lazy`.

## Next

* [Write your first app](first-app.md)
