# kiln for Compose developers

kiln ports Jetpack Compose to C++: the runtime, foundation, Material 3 and navigation.
Names and behavior follow Compose, so most of what you know carries over. This page lists
how Compose code reads in kiln.

## Conventions

* Composables are plain C++ functions. There is no annotation: a function is composable
  because it calls other composables, and it must run inside content.
* Function and type names are PascalCase: `rememberLazyListState()` is
  `RememberLazyListState()`.
* Optional parameters are gathered in an options struct, filled field by field or with
  designated initializers: `Text("Hi", {}, { .style = MaterialTheme::typography().titleLarge })`.
* Trailing lambdas become the last argument.
* Namespaces match the Compose modules: `kiln::foundation`, `kiln::material3`,
  `kiln::navigation`, `kiln::ui` and `kiln::rt` for the runtime.

## Code side by side

| Compose | kiln |
|---|---|
| `16.dp` | `16_dp` |
| `Color(0xFF6750A4)` | `Color(0xFF6750A4u)` |
| `remember { mutableStateOf(0) }` | `RememberState(0)` |
| `state.value` | `state.Get()` |
| `state.value = 1` | `state.Set(1)` |
| `rememberSaveable { mutableStateOf(0) }` | `kiln::rt::RememberSaveableState(0)` |
| `remember { Foo() }` | `Composition::Current()->Remember<Foo>([] { return Foo(); })` |
| `LaunchedEffect(key) { ... }` | `c.LaunchedEffect(Keys{ key }, []() -> Task<> { ... })` |
| `DisposableEffect(key) { onDispose { } }` | `c.DisposableEffect(Keys{ key }, [] { return [] { }; })` |
| `delay(500)` | `co_await kiln::rt::Delay(500)` |
| `rememberCoroutineScope()` | `kiln::rt::RememberCoroutineScope()` |
| `Modifier.padding(16.dp)` | `Padding(Modifier{}, 16_dp)` |
| `Modifier.fillMaxSize()` | `FillMax(Modifier{}, Axis::Both)` |
| `Modifier.fillMaxWidth()` | `FillMax(Modifier{}, Axis::Horizontal)` |
| `Modifier.size(48.dp)` | `SizeMod(Modifier{}, 48_dp, 48_dp)` |
| `Modifier.weight(1f)` | `Weight(Modifier{}, 1)` |
| `Modifier.background(c, RoundedCornerShape(12.dp))` | `Background(Modifier{}, c, CornerShape::Rounded(12_dp))` |
| `Modifier.clickable { }` | `Clickable(Modifier{}, [] { })` |
| `Column(verticalArrangement = ...) { }` | `Column(modifier, Arrangement::SpacedBy(8_dp), AlignmentH::Start(), [] { })` |
| `LazyColumn { items(n) { i -> } }` | `LazyColumn(modifier, state, [](LazyListScope& s) { s.Items(n, [](int i) { }); })` |
| `MaterialTheme.colorScheme.primary` | `MaterialTheme::colorScheme().primary` |

## Modifiers

A Compose modifier chain reads left to right:

```kotlin
Modifier.fillMaxWidth().padding(16.dp).background(color)
```

In kiln each modifier function takes the chain so far as its first argument, so the same
chain nests from the inside out:

<!-- prelude: Color color = Color::Black(); -->
```cpp
Background(Padding(FillMax(Modifier{}, Axis::Horizontal), 16_dp), color)
```

The order of effects is the same as in Compose: the innermost call is the first element.

## Lambdas and lifetimes

Content lambdas can run again on their own after the function that created them has
returned, when state they read changes. That decides how they capture:

* Remembered state lives as long as its call stays in the composition, so capture
  references to it by reference: `auto& count = RememberState(0);` then `[&count]`.
* Capture everything else by value, or create it inside the lambda.

```cpp
auto& name = RememberTextFieldState();
Column(Modifier{}, [&name] {
    TextFieldOptions options;            // created inside: safe
    options.label = [](const TextFieldLabelScope&) { Text("Name"); };
    OutlinedTextField(name, Modifier{}, options);
});
```

## What is different

* State is read with `Get()`, which subscribes the current scope, or `Peek()`, which does
  not. Use `Peek()` in callbacks such as click handlers.
* Coroutines are C++20 coroutines (`kiln::rt::Task<>`) on the main thread. Cancelling a
  task destroys its frame, so destructors run as `finally` blocks would.
