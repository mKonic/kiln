# Navigation

kiln has both Compose navigation libraries:

| Library | Header and namespace | Back stack |
|---|---|---|
| Navigation (`NavHost`) | `kiln/navigation/NavHost.h`, `kiln::navigation` | Kept by a `NavController`, addressed by route strings |
| Navigation 3 (`NavDisplay`) | `kiln/navigation3/NavDisplay.h`, `kiln::navigation3` | A list of keys that your code owns |

Both animate between screens, follow the predictive back gesture, and keep each screen's
saved state while it is on the back stack.

## NavHost

Declare the screens by route, then navigate by route:

```cpp
void App() {
    auto& nav = RememberNavController();
    NavHost(nav, "home", [&nav](NavGraphBuilder& b) {
        b.Composable("home", [&nav](const NavBackStackEntry&) {
            Button([&nav] { nav.Navigate("detail/42"); }, {}, {}, [] { Text("Open item 42"); });
        });
        b.Composable("detail/{id}", [](const NavBackStackEntry& entry) {
            Text("Item " + std::to_string(entry.GetInt("id").value_or(0)));
        }, { { NavArgumentOf("id", NavType::Int) } });
    });
}
```

* Route arguments are written in braces and typed with `NavArgumentOf`. Read them from
  the entry with `GetInt` and `GetString`.
* Optional arguments go in the query: `"profile?name={name}"`, with a default value.
* `nav.PopBackStack()` goes back one screen. `nav.PopBackStack(route, inclusive)` goes
  back to a route.
* `nav.Navigate(route, [](NavOptionsBuilder& o) { ... })` sets options such as
  `launchSingleTop` and `popUpTo`.
* The system back gesture pops the back stack, and its predictive animation previews the
  screen below.

### Deep links

A destination can declare deep links, by URI pattern, action or MIME type.
`nav.Navigate(uri)` and `nav.Navigate(request)` open the destination that matches, with
the arguments taken from the URI.

## Navigation 3

With Navigation 3 the back stack is plain state: a list of keys you push and pop
yourself.

```cpp
struct Home { bool operator==(const Home&) const = default; };
struct Detail { int id; bool operator==(const Detail&) const = default; };
using Route = std::variant<Home, Detail>;
// Each key's text: it identifies the entry's saved state.
std::string NavKeyToString(const Home&) { return "Home"; }
std::string NavKeyToString(const Detail& d) { return "Detail(" + std::to_string(d.id) + ")"; }

void App() {
    auto& stack = RememberState(std::vector<Route>{ Home{} });
    NavDisplayOptions<Route> options;
    options.onBack = [&stack] { stack.Update([](std::vector<Route>& s) { s.pop_back(); }); };
    NavDisplay(stack.Get(), EntryProvider<Route>([&stack](EntryProviderScope<Route>& p) {
        p.Entry<Home>([&stack](const Home&) {
            Button([&stack] { stack.Update([](std::vector<Route>& s) { s.push_back(Detail{ 7 }); }); },
                   {}, {}, [] { Text("Open item 7"); });
        });
        p.Entry<Detail>([](const Detail& d) { Text("Item " + std::to_string(d.id)); });
    }), Modifier{}, options);
}
```

Navigation 3 also supports scenes, which show more than one entry at a time, such as a
list and detail side by side on a large screen.

## Adaptive layouts

The Material 3 adaptive components build on navigation:

* `NavigationSuiteScaffold` shows a navigation bar, rail or drawer depending on the
  window size.
* `ThreePaneScaffold` and its navigator lay out list, detail and extra panes, as one pane
  on a phone and side by side on a tablet.
