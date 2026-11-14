# Files, preferences and databases

## App directories

`kiln/ui/AppDirectories.h` gives the app's private directories, as `Context` does:

| Function | Android | Desktop |
| --- | --- | --- |
| `ui::FilesDir()` | `getFilesDir()` | `$XDG_DATA_HOME/kiln/<app>/files` |
| `ui::CacheDir()` | `getCacheDir()` | `$XDG_CACHE_HOME/kiln/<app>` |
| `ui::NoBackupFilesDir()` | `getNoBackupFilesDir()` | `$XDG_DATA_HOME/kiln/<app>/no_backup` |
| `ui::DatabasePath(name)` | `getDatabasePath(name)` | `$XDG_DATA_HOME/kiln/<app>/databases/<name>` |

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

* Keys are typed: `BoolKey`, `IntKey`, `LongKey`, `FloatKey`, `DoubleKey`, `StringKey`,
  `StringSetKey` and `ByteArrayKey`. `Get` returns an empty optional when nothing is stored.
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

## SQLite

kiln carries SQLite (the NDK does not offer the platform's) and a database class in the
style of Room's runtime. Add `kiln_sqlite` to the app's `LIBS`:

```cmake
kiln_add_app(shelf
    SOURCES src/main.cpp
    LIBS kiln_material3 kiln_sqlite
    PACKAGE com.example.shelf
    LABEL "Shelf"
)
```

```cpp
#include <kiln/sqlite/Database.h>

using namespace kiln;

std::shared_ptr<sqlite::Database> OpenNotes() {
    return sqlite::Database::Open({
        .name = "notes.db",
        .version = 2,
        .onCreate = [](sqlite::Connection& c) {
            c.ExecuteScript("CREATE TABLE note (id INTEGER PRIMARY KEY, title TEXT NOT NULL, done INTEGER NOT NULL DEFAULT 0)");
        },
        .migrations = {
            { 1, 2, [](sqlite::Connection& c) { c.Execute("ALTER TABLE note ADD COLUMN done INTEGER NOT NULL DEFAULT 0"); } },
        },
    });
}

rt::Task<> AddNote(std::shared_ptr<sqlite::Database> db, std::string title) {
    co_await db->Write([title](sqlite::Connection& c) { c.Execute("INSERT INTO note (title) VALUES (?)", title); });
}

rt::Flow<std::vector<std::string>> NoteTitles(std::shared_ptr<sqlite::Database> db) {
    return db->Query({ "note" }, [](sqlite::Connection& c) {
        std::vector<std::string> titles;
        for (auto q = c.Query("SELECT title FROM note ORDER BY id"); q.Step();) titles.push_back(q.GetText(0));
        return titles;
    });
}
```

* The file is `ui::DatabasePath(name)`; leave `name` empty for a database in memory.
  Open the database once (on the main thread) and keep the pointer.
* A new file gets `onCreate`. A file at an older version runs the `migrations` that lead to
  `version`, in one transaction. Without a path, opening fails unless
  `fallbackToDestructiveMigration` is set, which drops every table and creates the schema
  again.
* `Write` runs on a background thread in a transaction: it commits when the function
  returns and rolls back when it throws. `Read` runs on one of several read-only
  connections and does not wait for a write in progress (write-ahead logging).
* `Query(tables, fn)` is a [flow](composables.md#flows): the result now, and again after
  each `Write` that changes one of the named tables. Read it in content with
  `rt::CollectAsState(flow, initial)`. `Invalidations(tables)` reports the changed tables
  alone.
* `sqlite::Connection` and `sqlite::Statement` (`kiln/sqlite/SQLite.h`) are the calls
  underneath: prepare, bind by 1-based index, step, read columns by 0-based index,
  `WithTransaction` (nested levels are savepoints). An error throws `sqlite::Error` with
  SQLite's result code.
* `sqlite3.h` is on the include path for anything else. C apps use it directly, with
  `kiln_database_path` for the file.

## Paging

Paging loads a long list a page at a time, as the list is scrolled. It is the Paging
library's design: a source that loads one page, a pager that decides which page is needed,
and items for a lazy list. Add `kiln_paging` to the app's `LIBS`.

```cpp
#include <kiln/foundation/Lazy.h>
#include <kiln/material3/ProgressIndicator.h>
#include <kiln/material3/Text.h>
#include <kiln/paging/LazyPagingItems.h>
#include <kiln/paging/Pager.h>

using namespace kiln;
using namespace kiln::foundation;

struct Note { int id; std::string title; };

// Pages are keyed by the offset of their first item.
class NoteSource : public paging::PagingSource<int, Note> {
public:
    rt::Task<paging::LoadResult<int, Note>> Load(paging::LoadParams<int> params) override {
        const int from = params.key.value_or(0);
        std::vector<Note> notes;   // read them here: a Database's Read, an HTTP call
        for (int i = from; i < std::min(from + params.loadSize, 500); ++i) notes.push_back({ i, "Note " + std::to_string(i) });
        paging::Page<int, Note> page;
        if (from > 0) page.prevKey = std::max(0, from - params.loadSize);
        if (from + int(notes.size()) < 500) page.nextKey = from + int(notes.size());
        page.data = std::move(notes);
        co_return page;
    }
    std::optional<int> GetRefreshKey(const paging::PagingState<int, Note>& state) override {
        return state.anchorPosition;
    }
};

void Notes() {
    auto scope = rt::RememberCoroutineScope();
    auto& notes = rt::Composition::Current()->Remember<paging::PagingDataFlow<Note>>([&] {
        paging::Pager<int, Note> pager({ .pageSize = 30 }, []() -> std::shared_ptr<paging::PagingSource<int, Note>> { return std::make_shared<NoteSource>(); });
        return paging::CachedIn(pager.Flow(), scope);
    });
    auto& items = paging::CollectAsLazyPagingItems(notes);
    auto& list = RememberLazyListState();
    LazyColumn(Modifier{}, list, [&items](LazyListScope& s) {
        s.Items(items.ItemCount(), [&items](int i) {
            if (const Note* note = items.Get(i)) material3::Text(note->title);
            else material3::Text("...");
        }, paging::ItemKey(items, [](const Note& n) { return n.id; }));
        if (items.LoadState().append.IsLoading()) s.Item([] { material3::CircularProgressIndicator(); });
    });
}
```

* `Load` returns a `Page` with its items and the keys of the pages before and after it
  (absent at an end), a `LoadError`, or `LoadInvalid`. It is a task: do blocking work
  through `rt::WithContext`, a database `Read` or an HTTP call.
* `PagingConfig` sets `pageSize`, `prefetchDistance` (how near the edge of what is loaded
  a read starts the next load), `initialLoadSize`, `maxSize` (pages beyond it are dropped
  and loaded again when scrolled back to) and `enablePlaceholders`. With placeholders the
  page reports `itemsBefore` and `itemsAfter`, the list has its full length at once, and
  `Get` returns null for an item not loaded yet.
* `CachedIn(flow, scope)` keeps what is loaded for as long as the scope lives. Keep the
  result in a view model or `Remember` it; without it every collection loads from the start.
* `items.LoadState()` has `refresh`, `append` and `prepend`, each not loading, loading or
  an error (`IsError`, `ErrorMessage`). `items.Retry()` loads again what failed and
  `items.Refresh()` starts a new source around the place being shown.
* Call `Invalidate()` on the source when its data changed; the pager makes a new one with
  `GetRefreshKey` and the list keeps its place. With a database, invalidate from
  `Invalidations(tables)`.
* `PagingData<T>::Map` and `Filter` change the items of a flow's data, and
  `PagingData<T>::From(vector)` presents a list that is already in memory.
