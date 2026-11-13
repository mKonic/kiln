# Layout and lists

kiln lays out composables with the same model as Compose: each parent measures its
children within constraints, then places them. Modifiers change size, spacing, drawing
and input of the composable they are given to.

## Box, Row and Column

| Layout | Places its children |
|---|---|
| `Box` | On top of one another |
| `Row` | Side by side |
| `Column` | One below the other |

```cpp
Row(Padding(Modifier{}, 16_dp), Arrangement::SpacedBy(12_dp), AlignmentV::CenterVertically(), [] {
    Box(SizeMod(Background(Modifier{}, Color(0xFF6750A4u), CornerShape::Pill()), 40_dp, 40_dp));
    Column(Weight(Modifier{}, 1), [] {
        Text("Ada Lovelace");
        Text("Online");
    });
});
```

### Arrangement

The arrangement places children along the main axis: `Start()`, `End()`, `Center()`,
`SpaceBetween()`, `SpaceAround()`, `SpaceEvenly()` and `SpacedBy(dp)`. For a `Column`,
`Top()` and `Bottom()` read better than `Start()` and `End()`.

### Alignment

| Type | Used by | Values |
|---|---|---|
| `AlignmentV` | `Row` | `Top()`, `CenterVertically()`, `Bottom()` |
| `AlignmentH` | `Column` | `Start()`, `CenterHorizontally()`, `End()` |
| `Alignment` | `Box` | `TopStart()`, `Center()`, `BottomEnd()` and the other six |

Start and end follow the layout direction, so they swap in right-to-left languages.
`TopLeft()` and the other `Left` and `Right` alignments do not.

A single child can override its parent's alignment with `AlignInRow`, `AlignInColumn`
or `AlignInBox`.

## Size and spacing modifiers

| Modifier | Effect |
|---|---|
| `Padding(m, all)`, `Padding(m, horizontal, vertical)` | Space inside the bounds |
| `SizeMod(m, w, h)`, `Width(m, w)`, `Height(m, h)` | An exact size |
| `SizeIn(m, minW, minH, maxW, maxH)` | Bounds on the size |
| `FillMax(m, axis, fraction)` | The available width, height or both |
| `Weight(m, weight)` | A share of the space left in a `Row` or `Column` |
| `AspectRatio(m, ratio)` | A width to height ratio |
| `OffsetMod(m, x, y)` | Moves the content without changing its layout |
| `Spacer(modifier)` | An empty composable that takes space |

## Scrolling

`VerticalScroll` and `HorizontalScroll` make content larger than the screen scroll:

```cpp
auto& scroll = RememberScrollState();
Column(VerticalScroll(FillMax(Modifier{}, Axis::Both), scroll), [] {
    for (int i = 0; i < 50; ++i) Text("Line " + std::to_string(i));
});
```

This composes every child. For long or unbounded content, use a lazy list.

## Lazy lists

`LazyColumn` and `LazyRow` compose only the items on screen:

```cpp
struct Contact { int id; std::string name; };

auto& contacts = RememberState(std::vector<Contact>{ { 1, "Ada" }, { 2, "Grace" } });
auto& state = RememberLazyListState();
LazyListOptions options;
options.contentPadding = { 16_dp, 8_dp, 16_dp, 8_dp };
options.arrangement = Arrangement::SpacedBy(8_dp);
LazyColumn(FillMax(Modifier{}, Axis::Both), state, [&contacts](LazyListScope& s) {
    s.StickyHeader([](int) { Text("Contacts"); });
    s.Items(int(contacts.Get().size()),
            [&contacts](int i) { Text(contacts.Get()[i].name); },
            [&contacts](int i) { return kiln::rt::Key(contacts.Peek()[i].id); });
}, options);
```

* `Item(content)` adds one item, `Items(count, content, key)` adds many.
* A key keeps an item's state with the item when the list changes order.
* A content type, the last argument of `Item` and `Items`, says which items look alike.
  An item that scrolls away keeps its nodes for the next item of the same type, so
  scrolling builds few new ones. Give items of different layouts different types.
* `StickyHeader` stays at the top while its section scrolls.
* `AnimateItem(modifier)` on an item animates it when it appears, moves or leaves.

While the list scrolls, the item about to come in is composed and measured ahead, in the
time each frame leaves before the next one.

The state scrolls the list from code: `state.ScrollToItem(i)` jumps,
`state.AnimateScrollToItem(i)` returns a task that animates. Read the position with
`state.FirstVisibleItemIndex()`.

### Grids

```cpp
auto& grid = RememberLazyGridState();
LazyVerticalGrid(Modifier{}, GridCells::Adaptive(120_dp), grid, [](LazyGridScope& s) {
    s.Items(100, [](int i) { Text("Cell " + std::to_string(i)); });
});
```

`GridCells::Fixed(n)` gives n columns. `GridCells::Adaptive(min)` fits as many columns
as are at least `min` wide. `LazyHorizontalGrid` and `LazyVerticalStaggeredGrid` work
the same way.

## Pagers

```cpp
auto& pager = RememberPagerState([] { return 5; });
HorizontalPager(FillMax(Modifier{}, Axis::Both), pager, [](int page) {
    Text("Page " + std::to_string(page));
});
```

## Flow layouts

`FlowRow` and `FlowColumn` wrap their children onto new lines when they run out of space,
which suits chips and tags:

```cpp
FlowRow(Modifier{}, Arrangement::SpacedBy(8_dp), Arrangement::SpacedBy(8_dp), AlignmentV::Top(), [] {
    for (const char* tag : { "Vulkan", "C++", "Android", "Material 3" }) Text(tag);
});
```

## Grid

`Grid` places its children in columns and rows, as CSS grid does. Columns and rows can be a
fixed size, a share of the space (`Fr`), a percentage, or sized to their content. Children
fill the next free cell, or take a cell, a span or a named area of their own:

```cpp
Grid([](GridConfigurationScope& g) {
    g.Column(80_dp);        // labels
    g.Column(Fr(1));        // values take the rest
    g.Gap(8_dp);
}, Modifier{}, [] {
    Text("Name");
    Text("Ada Lovelace");
    Text("Note", GridItem(Modifier{}, 2, 1, 1, 2));   // row 2, across both columns
});
```

## Window insets

kiln apps draw edge to edge, behind the status and navigation bars. Keep content clear of
the system bars, the display cutout and the keyboard with insets padding:

| Modifier | Keeps content clear of |
|---|---|
| `SafeDrawingPadding(m)` | System bars, display cutout and keyboard |
| `SystemBarsPadding(m)` | Status, navigation and caption bars |
| `StatusBarsPadding(m)`, `NavigationBarsPadding(m)` | One bar |
| `ImePadding(m)` | The keyboard |

`Scaffold` handles the insets for its bars and passes the rest to its content as padding.

### Hiding the system bars

For full-screen content such as video or games, hide the bars through the window's owner
(`WindowInsetsControllerCompat.hide` in Compose). The insets padding above follows: content
moves into the freed space, and back when the bars return.

<!-- prelude: auto* owner = rt::Composition::Current()->Read(LocalOwner); -->
```cpp
owner->SetSystemBarsBehavior(Owner::SystemBarsBehavior::ShowTransientBarsBySwipe);
owner->HideInsets(Owner::SystemBarsTypes());
// Later:
owner->ShowInsets(Owner::SystemBarsTypes());
```

With `SystemBarsBehavior::Default` a swipe from the edge brings the bars back for good. With
`ShowTransientBarsBySwipe` it shows them briefly over the content, and the insets do not change.
`AreSystemBarsVisible()` tells you whether they are showing.
