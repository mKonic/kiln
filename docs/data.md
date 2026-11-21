# Files, data and network

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

### A value of your own type

`kiln/datastore/DataStore.h` is typed DataStore: one value of the app's own type in a file,
with a serializer that turns it into bytes and back.

```cpp
#include <kiln/datastore/DataStore.h>

using namespace kiln;

struct Settings {
    int fontSize = 14;
    bool operator==(const Settings&) const = default;
};

datastore::Serializer<Settings> serializer{
    .defaultValue = {},
    .readFrom = [](std::string_view bytes) {
        if (bytes.size() != 1) throw datastore::CorruptionException("not a Settings");
        return Settings{ static_cast<unsigned char>(bytes[0]) };
    },
    .writeTo = [](const Settings& s) { return std::string(1, char(s.fontSize)); },
};

rt::Task<> Bigger(std::shared_ptr<datastore::DataStore<Settings>> store) {
    co_await store->UpdateData([](Settings s) { s.fontSize += 2; return s; });
}
```

* Create the store once with
  `datastore::DataStoreFactory::Create(serializer, datastore::DataStoreFile("settings.pb"))`
  and keep it. A second store on the same file while the first is alive throws.
* `Data()` is a flow of the value; `UpdateData` returns the new value once it is written.
  A value equal to the old one is not written.
* `readFrom` throws `CorruptionException` for bytes that are not a value. The read then
  throws, unless a `ReplaceFileCorruptionHandler` supplies a value, which is written back.
* `DataMigration`s run once, before the first read. Each one whose `shouldMigrate` holds
  changes the value, and its `cleanUp` runs after the result is written. Use them to move
  data from an older format or file.

## Secrets

Login tokens and other secrets go in a `security::SecureStore` (`kiln/security/SecureStore.h`)
rather than in preferences:

```cpp
#include <kiln/security/SecureStore.h>

void SignedIn(const std::string& token) {
    kiln::security::SecureStore::Open("session")->Set("token", token);
}
std::optional<std::string> Token() { return kiln::security::SecureStore::Open("session")->Get("token"); }
```

* The values are encrypted with AES-256-GCM under a key the Android Keystore makes and keeps:
  the key never leaves the device's secure hardware.
* The whole store, keys and values, is one file in `NoBackupFilesDir`. Keystore keys are not
  part of a backup, so the file is not either. After a restore, or if the key is gone, the
  store starts empty: sign the user in again. The file that could not be read is kept
  beside it with `.unreadable` added to its name, so a key store that failed only for a while
  does not cost the data.
* On the desktop there is no key store: values are kept in memory for the process only.

From C, `kiln_secure_store_open` and `kiln_secure_store_get` / `_set` / `_remove` / `_clear`;
kiln++ has `kpp::SecureStore` and Rust `kiln::SecureStore`.

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
* `PagingData<T>::Map`, `FlatMap` and `Filter` change the items of a flow's data, and
  `PagingData<T>::From(vector)` presents a list that is already in memory.
* `InvalidatingPagingSourceFactory` makes the sources and invalidates all of them at once,
  for data you know changed.

### Separators, headers and footers

`InsertSeparators` (`kiln/paging/Separators.h`) asks for a separator between each pair of
items, with null at the ends, and returns one or nothing. A header or footer is the separator
at an end. It shows once that end has been reached, so it doesn't jump around while pages
load. The items can turn into a different type, such as a variant of items and separators:

<!-- prelude: #include <kiln/paging/Separators.h> -->
<!-- prelude: struct Note { int id; std::string title; }; -->
```cpp
using NoteRow = std::variant<Note, std::string>;   // a note, or a letter heading

kiln::paging::PagingData<NoteRow> WithHeadings(const kiln::paging::PagingData<Note>& notes) {
    return kiln::paging::InsertSeparators<NoteRow>(notes, [](const Note* before, const Note* after) -> std::optional<NoteRow> {
        if (!after) return std::nullopt;   // nothing after the last note
        if (!before || before->title.front() != after->title.front()) return NoteRow(std::string(1, after->title.front()));
        return std::nullopt;
    });
}
```

`InsertHeaderItem` and `InsertFooterItem` add one item at an end. With a remote mediator,
`TerminalSeparatorType::SourceComplete` shows them as soon as local storage runs out, instead
of waiting for the network to run out too.

### Network and local storage

A `RemoteMediator` fills local storage from the network while the `PagingSource` reads local
storage. The pager calls it when the source runs out at an end (`Append` or `Prepend`), and
to start over (`Refresh`: when the pager first loads, and when `Refresh()` is called):

<!-- prelude: #include <kiln/paging/Pager.h> -->
<!-- prelude: struct Note { int id; std::string title; }; -->
<!-- prelude: class NoteSource : public kiln::paging::PagingSource<int, Note> { public: kiln::rt::Task<kiln::paging::LoadResult<int, Note>> Load(kiln::paging::LoadParams<int>) override { co_return kiln::paging::Page<int, Note>{}; } std::optional<int> GetRefreshKey(const kiln::paging::PagingState<int, Note>&) override { return std::nullopt; } }; -->
<!-- prelude: kiln::rt::Task<std::vector<Note>> FetchNotesAfter(std::optional<int> lastId); -->
<!-- prelude: void StoreNotes(const std::vector<Note>& notes, bool replace); -->
```cpp
class NoteMediator : public kiln::paging::RemoteMediator<int, Note> {
public:
    kiln::rt::Task<kiln::paging::MediatorResult> Load(kiln::paging::LoadType type, kiln::paging::PagingState<int, Note> state) override {
        if (type == kiln::paging::LoadType::Prepend) co_return kiln::paging::MediatorResult::Succeeded(true);   // newest first: nothing before
        std::optional<int> after;
        if (type == kiln::paging::LoadType::Append) {
            const Note* last = state.LastItemOrNull();
            if (!last) co_return kiln::paging::MediatorResult::Succeeded(true);
            after = last->id;
        }
        try {
            std::vector<Note> notes = co_await FetchNotesAfter(after);
            StoreNotes(notes, type == kiln::paging::LoadType::Refresh);   // invalidates the source
            co_return kiln::paging::MediatorResult::Succeeded(notes.empty());
        } catch (const std::exception&) {
            co_return kiln::paging::MediatorResult::Failed(std::current_exception());
        }
    }
};

kiln::paging::Pager<int, Note> MakePager() {
    return kiln::paging::Pager<int, Note>({ .pageSize = 30 }, [] { return std::make_shared<NoteSource>(); }, std::nullopt, std::make_shared<NoteMediator>());
}
```

* Storing the results must invalidate the source, so the new rows show. With a database,
  `Invalidations(tables)` does that.
* `Initialize()` returns `InitializeAction::SkipInitialRefresh` to show what is stored without
  refreshing first, for example while the cache is fresh.
* `items.LoadState().mediator` has the mediator's own states; `refresh`, `append` and `prepend`
  combine them with the source's. `Retry()` retries what failed in either.

## HTTP

`net::Fetch` runs a request in the background and gives the task its response. On Android it
goes through `HttpURLConnection`, so it uses the system's TLS and certificates, proxies and
the app's network security config; on the desktop it uses libcurl. The app declares the
permission:

```cmake
kiln_add_app(shelf
    SOURCES src/main.cpp
    LIBS kiln_material3
    PACKAGE com.example.shelf
    LABEL "Shelf"
    PERMISSIONS android.permission.INTERNET android.permission.ACCESS_NETWORK_STATE
)
```

```cpp
#include <kiln/net/Http.h>

using namespace kiln;

rt::Task<> LoadNotes(rt::State<std::string>& text) {
    try {
        net::HttpResponse r = co_await net::Fetch({ .url = "https://example.com/notes.json" });
        text.Set(r.Ok() ? r.body : "The server answered " + std::to_string(r.status));
    } catch (const net::HttpError& e) {
        text.Set(e.what());   // no connection, a timeout, a certificate that is not trusted
    }
}

rt::Task<bool> SaveNote(std::string json, std::string token) {
    net::HttpResponse r = co_await net::Fetch({
        .url = "https://example.com/notes",
        .method = "POST",
        .headers = { { "Content-Type", "application/json" }, { "Authorization", "Bearer " + token } },
        .body = std::move(json),
    });
    co_return r.Ok();
}

rt::Task<> FetchCover(std::string file, rt::State<float>& progress) {
    co_await net::Download("https://example.com/cover.jpg", file, [&progress](std::int64_t received, std::int64_t total) {
        if (total > 0) progress.Set(float(received) / float(total));
    });
}
```

* Launch the task from a `LaunchedEffect` or a [scope](composables.md#effects). When it
  is cancelled (its content leaves the screen), the connection is closed.
* Any status is a response: check `Ok()` (200 to 299) or `status`. `HttpError` means there
  was no response; its `kind` says why (`UnknownHost`, `Connect`, `Timeout`, `Tls`,
  `Cleartext`, `Permission`, `TooManyRedirects`, `Io`).
* `headers` are name and value pairs; `response.headers.Get("content-type")` compares names
  without case. A body without a `Content-Type` is sent as a form;
  `net::FormBody({ { "q", "tea" } })` encodes one.
* `bodyFile` sends a file as the body, and `responseFile` (or `net::Download`) writes the
  response to a file as it arrives, for anything large. `onProgress` reports the bytes
  received on the task's thread.
* `connectTimeoutMs` and `readTimeoutMs` default to 10 seconds; 0 waits without limit.
* Redirects are followed as OkHttp follows them, unless `followRedirects` is false. A 301, 302
  or 303 turns a POST into a GET without its body, while a 307 or 308 keeps both. A redirect
  to another host drops the `Authorization` header. A 408, or a 503 with `Retry-After: 0`, is
  retried once. More than 20 follow-ups is a `TooManyRedirects` error.
* Android refuses `http://` URLs unless the app allows them. Put a
  [network security config](https://developer.android.com/privacy-and-security/security-config)
  in `res/xml/` and name it with
  `APPLICATION_ATTRIBUTES "android:networkSecurityConfig=\"@xml/network\""`.
* `net::Connectivity()` is a state flow of the default network: `connected`, `validated`
  (the system reached the internet through it), `metered` and `transport`. In content,
  `rt::CollectAsState(net::Connectivity())`. It needs `ACCESS_NETWORK_STATE`; without the
  permission it always reports a connected network.
* `net::SetHttpEngine` replaces what performs requests, for tests.
* From C, `kiln_http_fetch` takes a request and a callback that runs on the main thread;
  kiln++ has `kpp::http_fetch` and Rust `kiln::http_fetch`.
