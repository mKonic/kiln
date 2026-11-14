# Files and preferences

## App directories

`kiln/ui/AppDirectories.h` gives the app's private directories, as `Context` does:

| Function | Android | Desktop |
| --- | --- | --- |
| `ui::FilesDir()` | `getFilesDir()` | `$XDG_DATA_HOME/kiln/<app>/files` |
| `ui::CacheDir()` | `getCacheDir()` | `$XDG_CACHE_HOME/kiln/<app>` |
| `ui::NoBackupFilesDir()` | `getNoBackupFilesDir()` | `$XDG_DATA_HOME/kiln/<app>/no_backup` |

The paths have no trailing slash and the directories exist. Read and write them with any
file API; do it off the main thread (`rt::WithContext` with the IO dispatcher).

## Preferences

`kiln/datastore/Preferences.h` is DataStore Preferences: typed key-value data in a file
under the files directory. It replaces `SharedPreferences`.

```cpp
#include <kiln/datastore/Preferences.h>
#include <kiln/material3/Switch.h>

using namespace kiln;

inline const auto kDarkTheme = datastore::BoolKey("dark_theme");
inline const auto kFontScale = datastore::FloatKey("font_scale");

void SettingsScreen() {
    auto store = datastore::PreferencesDataStore::Open("settings");
    const datastore::Preferences& prefs = datastore::CollectAsState(store).Get();
    const bool dark = prefs.Get(kDarkTheme).value_or(false);
    material3::Switch(dark, [store](bool on) {
        store->Update([on](datastore::MutablePreferences& p) { p.Set(kDarkTheme, on); });
    });
}
```

* Keys are typed: `BoolKey`, `IntKey`, `LongKey`, `FloatKey`, `DoubleKey`, `StringKey`
  and `StringSetKey`. `Get` returns an empty optional when nothing is stored.
* `Open(name)` returns the same store each time, so call it wherever it is needed.
* `CollectAsState(store)` recomposes the caller after each change. `store->Data()` is the
  same as a [flow](composables.md#flows).
* `Update` applies the change to the latest data and writes the file on a background
  thread. Changes are applied in the order they were made, and the file is replaced
  atomically, so a crash leaves either the old data or the new.
* In a coroutine, `co_await store->Edit(...)` does the same and returns once the file is
  written; a failed write throws.
* A file that cannot be parsed is read as empty preferences.

From C, `kiln_preferences_open` and the `kiln_preferences_*` getters and setters do the
same; kiln++ has `kpp::Preferences` and Rust `kiln::Preferences`.
