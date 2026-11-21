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

## ConstraintLayout

`ConstraintLayout` places children by constraints between them and to the parent. It suits
screens where a flat layout reads better than nested rows and columns. It is a port of
constraintlayout-compose and its solver. Add `kiln_constraintlayout` to the app's `LIBS`:

```cmake
kiln_add_app(contacts
    SOURCES src/main.cpp
    LIBS kiln_material3 kiln_constraintlayout
    PACKAGE com.example.contacts
    LABEL "Contacts"
)
```

Inside the content, `CreateRef` gives each child a reference and `ConstrainAs` ties the child
to it. The block links the child's anchors (`start`, `end`, `top`, `bottom`, `baseline`, and
`absoluteLeft` / `absoluteRight`, which ignore the layout direction) to other anchors:

```cpp
ConstraintLayout(FillMax(Modifier{}, Axis::Horizontal), [](ConstraintLayoutScope& s) {
    auto avatar = s.CreateRef();
    auto name = s.CreateRef();
    Box(s.ConstrainAs(SizeMod(Modifier{}, 48_dp, 48_dp), avatar, [](ConstrainScope& c) {
        c.start.LinkTo(c.parent.start, 16_dp);
        c.CenterVerticallyTo(c.parent);
    }));
    Text("Ada Lovelace", s.ConstrainAs(Modifier{}, name, [avatar](ConstrainScope& c) {
        c.start.LinkTo(avatar.end, 12_dp);
        c.end.LinkTo(c.parent.end, 16_dp);
        c.CenterVerticallyTo(avatar);
        c.width = Dimension::FillToConstraints();
    }));
});
```

A child's size is a `Dimension`:

| Dimension | Size |
|---|---|
| `WrapContent()` | Its own size, the default |
| `Value(dp)` | Exactly that |
| `FillToConstraints()` | The space between its constraints |
| `PreferredWrapContent()` | Its own size, unless the constraints are tighter |
| `Percent(f)` | That fraction of the parent |
| `Ratio("16:9")` | A width to height ratio |
| `MatchParent()` | The parent's size |

`AtLeast(dp)` and `AtMost(dp)` bound any of them. A `ConstrainScope` also takes
`horizontalBias` / `verticalBias` and `visibility` (`Visibility::Gone()` takes the child out
of the layout). It also takes layer properties: `alpha`, `scaleX`, `rotationZ`,
`translationX` and the rest.

The scope makes helpers that children can link to:

- guidelines at an offset or fraction (`CreateGuidelineFromStart(0.3f)`);
- barriers on the far side of several children (`CreateEndBarrier({ &a, &b })`);
- chains that spread or pack children along one axis (`CreateHorizontalChain`);
- flows that wrap children into rows (`CreateFlow`);
- grids, rows and columns (`CreateGrid`, `CreateRow`, `CreateColumn`).

`WithHorizontalChainParams(ref, ...)` gives a child its margins and weight in a chain.

A recomposition of the content runs the constraint blocks again. A block kept with `Remember`
(the `ConstrainBlock` overload of `ConstrainAs`) is not run again, which is what Compose's
compiler does for lambdas with stable captures.

### Constraint sets

A `ConstraintSet` keeps the constraints apart from the content. It finds children by their
`LayoutId`, so different sets can lay out the same children, for example per window size. It can
be written with the same scope:

```cpp
auto set = MakeConstraintSet([](ConstraintSetScope& s) {
    auto title = s.CreateRefFor("title");
    auto body = s.CreateRefFor("body");
    s.Constrain(title, [](ConstrainScope& c) { c.top.LinkTo(c.parent.top, 16_dp); });
    s.Constrain(body, [title](ConstrainScope& c) {
        c.top.LinkTo(title.bottom, 8_dp);
        c.width = Dimension::FillToConstraints();
        c.LinkTo(c.parent.start, c.parent.end, 16_dp, 16_dp);
    });
});
ConstraintLayout(set, FillMax(Modifier{}, Axis::Both), [] {
    Text("Notes", LayoutId(Modifier{}, "title"));
    Text("Constraint sets can be swapped at run time.", LayoutId(Modifier{}, "body"));
});
```

It can also be written in JSON5, as Compose's `ConstraintSet(json)` is:

```cpp
auto set = MakeConstraintSet(R"({
    title: { top: ['parent', 'top', 16], start: ['parent', 'start', 16] },
    body: { top: ['title', 'bottom', 8], centerHorizontally: 'parent' }
})");
```

`MakeConstraintSet(description, base)` extends `base`.

### Animating changes

Given `AnimateChanges{ spec }`, ConstraintLayout animates to its new constraints whenever they
change, instead of jumping. This works with the inline DSL or with a `ConstraintSet`. The
listener is called after each animation ends:

```cpp
auto& expanded = RememberState(false);
ConstraintLayout(
    FillMax(Modifier{}, Axis::Both),
    [&](ConstraintLayoutScope& s) {
        auto box = s.CreateRef();
        Box(Background(s.ConstrainAs(Modifier{}, box, [&](ConstrainScope& c) {
                           c.width = Dimension::Value(expanded.Get() ? 200_dp : 48_dp);
                           c.height = Dimension::Value(48_dp);
                           c.CenterTo(c.parent);
                       }),
                       Color::Blue()));
    },
    AnimateChanges{ anim::Tween(300) });
```

## MotionLayout

`MotionLayout` (`kiln/constraintlayout/MotionLayout.h`) animates its children between two
constraint sets at a `progress` from 0 to 1. Each child follows a path, with optional arcs,
keyframes, easing and stagger. The children are matched by `LayoutId`.

A `MotionScene` names its constraint sets and the transitions between them. It is written with
`MotionSceneScope`, or in JSON5 with `MakeMotionScene(json)`, which uses Compose's MotionScene
format:

```cpp
auto scene = MakeMotionScene([](MotionSceneScope& s) {
    auto box = s.CreateRefFor("box");
    auto start = s.ConstraintSet([&](ConstraintSetScope& cs) {
        cs.Constrain(box, [](ConstrainScope& c) {
            c.width = Dimension::Value(64_dp);
            c.height = Dimension::Value(64_dp);
            c.top.LinkTo(c.parent.top, 16_dp);
            c.start.LinkTo(c.parent.start, 16_dp);
            MotionSceneScope::CustomColor(c, "background", Color::Red());
        });
    });
    auto end = s.ConstraintSet([&](ConstraintSetScope& cs) {
        cs.Constrain(box, [](ConstrainScope& c) {
            c.width = Dimension::Value(128_dp);
            c.height = Dimension::Value(128_dp);
            c.bottom.LinkTo(c.parent.bottom, 16_dp);
            c.end.LinkTo(c.parent.end, 16_dp);
            MotionSceneScope::CustomColor(c, "background", Color::Blue());
        });
    });
    s.DefaultTransition(start, end, [&](TransitionScope& t) {
        t.motionArc = Arc::StartVertical();
        t.KeyAttributes({ box }, [](KeyAttributesScope& k) {
            k.Frame(50, [](KeyAttributeScope& f) { f.rotationZ = 90; });
        });
        t.onSwipe.emplace(OnSwipe{ box, SwipeSide::End(), SwipeDirection::Down() });
    });
});
auto& progress = RememberState(0.0f);
MotionLayout(scene, progress.Get(), FillMax(Modifier{}, Axis::Both), [](MotionLayoutScope& m) {
    Box(Background(LayoutId(Modifier{}, "box"), m.CustomColor("box", "background")));
});
```

The content can read each child's custom values (colours and numbers) at the current progress
from the scope. An `onSwipe` lets the user drive the progress by dragging. When the drag is
released, the progress settles by velocity or by a spring, according to the transition's
touch-up rule.

Other ways to run it:

- **Two constraint sets.** Pass them directly, optionally with a `MakeTransition`.
- **Named set.** Pass the name of a set and an animation spec. MotionLayout animates to the
  named set whenever the name changes.
- **Debug overlay.** `MotionLayoutOptions` holds `DebugFlags`, which draws each child's start
  and end bounds, its path and its key positions.

By default, MotionLayout measures its children again whenever its content recomposes. Pass an
`InvalidationStrategy` to change that. It can name the state whose changes should trigger a
remeasure, or rate-limit remeasures when the incoming size changes.

`MotionCarousel` puts consecutive items into a scene's `slot0`, `slot1`, … children.
Swiping horizontally runs the scene's `forward` or `backward` transition and moves the items
along by one slot.

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
